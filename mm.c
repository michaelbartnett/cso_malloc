/*
 * mm.c - The fastest, least memory-efficient malloc package.
 *
 * This file uses functions and macros from the Virtual Memory chapter in CS:APP
 * implicit free list example. Names of functions and macros have been changed
 * where I felt they better communicated their purpose.
 *
 * These functions and macros have also been changed to reflect my specific
 * implementation of segregated free lists. Some of the code was reusable, some
 * was not. I make no effort to indicate which lines are taken from the text--
 * I feel my code is different enough to make this unnecessary.
 *
 * Public Functions


 * Private Functions:
 *
 *      allocate_block - Based on the 'place' function in CS:APP implicit free
 *    					  list example.
 */



/*
 * The To-Do list:
 *
 *
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
	"Anything-But-Java",
	/* First member's full name */
	"Michael Bartnett",
	/* First member's NYU NetID*/
	"mrb402",
	/* Second member's full name (leave blank if none) */
	"Nabil Hassein",
	/* Second member's email address (leave blank if none) */
	"nah285"
};

/* single word (4) or double word (8) alignment */
#define WSIZE 4
#define DSIZE 8
#define ALIGNMENT DSIZE
#define MIN_SIZE 3 * WSIZE

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

/* The size of a size_t type */
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* This sets the type CHUNK to be the minimum block size.
The name CHUNK was chosen because a BLOCK refers to an allocated block of memory,
and can be any size, and MINIMUM_BLOCK_SIZE is a bit verbose.
I realize that CHUNKSIZE in the CS:APP implicit free list implementation refers
to how much memory to start allocating, but I'll likely be increasing the break aligned
to page size. */
#if ALIGNMENT == 8
#define CHUNK unsigned short
#else
#define CHUNK unsigned int
#endif

#define THISALLOC 0x01
#define PREVALLOC (0x01 << 1)

/* Self-explanatory */
#define MAX(x, y) ((x) > (y)? (x) : (y))

/* Read and write word value at address 'p' */
#define GETW(p) (*(unsigned int *)(p))
#define PUTW(p, val) (*(unsigned int *)(p) = (val))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read size field */
#define GET_SIZE(p) (GETW(p) & 0xFFFFFFF8)
/* Read <allocated?> field */
#define GET_ALLOC(p) (GETW(p) & THISALLOC)
/* Read <previous allocated?> field */
#define GET_PREVALLOC(p) ((GETW(p) & PREVALLOC))
/* Read <next allocated?> field */
#define GET_NEXTALLOC(p) (GET_ALLOC(GET_BLOCKHDR(GET_NEXTBLOCK(p))))

/* Get address of header and footer */
#define GET_BLOCKHDR(bp) ((char *)(bp) - WSIZE)
#define GET_BLOCKFTR(bp) ((char *)(bp) + GET_SIZE(GET_BLOCKHDR(bp)) - DSIZE)

/*  */
#define GET_NEXTBLOCK(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define GET_PREVBLOCK(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

#define FREELIST_COUNT 13

/* This implementation requires that block sizes be odd and >= 3 words */
#define ADJUST_WORDSIZE(size) ((size) < 3 ? 3 : (size) + (((size) % 2) ^ 0x01))
#define ADJUST_BYTESIZE(size) ((ADJUST_WORDSIZE(((size) + WSIZE - 1)/WSIZE)) * WSIZE)



/* Using size segregated explicit free lists */
static char * free_lists[FREELIST_COUNT]; /* Segregate by word size power of 2, up to 4096 words */

/* Helper macro to get the mem_header of a payload pointer */
#define AS_MEM_HEADER(p) ((mem_header *)GET_BLOCKHDR(p))
/* Struct for making linked list manipulation easier (casting FTW) */
typedef struct {			/* IMPORTANT!!! The next_free and prev_free fields*/
	unsigned int size_alloc;/* of this struct point to the START of payload,  */
	char *next_free;		/* not the start of the struct. Remember to always*/
	char *prev_free;		/* adjust if you want to set the *_free of one	  */
							/* mem_header to another mem_header.			  */
} mem_header;

