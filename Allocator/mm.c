/*
 * mm.c - A space- and time-efficient malloc package.
 * 
 * Jack Nettleton
 * U1446778
 *
 * DESIGN OVERVIEW
 * ---------------
 * Memory comes from mem_map() in "chunks" (page multiples).  Chunks are not
 * contiguous, so each chunk is self-contained:
 *
 *   base:      [pad 8][blk][blk]...[blk][epilogue hdr 8]      (page aligned)
 *
 * The padding word makes every payload 16-byte aligned; the epilogue is a
 * zero-size allocated header that ends the chunk.
 *
 * Block format (sizes are multiples of 16, minimum block is 32 bytes):
 *   allocated:  [hdr 8][payload ...............]          (no footer)
 *   free:       [hdr 8][next 8][prev 8][ ... ][ftr 8]     (footer = copy of hdr)
 *
 * Header bits:  bit0 = this block allocated
 *               bit1 = previous block allocated  (lets us drop footers on
 *                      allocated blocks => only 8 bytes overhead per block)
 *               bit2 = first block of its chunk  (nothing precedes it)
 *
 * Free blocks live in segregated, doubly linked, LIFO free lists: exact 16-byte
 * classes up to 512 bytes, then log2-based classes.  Allocation does a
 * bounded best-fit scan of the matching class, then takes from the first
 * non-empty larger class.  Freed blocks are coalesced immediately with both
 * neighbours.
 *
 * Returning memory: whenever a coalesced free block covers whole pages, those
 * pages are handed back with mem_unmap().  A block in the middle of a chunk
 * can be trimmed: the leftover pieces at either end become their own small
 * chunks (with a fresh epilogue / padding word).
 *
 * realloc shrinks in place, grows in place when the following block is free or
 * when the preceding block is free and large enough, and otherwise falls back
 * to malloc + copy + free.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

#include "mm.h"
#include "memlib.h"

 /* ---------- constants ---------- */
#define ALIGNMENT   16
#define WSIZE       8                 /* header / footer / pointer size */
#define DSIZE       16
#define MIN_BLOCK   32                /* hdr + next + prev + ftr */

#define F_ALLOC     ((size_t)0x1)     /* this block is allocated            */
#define F_PREV      ((size_t)0x2)     /* previous block is allocated        */
#define F_START     ((size_t)0x4)     /* first block of its chunk           */
#define SIZE_MASK   (~(size_t)0xF)

#define SMALL_MAX   512               /* exact-size classes up to here      */
#define NSMALL      31                /* (512-32)/16 + 1                    */
#define NCLASSES    64

#define MAX_SCAN    192               /* max nodes examined per list        */
#define BEST_EXTRA  24                /* extra nodes examined after a fit   */

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~(size_t)(ALIGNMENT - 1))

/* ---------- global state ---------- */
static void** free_lists;
static size_t page_size;

/* ---------- raw word access / block navigation ---------- */
static inline size_t get(const void* p) { return *(const size_t*)p; }
static inline void   put(void* p, size_t v) { *(size_t*)p = v; }
static inline size_t pack(size_t size, size_t flags) { return size | flags; }

static inline char* hdrp(void* bp) { return (char*)bp - WSIZE; }
static inline size_t bsize(void* bp) { return get(hdrp(bp)) & SIZE_MASK; }
static inline char* ftrp(void* bp) { return (char*)bp + bsize(bp) - DSIZE; }
static inline void* next_blk(void* bp) { return (char*)bp + bsize(bp); }

static inline int log2_floor(size_t size)
{
    int result = 0;

    while (size >>= 1)
        result++;

    return result;
}

/* only valid when the previous block is free (uses its footer) */
static inline void* prev_blk(void* bp)
{
    return (char*)bp - (get((char*)bp - DSIZE) & SIZE_MASK);
}

/* free-list links live in the payload of free blocks */
#define NEXT_FREE(bp) (*(void **)(bp))
#define PREV_FREE(bp) (*((void **)(bp) + 1))

