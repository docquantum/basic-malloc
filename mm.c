/* 
 * mm-handout.c -  Simple allocator based on implicit free lists and 
 *                 first fit placement (similar to lecture4.pptx). 
 *                 It does not use boundary tags and does not perform
 *                 coalescing. Thus, it tends to run out of memory 
 *                 when used to allocate objects in large traces  
 *                 due to external fragmentation.
 *
 * Each block has a header of the form:
 * 
 *      31                     3  2  1  0 
 *      -----------------------------------
 *     | s  s  s  s  ... s  s  s  0  0  a/f
 *      ----------------------------------- 
 * 
 * where s are the meaningful size bits and a/f is set 
 * iff the block is allocated. The list has the following form:
 *
 * begin                                                         end
 * heap                                                          heap  
 *  -----------------------------------------------------------------   
 * |  pad   | hdr(8:a) |   pad   | zero or more usr blks | hdr(8:a) |
 *  -----------------------------------------------------------------
 *    four  | prologue |  four   |                       | epilogue |
 *    bytes | block    |  bytes  |                       | block    |
 *
 */
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "mm.h"
#include "memlib.h"


/* Team structure */
team_t team = {
	/* Team name */
	"Daniel Shchur",
	/* Full name */
	"Daniel Shchur",
	/* Email address */
	"daniel.shchur@huskers.unl.edu",
	"",""
};


/* $begin mallocmacros */
/* Basic constants and macros */

/* You can add more macros and constants in this section */
#define WSIZE       4       /* word size (bytes) */  
#define DSIZE       8       /* doubleword size (bytes) */
#define CHUNKSIZE  (1<<12)  /* initial heap size (bytes) */
#define OVERHEAD    8       /* overhead of headers (bytes) */

#define MAX(x, y)		((x) > (y)? (x) : (y))  

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc)	((size) | (alloc))

/* Read and write a word at address p */
#define GET(p)			(*(size_t *)(p))
#define PUT(p, val)		(*(size_t *)(p) = (val))  

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)		(GET(p) & ~0x7)
#define GET_ALLOC(p)	(GET(p) & 0x1)

/* Given block ptr bp, compute address of its header and footer*/
#define HDRP(bp)		((char *)(bp) - WSIZE)
#define FTRP(bp)		((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and prev blocks */
#define NEXT_BLKP(bp)	((char *)(bp) + GET_SIZE((char *)(bp) - WSIZE))
#define PREV_BLKP(bp)	((char *)(bp) - GET_SIZE((char *)(bp) - DSIZE))

/* $end mallocmacros */


/* Global variables */
static char *heap_listp;  /* pointer to first block */  
// static char *free_listp;  /* pointer to free list */

// typedef struct Node
// {
// 	char * prev;
// 	char * next;
// }Node;




/* function prototypes for internal helper routines */
static void *extend_heap(size_t words);
static void place(void *bp, size_t asize);
static void *find_fit(size_t asize);
static void printblock(void *bp);
static int mm_coalesce(void *bp);
static void mm_memcpy(void * dest, void * src);

/* 
 * mm_init - Initialize the memory manager 
 */
/* $begin mminit */
int mm_init(void) 
{
   /* create the initial empty heap */
   if ((heap_listp = mem_sbrk(4*WSIZE)) == NULL)
		return -1;
   PUT(heap_listp, KEY);               /* alignment padding */
   PUT(heap_listp+WSIZE, PACK(DSIZE, 0));  /* prologue header */ 
   PUT(heap_listp+DSIZE, PACK(0, 0));  /* empty word*/ 
   PUT(heap_listp+DSIZE+WSIZE, PACK(0, 0));   /* epilogue header */
   heap_listp += (DSIZE);


   /* Extend the empty heap with a free block of CHUNKSIZE bytes */
   if (extend_heap(CHUNKSIZE/WSIZE) == NULL)
		return -1;
   return (int) heap_listp;
}
/* $end mminit */

/* 
 * mm_malloc - Allocate a block with at least size bytes of payload 
 */
/* $begin mmmalloc */
void *mm_malloc(size_t size) 
{
	size_t asize;      /* adjusted block size */
	size_t extendsize; /* amount to extend heap if no fit */
	char *bp;      
	
	//printf("call mm_malloc\n");

	/* Ignore spurious requests */
	if (size <= 0)
		return NULL;

	/* Adjust block size to include overhead and alignment reqs. */
	if (size <= WSIZE)
		asize = WSIZE + OVERHEAD;
	else
		asize = DSIZE * ((size + (OVERHEAD) + (DSIZE-1)) / DSIZE);
	
	//printf("asize = %d\n", asize);

	/* Search the free list for a fit */
	if ((bp = find_fit(asize)) != NULL) {
		place(bp, asize);
		return bp;
	}

	/* No fit found. Get more memory and place the block */
	extendsize = MAX(asize,CHUNKSIZE);
	//printf("extendsize = %d\n", extendsize);
	if ((bp = extend_heap(extendsize/WSIZE)) == NULL) {
		printf("mm_malloc = NULL\n");
		return NULL;
	}

	//printf("return address = %p\n", bp);

	place(bp, asize);

	mm_checkheap(0);

	return bp;
} 
/* $end mmmalloc */

