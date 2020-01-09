/* mm.c - a simple dynamic memory allocator based on an implicit
 * free list with immediate boundary-tag coalescing.
 * 
 * Note: this allocator uses a model of the memory system
 * provided by the memlib.c package (max heap size: 20MB).
 * 
 * Allocator: implicit free list.
 * heap block: boundary tags.
 * mm_malloc: under progress...
 * mm_free: not yet start
 * mm_realloc: not yet start
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "ateam",
    /* First member's full name */
    "Harry Bovik",
    /* First member's email address */
    "bovik@cs.cmu.edu",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

/* private global variables */
static char *heap_listp;

/* private functions */
static void *extend_heap(size_t words);
static void *find_fit(size_t asize);

/* basic constants and macros */
#define WSIZE 4             /* word size (bytes) */
#define DSIZE 8             /* double word size (bytes) */
#define CHUNKSIZE (1<<12)   /* extend heap by 4kB */

#define MAX(x, y) ((x) > (y)? (x):(y))

/* pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size)|(alloc))

/* read and write a word at address p */
#define GETW(p)       (*(unsigned int *)(p))
#define PUTW(p, val)  (*(unsigned int *)(p) = (val))

/* read the size and allocated fields from address p */
#define GET_SIZE(p)   (GETW(p) & 0x7)
#define GET_ALLOC(p)  (GETW(p) & 0x1)

/* given block ptr bp, compute address of its header and footer */
#define HDRP(bp)      ((char *)(bp) - WSIZE)
#define FTRP(bp)      ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* 
 * mm_init - initialize the malloc package.
 * return 0 on success, -1 on error
 */
int mm_init(void)
{
    /* create the initial empty heap */
    if((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1)
        return -1;
    PUTW(heap_listp, 0);                           /* alignment padding */
    PUTW(heap_listp + (WSIZE*1), PACK(DSIZE, 1));  /* prologue header */
    PUTW(heap_listp + (WSIZE*2), PACK(DSIZE, 1));  /* prologue footer */
    PUTW(heap_listp + (WSIZE*3), PACK(0, 1));      /* epilogue header */

    heap_listp += (WSIZE*2);

    /* extend the empty heap size by 4kB */
    if(extend_heap(CHUNKSIZE/WSIZE) == NULL)
        return -1;

    return 0;
}

/* 
 * The extend_heap function is invoked in two different circumstances:
 * (1) when the heap is initialized
 * (2) when mm_malloc is unable to find a suitable fit.
 * 
 */
static void *extend_heap(size_t words)
{
    char *bp;
    size_t size;

    /* allocate an even number of words to maintain allignment */
    size  =  ALIGN(words*WSIZE);
    if((bp = mem_sbrk(size)) == (void *)-1)
        return NULL;

    /* initialize free block header and footer and the epilogue header */
    PUTW(HDRP(bp), PACK(size, 0));          /* free block header */
    PUTW(FTRP(bp), PACK(size, 0));          /* free block footer */
    PUTW(HDRP(NEXT_BLKP(bp)), PACK(0, 1));  /* new epilogue header */

    return (void *)bp;
}

static void *find_fit(size_t asize)
{
    return 0;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    int newsize = ALIGN(size + SIZE_T_SIZE);
    void *p = mem_sbrk(newsize);
    if (p == (void *)-1)
	return NULL;
    else {
        *(size_t *)p = size;
        return (void *)((char *)p + SIZE_T_SIZE);
    }
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;
    
    newptr = mm_malloc(size);
    if (newptr == NULL)
      return NULL;
    copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
    if (size < copySize)
      copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}