/* set/clear the "previous allocated" bit in the header following bp */
static inline void set_next_prev(void* bp, int prev_alloc)
{
    char* h = hdrp(next_blk(bp));
    put(h, prev_alloc ? (get(h) | F_PREV) : (get(h) & ~F_PREV));
}

/* ---------- segregated free lists ---------- */
static inline int class_of(size_t size)
{
    if (size <= SMALL_MAX)
        return (int)((size - MIN_BLOCK) / DSIZE);
    int lg = log2_floor(size);        /* >= 9 for size > 512 */
    int idx = NSMALL + (lg - 9);
    return idx < NCLASSES ? idx : NCLASSES - 1;
}

static void list_insert(void* bp)
{
    int c = class_of(bsize(bp));
    void* head = free_lists[c];
    NEXT_FREE(bp) = head;
    PREV_FREE(bp) = NULL;
    if (head != NULL)
        PREV_FREE(head) = bp;
    free_lists[c] = bp;
}

/* must be called while bp's header still holds the size it was inserted with */
static void list_remove(void* bp)
{
    void* n = NEXT_FREE(bp);
    void* p = PREV_FREE(bp);
    if (p != NULL)
        NEXT_FREE(p) = n;
    else
        free_lists[class_of(bsize(bp))] = n;
    if (n != NULL)
        PREV_FREE(n) = p;
}

/* bounded best-fit scan of one list; NULL if nothing fits in the window */
static void* scan_list(void* p, size_t required)
{
    void* best = NULL;
    size_t best_sz = (size_t)-1;
    int budget = MAX_SCAN;

    for (; p != NULL; p = NEXT_FREE(p)) {
        size_t s = bsize(p);
        if (s >= required && s < best_sz) {
            if (best == NULL && budget > BEST_EXTRA)
                budget = BEST_EXTRA;
            best = p;
            best_sz = s;
            if (s == required)
                break;
        }
        if (--budget <= 0)
            break;
    }
    return best;
}

static void* find_fit(size_t required)
{
    int c = class_of(required);
    void* bp = scan_list(free_lists[c], required);
    if (bp != NULL)
        return bp;
    /* every block in a strictly larger class is big enough */
    for (int i = c + 1; i < NCLASSES; i++) {
        if (free_lists[i] != NULL) {
            bp = scan_list(free_lists[i], required);
            if (bp != NULL)
                return bp;
        }
    }
    return NULL;
}

/* ---------- allocation helpers ---------- */

/* Map a new chunk holding one big free block; the block is put on a list. */
static void* extend_heap(size_t required)
{
    if (required > SIZE_MAX - 2 * WSIZE - page_size)
        return NULL;
    size_t chunk = (required + 2 * WSIZE + page_size - 1) & ~(page_size - 1);
    char* base = mem_map(chunk);
    if (base == NULL)
        return NULL;

    size_t bs = chunk - 2 * WSIZE;
    char* bp = base + DSIZE;

    put(base, 0);                                   /* padding word */
    put(hdrp(bp), pack(bs, F_PREV | F_START));
    put(ftrp(bp), pack(bs, F_PREV | F_START));
    put(hdrp(next_blk(bp)), pack(0, F_ALLOC));      /* epilogue (prev is free) */
    list_insert(bp);
    return bp;
}

/* Carve `required` bytes out of free block bp (bp is on a free list). */
static void place(void* bp, size_t required)
{
    size_t size = bsize(bp);
    size_t flags = get(hdrp(bp)) & (F_PREV | F_START);
    size_t rem = size - required;

    list_remove(bp);
    if (rem >= MIN_BLOCK) {
        put(hdrp(bp), pack(required, flags | F_ALLOC));
        void* nb = next_blk(bp);
        put(hdrp(nb), pack(rem, F_PREV));
        put(ftrp(nb), pack(rem, F_PREV));
        list_insert(nb);                /* block after nb already has prev=0 */
    }
    else {
        put(hdrp(bp), pack(size, flags | F_ALLOC));
        set_next_prev(bp, 1);
    }
}

