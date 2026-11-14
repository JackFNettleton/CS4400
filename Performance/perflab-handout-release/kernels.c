/*******************************************
 * Solutions for the CS:APP Performance Lab
 ********************************************/

#include <stdio.h>
#include <stdlib.h>
#include "defs.h"

/* 
 * Please fill in the following student struct 
 */
student_t student = {
  "Jack Nettleton",     /* Full name */
  "u1446778@umail.utah.edu",  /* Email address */
};

/***************
 * COMPLEX KERNEL
 ***************/

 // Helper method to average the pixel values of a given pixel
short Average_pixel(short n, short i, short j, pixel* src) 
{
    return (src[RIDX(i, j, n)].red + src[RIDX(i, j, n)].green + src[RIDX(i, j, n)].blue) / 3;
}

/******************************************************
 * Your different versions of the complex kernel go here
 ******************************************************/

 /*
  * Calculations:
  * 
  * Since the struct for a pixel is 6 bytes, we know that touching a source and destination at the same time will be 12 bytes.
  * A typical L1 cache is 32KB, which is 32 * 1024 = 32768 bytes. Dividing this by 12 gives us 2730.6667, which means we can fit approximately 2730 pixels in the cache at a time.
  * 
  * It is known that this image has a dimension (n) which is a multiple of 32. 
  * Therefore, we can process the image in 32x32 tiles, which is 1024 pixels per tile. 
  * This is well within the cache limit of 2730 pixels, allowing us to keep both the source rows and destination columns in cache while working on a tile.
  */

static const int BLOCK = 32;

/*
 * opt_complex - An optimized version of complex
 */
char opt_complex_descr[] = "opt_complex: Optimized implementation";
void opt_complex(int n, pixel* src, pixel* dest)
{
    int i, j, r, c;

    // Partition the image in BLOCK x BLOCK tiles so both the source rows and the destination columns stay in the cache while working on a tile.
    for (i = 0; i < n; i += BLOCK)
        for (j = 0; j < n; j += BLOCK)

			// Process a BLOCK x BLOCK tile, first iterating over the rows of the tile, then iterating over the columns of the tile.
            for (r = i; r < i + BLOCK; r++)
            {
                pixel* d = dest + (n - j - 1) * n + (n - r - 1); // dest(n-j-1, n-r-1) 
                pixel* s = src + r * n + j;                      // src(r, j)

                for (c = 0; c < BLOCK; c++)
                {
                    unsigned short avg = (s->red + s->green + s->blue) / 3;

                    d->red = d->green = d->blue = avg;

                    s++;      // next pixel in the row
                    d -= n;   // previous row in dest
                }
            }
}

/* 
 * naive_complex - The naive baseline version of complex 
 */
char naive_complex_descr[] = "naive_complex: Naive baseline implementation";
void naive_complex(int n, pixel *src, pixel *dest)
{
  int i, j;

  for(i = 0; i < n; i++)
    for(j = 0; j < n; j++)
    {

      dest[RIDX(n - j - 1, n - i - 1, n)].red = ((int)src[RIDX(i, j, n)].red + (int)src[RIDX(i, j, n)].green + (int)src[RIDX(i, j, n)].blue) / 3;
      
      dest[RIDX(n - j - 1, n - i - 1, n)].green = ((int)src[RIDX(i, j, n)].red + (int)src[RIDX(i, j, n)].green + (int)src[RIDX(i, j, n)].blue) / 3;
      
      dest[RIDX(n - j - 1, n - i - 1, n)].blue = ((int)src[RIDX(i, j, n)].red + (int)src[RIDX(i, j, n)].green + (int)src[RIDX(i, j, n)].blue) / 3;

    }
}


/* 
 * complex - Your current working version of complex
 * IMPORTANT: This is the version you will be graded on
 */
char complex_descr[] = "complex: Current working version";
void complex(int n, pixel *src, pixel *dest)
{
  opt_complex(n, src, dest);
}

/*********************************************************************
 * register_complex_functions - Register all of your different versions
 *     of the complex kernel with the driver by calling the
 *     add_complex_function() for each test function. When you run the
 *     driver program, it will test and report the performance of each
 *     registered test function.  
 *********************************************************************/

void register_complex_functions() {
  add_complex_function(&complex, complex_descr);
  add_complex_function(&naive_complex, naive_complex_descr);
}


/***************
 * MOTION KERNEL
 **************/

/***************************************************************
 * Various helper functions for the motion kernel
 * You may modify these or add new ones any way you like.
 **************************************************************/


/* 
 * weighted_combo - Returns new pixel value at (i,j) 
 */
