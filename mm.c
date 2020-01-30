/* mm.c - a simple dynamic memory allocator based on an explicit
 * free list with immediate boundary-tag coalescing.
 * 
 * Note: this allocator uses a model of the memory system
 * provided by the memlib.c package (max heap size: 20MB).
 * 
 * Allocator: explicit free list (LIFO).
 * Note: This allocator is compiled with option -m32, which sets 
 * long and pointer types to 32 bits.
 * 
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
#define NEXT_FITx
#define NO_DEBUG_SIMPLE_MODE
#define TEST_REALLOC

/* private global variables */
static char *heap_listp;
static char *freelist_root;  /* start ptr for explixit free list */
#ifdef NEXT_FIT
static char *prev_hit;
#endif

/* private functions */
static void *extend_heap(size_t size);
static void *find_fit(size_t asize);
static void *coalesce(void *bp);
static void place(void *bp, size_t asize);
static void insert_root(void *bp);
static void detach(void *bp);

/* basic constants and macros */
#define WSIZE 4             /* word size (bytes) */
#define DSIZE 8             /* double word size (bytes) */
#define CHUNKSIZE (1<<12)   /* extend heap by 4kB */

#define MAX(x, y) ((x) > (y)? (x):(y))

/* pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size)|(alloc))

/* read and write a word at address p */
#define GETW(p)       (*(unsigned int *)(p))
#define PUTW(p, val)  (*(unsigned int *)(p) = (unsigned int)(val))

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

/* double-linked free list manipulations */
#define GET_PREV(bp)       ((void *)(*(unsigned int *)(bp)))
#define PUT_PREV(bp, val)  (*(unsigned int *)(bp) = (unsigned int)(val))
#define GET_NEXT(bp)       ((void *)(*((unsigned int *)(bp) + 1)))
#define PUT_NEXT(bp, val)  (*((unsigned int *)(bp) + 1) = (unsigned int)(val))

/* 
 * mm_init - initialize the malloc package.
 * return 0 on success, -1 on error
 */
int mm_init(void)
{
    /* create the initial empty heap */
    if((heap_listp = mem_sbrk(6*WSIZE)) == (void *)-1)
        return -1;
    PUTW(heap_listp, 0);                           /* alignment padding */
    PUTW(heap_listp + (WSIZE*1), 0);               /* prev free block ptr */  /* <- freelist_root */
    PUTW(heap_listp + (WSIZE*2), 0);               /* next free block ptr */
    PUTW(heap_listp + (WSIZE*3), PACK(DSIZE, 1));  /* prologue header */
    PUTW(heap_listp + (WSIZE*4), PACK(DSIZE, 1));  /* prologue footer */      /* <- heap_listp */
    PUTW(heap_listp + (WSIZE*5), PACK(0, 1));      /* epilogue header */

    freelist_root = heap_listp + (WSIZE*1);        /* init the free list root ptr */
    heap_listp += (WSIZE*4);
    #ifdef NEXT_FIT
    prev_hit = freelist_root;  /* init the next-fit pointer */
    #endif

    /* extend the empty heap size (bytes) */
    if(extend_heap(2*DSIZE) == NULL)
        return -1;

    return 0;
}

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
    bp = coalesce(bp);

    /* insert the new block at the root of the list */
    insert_root(bp);

    return bp;
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
        place(bp, asize);
        return (void *)bp;
    }

    /* no fit found, extend heap to place the block */
    extendsize = MAX(asize, CHUNKSIZE);
    if((bp = extend_heap(extendsize)) == NULL)
        return NULL;

    place(bp, asize);
    return (void *)bp;
}

/*
 * find_fit - first fit search / next fit search
 */
