/* mm.c - a simple dynamic memory allocator based on an implicit
 * free list with immediate boundary-tag coalescing.
 * 
 * Note: this allocator uses a model of the memory system
 * provided by the memlib.c package (max heap size: 20MB).
 * 
 * Allocator: implicit free list.
 * heap block: boundary tags on both free and allocated blocks.
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

/* switch between first-fit / next-fit search by using conditional compilation:
 * 1. define nothing - using first-fit search
 * 2. define NEXT_FIT - using next-fit search
 */
#define NEXT_FIT

/* private global variables */
static char *heap_listp;
#ifdef NEXT_FIT
static char *prev_hit;
#endif

/* private functions */
static void *extend_heap(size_t size);
static void *find_fit(size_t asize);
static void *coalesce(void *bp);
static void *place(void *bp, size_t asize);

/* heap checker */
void mm_checkheap(int verbose);
static void checkheap(int verbose);
static void checkblock(void *bp);
static void printblock(void *bp);

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
#define GET_SIZE(p)   (GETW(p) & ~0x7)
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
    #ifdef NEXT_FIT
    prev_hit = heap_listp;  /* init the next-fit pointer */
    #endif

    /* extend the empty heap size by 4kB */
    if(extend_heap(2*DSIZE) == NULL)
        return -1;

    return 0;
}

/* 
 * mm_malloc - 
 * Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t asize;
    size_t extendsize;
    char *bp;

    /* ignore spurious requests */
    if(size == 0)
        return NULL;

    /* min block size = 4 words (header + footer + 2 words free block) */
    if(size <= DSIZE)
        asize = 2*DSIZE;
    else
        asize = ALIGN(size + 2*WSIZE);

    /* search the free list for a fit */
    if((bp = find_fit(asize)) != NULL) {
        bp = place(bp, asize);
        return (void *)bp;
    }

    /* no fit found, extend heap to place the block */
    extendsize = MAX(asize, CHUNKSIZE);
    if((bp = extend_heap(extendsize)) == NULL)
        return NULL;

    bp = place(bp, asize);
    return (void *)bp;
}

/*
 * mm_free - Freeing a block and coalesce prev/next free block if exist.
 */
void mm_free(void *bp)
{
    size_t size = GET_SIZE(HDRP(bp));

    PUTW(HDRP(bp), PACK(size, 0));
    PUTW(FTRP(bp), PACK(size, 0));

    coalesce(bp);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    char *new_bp;
    size_t old_size;

    /* ignore spurious requests */
    if(ptr == NULL && size == 0) {
        return NULL;
    }
    /* if ptr is NULL, the call is equivalent to mm malloc(size) */
    else if(ptr == NULL) {
        return mm_malloc(size);
    }
    /* if size is equal to zero, the call is equivalent to mm free(ptr) */
    else if(size == 0) {
        mm_free(ptr);
        return NULL;
    }

    /* allocate the new block */
    if((new_bp = mm_malloc(size)) == NULL)
        return NULL;

    old_size = GET_SIZE(HDRP(ptr)) - 2*WSIZE;  /* content size of the old block */

    /* The contents of the new block are same as the old ptr block 
       (up to the minimum of the old and new block sizes). */
    if(old_size <= size) {
        memcpy(new_bp, ptr, old_size);
    }
    else {
        memcpy(new_bp, ptr, size);
    }
    
    mm_free(ptr);  /* free the old block */
    return (void *)new_bp;
}

/* 
 * mm_checkheap - Check the heap for correctness
 * This function is meant to be called through gdb
 */
void mm_checkheap(int verbose)  
{ 
    checkheap(verbose);
}

/* 
 * internal helper functions
 */

/* 
 * The extend_heap function is invoked in two different circumstances:
 * (1) when the heap is initialized
 * (2) when mm_malloc is unable to find a suitable fit.
 */
static void *extend_heap(size_t size)
{
    char *bp;

    /* allocate an even number of words to maintain allignment */
    size  =  ALIGN(size);
    if((bp = mem_sbrk(size)) == (void *)-1)
        return NULL;

    /* initialize free block header and footer and the epilogue header */
    PUTW(HDRP(bp), PACK(size, 0));          /* free block header */
    PUTW(FTRP(bp), PACK(size, 0));          /* free block footer */
    PUTW(HDRP(NEXT_BLKP(bp)), PACK(0, 1));  /* new epilogue header */

    /* coalesce if the previous block was free */
    return coalesce(bp);
}

/*
 * find_fit - first fit search / next fit search
 */
static void *find_fit(size_t asize)
{
    char *bp;

    #ifdef NEXT_FIT
    /* start searching from the last position */
    for(bp = prev_hit; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        if(!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) {
            prev_hit = bp;  /* update the new next-fit position */
            return (void *)bp;
        }
    }

    /* if failed, start searching from the beginning */
    for(bp = heap_listp; bp < prev_hit; bp = NEXT_BLKP(bp)) {
        if(!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) {
            prev_hit = bp;  /* update the new next-fit position */
            return (void *)bp;
        }
    }
    #else
    for(bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        if(!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) {
            return (void *)bp;
        }
    }
    #endif

    return NULL;  /* no fit */
}

