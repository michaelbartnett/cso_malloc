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

#ifdef MTEST
	#define _DEBUG
	#define test_main main
#endif

#define _DEBUG

#ifdef _DEBUG
	#define TRACE(...) printf(__VA_ARGS__); fflush(stdout)

	char* get_heap_str(char *addr, size_t len, size_t bytes_per_block, size_t blocks_per_row);
#else
	#define TRACE(...) ;
#endif

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
#define MIN_SIZE 4 * WSIZE

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

/* The size of a size_t type */
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))


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

/* Get address of header and footer */
#define GET_BLOCKHDR(bp) ((char *)(bp) - WSIZE)
#define GET_BLOCKFTR(bp) ((char *)(bp) + GET_SIZE(GET_BLOCKHDR(bp)) - DSIZE)
/* Read <previous allocated?> field */
#define GET_PREVALLOC(bp) ((GETW(GET_BLOCKHDR(bp)) & PREVALLOC))
/* Read <next allocated?> field */
#define GET_NEXTALLOC(bp) (GET_ALLOC(GET_BLOCKHDR(GET_NEXTBLOCK(bp))))

/*  */
#define GET_NEXTBLOCK(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define GET_PREVBLOCK(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

#define FREELIST_COUNT 13

/* This implementation requires that block sizes be odd and >= 3 words
 * ALIGN_WORDCOUNT aligns to word alignment requirement, whereas
 * ADJUST_BYTESIZE aligns to proper ALIGNMENT in bytes */
#define ADJUST_WORDCOUNT(size) ((size) < 3 ? 3 : (size) + (((size) % 2) ^ 0x01))
#define ADJUST_BYTESIZE(size) (ALIGN((ADJUST_WORDCOUNT(((size) + WSIZE - 1)/WSIZE)) * WSIZE))



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
static size_t ADJUSTED_PAGESIZE;

static char * heap_start = NULL;
static char * heap_end = NULL;

static int calc_min_bits(size_t size);
static void *extend_heap(size_t adjusted_size);
static void *coalesce(void *bp);
static void allocate(void *bp, size_t adjusted_size);
static void *find_fit(size_t block_size, int *result_index);
static void *find_end_of_list(int list_index);
static void add_to_list(char *bp, int list_index);
static void remove_from_list(char *bp, int list_index);
static void free_block(void *bp, size_t adjusted_size);



/**
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
	TRACE(">>>Entering mm_init()\n");
	mem_init();

	memset(free_lists, (int)NULL, sizeof(free_lists));

	/* Initialize write-once variables */
	PAGE_SIZE = mem_pagesize();
	ADJUSTED_PAGESIZE = ADJUST_BYTESIZE(PAGE_SIZE);

	/* Initially allocate 1 page of memory plus room for
		the prologue and epilogue blocks and free block header */
	if((heap_start = mem_sbrk(ADJUSTED_PAGESIZE + (3 * WSIZE))) == NULL)
		return -1;

	/* TODO: Determine if I actually need this variable */
	heap_end = mem_heap_hi();
	PUTW(heap_end-3, 0xC0DEDBAD);

	/* Alignment word */
	PUTW(heap_start, 0x8BADF00D);

	/* Prologue header */
	PUTW(heap_start + (1 * WSIZE), PACK(DSIZE, THISALLOC | PREVALLOC));
	PUTW(heap_start + (2 * WSIZE), PACK(DSIZE, THISALLOC | PREVALLOC));

	/* Epilogue header */
	PUTW(heap_end - WSIZE + 1, PACK(0, THISALLOC));

	/* Setup initial free block */
	PUTW(heap_start + (3 * WSIZE), PACK(ADJUSTED_PAGESIZE, PREVALLOC));
	PUTW((heap_end - WSIZE + 1) - WSIZE, PACK(ADJUSTED_PAGESIZE, PREVALLOC));
	add_to_list(heap_start + (4 * WSIZE), calc_min_bits(ADJUSTED_PAGESIZE));

