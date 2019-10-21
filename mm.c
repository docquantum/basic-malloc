/* 
 * mm.c  -  Simple allocator based on explicit free lists and 
 *         	first fit placement. It uses boundary tags and
 * 			a circular doubly linked list to keep track of
 * 			free blocks.
 *
 * Each block has a header and footer of the form:
 * 
 *      31                     3  2  1  0 
 *      -----------------------------------
 *     | s  s  s  s  ... s  s  s  0  0  a/f
 *      ----------------------------------- 
 * 
 * where s are the meaningful size bits and a/f is set iff the
 * block is allocated (0=a/1=f). Every block follows the form:
 * 
 *      31             0
 *      ----------------
 *     | Header
 *      ------------------
 *     | prev_free_block  \
 *      ----------------   | - Payload when not free
 *     | next_free_block  /
 *      ------------------
 *     | Footer
 *      ----------------
 * 
 * Due to the size of the header and the bytes required to store
 * the next and previous pointers, the minsize of a free block MUST
 * be 4 words, or 2 DWORDS (16 bytes)
 * 
 * The list has the following form:
 *
 * begin                                                         end
 * heap                                                          heap  
 *  -----------------------------------------------------------------   
 * |  key   | hdr(8:a) |   pad   | zero or more usr blks | hdr(8:a) |
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
#define MINSIZE		16		/* min size of block for overhead + explicit list */

#define MAX(x, y)		((x) > (y) ? (x) : (y))  

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

/* Gets next/previous free list pointers in free area of a free block (bp) */
#define GET_NEXT_FREE(bp)		(*(char **)(bp))
#define GET_PREV_FREE(bp)		(*(char **)(bp + WSIZE))

/* Puts next/previous free list pointers (fp) in free area of a free block (bp) */
#define SET_NEXT_FREE(bp, fp)	(GET_NEXT_FREE(bp) = (fp))
#define SET_PREV_FREE(bp, fp)	(GET_PREV_FREE(bp) = (fp))

/* $end mallocmacros */

/* Global variables */
static char *heap_listp;  /* pointer to first block */  
static char *free_listp;  /* pointer to free list */

/* function prototypes for internal helper routines */
static void *extend_heap(size_t words);
static void place(void *bp, size_t asize);
static void *find_fit(size_t asize);
static void printblock(void *bp);
static void mm_memcpy(void * dest, void * src);
static void add_to_list(void* bp);
static void remove_from_list(void* bp);

/* 
 * mm_init - Initialize the memory manager 
 */
/* $begin mminit */
int mm_init(void) 
{
	/* create the initial empty heap */
	if ((heap_listp = mem_sbrk(4*WSIZE)) == NULL){
		return -1;
	}
	PUT(heap_listp, KEY);						/* alignment padding */
	PUT(heap_listp+WSIZE, PACK(DSIZE, 0));		/* prologue header */ 
	PUT(heap_listp+DSIZE, PACK(0, 0));			/* empty word*/ 
	PUT(heap_listp+DSIZE+WSIZE, PACK(0, 0));	/* epilogue header */
	heap_listp += (DSIZE);						/* move pointer to user blocks */
	free_listp = NULL;							/* clear free list */

	/* Extend the empty heap with a free block of CHUNKSIZE bytes 
	 * point free list at the returned block
	*/
	if (extend_heap(CHUNKSIZE/WSIZE) == NULL){
		return -1;
	}

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

	/* Ignore spurious requests */
	if (size <= 0){
		return NULL;
	}

	/* Adjust block size to include overhead and alignment reqs. */
	if (size <= DSIZE){
		asize = MINSIZE;
	} else{
		asize = DSIZE * ((size + (OVERHEAD) + (DSIZE-1)) / DSIZE);
	}

	/* Search the free list for a fit */
	if ((bp = find_fit(asize)) != NULL) {
		place(bp, asize);
		//mm_checkheap(0);
		return bp;
	}

	/* No fit found. Get more memory and place the block */
	extendsize = MAX(asize,CHUNKSIZE);
	if ((bp = extend_heap(extendsize/WSIZE)) == NULL) {
		printf("mm_malloc = NULL\n");
		return NULL;
	}

	place(bp, asize);

	//mm_checkheap(0);

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
		fprintf(stderr, "mm_free(): null pointer");
		return;
	}

	// If allocated, free
	if(!GET_ALLOC(HDRP(bp))){
		*HDRP(bp) |= 1;
		*FTRP(bp) |= 1;
		add_to_list(bp);
	} else {
		fprintf(stderr, "mm_free(): memory not alloced or corrupted");
		return;
	}
		
}