static void *find_fit(size_t asize)
{
    char *bp;

    /* no free block */
    if(GET_NEXT(freelist_root) == NULL) {
        return NULL;
    }

    #ifdef NEXT_FIT
    /* start searching from the last position */
    for(bp = prev_hit; bp != freelist_root; bp = GET_NEXT(bp)) {
        if(asize <= GET_SIZE(HDRP(bp))) {
            prev_hit = GET_NEXT(bp);  /* update the new next-fit position */
            return (void *)bp;
        }
    }

    /* if failed, start searching from the beginning */
    for(bp = GET_NEXT(freelist_root); bp != prev_hit; bp = GET_NEXT(bp)) {
        if(asize <= GET_SIZE(HDRP(bp))) {
            prev_hit = GET_NEXT(bp);  /* update the new next-fit position */
            return (void *)bp;
        }
    }
    #else
    for(bp = GET_NEXT(freelist_root); bp != freelist_root; bp = GET_NEXT(bp)) {
        if(asize <= GET_SIZE(HDRP(bp))) {
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
static void place(void *bp, size_t asize)
{   
    size_t fsize = GET_SIZE(HDRP(bp));  /* size of the choosed free block */

    detach(bp);
    /* if the remainder of the free block > required min block size (4 words) */
    if((fsize - asize) >= (2*DSIZE)) {
        PUTW(HDRP(bp), PACK(asize, 1));  /* allocated block header */
        PUTW(FTRP(bp), PACK(asize, 1));  /* allocated block footer */
        fsize -= asize;                  /* size of the remainder of the free block */
        bp = NEXT_BLKP(bp);              /* point bp to the remainder */
        PUTW(HDRP(bp), PACK(fsize, 0));  /* new free block header */
        PUTW(FTRP(bp), PACK(fsize, 0));  /* new free block footer */
        /* update the explicit free list (LIFO) */
        insert_root(bp);
    }
    else {  /* use the whole free block without splitting */
        PUTW(HDRP(bp), PACK(fsize, 1));  /* allocated block header */
        PUTW(FTRP(bp), PACK(fsize, 1));  /* allocated block footer */
    }
}

/*
 * mm_free - Freeing a block and coalesce prev/next free block if exist.
 */
void mm_free(void *bp)
{
    size_t size = GET_SIZE(HDRP(bp));

    PUTW(HDRP(bp), PACK(size, 0));
    PUTW(FTRP(bp), PACK(size, 0));

    bp = coalesce(bp);
    /* insert the free block at the root of the list */
    insert_root(bp);
}

/* 
 * coalesce - merges adjacent free blocks using the boundary-tags coalescing technique
 */
static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    #ifdef NEXT_FIT
    /* prevent prev_hit points to the coalesced block */
    while((prev_hit == PREV_BLKP(bp)) || (prev_hit == NEXT_BLKP(bp))) {
        prev_hit = GET_NEXT(prev_hit);
    }
    #endif

    /* prev and next allocated */
    if(prev_alloc && next_alloc) {
        return bp;
    }
    /* prev allocated, next free */
    else if(prev_alloc && !next_alloc) {
        /* detach the next free block */
        detach(NEXT_BLKP(bp));
        /* coalesce the prev free block */
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUTW(HDRP(bp), PACK(size, 0));
        PUTW(FTRP(bp), PACK(size, 0));
    }
    /* prev free, next allocated */
    else if(!prev_alloc && next_alloc) {
        /* detach the prev free block */
        detach(PREV_BLKP(bp));
        /* coalesce the next free block */
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        bp = PREV_BLKP(bp);
        PUTW(HDRP(bp), PACK(size, 0));
        PUTW(FTRP(bp), PACK(size, 0));
    }
    /* prev and next free */
    else if(!prev_alloc && !next_alloc) {
        /* detach the prev and next free block */
        detach(NEXT_BLKP(bp));
        detach(PREV_BLKP(bp));
        /* coalesce the next and prev free blocks */
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        bp = PREV_BLKP(bp);
        PUTW(HDRP(bp), PACK(size, 0));
        PUTW(FTRP(bp), PACK(size, 0));
    }

    return bp;
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    char *new_bp;
    size_t old_size = GET_SIZE(HDRP(ptr)) - 2*WSIZE;  /* content size of the old block */

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

    if(size > old_size) {  /* allocate a larger space */
        /* allocate the new block */
        if((new_bp = mm_malloc(size)) == NULL)
            return NULL;
        memcpy(new_bp, ptr, old_size);
        mm_free(ptr);  /* free the old block */
        return (void *)new_bp;
    }
    else {  /**/
        size = ALIGN(size + 2*WSIZE);
        old_size += 2*WSIZE;
        if((old_size - size) >= (2*DSIZE)) {
            /* shrink the block to size */
            PUTW(HDRP(ptr), PACK(size, 1));
            PUTW(FTRP(ptr), PACK(size, 1));
            /* free the remainder block */
            PUTW(HDRP(NEXT_BLKP(ptr)), PACK((old_size - size), 0));
            PUTW(FTRP(NEXT_BLKP(ptr)), PACK((old_size - size), 0));
            insert_root(NEXT_BLKP(ptr));
            return ptr;
        }
        else {
            return ptr;
        }
    }
}

/* helper functions */
/* insert bp to the root of exlicit free list */
static void insert_root(void *bp)
{
    char *next_bp = GET_NEXT(freelist_root);

    /* insert the free block at the root of the list */
    if(next_bp == NULL) {
        PUT_NEXT(freelist_root, bp);
        PUT_PREV(freelist_root, bp);
        PUT_NEXT(bp, freelist_root);
        PUT_PREV(bp, freelist_root);
    }
    else {
        PUT_NEXT(freelist_root, bp);
        PUT_NEXT(bp, next_bp);
        PUT_PREV(next_bp, bp);
        PUT_PREV(bp, freelist_root);
    }
}

/* detach bp from the explicit free list 
 * Note: need to re-insert bp back to the list after manipulation finished.
 */
static void detach(void *bp)
{
    char *next_bp = GET_NEXT(bp);
    char *prev_bp = GET_PREV(bp);

    /* update the explicit free list (LIFO) */
    if(next_bp == prev_bp) {  /* if there's only 1 free block left */
    next_bp = NULL;
    prev_bp = NULL;
    }
    PUT_NEXT(GET_PREV(bp), next_bp);  /* update prev free block */
    PUT_PREV(GET_NEXT(bp), prev_bp);  /* update next free block */
}