/*
 * bp is a free block (header + footer written, not on any list).  Merge it
 * with free neighbours and return the merged block, still not on a list.
 */
static void* coalesce(void* bp)
{
    size_t size = bsize(bp);
    size_t flags = get(hdrp(bp)) & (F_PREV | F_START);
    void* next = next_blk(bp);

    if (!(get(hdrp(next)) & F_ALLOC)) {             /* epilogue counts as allocated */
        size += bsize(next);
        list_remove(next);
    }
    if (!(flags & F_PREV)) {
        void* prev = prev_blk(bp);
        size += bsize(prev);
        list_remove(prev);
        flags = get(hdrp(prev)) & (F_PREV | F_START);
        bp = prev;
    }
    put(hdrp(bp), pack(size, flags));
    put(ftrp(bp), pack(size, flags));
    return bp;
}

/*
 * bp is a coalesced free block not on any list.  If it covers whole pages,
 * give them back to the OS; whatever is left at either end is re-formed into
 * valid (smaller) chunks and put on the free lists.  Otherwise just insert it.
 */
static void insert_or_release(void* bp)
{
    size_t size = bsize(bp);

    if (size + 2 * WSIZE < page_size) {             /* can't contain a page */
        list_insert(bp);
        return;
    }

    char* hdr = hdrp(bp);
    char* end = hdr + size;                         /* next header / epilogue */
    int first = (get(hdr) & F_START) != 0;
    int last = (get(end) & SIZE_MASK) == 0;

    /* extent we own: includes the pad word / epilogue at the chunk edges */
    uintptr_t lo = (uintptr_t)hdr - (first ? WSIZE : 0);
    uintptr_t hi = (uintptr_t)end + (last ? WSIZE : 0);
    uintptr_t pmask = ~(uintptr_t)(page_size - 1);
    uintptr_t a = (lo + page_size - 1) & pmask;     /* first page to unmap */
    uintptr_t b = hi & pmask;                       /* end of unmapped range */

    /* leftovers must be empty or at least MIN_BLOCK bytes (16 is too small) */
    if (!first && a - WSIZE - (uintptr_t)hdr == DSIZE)
        a += page_size;
    if (!last && (uintptr_t)end - WSIZE - b == DSIZE)
        b -= page_size;

    if (a >= b) {
        list_insert(bp);
        return;
    }

    if (!last) {                                    /* leftover above [a,b) */
        char* rb = (char*)b + WSIZE;               /* its first header */
        size_t rs = (size_t)(end - rb);
        put((char*)b, 0);                          /* padding word */
        if (rs >= MIN_BLOCK) {
            put(rb, pack(rs, F_PREV | F_START));
            put(rb + rs - WSIZE, pack(rs, F_PREV | F_START));
            list_insert(rb + WSIZE);
        }
        else {                                    /* rs == 0: next block starts the chunk */
            put(end, get(end) | F_PREV | F_START);
        }
    }
    if (!first) {                                   /* leftover below [a,b) */
        size_t ls = a - WSIZE - (uintptr_t)hdr;
        if (ls >= MIN_BLOCK) {
            put(hdr, pack(ls, F_PREV));
            put(hdr + ls - WSIZE, pack(ls, F_PREV));
            put(hdr + ls, pack(0, F_ALLOC));        /* new epilogue */
            list_insert(bp);
        }
        else {                                    /* ls == 0: epilogue right here */
            put(hdr, pack(0, F_ALLOC | F_PREV));
        }
    }
    mem_unmap((void*)a, (size_t)(b - a));
}

/* Mark [bp, size) free, merge, and either list it or release pages. */
static void free_block(void* bp, size_t size, size_t flags)
{
    put(hdrp(bp), pack(size, flags));
    put(ftrp(bp), pack(size, flags));
    set_next_prev(bp, 0);
    insert_or_release(coalesce(bp));
}