/*
	PUTW(GET_BLOCKHDR(heap_start + (2 * ALIGNMENT)), PACK(PAGE_SIZE, 0));
	PUTW(GET_BLOCKFTR(heap_start + (2 * ALIGNMENT)), PACK(PAGE_SIZE, 0));

	/* Ensure first block of free memory is aligned to * /
	free_lists[calc_min_bits(PAGE_WORDS)] = heap_start + ALIGNMENT;
	mem_header *header = AS_MEM_HEADER(heap_start + (2 * ALIGNMENT));
	header->prev_free = NULL;
	header->next_free = NULL;
*/

	TRACE("<<<---Leaving mm_init()\n");
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
	int list_index;
	TRACE(">>>Entering mm_malloc(size=%u)\n", size);

	/* Ignore stupid/ugly programmers */
	if (size == 0) {
		TRACE("<<<---Leaving mm_malloc() because some stupid/ugly programmed asked for size 0\n");
		return NULL;
	}

	/* Adjust block size to allow for header and match alignment */
	adjusted_size = ADJUST_BYTESIZE(size);

	/* Search for a best fit */
	if ((bp = find_fit(adjusted_size, &list_index)) != NULL) {
		/* Mark block as allocated, write header info */
		allocate(bp, adjusted_size);

		/* Remove this block from its respective free list */
		remove_from_list(bp, list_index);

		TRACE("<<<---Leaving mm_malloc(), returning 0x%X\n", bp);
		return bp;
	}

	/* No fit found, extend the heap */
	if ((bp = extend_heap(MAX(adjusted_size, ADJUSTED_PAGESIZE))) == NULL)
		TRACE("<<<---Leaving mm_malloc(), returning NULL because extend_heap failed\n");
		return NULL;

	bp += WSIZE; /* Move bp up to payload address */
	allocate(bp, adjusted_size);

	TRACE("<<<---Leaving mm_malloc() returning 0x%X\n", bp);
	return bp;
}



/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
	TRACE(">>>Entering mm_free(ptr=0x%X)\n", (unsigned int)ptr);
	TRACE("<<<---Leaving mm_free()\n");
}

/**
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
	TRACE(">>>Entering mm_realloc(ptr=0x%X, size=%u)\n", (unsigned int)ptr, size);
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
	TRACE("<<<---Leaving mm_realloc()\n");
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
	TRACE(">>>Entering calc_min_bits(size=%u)\n", size);
	while (size >> bits > 1) {
		bits++;
	}
	TRACE("<<<---Leaving calc_min_bits(), returning %d\n", bits);
	return bits;
}


/**
 * extend_heap(size_t adjusted_size)
 *
 * Extend the heap by number of words
 *
 * This differs from the example extend_heap function in that the parameter
 * passed is in BYTES rather than WORDS. Constantly converting between the two
 * is confusing and unnecessary.
 *
 * Furthermore, it should be a size already adjusted to fit byte and header
 * alignment. This method merely sets header/footer/successor as needed
 */
static void *extend_heap(size_t adjusted_size)
{
	char *bp;
	size_t prev_alloc;

	TRACE("Entering extend_heap(adjusted_size=%u)\n", adjusted_size);

	if ((long)(bp = mem_sbrk(adjusted_size)) == -1)
		return NULL;

	/*memset((unsigned int *)bp, 0xDEADBEEF, adjusted_size/WSIZE);*/

	/* Initialize free block header/footer and the epilogue header */
	prev_alloc = GET_PREVALLOC(bp);

	/* Free block header */
	PUTW(GET_BLOCKHDR(bp), PACK(adjusted_size, prev_alloc));

	/* Free block header */
	PUTW(GET_BLOCKFTR(bp), PACK(adjusted_size, prev_alloc));

	/* New epilogue header */
	PUTW(GET_BLOCKHDR(GET_NEXTBLOCK(bp)), PACK(0xC0DEDBAD, THISALLOC));
	heap_end = GET_BLOCKHDR(GET_NEXTBLOCK(bp));

	TRACE("<<<---Leaving extend_heap() with a call to coalesce()\n");
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

	TRACE(">>>Entering coalesce(bp=0x%X)\n", (unsigned int)bp);

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
	TRACE("<<<---Leaving coalesce()\n");
	return bp;
}