static size_t PAGE_SIZE;
static size_t PAGE_WORDS;
static size_t ADJUSTED_PAGESIZE;
static size_t ADJUSTED_PAGEWORDS;

static unsigned char * heap_start = NULL;
static unsigned char * heap_end = NULL;

static int calc_min_bits(size_t size);
static void *extend_heap(size_t adjusted_size);
static void *coalesce(void *bp);
static void allocate(void *bp, size_t adjusted_size);
static void * find_fit(size_t block_size);
static void *find_end_of_list(int list_index);
static void add_to_list(char *bp, int list_index);
static void remove_from_list(char *bp, int list_index);



/**
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
	mem_init();

	memset(free_lists, (int)NULL, sizeof(free_lists));

	/* Initialize write-once variables */
	PAGE_SIZE = mem_pagesize();
	PAGE_WORDS = PAGE_SIZE / ALIGNMENT;
	ADJUSTED_PAGESIZE = ADJUST_BYTESIZE(PAGE_SIZE);
	ADJUSTED_PAGEWORDS = ADJUST_WORDSIZE(PAGE_WORDS);

	/* Initially allocate 1 page of memory plus room for
		the prologue and epilogue blocks and free block header */
	if((heap_start = mem_sbrk(ADJUSTED_PAGESIZE + (3 * ALIGNMENT))) == NULL)
		return -1;

	/* TODO: Determine if I actually need this variable */
	heap_end = mem_heap_hi();

	/* Alignment word */
	PUTW(heap_start, 0);

	/* Prologue header */
	PUTW(heap_start + (1 * WSIZE), PACK(0, 1));
	PUTW(heap_start + (2 * WSIZE), PACK(0, 1));

	/* Epilogue header */
	PUTW(heap_end - WSIZE + 1, PACK(0, 1));

	/* Initial free block */
	PUTW(GET_BLOCKHDR(heap_start + (2 * ALIGNMENT)), PACK(PAGE_SIZE, 0));
	PUTW(GET_BLOCKFTR(heap_start + (2 * ALIGNMENT)), PACK(PAGE_SIZE, 0));

	/* Ensure first block of free memory is aligned to */
	free_lists[calc_min_bits(PAGE_WORDS)] = heap_start + ALIGNMENT;
	mem_header *header = AS_MEM_HEADER(heap_start + (2 * ALIGNMENT));
	header->prev_free = NULL;
	header->next_free = NULL;

	return 0;
}



/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
	size_t adjusted_size; /* Adjusted (aligned) block size */
	char *bp;

	/* Ignore stupid/ugly programmers */
	if (size == 0)
		return NULL;

	/* Adjust block size to allow for header and match alignment */
	adjusted_size = ADJUST_BYTESIZE(size);

	/* Search for a best fit */
	if ((bp = find_fit(adjusted_size)) != NULL) {
		/* Mark block as allocated, write header info */
		allocate(bp, adjusted_size);

		/* Remove this block from its respective free list */
		remove_from_list(bp, calc_min_bits(adjusted_size));

		return bp;
	}

	/* No fit found, extend the heap */
	/* Reuse existing adjusted_size variable */
	adjusted_size = MAX(adjusted_size, ADJUSTED_PAGESIZE);

	if ((bp = extend_heap(adjusted_size)) == NULL)
		return NULL;

	allocate(bp, adjusted_size);

	return bp;
}



/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
}

/**
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
/*
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

*/
	return NULL;
}


/** TODO: Better comment
 * TODO: Rename this to something more meaningful
 * Calculate bits needed to store a value
 * This is used to determine which free list to look in.
 */
static int calc_min_bits(size_t size)
{
	int bits = 0;
	while (size >> bits > 1) {
		bits++;
	}
	return bits;
}


/**
 * extend_heap(size_t adjusted_size)
 *
 * Extend the heap by number of words
 *
 * This differs from the example extend_heap function in that the parameter
 * passed is in BYTES rather than WORDS. Constantly converting between the two
 *g is confusing and unnecessary.
 *
 * Furthermore, it should be a size already adjusted to fit byte and header
 * alignment. This method merely sets header/footer/successor as needed
 */