static inline size_t adjust_size(size_t size)
{
    size_t r = ALIGN(size + WSIZE);
    return r < MIN_BLOCK ? MIN_BLOCK : r;
}

/* ---------- public interface ---------- */

int mm_init(void)
{
    page_size = mem_pagesize();

    size_t list_bytes = NCLASSES * sizeof(void*);
    size_t allocation_size =
        (list_bytes + page_size - 1) & ~(page_size - 1);

    free_lists = mem_map(allocation_size);

    if (free_lists == NULL) {
        free_lists = NULL;
        return -1;
    }

    for (int i = 0; i < NCLASSES; i++)
        free_lists[i] = NULL;

    return 0;
}

void* mm_malloc(size_t size)
{
    if (size == 0 || size > SIZE_MAX - ALIGNMENT - WSIZE)
        return NULL;

    size_t required = adjust_size(size);
    void* bp = find_fit(required);
    if (bp == NULL) {
        bp = extend_heap(required);
        if (bp == NULL)
            return NULL;
    }
    place(bp, required);
    return bp;
}

void mm_free(void* ptr)
{
    if (ptr == NULL)
        return;
    free_block(ptr, bsize(ptr), get(hdrp(ptr)) & (F_PREV | F_START));
}

void* mm_realloc(void* ptr, size_t size)
{
    if (ptr == NULL)
        return mm_malloc(size);
    if (size == 0) {
        mm_free(ptr);
        return NULL;
    }
    if (size > SIZE_MAX - ALIGNMENT - WSIZE)
        return NULL;

    size_t required = adjust_size(size);
    size_t old = bsize(ptr);
    size_t flags = get(hdrp(ptr)) & (F_PREV | F_START);

    /* shrink (or same size): keep the block, give back a big enough tail */
    if (required <= old) {
        size_t rem = old - required;
        if (rem >= MIN_BLOCK) {
            put(hdrp(ptr), pack(required, flags | F_ALLOC));
            free_block(next_blk(ptr), rem, F_PREV);
        }
        return ptr;
    }

    /* grow in place by absorbing a free successor */
    void* next = next_blk(ptr);
    if (!(get(hdrp(next)) & F_ALLOC)) {
        size_t total = old + bsize(next);
        if (total >= required) {
            size_t rem = total - required;
            list_remove(next);
            if (rem >= MIN_BLOCK) {
                put(hdrp(ptr), pack(required, flags | F_ALLOC));
                void* nb = next_blk(ptr);
                put(hdrp(nb), pack(rem, F_PREV));
                put(ftrp(nb), pack(rem, F_PREV));
                list_insert(nb);
            }
            else {
                put(hdrp(ptr), pack(total, flags | F_ALLOC));
                set_next_prev(ptr, 1);
            }
            return ptr;
        }
    }

    /* grow in place by absorbing a free predecessor */
    if (!(flags & F_PREV)) {
        void* prev = prev_blk(ptr);
        size_t total = bsize(prev) + old;
        if (total >= required) {
            size_t prev_flags = get(hdrp(prev)) & (F_PREV | F_START);
            size_t copy_bytes = old - WSIZE;
            size_t rem = total - required;

            list_remove(prev);
            memmove(prev, ptr, copy_bytes);

            if (rem >= MIN_BLOCK) {
                put(hdrp(prev), pack(required, prev_flags | F_ALLOC));
                void* nb = next_blk(prev);
                put(hdrp(nb), pack(rem, F_PREV));
                put(ftrp(nb), pack(rem, F_PREV));
                list_insert(nb);
            }
            else {
                put(hdrp(prev), pack(total, prev_flags | F_ALLOC));
                set_next_prev(prev, 1);
            }
            return prev;
        }
    }

    /* fall back: allocate, copy, free */
    void* np = mm_malloc(size);
    if (np == NULL)
        return NULL;
    memcpy(np, ptr, old - WSIZE);       /* old payload < size here */
    mm_free(ptr);
    return np;
}