/* 
 * mm_free - Free a block 
 * 
 * Given the alloced bit is set, tries to coalesce which
 * frees be default, else OR's header and footer with 1,
 * else print error to stderr.
 */
/* $begin mmfree */
void mm_free(void *bp)
{
	if(bp == NULL){
		fprintf(stderr, "Error: memory not alloced or corrupted");
		return;
	}

	if(!GET_ALLOC(HDRP(bp))){
		if(mm_coalesce(bp)){
			*HDRP(bp) |= 1;
			*FTRP(bp) |= 1;
		}
	} else {
		fprintf(stderr, "Error: memory not alloced or corrupted");
		return;
	}
	mm_checkheap(0);
		
}

/* $end mmfree */

/**
 * mm_coalesce - Coalesce the freespace around a block
 * 
 * Given a block pointer that has been freed, find empty
 * blocks near it to be combined into a large free chunk.
 * 
 * Returns 0 on success, 1 on failure
 * 
 */
static int mm_coalesce(void *bp)
{	
	size_t nextBlock = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
	size_t prevBlock = GET_ALLOC(HDRP(PREV_BLKP(bp)));
	size_t size = GET_SIZE(HDRP(bp));

	if(prevBlock && nextBlock){
		size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 1));
		PUT(FTRP(PREV_BLKP(bp)), PACK(size, 1));
		return 0;
	} else if(prevBlock && !nextBlock){
		size += GET_SIZE(HDRP(PREV_BLKP(bp)));
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 1));
		PUT(FTRP(PREV_BLKP(bp)), PACK(size, 1));
		return 0;
	} else if(!prevBlock && nextBlock){
		size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
		PUT(HDRP(bp), PACK(size, 1));
		PUT(FTRP(bp), PACK(size, 1));
		return 0;
	}
	return 1;
}

/*
 * mm_realloc - naive implementation of mm_realloc
 * 
 * Given an alloced pointer, get size, add to that size, pass to malloc to
 * get new block.
 * 
 */
void *mm_realloc(void *ptr, size_t size)
{
	/**
	 * 1. if ptr is NULL, the call is equivalent to mm malloc(size);
	 * 2. if size is equal to zero, the call is equivalent to mm free(ptr);
	 * 3. if ptr is not NULL, it must have been returned by an earlier call to mm malloc or mm realloc.
	 * 
	 * The call to mm realloc changes the size of the memory block pointed to by ptr (the old block)
	 * to size bytes and returns the address of the new block. Notice that the address of the 
	 * new block might be the same as the old block, or it might be different, depending on your
	 * implementation, the amount of internal fragmentation in the old block, and the size of the
	 * realloc request. The contents of the new block are the same as those of the old ptr block, up
	 * to the minimum of the old and new sizes. Everything else is uninitialized.
	 * 
	 * For example, if the old block is 8 bytes and the new block is 12 bytes, then the first 8 bytes
	 * of the new block are identical to the first 8 bytes of the old block and the last 4 bytes are
	 * uninitialized.
	 * 
	 * Similarly, if the old block is 8 bytes and the new block is 4 bytes, then the
	 * contents of the new block are identical to the first 4 bytes of the old block.
	 */

	if(ptr == NULL && size > 0){
		return mm_malloc(size);
	} else if(ptr != NULL && size == 0) {
		mm_free(ptr);
		return NULL;
	} else if(ptr != NULL && size > 0) {
		size_t oldSize = GET_SIZE(HDRP(ptr));

		/* Adjust block size to include overhead and alignment reqs. */
		if (size <= WSIZE) {
			size = WSIZE + OVERHEAD;
		}
		else {
			size = DSIZE * ((size + OVERHEAD + DSIZE - 1) / DSIZE);
		}

		// if sizes are the same or the difference is less than a DSIZE, no changes needed
		if(oldSize == size || (oldSize - size <= WSIZE)) {
			return ptr;
		} else {
			
			// If size is less than old size, shrink and free end
			if(size < oldSize) {
				if ((oldSize - size) >= DSIZE) {
					PUT(HDRP(ptr), PACK(size, 0));
					PUT(FTRP(ptr), PACK(size, 0));
					PUT(HDRP(NEXT_BLKP(ptr)), PACK(oldSize-size, 1));
					PUT(FTRP(NEXT_BLKP(ptr)), PACK(oldSize-size, 1));
					mm_coalesce(NEXT_BLKP(ptr));
				}
				else { 
					PUT(HDRP(ptr), PACK(oldSize, 0));
					PUT(FTRP(ptr), PACK(oldSize, 0));
				}
				return ptr;
			}
			
			// If size is greater than old size, grow and free end
			else if(size > oldSize) {
				size_t nextSize = GET_SIZE(HDRP(NEXT_BLKP(ptr))) + oldSize;
				if(GET_ALLOC(HDRP(NEXT_BLKP(ptr))) && nextSize >= size){
					if ((nextSize - size) >= DSIZE) {
						PUT(HDRP(ptr), PACK(size, 0));
						PUT(FTRP(ptr), PACK(size, 0));
						PUT(HDRP(NEXT_BLKP(ptr)), PACK(nextSize-size, 1));
						PUT(FTRP(NEXT_BLKP(ptr)), PACK(nextSize-size, 1));
						mm_coalesce(NEXT_BLKP(ptr));
					}
					else { 
						PUT(HDRP(ptr), PACK(nextSize, 0));
						PUT(FTRP(ptr), PACK(nextSize, 0));
					}
					
					return ptr;
				}
				// else malloc
				void * newPtr;
				if((newPtr = mm_malloc(size)) == NULL){
					return NULL;
				}
				// copy memory
				mm_memcpy(newPtr, ptr);
				// free
				mm_free(ptr);
				return newPtr;
			}

		}
	}

	return NULL;
}

