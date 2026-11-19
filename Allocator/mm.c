/*
 * mm-naive.c - The least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by allocating a
 * new page as needed.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

#include "mm.h"
#include "memlib.h"

static char* heap_base = NULL;
static size_t heap_size = 0;
static void* first_block = NULL;

/* always use 16-byte alignment */
#define ALIGNMENT 16

// rounds up to the nearest multiple of ALIGNMENT
#define MIN_BLOCK_SIZE 32

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))

/* rounds up to the nearest multiple of mem_pagesize() */
#define PAGE_ALIGN(size) (((size) + (mem_pagesize()-1)) & ~(mem_pagesize()-1))

// Memory is allocated in the following format:
// [Header | Payload | Footer]
// Header and Footer are 8 bytes each, and Payload is the requested size aligned to 16 bytes

// Helper MACROS
#define WSIZE      8 // Word and header/footer size (bytes)
#define DSIZE      16 // Double word size (bytes)

/* Block metadata */
#define PACK(size, alloc) ((size) | (alloc)) // Pack a size and allocated bit into a word

/* Read and write memory */
#define GET(p)       (*(size_t *)(p)) // Read a word at address p
#define PUT(p, val)  (*(size_t *)(p) = (val)) // Write a word at address p

/* Extract block information */
#define GET_SIZE(p)  (GET(p) & ~(size_t)0xF) // Get the size of a block (mask out the lower 4 bits since they are used for metadata)
#define GET_ALLOC(p) (GET(p) & 0x1) // Get the allocation status of a block (check the least significant bit) 1 if allocated, 0 if free.

/*
 * HDRP - returns a pointer to the header of a block
 */
static inline void* HDRP(void* bp)
{
    return (char*)bp - WSIZE;
}

/*
 * FTRP - returns a pointer to the footer of a block
 */
static inline void* FTRP(void* bp)
{
    return (char*)bp + GET_SIZE(HDRP(bp)) - DSIZE;
}

/*
 * NEXT_BLKP - returns a pointer to the next block
 */
static inline void* NEXT_BLKP(void* bp)
{
    return (char*)bp + GET_SIZE(HDRP(bp));
}

/*
 * PREV_BLKP - returns a pointer to the previous block
 */
static inline void* PREV_BLKP(void* bp)
{
    return (char*)bp - GET_SIZE((char*)bp - DSIZE);
}

// Extend the heap by mapping a new page and initializing it as a free block
static void* extend_heap(size_t need)
{
    size_t chunk = PAGE_ALIGN(need + 2 * WSIZE);
    char* base = mem_map(chunk);
    if (base == NULL)
        return NULL;

    size_t block_size = chunk - 2 * WSIZE;          // multiple of 16 since chunk is page-multiple
    char* bp = base + DSIZE;                        // payload is 16-byte aligned

    PUT(base, (size_t)first_block);                 // padding word = link to previous chunk's first block
    PUT(HDRP(bp), PACK(block_size, 0));
    PUT(FTRP(bp), PACK(block_size, 0));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));           // epilogue

    first_block = bp;                               // new chunk becomes the head
    return bp;
}

// Find a free block that fits the required size and place the allocated block there
static void* find_and_place(size_t required)
{
    for (char* chunk = first_block; chunk != NULL;
        chunk = (char*)GET((char*)chunk - DSIZE)) {      // follow padding-word link
        for (char* bp = chunk; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
            size_t bsize = GET_SIZE(HDRP(bp));
            if (!GET_ALLOC(HDRP(bp)) && bsize >= required) {
                size_t rem = bsize - required;
                if (rem >= MIN_BLOCK_SIZE) {
                    PUT(HDRP(bp), PACK(required, 1));
                    PUT(FTRP(bp), PACK(required, 1));
                    char* nb = NEXT_BLKP(bp);
                    PUT(HDRP(nb), PACK(rem, 0));
                    PUT(FTRP(nb), PACK(rem, 0));
                }
                else {
                    PUT(HDRP(bp), PACK(bsize, 1));
                    PUT(FTRP(bp), PACK(bsize, 1));
                }
                return bp;
            }
        }
    }
    return NULL;
}

// Initializing allocator as a single free block in the first page of memory.
int mm_init(void)
{
    size_t page_size = mem_pagesize();
    size_t block_size;
    char* base;

    // Reset allocator state
    heap_base = NULL;
    heap_size = 0;
    first_block = NULL;

    // Map one page of memory
    base = mem_map(page_size);

    if (base == NULL)
        return -1;

    // Calculate the size of the initial free block
    // Reserve 8 bytes for initial padding and 8 bytes for the footer
    block_size = (page_size - 2 * WSIZE) & ~(size_t)(DSIZE - 1);

    // Save the mapped region information
    heap_base = base;
    heap_size = page_size;

    // Initialize the initial free block
    first_block = base + 2 * WSIZE;

    // Write the free block header
    PUT(HDRP(first_block), PACK(block_size, 0));

    // Write the free block footer
    PUT(FTRP(first_block), PACK(block_size, 0));

    // Initialize the epilogue header
    PUT(HDRP(NEXT_BLKP(first_block)), PACK(0, 1));

    return 0;
}

// Extend the heap by mapping a new page and initializing it as a free block
void* mm_malloc(size_t size)
{
    if (size == 0 || size > SIZE_MAX - ALIGNMENT - DSIZE)
        return NULL;

    size_t required = ALIGN(size) + DSIZE;
    if (required < MIN_BLOCK_SIZE)
        required = MIN_BLOCK_SIZE;

    void* bp = find_and_place(required);
    if (bp == NULL) {
        if (extend_heap(required) == NULL)
            return NULL;
        bp = find_and_place(required);
    }
    return bp;
}

// Free a block by marking it as free and updating its header and footer
void mm_free(void* ptr)
{
    if (ptr == NULL)
        return;

    size_t blockSize = GET_SIZE(HDRP(ptr));

    PUT(HDRP(ptr), PACK(blockSize, 0));
    PUT(FTRP(ptr), PACK(blockSize, 0));
}