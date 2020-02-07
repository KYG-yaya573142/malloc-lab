/* mm.c - a simple dynamic memory allocator based on an segregated
 * free list with immediate boundary-tag coalescing.
 * 
 * Note: this allocator uses a model of the memory system
 * provided by the memlib.c package (max heap size: 20MB).
 * 
 * Allocator: segregated free list.
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

/* private global variables */
static char *heap_listp;
static char *freelist_root;

/* private functions */
static void *extend_heap(size_t size);
static void *find_fit(size_t asize);
static void *coalesce(void *bp);
static void *place(void *bp, size_t asize);
static void insert_list(void *bp);
static void detach_node(void *bp);
static void realloc_place(void *bp, size_t asize);

/* heap checker */
void mm_checkheap(int verbose);
void mm_checklist(int verbose);
static void checkheap(int verbose);
static void checkblock(void *bp);
static void printblock(void *bp);
static void checklist(int verbose);
static void printlist(void *bp);

/* basic constants and macros */
#define WSIZE 4             /* word size (bytes) */
#define DSIZE 8             /* double word size (bytes) */
#define CHUNKSIZE (1<<12)   /* extend heap by 4kB */
#define MAXN 12             /* max size class number */

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
#define GET_NEXT(bp)       (*(void **)(bp))
#define PUT_NEXT(bp, ptr)  (*(void **)(bp) = (ptr))
#define GET_PREV(bp)       (*(void **)((char *)(bp) + 4))
#define PUT_PREV(bp, ptr)  (*(void **)((char *)(bp) + 4) = (ptr))

/* 
 * mm_init - initialize the malloc package.
 * return 0 on success, -1 on error
 */
int mm_init(void)
{
    /* create the initial empty heap */
    if((heap_listp = mem_sbrk(16*WSIZE)) == (void *)-1)
        return -1;
    PUTW(heap_listp, 0);                 /* alignment padding */  /* <- freelist_root */
    /* initialize seglist, for n block, 2^n <= size < 2^(n+1) */  
    PUTW(heap_listp + (WSIZE*1), 0);     /* block size < 4 */     
    PUTW(heap_listp + (WSIZE*2), 0);     /* block size < 8 */     /* <- freelist_root + (WSIZE*2) */
    PUTW(heap_listp + (WSIZE*3), 0);     /* block size < 16 */
    PUTW(heap_listp + (WSIZE*4), 0);     /* block size < 32 */
    PUTW(heap_listp + (WSIZE*5), 0);     /* block size < 64 */
    PUTW(heap_listp + (WSIZE*6), 0);     /* block size < 128 */
    PUTW(heap_listp + (WSIZE*7), 0);     /* block size < 256 */
    PUTW(heap_listp + (WSIZE*8), 0);     /* block size < 512 */
    PUTW(heap_listp + (WSIZE*9), 0);     /* block size < 1024 */
    PUTW(heap_listp + (WSIZE*10), 0);    /* block size < 2048 */
    PUTW(heap_listp + (WSIZE*11), 0);    /* block size < 4096 */
    PUTW(heap_listp + (WSIZE*12), 0);    /* block size >= 4096 */
    PUTW(heap_listp + (WSIZE*13), PACK(DSIZE, 1));  /* prologue header */
    PUTW(heap_listp + (WSIZE*14), PACK(DSIZE, 1));  /* prologue footer */  /* <- heap_listp */
    PUTW(heap_listp + (WSIZE*15), PACK(0, 1));      /* epilogue header */

    freelist_root = heap_listp;         /* init the freelist_root ptr */
    heap_listp += (WSIZE*14);

    /* extend the empty heap size (bytes) */
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
        detach_node(bp);
        bp = place(bp, asize);
        return (void *)bp;
    }

    /* no fit found, extend heap to place the block */
    extendsize = MAX(asize, CHUNKSIZE);
    if((bp = extend_heap(extendsize)) == NULL)
        return NULL;

    detach_node(bp);
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

    bp = coalesce(bp);
    insert_list(bp);
}