/**
 * mm_memcpy - copies memory to destination from source
 * 
 * Memory is coppied block by block and truncated if
 * destination is smaller than source
 */ 

void mm_memcpy(void * dest, void * src)
{
	for(size_t i = 0; i < GET_SIZE(HDRP(dest))-OVERHEAD; i++){
		((char *) dest)[i] = ((char *) src)[i];
	}
}

/* 
 * mm_checkheap - Check the heap for consistency 
 */
void mm_checkheap(int verbose) 
{
	char *bp = heap_listp;

	if (verbose)
		printf("Heap (%p):\n", heap_listp);
	if ((GET_SIZE(HDRP(heap_listp)) != DSIZE) || GET_ALLOC(HDRP(heap_listp)))
		printf("Bad prologue header\n");

	for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
		if (verbose)
			printblock(bp);
	}

	if (verbose)
		printblock(bp);
	if ((GET_SIZE(HDRP(bp)) != 0) || (GET_ALLOC(HDRP(bp))))
		printf("Bad epilogue header\n");
}

/* The remaining routines are internal helper routines */

/* 
 * extend_heap - Extend heap with free block and return its block pointer
 */
/* $begin mmextendheap */
static void *extend_heap(size_t words) 
{
    char *bp;
    size_t size;
	
    /* Allocate an even number of words to maintain alignment */
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
    if ((bp = mem_sbrk(size)) == (void *)-1) 
		return NULL;

    /* Initialize free block header and the epilogue header */
    PUT(HDRP(bp), PACK(size, 1));         /* free block header */
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 0)); /* new epilogue header */
    return bp;
}
/* $end mmextendheap */

/* 
 * place - Place block of asize bytes at start of free block bp 
 *         and split if remainder would be at least minimum block size
 */
/* $begin mmplace */
/* $begin mmplace-proto */
static void place(void *bp, size_t asize)
/* $end mmplace-proto */
{
	size_t csize = GET_SIZE(HDRP(bp));   
	// printf("csize = %d\n", csize);

	if ((csize - asize) >= (DSIZE)) {
		PUT(HDRP(bp), PACK(asize, 0));
		PUT(FTRP(bp), PACK(asize, 0));
		bp = NEXT_BLKP(bp);
		PUT(HDRP(bp), PACK(csize-asize, 1));
		PUT(FTRP(bp), PACK(csize-asize, 1));
	}
	else { 
		PUT(HDRP(bp), PACK(csize, 0));
		PUT(FTRP(bp), PACK(csize, 0));
	}
}
/* $end mmplace */

/* 
 * find_fit - Find a fit for a block with asize bytes 
 */
static void *find_fit(size_t asize)
{
    /* first fit search */
    void *bp;

    for (bp = heap_listp; GET_SIZE(bp-WSIZE) > 0; bp = NEXT_BLKP(bp)) {
		if (GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) {
	    	return bp;
		}
    }
    return NULL; /* no fit */
}
static void printblock(void *bp)
{
	    size_t hsize, halloc;

		hsize = GET_SIZE(HDRP(bp));
		halloc = GET_ALLOC(HDRP(bp));
		
		if (hsize == 0) {
			printf("%p: EOL\n", bp);
			return;
		}

		printf("%p: header: [%zu:%c]\n", bp, hsize, (halloc ? 'f' : 'a'));
}