/** TODO: Better comment
 * place block (Write header & footer)
 */
static void allocate(void *bp, size_t adjusted_size)
{
	size_t csize = GET_SIZE(GET_BLOCKHDR(bp));
	size_t is_prev_alloc;
	/*int available_index;*/

	TRACE(">>>Entering allocate(bp=0x%X, adjusted_size=%u)\n", (unsigned int)bp, adjusted_size);

	if ((csize - adjusted_size) >= (MIN_SIZE)) {
		is_prev_alloc = GET_PREVALLOC(bp);

		PUTW(GET_BLOCKHDR(bp), PACK(adjusted_size, THISALLOC | is_prev_alloc));
		PUTW(GET_BLOCKFTR(bp), PACK(adjusted_size, THISALLOC | is_prev_alloc));

		/*find_fit(adjusted_size, &available_index);*/
		/* Marking this memory as NOT in teh free list, bp should be a
			valid pointer to a node in a free list*/
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
	TRACE("<<<---Leaving allocate()\n");
}


/** TODO: Better comment
 * Mark block at specified address as free
 */
static void free_block(void *bp, size_t adjusted_size)
{
	size_t size;
	size_t is_prev_alloc;

	TRACE(">>>Entering free_block(bp=0x%X, adjusted_size=%u)\n", (unsigned int)bp, adjusted_size);

	/* Trying to free NULL pointers will only result in chaos */
    if(bp == NULL)
		return;

	is_prev_alloc = GET_PREVALLOC(bp);
	size = GET_SIZE(GET_BLOCKHDR(bp));

	PUTW(GET_BLOCKHDR(bp), PACK(size, is_prev_alloc));
	PUTW(GET_BLOCKFTR(bp), PACK(size, is_prev_alloc));

	TRACE("<<<---Leaving free_block()\n");
}

/** TODO: Better comment
 * Find a free block of memory of size block_size
 */
static void * find_fit(size_t block_size, int *result_index)
{
	int list_index,
		/* Make sure we search according to size & alignment requirements */
		min_index = /*ADJUST_WORDCOUNT(*/calc_min_bits(block_size)/*)*/;

	TRACE(">>>Entering find_fit(block_size=%u, [retval result_index])\n", block_size);

	/* Look at the free list with the minimum size needed to hold block_size */
	for (list_index = min_index; list_index < FREELIST_COUNT; list_index++) {
		/* If the head of the list is not null, we can use it */
		if (free_lists[list_index] != NULL) {
			*result_index = list_index;
			TRACE("<<<---Leaving find_fit, result_index=%d\n", *result_index);
			return (void *)free_lists[list_index];
		}
		/* Otherwise, we can't */
	}
	TRACE("<<<---Leaving find_fit()\n");
	return NULL;
}



/** TODO: Better comment
 *
 */
static void *find_end_of_list(int list_index)
{
	size_t *bp = (size_t *)free_lists[list_index];
	size_t *next_bp = bp;

	TRACE(">>>Entering find_end_of_list(list_index=%d)\n", list_index);

	while (next_bp != NULL) {
		bp = next_bp;
		next_bp = (size_t *)(*((size_t *)bp));
	}
	TRACE("<<<---Leaving find_end_of_list() returning 0x%X\n", bp);
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
	mem_header *next_header;
	mem_header *prev_header;

	TRACE(">>>Entering remove_from_list(bp=0x%X, list_index=%d)\n", (unsigned int)bp, list_index);

	if (header->next_free != NULL) {
		next_header = AS_MEM_HEADER(header->next_free);
		next_header->prev_free = header->prev_free;
	}

	if (header->prev_free != NULL) {
		prev_header = AS_MEM_HEADER(header->prev_free);
		prev_header->next_free = header->next_free;
	}

	if (free_lists[list_index] == bp) {
		free_lists[list_index] = header->next_free;
	}
	TRACE("<<<---Leaving remove_from_list()\n");
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

	TRACE(">>>Entering add_to_list(bp=0x%X, list_index=%d)\n", (unsigned int)bp, list_index);

	prev = find_end_of_list(list_index);

	if (prev == NULL) {
		free_lists[list_index] = bp;
		header->next_free = NULL;
		header->prev_free = NULL;
		TRACE("<<<---Leaving add_to_list(), list's head pointer NULL, list empty\n");
		return;
	}

	last_node = AS_MEM_HEADER(prev);

	last_node->next_free = bp;
	header->prev_free = prev;
	header->next_free = NULL;

	TRACE("<<<---Leaving add_to_list()\n");
}








/************************  Welcome to testing land!  **************************/
/*																			  */
/*                          oooo$$$$$$$$$$$$oooo							  */
/*                      oo$$$$$$$$$$$$$$$$$$$$$$$$o							  */
/*                  oo$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$o         o$   $$ o$	  */
/*   o $ oo        o$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$o       $$ $$ $$o$	  */
/*oo $ $ "$      o$$$$$$$$$    $$$$$$$$$$$$$    $$$$$$$$$o       $$$o$$o$	  */
/*"$$$$$$o$     o$$$$$$$$$      $$$$$$$$$$$      $$$$$$$$$$o    $$$$$$$$	  */
/*  $$$$$$$    $$$$$$$$$$$      $$$$$$$$$$$      $$$$$$$$$$$$$$$$$$$$$$$	  */
/*  $$$$$$$$$$$$$$$$$$$$$$$    $$$$$$$$$$$$$    $$$$$$$$$$$$$$  """$$$		  */
/*   "$$$""""$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$     "$$$		  */
/*    $$$   o$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$     "$$$o	  */
/*   o$$"   $$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$       $$$o	  */
/*   $$$    $$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$" "$$$$$$ooooo$$$$o  */
/*  o$$$oooo$$$$$  $$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$   o$$$$$$$$$$$$$$$$$ */
/*  $$$$$$$$"$$$$   $$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$     $$$$""""""""		  */
/* """"       $$$$    "$$$$$$$$$$$$$$$$$$$$$$$$$$$$"      o$$$				  */
/*            "$$$o     """$$$$$$$$$$$$$$$$$$"$$"         $$$				  */
/*              $$$o          "$$""$$$$$$""""           o$$$				  */
/*               $$$$o                                o$$$"					  */
/*                "$$$$o      o$$$$$$o"$$$$o        o$$$$					  */
/*                  "$$$$$oo     ""$$$$o$$$$$o   o$$$$""					  */
/*                     ""$$$$$oooo  "$$$o$$$$$$$$$"""						  */
/*                        ""$$$$$$$oo $$$$$$$$$$							  */
/*                                """"$$$$$$$$$$$							  */
/*                                    $$$$$$$$$$$$							  */
/*                                     $$$$$$$$$$"							  */
/*                                      "$$$""  							  */
/*																			  */
/***************************  It's a silly place  *****************************/

/**
 * debuggable_memset
 *
 * Our own memset(), existing solely so we can watch each byte get set
 */
void debuggable_memset(void* addr, unsigned char value, size_t len)
{
	unsigned char *byte_pointer = addr;

	TRACE("------ YOU ARE NOW IN DEBUGGABLE_MEMSET. LOVE IT, FEAR IT, HATE IT. ------\n");

	while (byte_pointer < len) {
		*byte_pointer = value;
		byte_pointer++;
	}
	TRACE("...... ok, dun memsetting ......\n");
}



/*********************************************************************
 *                           test_main                               *
 *     Function for testing mm_malloc in a controlled environment    *
 *********************************************************************/
int test_main(int argc, char* argv[])
{
	char *arr[] = {NULL, NULL, NULL, NULL, NULL};
	mm_init();

	arr[0] = mm_malloc(2040);
	TRACE("Got pointer to memory from malloc, 0x%X.\n Memsetting to 0xFE\n", arr[0]);
	memset(arr[0], 0xFE, 2040);

	arr[0] = mm_malloc(2040);
	TRACE("Got pointer to memory from malloc, 0x%X.\n. Memsetting to 0xF1\n", arr[1]);
	debuggable_memset(arr[1], 0xF1, 2040);

	arr[0] = mm_malloc(48);
	TRACE("Got pointer to memory from malloc, 0x%X.\n Memsetting to 0xF2\n", arr[2]);
	memset(arr[2], 0xF2, 48);

	arr[0] = mm_malloc(4072);
	TRACE("Got pointer to memory from malloc, 0x%X.\n Memsetting to 0xF3\n", arr[3]);
	memset(arr[3], 0xF3, 4072);

	return 0;
}




#ifdef _DEBUG

/**
 * print_heap
 *
 *
 * Parameters:
 *
 * addr - The address at which to start examining memory
 *
 * len  - The amount of memory to examine
 *
 * byte_per_block - The number of bytes in a block (formatting)
 *
 * blocks_per_row - How many blocks to show on each line
 *
 *
 * Returns a dynamically allocated string that you can print to show the heap.
 */
char* get_heap_str(char *addr, size_t len, size_t bytes_per_block, size_t blocks_per_row)
{
	char **rows;
	char *rowbuffer, *output;
	char *bytes = malloc(len);
	unsigned int bptr, rowptr, byte_blockprogress, block_progress;
	size_t rowlen, blockcount, rowcount, totalsize = 0;

	if (len == 0 || bytes_per_block == 0 || blocks_per_row == 0)
		return NULL;

	blockcount = len / bytes_per_block + (len % bytes_per_block ? 1 : 0);
	rowcount = blockcount / blocks_per_row + (blockcount % blocks_per_row ? 1 : 0);

	rowlen = ((bytes_per_block + 1) * 2 + 3) * blocks_per_row  + 3 + (8);
	rowbuffer = malloc(rowlen);

	memset(rowbuffer, 0, rowlen);
	rows = calloc(sizeof(char *), rowcount);
	memset(rows, 0, sizeof(char*)*rowcount);

	for (bptr = 0; bptr < len; bptr++) {
		bytes[bptr] = *((char *)(bptr + addr));
	}

	bptr = 0;
	rowptr = 0;
	byte_blockprogress = 0;
	block_progress = 0;
	while (bptr < len) {

		if (block_progress == 0) {
			rowbuffer = malloc(rowlen);
			sprintf(rowbuffer, "0x%X:  ", (unsigned int)(addr + bptr));
		}

		if (byte_blockprogress == 0) {
			strcat(rowbuffer, "  0x");
			block_progress++;
		}

		sprintf(rowbuffer + strlen(rowbuffer), "%X", (unsigned char)(*(addr + bptr)));
		byte_blockprogress++;

		bptr++;

		if (byte_blockprogress >= bytes_per_block) {
			byte_blockprogress = 0;
			block_progress++;
		}

		if (block_progress >= blocks_per_row) {
			block_progress = 0;
			strcat(rowbuffer, "\n");
			rows[rowptr] = rowbuffer;
			rowptr++;
			totalsize += strlen(rowbuffer);
		}

	}

	output = malloc(totalsize+1);
	memset(output, 0, totalsize+1);
	for (rowptr = 0; rowptr < rowcount; rowptr++) {
		strcat(output, rows[rowptr]);
		free(rows[rowptr]);
	}
	free(rows);
	free(bytes);

	return output;
}

#endif