static void *extend_heap(size_t adjusted_size)
{
	char *bp;
	size_t prev_alloc;

	if ((long)(bp = mem_sbrk(adjusted_size)) == -1)
		return NULL;

	/* Initialize free block header/footer and the epilogue header */
	prev_alloc = GET_PREVALLOC(bp);

	/* Free block header */
	PUTW(GET_BLOCKHDR(bp), PACK(adjusted_size, prev_alloc));

	/* Free block header */
	PUTW(GET_BLOCKFTR(bp), PACK(adjusted_size, prev_alloc));

	/* New epilogue header */
	PUTW(GET_BLOCKHDR(GET_NEXTBLOCK(bp)), PACK(0, THISALLOC));

	/* Coalesce if the previous block was free */
	return coalesce(bp); /* coalesce handles adding block to free list */
}

/** TODO: Better comment
 * Concatenate adjacent blocks
 *
 * Should upkeep the free list. Assumes that bp is always a free block
 */
static void *coalesce(void *bp)
{/* TODO: Cleanup variable declarations */
	size_t prev_alloc/* = GET_ALLOC(GET_BLOCKFTR(GET_PREVBLOCK(bp)))*/;
	prev_alloc = GET_PREVALLOC(bp);
	size_t next_alloc/* = GET_ALLOC(GET_BLOCKHDR(GET_NEXTBLOCK(bp)))*/;
	next_alloc = GET_NEXTALLOC(bp);
	size_t size = GET_SIZE(GET_BLOCKHDR(bp));
	char *next_block = GET_NEXTBLOCK(bp);
	char *prev_block = GET_PREVBLOCK(bp);

	/* Case 1, Both blocks allocated, does not need its own if statement */

	if (prev_alloc && !next_alloc) { /* next_block is free */
		remove_from_list(next_block, calc_min_bits(GET_SIZE(next_block)));

		size += GET_SIZE(GET_BLOCKHDR(GET_NEXTBLOCK(bp)));
		PUTW(GET_BLOCKHDR(bp), PACK(size, prev_alloc));
		PUTW(GET_BLOCKFTR(bp), PACK(size, prev_alloc));
	}

	else if (!prev_alloc && next_alloc) { /* prev_block is free */
		remove_from_list(prev_block, calc_min_bits(GET_SIZE(prev_block)));

		size += GET_SIZE(GET_BLOCKHDR(GET_PREVBLOCK(bp)));
		PUTW(GET_BLOCKFTR(bp), PACK(size, prev_alloc));
		PUTW(GET_BLOCKHDR(GET_PREVBLOCK(bp)), PACK(size, prev_alloc));
		bp = GET_PREVBLOCK(bp);
	}

	else if (!prev_alloc && !next_alloc) { /* Both blocks are free */
		remove_from_list(next_block, calc_min_bits(GET_SIZE(next_block)));
		remove_from_list(prev_block, calc_min_bits(GET_SIZE(prev_block)));

		size += GET_SIZE(GET_BLOCKHDR(GET_PREVBLOCK(bp))) +
		  GET_SIZE(GET_BLOCKFTR(GET_NEXTBLOCK(bp)));
		PUTW(GET_BLOCKHDR(GET_PREVBLOCK(bp)), PACK(size, 0));
		PUTW(GET_BLOCKFTR(GET_NEXTBLOCK(bp)), PACK(size, 0));
		bp = GET_PREVBLOCK(bp);
	}

	add_to_list(bp, calc_min_bits(size));
	return bp;
}

/** TODO: Better comment
 * place block (Write header & footer)
 */