/*
 * place - update the footer and header for both the allocated block and
 * the remainder of the free block (if exist).
 * The free block only got splitted when the remainder of the free block also
 * follows the alignment requirement, otherwise the whole free block would
 * being used instead.
 * 
 * Note: Internal fragmentation increases in the case (free size - asize) <= 2.
 */
static void *place(void *bp, size_t asize)
{   
    size_t fsize = GET_SIZE(HDRP(bp));  /* size of the choosed free block */

    /* if the remainder of the free block > required min block size (4 words) */
    if((fsize - asize) >= (2*DSIZE)) {
        /* try to let small remainder on the right, big remainder on the left */
        if(asize < 96) {
            PUTW(HDRP(bp), PACK(asize, 1));  /* allocated block header */
            PUTW(FTRP(bp), PACK(asize, 1));  /* allocated block footer */
            PUTW(HDRP(NEXT_BLKP(bp)), PACK((fsize - asize), 0));  /* new free block header */
            PUTW(FTRP(NEXT_BLKP(bp)), PACK((fsize - asize), 0));  /* new free block footer */
            return bp;
        }
        else {
            PUTW(HDRP(bp), PACK((fsize - asize), 0));  /* new free block header */
            PUTW(FTRP(bp), PACK((fsize - asize), 0));  /* new free block footer */
            PUTW(HDRP(NEXT_BLKP(bp)), PACK(asize, 1));  /* allocated block header */
            PUTW(FTRP(NEXT_BLKP(bp)), PACK(asize, 1));  /* allocated block footer */
            bp = NEXT_BLKP(bp);
            return bp;
        }
    }
    else {  /* use the whole free block without splitting */
        PUTW(HDRP(bp), PACK(fsize, 1));  /* allocated block header */
        PUTW(FTRP(bp), PACK(fsize, 1));  /* allocated block footer */
        return bp;
    }
}

/* 
 * coalesce - merges adjacent free blocks using the boundary-tags coalescing technique
 */
static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    /* prev and next allocated */
    if(prev_alloc && next_alloc) {
        return bp;
    }
    /* prev allocated, next free */
    else if(prev_alloc && !next_alloc) {
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
    }
    /* prev free, next allocated */
    else if(!prev_alloc && next_alloc) {
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        bp = PREV_BLKP(bp);
    }
    /* prev and next free */
    else if(!prev_alloc && !next_alloc) {
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        bp = PREV_BLKP(bp);
    }

    PUTW(HDRP(bp), PACK(size, 0));
    PUTW(FTRP(bp), PACK(size, 0));

    #ifdef NEXT_FIT
    /* prevent prev_hit points to the coalesced block */
    if((prev_hit < NEXT_BLKP(bp)) && (prev_hit > (char *)bp)) {
        prev_hit = bp;
    }
    #endif

    return bp;
}

/* check the consistency of heap */
static void checkheap(int verbose)
{
    char *bp = heap_listp;

    /* chech prelogue block */
    if((GET_SIZE(HDRP(heap_listp)) != DSIZE) || !GET_ALLOC(HDRP(heap_listp)))
        printf("Bad prologue header\n");

    if((GET_SIZE(FTRP(heap_listp)) != DSIZE) || !GET_ALLOC(FTRP(heap_listp)))
        printf("Error: bad prologue footer\n");

    /* check heap */
    for(bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        if(verbose)
            printblock(bp);
        checkblock(bp);
    }

    /* check epilogue block */
    if ((GET_SIZE(HDRP(bp)) != 0) || !(GET_ALLOC(HDRP(bp))))
        printf("Error: bad epilogue header\n");

    if(bp != (mem_heap_hi() + 1))
        printf("Error: epilogue is not at the end of heap\n");
}

/* check the heap content */
static void checkblock(void *bp)
{
    if((size_t)bp % 8) {
        printf("Error: bp is not doubleword aligned\n");
        printblock(bp);
    }

    if(GETW(HDRP(bp)) != GETW(FTRP(bp))) {
        printf("Error: header does not match footer\n");
        printblock(bp);
    }
}

/* print the block header and footer */
static void printblock(void *bp)
{
    size_t header_size = GET_SIZE(HDRP(bp));
    size_t header_alloc = GET_ALLOC(HDRP(bp));
    size_t footer_size = GET_SIZE(FTRP(bp));
    size_t footer_alloc = GET_ALLOC(FTRP(bp));
        
    printf("%p: header: [%zu/%c] footer: [%zu/%c]\n", bp, 
           header_size, (header_alloc ? 'a' : 'f'), 
           footer_size, (footer_alloc ? 'a' : 'f')); 
}