/* $end mmfree */

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
		if (size <= DSIZE) {
			size = MINSIZE;
		}
		else {
			size = DSIZE * ((size + OVERHEAD + DSIZE - 1) / DSIZE);
		}

		// if sizes are the same or the difference is less than a MINSIZE, no changes needed
		if(oldSize == size || (oldSize - size < MINSIZE)) {
			return ptr;
		} else {
			
			// If size is less than old size, shrink and free end
			if(size < oldSize) {
				PUT(HDRP(ptr), PACK(size, 0));
				PUT(FTRP(ptr), PACK(size, 0));
				PUT(HDRP(NEXT_BLKP(ptr)), PACK(oldSize-size, 1));
				PUT(FTRP(NEXT_BLKP(ptr)), PACK(oldSize-size, 1));
				add_to_list(NEXT_BLKP(ptr));
				return ptr;
			}
			
			// If size is greater than old size, grow and free end
			else if(size > oldSize) {
				size_t nextSize = GET_SIZE(HDRP(NEXT_BLKP(ptr))) + oldSize;
				if(GET_ALLOC(HDRP(NEXT_BLKP(ptr))) && nextSize >= size){
					if ((nextSize - size) >= MINSIZE) {
						remove_from_list(NEXT_BLKP(ptr));
						PUT(HDRP(ptr), PACK(size, 0));
						PUT(FTRP(ptr), PACK(size, 0));
						PUT(HDRP(NEXT_BLKP(ptr)), PACK(nextSize-size, 1));
						PUT(FTRP(NEXT_BLKP(ptr)), PACK(nextSize-size, 1));
						add_to_list(NEXT_BLKP(ptr));
					}
					else { 
						remove_from_list(NEXT_BLKP(ptr));
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
	printf("\n");

	char *bp = heap_listp;

	if (verbose){
		printf("Heap (%p):\n", heap_listp);
	}

	// Check if prologue header is good
	if ((GET_SIZE(HDRP(heap_listp)) != DSIZE) || GET_ALLOC(HDRP(heap_listp))){
		printf("Bad prologue header\n");
	}
	
	// check if every block in free list is actaully free
	char * node = free_listp;
	int freeCount = 0;

	do
	{
		if(node == NULL){
			break;
		}
		if(!GET_ALLOC(HDRP(node))){
			printf("%p not free but is in list!\n", node);
		}
		freeCount++;
		node = GET_NEXT_FREE(node);
	} while (node != free_listp);
	

	for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
		//if free, see if it can be found in the free list
		if(GET_ALLOC(HDRP(bp))){
			node = free_listp;
			do
			{	
				if(node == bp){
					break;
				}else{
					node = GET_NEXT_FREE(node);
				}
			} while (node != free_listp);
			if(node != bp){
				printf("%p is free but not in list!\n", bp);
			}
		}
		if (verbose){
			printblock(bp);
		}
	}

	if (verbose){
		printblock(bp);
	}
	if ((GET_SIZE(HDRP(bp)) != 0) || (GET_ALLOC(HDRP(bp)))){
		printf("Bad epilogue header\n");
	}
}

/**
 * add_to_list - Adds free block to list and coalesces
 * 
 * Will try to add block in address order, or combine
 */
static void add_to_list(void* bp){

	// case for nothing to add or empty list
	if(bp == NULL || !GET_ALLOC(HDRP(bp))){
		fprintf(stderr, "add_to_list(): Pointer is null or not free\n");
		return;
	}

	/**
	 * If list is empty, set head to free bp,
	 * then add next and prev pointers.
	 */ 
	if(free_listp == NULL){
		free_listp = bp;
		SET_NEXT_FREE(free_listp, free_listp);
		SET_PREV_FREE(free_listp, free_listp);
		return;
	}
	
	// case for 1 node in list.
	if(free_listp == GET_NEXT_FREE(free_listp)){
		//make sure they're not next to eachother
		if((char *) NEXT_BLKP(bp) == free_listp){
			size_t size = GET_SIZE(HDRP(free_listp)) + GET_SIZE(HDRP(bp));
			PUT(HDRP(bp), PACK(size, 1));
			PUT(FTRP(bp), PACK(size, 1));
			free_listp = bp;
			SET_NEXT_FREE(free_listp, free_listp);
			SET_PREV_FREE(free_listp, free_listp);
			return;
		} else if((char *) PREV_BLKP(bp) == free_listp){
			size_t size = GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(bp));
			PUT(HDRP(free_listp), PACK(size, 1));
			PUT(FTRP(free_listp), PACK(size, 1));
			return;
		}
		// Set head next to new
		SET_NEXT_FREE(free_listp, bp);
		// Set pre prev as new
		SET_PREV_FREE(free_listp, bp);
		// Set new next as head
		SET_NEXT_FREE(bp, free_listp);
		// Set new prev as head
		SET_PREV_FREE(bp, free_listp);
		return;
	}

	/**
	 * If previous cases don't apply, move head through list until
	 * the new bp is in between two addresses.
	 */ 
	char * node = free_listp;
	do
	{	
		if((char *) bp > node){
			if((char *) bp < GET_NEXT_FREE(node) || node > GET_NEXT_FREE(node)){
				if((char *) PREV_BLKP(bp) == node && (char *) NEXT_BLKP(bp) == GET_NEXT_FREE(node)){
					size_t size = GET_SIZE(HDRP(node)) + GET_SIZE(HDRP(bp)) + GET_SIZE(HDRP(GET_NEXT_FREE(node)));
					PUT(HDRP(node), PACK(size, 1));
					PUT(FTRP(node), PACK(size, 1));
					remove_from_list(GET_NEXT_FREE(node));
					free_listp = node;
					return;
				} else if((char *) PREV_BLKP(bp) == node && (char *) NEXT_BLKP(bp) != GET_NEXT_FREE(node) && GET_ALLOC(HDRP(NEXT_BLKP(bp)))){
					size_t size = GET_SIZE(HDRP(node)) + GET_SIZE(HDRP(bp));
					PUT(HDRP(node), PACK(size, 1));
					PUT(FTRP(node), PACK(size, 1));
					free_listp = node;
					return;
				} else if((char *) PREV_BLKP(bp) != node && (char *) NEXT_BLKP(bp) == GET_NEXT_FREE(node)){
					size_t size = GET_SIZE(HDRP(bp)) + GET_SIZE(HDRP(GET_NEXT_FREE(node)));
					PUT(HDRP(bp), PACK(size, 1));
					PUT(FTRP(bp), PACK(size, 1));
					char * old = GET_NEXT_FREE(node);
					// set next to the next of next-next
					SET_NEXT_FREE(bp, GET_NEXT_FREE(old));
					// set the prev of next-next node to bp
					SET_PREV_FREE(GET_NEXT_FREE(old), bp);
					SET_PREV_FREE(bp, node);
					SET_NEXT_FREE(node, bp);
					free_listp = node;
					return;
				}
				/* bp inserted after node */
				// Set new next as current next
				SET_NEXT_FREE(bp, GET_NEXT_FREE(node));
				// Set current next as new
				SET_NEXT_FREE(node, bp);
				// Set new prev as current
				SET_PREV_FREE(bp, node);
				// Set new next's prev as new
				SET_PREV_FREE(GET_NEXT_FREE(bp), bp);
				// Move head to the inserted node to keep it closer to new inserts
				free_listp = node;
				return;
			}
			node = GET_NEXT_FREE(node);
		}
		else if((char *) bp < node){
			if((char *) bp > GET_PREV_FREE(node) || node < GET_PREV_FREE(node)){
				if((char *) NEXT_BLKP(bp) == node && (char *) PREV_BLKP(bp) == GET_PREV_FREE(node)){
					node = GET_PREV_FREE(node);
					size_t size = GET_SIZE(HDRP(node)) + GET_SIZE(HDRP(bp)) + GET_SIZE(HDRP(GET_NEXT_FREE(node)));
					PUT(HDRP(node), PACK(size, 1));
					PUT(FTRP(node), PACK(size, 1));
					remove_from_list(GET_NEXT_FREE(node));
					free_listp = node;
					return;
				} else if((char *) PREV_BLKP(bp) == GET_PREV_FREE(node) && (char *) NEXT_BLKP(bp) != node){
					node = GET_PREV_FREE(node);
					size_t size = GET_SIZE(HDRP(node)) + GET_SIZE(HDRP(bp));
					PUT(HDRP(node), PACK(size, 1));
					PUT(FTRP(node), PACK(size, 1));
					free_listp = node;
					return;
				} else if((char *) NEXT_BLKP(bp) == node && (char *) PREV_BLKP(bp) != GET_PREV_FREE(node)){
					node = GET_PREV_FREE(node);
					size_t size = GET_SIZE(HDRP(bp)) + GET_SIZE(HDRP(GET_NEXT_FREE(node)));
					PUT(HDRP(bp), PACK(size, 1));
					PUT(FTRP(bp), PACK(size, 1));
					char * old = GET_NEXT_FREE(node);
					// set next to the next of next-next
					SET_NEXT_FREE(bp, GET_NEXT_FREE(old));
					// set the prev of next-next node to bp
					SET_PREV_FREE(GET_NEXT_FREE(old), bp);
					SET_PREV_FREE(bp, node);
					SET_NEXT_FREE(node, bp);
					free_listp = node;
					return;
				}
				/* bp inserted before node */
				// Set new next to node
				SET_NEXT_FREE(bp, node);
				// Set new prev as node's prev
				SET_PREV_FREE(bp, GET_PREV_FREE(node));
				// Set prev as new
				SET_NEXT_FREE(GET_PREV_FREE(bp), bp);
				// set node prev as new
				SET_PREV_FREE(node, bp);
				// Move head to the inserted node to keep it closer to new inserts
				free_listp = node;
				return;
			}
			node = GET_PREV_FREE(node);
		} else if(node == (char *) bp){
			fprintf(stderr, "add_to_list(): %p is a duplicate\n", bp);
			return;
		}
	} while (node != free_listp);

	fprintf(stderr, "add_to_list(): failed to add %p\n", bp);

	return;
}

/**
 * remove_from_list - Removes free block from list
 * 
 */
static void remove_from_list(void* bp){

	// case for nothing to remove or empty list
	if(bp == NULL || free_listp == NULL){
		fprintf(stderr, "remove_from_list(): List is free or memory is corrupt\n");
		return;
	}

	if(free_listp == GET_NEXT_FREE(free_listp)){ /* case for 1 item */
		free_listp = NULL;
	} else {
		/* case for head being removed */
		if(free_listp == bp){
			free_listp = GET_NEXT_FREE(free_listp);
		}
		// Set next previous to current previous
		SET_PREV_FREE(GET_NEXT_FREE(bp), GET_PREV_FREE(bp));
		// Set previous next to current next
		SET_NEXT_FREE(GET_PREV_FREE(bp), GET_NEXT_FREE(bp));
	}
	return;
}

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

    /* Initialize free block and the epilogue header */
    PUT(HDRP(bp), PACK(size, 1));			/* free block header */
	PUT(FTRP(bp), PACK(size, 1));			/* free block footer */
    PUT(HDRP(NEXT_BLKP(bp)), PACK(8, 0));	/* new epilogue header */
	add_to_list(bp);
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

	if ((csize - asize) >= MINSIZE) {
		PUT(HDRP(bp), PACK(asize, 0));
		PUT(FTRP(bp), PACK(asize, 0));
		remove_from_list(bp);
		bp = NEXT_BLKP(bp);
		PUT(HDRP(bp), PACK(csize-asize, 1));
		PUT(FTRP(bp), PACK(csize-asize, 1));
		add_to_list(bp);
	}
	else { 
		PUT(HDRP(bp), PACK(csize, 0));
		PUT(FTRP(bp), PACK(csize, 0));
		remove_from_list(bp);
	}
}
/* $end mmplace */

/* 
 * find_fit - Find a fit for a block with asize bytes 
 */
static void *find_fit(size_t asize)
{
	// No more free blocks
	if(free_listp == NULL){
		return NULL;
	}

    /* first fit search */
	void *bp = free_listp;
	do {
		if (asize <= GET_SIZE(HDRP(bp))) {
	    	return bp;
		}
		if((bp = GET_NEXT_FREE(bp)) == NULL){
			return NULL;
		}
	} while(bp != free_listp);
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