/*
 * mm_realloc - use previous and next block if it is free.
 * First coalesce the prev and next block, then check the size of the new block
 * if block size > size, use this block
 * if block size < size, malloc a new block and re-insert the old block to free list
 */
void *mm_realloc(void *ptr, size_t size)
{
    char *bp, *new_bp;
    size_t old_asize = GET_SIZE(HDRP(ptr));
    size_t asize = ALIGN(size + 2*WSIZE);

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

    size_t prev_alloc = GET_ALLOC(HDRP(PREV_BLKP(ptr)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));
    size_t prev_asize = prev_alloc? 0:GET_SIZE(HDRP(PREV_BLKP(ptr)));
    size_t next_asize = next_alloc? 0:GET_SIZE(HDRP(NEXT_BLKP(ptr)));
    size_t fsize = old_asize + prev_asize + next_asize;

    if(asize > old_asize) {  /* new block is bigger than ptr */
        if(asize > fsize) {  /* realloc a new block */
            /* 
            * Because realloc2-bal.rep always realloc a block whick is bigger by 5 bytes,
            * malloc a block bigger by 8 bytes can slightly increase the util rate.
            */
            if((new_bp = mm_malloc(asize)) == NULL)
                return NULL;
            memmove(new_bp, ptr, (old_asize - 2*WSIZE));
            mm_free(ptr);
            return (void *)new_bp;
        }
        else if(prev_alloc) {
            ptr = coalesce(ptr);
            realloc_place(ptr, asize);
            return ptr;
        }
        else {
            bp = coalesce(ptr);
            memmove(bp, ptr, ((old_asize > asize)? (asize - 2*WSIZE):(old_asize - 2*WSIZE)));
            realloc_place(bp, asize);
            return bp;
        }
    }
    else {  /* new block is smaller than ptr */
        realloc_place(ptr, asize);  /* simply change the size of ptr */
        return ptr;
    }
    return ptr;
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
 * mm_checklist- Check the free list for correctness
 * This function is meant to be called through gdb
 */
void mm_checklist(int verbose)  
{ 
    checklist(verbose);
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
    bp = coalesce(bp);

    /* insert the new block to the free list */
    insert_list(bp);

    return bp;
}

/*
 * find_fit - best-fit search
 */
static void *find_fit(size_t asize)
{
    char *bp;
    char *size_class = NULL;
    size_t size = asize;
    size_t n = 0;

    /* calculate the size class n */
    while(size > 1 && n < MAXN) {
        size >>= 1;
        n++;
    }
    size_class = freelist_root + (WSIZE*n);

    /* jump to next size class if current list is empty */
    while(GET_NEXT(size_class) == NULL) {
        size_class += WSIZE;
        /* if all size class is empty */
        if(size_class > (freelist_root + MAXN*WSIZE))
            return NULL;
    }

    while(size_class <= (freelist_root + MAXN*WSIZE)) {
        for(bp = GET_NEXT(size_class); bp != NULL; bp = GET_NEXT(bp)) {
            if(asize <= GET_SIZE(HDRP(bp)))
                return (void *)bp;
        }
        size_class += WSIZE;  /* jump to next size class */
    }

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
 * 
 * Optimization:
 * First observe the following 2 trace files,
 *   binaty-bal.rep -> consistently allocating 64 and 448 bytes blocks,
 *                     then free all 448 bytes block first
 *   binaty2-bal.rep -> consistently allocating 16 and 112 bytes blocks,
 *                      then free all 112 bytes block first
 * 
 * the original place policy always put data on the left side, which leads
 * to the result data structure like below
 * +-+----------+-+----------+-+-----------+-+---------+
 * | |          | |          | |           | |         |
 * |a|   free   |a|   free   |a|    free   |a|   free  |
 * | |          | |          | |           | |         |
 * +-+----------+-+----------+-+-----------+-+---------+
 * 
 * In order to prevent this situation, we need to put the data on the
 * left/right side based on a magic number which is between 64 and 112
 * +--+--+--+--+--+--+----------+------------+-----------+
 * |  |  |  |  |  |  |          |            |           |
 * |  |  |  |  |  |  |   free   |    free    |   free    |
 * |  |  |  |  |  |  |          |            |           |
 * +--+--+--+--+--+--+----------+------------+-----------+
 */
static void *place(void *bp, size_t asize)
{   
    size_t fsize = GET_SIZE(HDRP(bp));  /* size of the choosed free block */

    /* if the remainder of the free block > required min block size (4 words) */
    if((fsize - asize) >= (2*DSIZE)) {
        if(asize < 96) {  /* allocate small block on the left side */
            PUTW(HDRP(bp), PACK(asize, 1));  /* allocated block header */
            PUTW(FTRP(bp), PACK(asize, 1));  /* allocated block footer */
            PUTW(HDRP(NEXT_BLKP(bp)), PACK((fsize - asize), 0));  /* new free block header */
            PUTW(FTRP(NEXT_BLKP(bp)), PACK((fsize - asize), 0));  /* new free block footer */
            /* update the segregated free list */
            insert_list(NEXT_BLKP(bp));
            return bp;
        }
        else {  /* allocate big block on the right */
            PUTW(HDRP(bp), PACK((fsize - asize), 0));  /* new free block header */
            PUTW(FTRP(bp), PACK((fsize - asize), 0));  /* new free block footer */
            PUTW(HDRP(NEXT_BLKP(bp)), PACK(asize, 1));  /* allocated block header */
            PUTW(FTRP(NEXT_BLKP(bp)), PACK(asize, 1));  /* allocated block footer */
            bp = NEXT_BLKP(bp);
            insert_list(PREV_BLKP(bp));
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
        PUTW(HDRP(bp), PACK(size, 0));
        PUTW(FTRP(bp), PACK(size, 0));
        return bp;
    }
    /* prev allocated, next free */
    else if(prev_alloc && !next_alloc) {
        /* detach the next free block */
        detach_node(NEXT_BLKP(bp));
        /* coalesce the prev free block */
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUTW(HDRP(bp), PACK(size, 0));
        PUTW(FTRP(bp), PACK(size, 0));
    }
    /* prev free, next allocated */
    else if(!prev_alloc && next_alloc) {
        /* detach the prev free block */
        detach_node(PREV_BLKP(bp));
        /* coalesce the next free block */
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        bp = PREV_BLKP(bp);
        PUTW(HDRP(bp), PACK(size, 0));
        PUTW(FTRP(bp), PACK(size, 0));
    }
    /* prev and next free */
    else if(!prev_alloc && !next_alloc) {
        /* detach the prev and next free block */
        detach_node(NEXT_BLKP(bp));
        detach_node(PREV_BLKP(bp));
        /* coalesce the next and prev free blocks */
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        bp = PREV_BLKP(bp);
        PUTW(HDRP(bp), PACK(size, 0));
        PUTW(FTRP(bp), PACK(size, 0));
    }

    return bp;
}

/* 
 * special place function for optimized mm_realloc
 */
static void realloc_place(void *bp, size_t asize)
{   
    size_t fsize = GET_SIZE(HDRP(bp));  /* size of the choosed free block */

    /* 
     * I found that always use the whole block in realloc would lead to a
     * super high util rate.
     * The exact reason should be further discussed.
     */
    if(0) {
        PUTW(HDRP(bp), PACK(asize, 1));  /* allocated block header */
        PUTW(FTRP(bp), PACK(asize, 1));  /* allocated block footer */
        fsize -= asize;                  /* size of the remainder of the free block */
        bp = NEXT_BLKP(bp);              /* point bp to the remainder */
        PUTW(HDRP(bp), PACK(fsize, 0));  /* new free block header */
        PUTW(FTRP(bp), PACK(fsize, 0));  /* new free block footer */
        /* update the segregated free list */
        insert_list(bp);
    }
    else {  /* use the whole free block without splitting */
        PUTW(HDRP(bp), PACK(fsize, 1));  /* allocated block header */
        PUTW(FTRP(bp), PACK(fsize, 1));  /* allocated block footer */
    }
}

/* 
 * insert bp to the segregated free list
 */
static void insert_list(void *bp)
{
    char *size_class = NULL;
    char *prev_node = NULL;
    char *next_node = NULL;
    size_t n = 0;
    size_t asize = GET_SIZE(HDRP(bp));

    /* calculate the size class n */
    while(asize > 1 && n < MAXN) {
        asize >>= 1;
        n++;
    }
    size_class = freelist_root + (WSIZE*n);

    /* bp should be put between prev and next node */
    prev_node = size_class;
    next_node = GET_NEXT(size_class);
    while(next_node != NULL) {
        if(asize > GET_SIZE(HDRP(next_node))) {
            prev_node = next_node;
            next_node = GET_NEXT(next_node);
        }
        else {
            break;
        }
    }

    /* insert bp in between prev and next node */
    if(prev_node == size_class) {
        PUT_NEXT(size_class, bp);
        PUT_PREV(bp, size_class);
        PUT_NEXT(bp, next_node);
        if(next_node != NULL)  /* if prev_node is not the last node */
            PUT_PREV(next_node, bp);
    }
    else {
        PUT_NEXT(prev_node, bp);
        PUT_PREV(bp, prev_node);
        PUT_NEXT(bp, next_node);
        if(next_node != NULL)  /* if prev_node is not the last node */
            PUT_PREV(next_node, bp);
    }
}

/* 
 * detach bp from the segregated free list 
 * 
 * Note: need to re-insert bp back to the list after manipulation finished.
 */
static void detach_node(void *bp)
{
    char *next_bp = GET_NEXT(bp);
    char *prev_bp = GET_PREV(bp);

    PUT_NEXT(GET_PREV(bp), next_bp);  /* update prev free block */
    if(next_bp != NULL)
        PUT_PREV(GET_NEXT(bp), prev_bp);  /* update next free block */
}

/* 
 * check the consistency of heap
 */
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

/* 
 * check the heap content
 */
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
    if(!GET_ALLOC(HDRP(bp))) {
        if(!GET_ALLOC(HDRP(PREV_BLKP(bp))) || !GET_ALLOC(HDRP(NEXT_BLKP(bp))))
            printf("Error: contiguous free block\n");
    }
}

/* 
 * check the segregated free list
 */
static void checklist(int verbose)
{
    char *bp;
    char *size_class;
    int n = 1;

    /* check the segregated free list */
    for(n = 1; n <= MAXN; n++) {
        size_class = (freelist_root + (n*WSIZE));
        if(verbose) 
            printf("Size class: %d ~ %d\n", (1<<n), ((1<<(n + 1)) - 1));
        for(bp = GET_NEXT(size_class); bp != NULL; bp = GET_NEXT(bp)) {
            if(verbose) 
                printlist(bp);
            /* mismatched prev and next block */
            if((GET_PREV(bp) != NULL) && (GET_NEXT(bp) != NULL)) {
                if(GET_PREV(GET_NEXT(bp)) != bp) 
                    printf("Error: the double-linked list is broken");
                if(GET_SIZE(HDRP(bp)) > GET_SIZE(HDRP(GET_NEXT(bp))))
                    printf("Error: wrong size order");
            }
            /* check if any allocated block still in the free list */
            if(GET_ALLOC(HDRP(bp)) || GET_ALLOC(FTRP(bp)))
                printf("Error: allocated block exist in the free list");
        }
    }
}

/* 
 * print the block header and footer
 */
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

/* 
 * print the segregated free list
 */
static void printlist(void *bp)
{
    size_t header_size = GET_SIZE(HDRP(bp));
    size_t header_alloc = GET_ALLOC(HDRP(bp));
    size_t footer_size = GET_SIZE(FTRP(bp));
    size_t footer_alloc = GET_ALLOC(FTRP(bp));
    void *prev_bp = GET_PREV(bp);
    void *next_bp = GET_NEXT(bp);

    printf("%p: header: [%zu/%c] footer: [%zu/%c] prev_bp: [%p] next_bp: [%p]\n", bp, 
           header_size, (header_alloc ? 'a' : 'f'), 
           footer_size, (footer_alloc ? 'a' : 'f'),
           prev_bp,
           next_bp);
}