static pixel weighted_combo(int n, int i, int j, pixel *src) 
{
  int ii, jj;
  pixel current_pixel;

  int red, green, blue;
  red = green = blue = 0;

  int num_neighbors = 0;
  for(ii=0; ii < 3; ii++)
    for(jj=0; jj < 3; jj++) 
      if ((i + ii < n) && (j + jj < n)) 
      {
	num_neighbors++;
	red += (int) src[RIDX(i+ii,j+jj,n)].red;
	green += (int) src[RIDX(i+ii,j+jj,n)].green;
	blue += (int) src[RIDX(i+ii,j+jj,n)].blue;
      }
  
  current_pixel.red = (unsigned short) (red / num_neighbors);
  current_pixel.green = (unsigned short) (green / num_neighbors);
  current_pixel.blue = (unsigned short) (blue / num_neighbors);
  
  return current_pixel;
}

/******************************************************
 * Your different versions of the motion kernel go here
 ******************************************************/


 // Helper struct to hold the sum of the RGB values of a window
typedef struct {
    int red;
    int green;
    int blue;
} rgb_sum;

// Helper function to return the minimum of two integers
static inline int min_int(int a, int b)
{
    return a < b ? a : b;
}

// Helper function to update the sum of the RGB values in a window
static inline void add_column(rgb_sum* sum, const pixel* src, int n,
    int top, int rows, int col)
{
    int r;
    for (r = 0; r < rows; r++)
    {
        const pixel* p = src + (top + r) * n + col;
        sum->red += p->red;
        sum->green += p->green;
        sum->blue += p->blue;
    }
}

// Helper function to update the sum of the RGB values in a window
static inline void sub_column(rgb_sum* sum, const pixel* src, int n,
    int top, int rows, int col)
{
    int r;
    for (r = 0; r < rows; r++)
    {
        const pixel* p = src + (top + r) * n + col;
        sum->red -= p->red;
        sum->green -= p->green;
        sum->blue -= p->blue;
    }
}

// Helper function to store the average of the RGB values in the target pixel
static inline void store_average(pixel* target, const rgb_sum* sum, int count)
{
    target->red = sum->red / count;
    target->green = sum->green / count;
    target->blue = sum->blue / count;
}

 /*
 * opt_motion - Sliding-window optimized version of motion
 *
 * Each output pixel is the average of the (up to) 3x3 region beginning
 * at (i,j). Moving one pixel right, we remove the leftmost column of the
 * window and add the new rightmost column instead of re-summing all 9
 * pixels.
 * 
 * I probably could have used pointer arithmetic and Row-Major order to make this even faster, but I wanted to keep it simple and readable. The performance is already quite good.
 */
char opt_motion_descr[] = "opt_motion: Optimized implementation";

void opt_motion(int n, pixel* src, pixel* dst)
{
    int i, j, c;

    for (i = 0; i < n; i++)
    {
		// Window height: shrinks to 2 or 1 near the bottom edge.
        int rows = min_int(3, n - i);

        // Build the initial window covering columns 0..2.
        rgb_sum sum = { 0, 0, 0 };
        for (c = 0; c < min_int(3, n); c++)
            add_column(&sum, src, n, i, rows, c);

        for (j = 0; j < n; j++)
        {
			// Window width: shrinks to 2 or 1 near the right edge.
            int width = min_int(3, n - j);

            store_average(dst + i * n + j, &sum, rows * width);

			// Update the sum for the next column: remove the leftmost column and add the new rightmost column.
            sub_column(&sum, src, n, i, rows, j);
            if (j + 3 < n)
                add_column(&sum, src, n, i, rows, j + 3);
        }
    }
}

/*
 * naive_motion - The naive baseline version of motion 
 */
char naive_motion_descr[] = "naive_motion: Naive baseline implementation";
void naive_motion(int n, pixel *src, pixel *dst) 
{
  int i, j;
    
  for (i = 0; i < n; i++)
      for (j = 0; j < n; j++)
        dst[RIDX(i, j, n)] = weighted_combo(n, i, j, src);
}


/*
 * motion - Your current working version of motion. 
 * IMPORTANT: This is the version you will be graded on
 */
char motion_descr[] = "motion: Current working version";
void motion(int n, pixel *src, pixel *dst) 
{
  opt_motion(n, src, dst);
}

/********************************************************************* 
 * register_motion_functions - Register all of your different versions
 *     of the motion kernel with the driver by calling the
 *     add_motion_function() for each test function.  When you run the
 *     driver program, it will test and report the performance of each
 *     registered test function.  
 *********************************************************************/

void register_motion_functions() {
  add_motion_function(&motion, motion_descr);
  add_motion_function(&naive_motion, naive_motion_descr);
}