static void allocate(void *bp, size_t adjusted_size)
{
	size_t csize = GET_SIZE(GET_BLOCKHDR(bp));
	size_t is_prev_alloc;

	if ((csize - adjusted_size) >= (MIN_SIZE)) {
		is_prev_alloc = GET_PREVALLOC(bp);

		PUTW(GET_BLOCKHDR(bp), PACK(adjusted_size, THISALLOC | is_prev_alloc));
		PUTW(GET_BLOCKFTR(bp), PACK(adjusted_size, THISALLOC | is_prev_alloc));

		remove_from_list(bp, calc_min_bits(adjusted_size));

		bp = GET_NEXTBLOCK(bp);
		PUTW(GET_BLOCKHDR(bp), PACK(csize - adjusted_size, PREVALLOC));
		PUTW(GET_BLOCKFTR(bp), PACK(csize - adjusted_size, PREVALLOC));

		add_to_list(bp, calc_min_bits(csize - adjusted_size));
	}
	else {
		PUTW(GET_BLOCKHDR(bp), PACK(csize, THISALLOC | is_prev_alloc));
		PUTW(GET_BLOCKFTR(bp), PACK(csize, THISALLOC | is_prev_alloc));

		remove_from_list(bp, calc_min_bits(csize));
	}
}


/** TODO: Better comment
 * Mark block at specified address as free and coalesce
 */
static void free_block(void *bp, size_t adjusted_size)
{
	size_t size;
	size_t is_prev_alloc;

	/* Trying to free NULL pointers will only result in chaos */
    if(bp == NULL)
		return;

	is_prev_alloc = GET_PREVALLOC(GET_BLOCKHDR(bp));
	size = GET_SIZE(GET_BLOCKHDR(bp));

	PUTW(GET_BLOCKHDR(bp), PACK(size, is_prev_alloc));
	PUTW(GET_BLOCKFTR(bp), PACK(size, is_prev_alloc));

    coalesce(bp);
}

/** TODO: Better comment
 * Find a free block of memory of size block_size
 */
static void * find_fit(size_t block_size)
{
	int list_index,
		/* Make sure we search according to size & alignment requirements */
		min_index = ADJUST_WORDSIZE(calc_min_bits(block_size));


	/* Look at the free list with the minimum size needed to hold block_size */
	for (list_index = min_index; list_index < FREELIST_COUNT; list_index++) {
		/* If the head of the list is not null, we can use it */
		if (free_lists[list_index] != NULL) {
			return (void *)free_lists[list_index];
		}
		/* Otherwise, we can't */
	}
	return NULL;
}



/** TODO: Better comment
 *
 */
static void *find_end_of_list(int list_index)
{
	size_t *bp = (size_t *)free_lists[list_index];
	size_t *next_bp = bp;

	while (next_bp != NULL) {
		bp = next_bp;
		next_bp = (size_t *)(*((size_t *)bp)); /* TODO: Make sure this is the right syntax */
	}
	return bp;
}



/**
 * remove_from_list(char *bp, int list_index)
 *
 * Remove the block from the specified free list
 *
 * Parameters:
 *
 * 	  bp - Pointer to the payload
 * 	  list_index - The free list index (power of two representing word size)
 */
static void remove_from_list(char *bp, int list_index)
{
	mem_header *header = AS_MEM_HEADER(bp);
	mem_header *next_header = AS_MEM_HEADER(header->next_free);
	mem_header *prev_header;

	/* Case where block is head of list */
	if (free_lists[list_index] == bp) {
		free_lists[list_index] = header->next_free;
		next_header->prev_free = NULL;

		return;
	}

	/* Default case */

	/* Make the prev_free and next_free of the previous and next list items
	 * point to each other's payload. */
/*	if (header->prev_free != NULL) { /* Don't access invalid memory */
	/* Find the previous header (since we didn't have it before) */
		prev_header = AS_MEM_HEADER(header->prev_free);
		prev_header->next_free = header->next_free;
	/*}*/
	next_header->prev_free = header->prev_free;
}



/**
 * add_to_list(char *bp, int list_index)
 *
 * Add the block to the specified free list
 *
 * Parameters:
 *
 * 	  bp - Pointer to the payload
 * 	  list_index - The free list index (power of two representing word size)
 */
static void add_to_list(char *bp, int list_index)
{
	char *prev;
	mem_header *header = AS_MEM_HEADER(bp);
	mem_header *last_node;

	prev = find_end_of_list(list_index);
	last_node = AS_MEM_HEADER(prev);

	last_node->next_free = bp;
	header->prev_free = prev;
	header->next_free = NULL;
}


/*
int main(int argc, char* argv[])
{
	return 0;
}
*/

