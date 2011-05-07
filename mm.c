/*
 * mm.c - This file uses functions and macros from the Virtual Memory chapter
 * in CS:APP implicit free list example. Names of functions and macros have
 * been changed where we felt doing so better communicated their purpose.
 *
 * These functions and macros have also been changed to reflect my specific
 * implementation of segregated free lists. Some of the code was reusable, some
 * was not. We make no effort to indicate which lines are taken from the text--
 * we feel our code is different enough to make this unnecessary.
 *
 * Our implementation is based on segregated free lists.
 *
 * The minimum payload size is 3 words.  					  list example.
 */


/*
 * The To-Do list:
 *
 * Traces to pass:
 *	./mdriver -V -f traces/random2-bal.rep
 *	./mdriver -V -f traces/realloc-bal.rep
 *	./mdriver -V -f traces/realloc2-bal.rep
 * Assertion failed: (!avg_tput || *avg_tput > 0), function sumresults, file mdriver.c, line 1011.
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

/*#define _DEBUG*/
/*#define DO_MM_CHECK*/

#ifdef _DEBUG
	#define TRACE(...) printf(__VA_ARGS__); fflush(stdout)
	#define DO_MM_CHECK

	#ifndef MTEST
		/* We created these globals variables in our local copy of mdriver.c so
			we could track exactly which trace command was failing and also set
			conditional breakpoints (ie: break mm_free if traceop_ptr == 158) */
		typedef struct tag_traceop_t traceop_t;
		extern int traceop_index;
		extern int traceop_ptr;
		extern traceop_t* trace_operations;
		extern char* current_trace_name;
	#endif
#else
	#define TRACE(...) ;
#endif

#ifdef DO_MM_CHECK
	/*#define DO_HEAP_OVERWRITE_CHECK*/
	/*
	 * Maximum heap size in bytes
	 * Copied from config.h
	 */
	#define MAX_HEAP (20*(1<<20))  /* 20 MB */

	/*
	 * Estimated maximum size that is ever allocated by traces
	 */
	#define MAX_BLOCK_ALLOCSIZE 1000000

	/*
	 * Wrapper macro for running mm_check
	 */
	#define RUN_MM_CHECK() mm_check()

	void mm_check();
#else
	#define RUN_MM_CHECK() ;
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
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))/* TODO: Delete this unless used UPDATE: currently used just once, in realloc */

/* Bit flags for alloc fields in block headers */
#define THISALLOC 0x01
#define PREVALLOC 0x02

/* Self-explanatory */
#define MAX(x, y) ((x) > (y)? (x) : (y))

/* Read and write word value at address 'p' */
#define GETW(p) (*(unsigned int *)(p))
#define PUTW(p, val) (*(unsigned int *)(p) = (val))

/* Read and write byte value at address 'p' */
#define GETB(p) (*(unsigned char *)(p))
#define PUTB(p, val) (*(unsigned char *)(p) = (val))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read size field */
#define GET_SIZE(p) (GETW(p) & ~(THISALLOC | PREVALLOC))
/* Read <allocated?> field */
#define GET_ALLOC(p) (GETW(p) & THISALLOC)

/* Get address of header and footer */
#define GET_BLOCKHDR(bp) ((char *)(bp) - WSIZE)
#define GET_BLOCKFTR(bp) ((char *)(bp) + GET_SIZE(GET_BLOCKHDR(bp)) - DSIZE)
/* Read <previous allocated?> field */
#define GET_PREVALLOC(bp) ((GETW(GET_BLOCKHDR(bp)) & PREVALLOC))
/* Read <next allocated?> field */
#define GET_NEXTALLOC(bp) (GET_ALLOC(GET_BLOCKHDR(GET_NEXTBLOCK(bp))))
/* Read <is allocated?> field--convenience for GET_ALLOC() using payload ptr */
#define GET_THISALLOC(bp) (GET_ALLOC(GET_BLOCKHDR(bp)))
/* Read <size> field--convenience for GET_SIZE() using payload ptr */
#define GET_THISSIZE(bp) (GET_SIZE(GET_BLOCKHDR(bp)))
/* Get address of payload */
#define GET_PAYLOAD(bp) ((char *)(bp) + WSIZE)

/* Return a pointer to payload of the next block */
#define GET_NEXTBLOCK(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
/* Return a pointer to the payload of the previous block */
#define GET_PREVBLOCK(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* This implementation requires that block sizes be odd and >= 3 words
 * ALIGN_WORDCOUNT aligns to word alignment requirement, whereas
 * ADJUST_BYTESIZE aligns to proper ALIGNMENT in bytes */
#define ADJUST_WORDCOUNT(size) ((size) < 3 ? 3 : (size) + (((size) % 2) ^ 0x01))
#define ADJUST_BYTESIZE(size) (ALIGN((ADJUST_WORDCOUNT(((size) + WSIZE - 1)/WSIZE)) * WSIZE))


/* Using size segregated explicit free lists */
#define FREELIST_COUNT 13
static char * free_lists[FREELIST_COUNT]; /* Segregate by word size power of 2, up to 4096 words */


/* Helper macro to get the mem_header of a payload pointer */
#define MEMHEADER_FROM_PAYLOAD(p) ((mem_header *)GET_BLOCKHDR(p))
#define PAYLOAD_FROM_MEMHEADER(mh) ((char *)((mh) + WSIZE))

/* Struct for making linked list manipulation easier */
typedef struct {			 /* IMPORTANT!!! The next_free and prev_free fields */
	unsigned int size_alloc; /* of this struct point to the START of payload,   */
	char *next_free;		 /* not the start of the struct. Remember to always */
	char *prev_free;		 /* adjust if you want to set the *_free of one		*/
} mem_header;				 /* mem_header to another mem_header.			    */


static size_t PAGE_SIZE;
static size_t ADJUSTED_PAGESIZE;

static char * heap_start = NULL;
static char * heap_end = NULL;

/* Function prototypes */
static int calc_list_index(size_t size);
static void *extend_heap(size_t adjusted_size);
static void *coalesce(void *bp);
static void allocate(void *bp, size_t adjusted_size);
static void *find_fit(size_t block_size, int *result_index);
static void *find_end_of_list(int list_index);
static void add_to_list(char *bp, int list_index);
static void remove_from_list(char *bp, int list_index);
static void free_block(void *bp, size_t adjusted_size);
static int get_node_listindex(void *bp);



/**
 * mm_init - Initialize the malloc package.
 */
int mm_init(void)
{
	TRACE(">>>Entering mm_init()\n");
	mem_init();

	#ifdef DO_MM_CHECK
		/* initialize the ENTIRE heap provided by memlib to 0x00 */
		memset(mem_heap_lo(), 0, MAX_HEAP);
	#endif


	memset(free_lists, (int)NULL, sizeof(free_lists));

	/* Initialize write-once variables */
	PAGE_SIZE = mem_pagesize();
	ADJUSTED_PAGESIZE = ADJUST_BYTESIZE(PAGE_SIZE);

	/* Initially allocate 1 page of memory plus room for
		the prologue and epilogue blocks and free block header */
	if((heap_start = mem_sbrk(ADJUSTED_PAGESIZE + (4 * WSIZE))) == NULL)
		return -1;

	heap_end = mem_heap_hi();

	/* Alignment word */
	PUTW(heap_start, 0x8BADF00D);

	/* Prologue header */
	PUTW(heap_start + (1 * WSIZE), PACK(DSIZE, THISALLOC | PREVALLOC));
	PUTW(heap_start + (2 * WSIZE), PACK(DSIZE, THISALLOC | PREVALLOC));

	/* Epilogue header */
	PUTW(heap_start + ADJUSTED_PAGESIZE + 3	* WSIZE, PACK(0xEA7F00D0, THISALLOC));

	/* Setup initial free block */
	PUTW(heap_start + (3 * WSIZE), PACK(ADJUSTED_PAGESIZE, PREVALLOC));
	PUTW((heap_end - WSIZE + 1) - WSIZE, PACK(ADJUSTED_PAGESIZE, PREVALLOC));
	add_to_list(heap_start + (4 * WSIZE), calc_list_index(ADJUSTED_PAGESIZE));

	RUN_MM_CHECK();
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
		TRACE("<<<---Leaving mm_malloc() because some stupid/ugly programmer asked for size 0\n");
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
	if ((bp = extend_heap(MAX(adjusted_size, ADJUSTED_PAGESIZE))) == NULL) {
		TRACE("<<<---Leaving mm_malloc(), returning NULL because extend_heap failed\n");
		return NULL;
	}

	allocate(bp, adjusted_size);

	RUN_MM_CHECK();
	TRACE("<<<---Leaving mm_malloc() returning 0x%X\n", bp);
	return bp;
}



/**
 * mm_free - Free a block previously allocated by mm_malloc or mm_realloc.
 *
 */
void mm_free(void *ptr)
{
	TRACE(">>>Entering mm_free(ptr=0x%X)\n", (unsigned int)ptr);

	free_block(ptr, GET_THISSIZE(ptr));

	coalesce(ptr);

	RUN_MM_CHECK();
	TRACE("<<<---Leaving mm_free()\n");
}

/**
 * mm_realloc - Implemented in terms of mm_malloc and mm_free.
 */
void *mm_realloc(void *ptr, size_t size)
{
	TRACE(">>>Entering mm_realloc(ptr=0x%X, size=%u)\n", (unsigned int)ptr, size);

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

	/* Unreachable -- should be discarded? */
	RUN_MM_CHECK();
	TRACE("<<<---Leaving mm_realloc()\n");
	return newptr;
}


/**
 * extend_heap - Extend the heap by number of bytes adjusted_size.
 *
 * This differs from the example extend_heap function in that the parameter
 * passed is in BYTES rather than WORDS. Constantly converting between the two
 * is confusing and unnecessary.
 *
 * Furthermore, it should be a size already adjusted to fit byte and header
 * alignment. This function merely sets header/footer/successor as needed.
 */
static void *extend_heap(size_t adjusted_size)
{
	char *bp;
	size_t prev_alloc;

	TRACE("Entering extend_heap(adjusted_size=%u)\n", adjusted_size);

	if ((long)(bp = mem_sbrk(adjusted_size)) == -1)
		return NULL;

	/* Initialize free block header/footer and the epilogue header.
		heap_end points to one byte before the next payload, so reading
		the PREVALLOC field of heap_end + 1 will yield the actual prev-alloc
		for the block just before the end of the heap. */
	prev_alloc = GET_PREVALLOC(heap_end + 1);

	/* Free block header */
	PUTW(GET_BLOCKHDR(bp), PACK(adjusted_size, prev_alloc));

	/* Free block header */
	PUTW(GET_BLOCKFTR(bp), PACK(adjusted_size, prev_alloc));

	/* New epilogue header */
	PUTW(GET_BLOCKHDR(GET_NEXTBLOCK(bp)), PACK(0xEA7F00D0, THISALLOC));
	heap_end = mem_heap_hi();

	TRACE("<<<---Leaving extend_heap() with a call to coalesce()\n");
	/* Coalesce if the previous block was free */
	return coalesce(bp); /* coalesce handles adding block to free list */
}



/**
 * coalesce - Concatenate adjacent blocks to prevent fragmentation.
 *
 * Should upkeep the free list. Assumes that bp is a free block.
 * Also assumes that bp has not yet been added to a free list.
 */
static void *coalesce(void *bp)
{
	size_t prev_alloc = GET_PREVALLOC(bp);
	size_t next_alloc = GET_NEXTALLOC(bp);
	size_t size = GET_THISSIZE(bp);
	char *next_block = GET_NEXTBLOCK(bp);
	char *prev_block = GET_PREVBLOCK(bp);

	TRACE(">>>Entering coalesce(bp=0x%X)\n", (unsigned int)bp);

	/* Case 1, Both blocks allocated, does not need its own if statement */
	if (prev_alloc && !next_alloc) { /* Case 2: only next_block is free */
		remove_from_list(next_block, calc_list_index(GET_THISSIZE(next_block)));

		/* Only need to update the size field */
		size += GET_SIZE(GET_BLOCKHDR(next_block));

		PUTW(GET_BLOCKHDR(bp), PACK(size, prev_alloc));
		PUTW(GET_BLOCKFTR(bp), PACK(size, prev_alloc));
	}

	else if (!prev_alloc && next_alloc) { /* Case 3: only prev_block is free */
		remove_from_list(prev_block, calc_list_index(GET_THISSIZE(prev_block)));

		/* Need to update the size and prev_alloc field */
		size += GET_THISSIZE(prev_block);
		prev_alloc = GET_PREVALLOC(prev_block);

		PUTW(GET_BLOCKFTR(bp), PACK(size, prev_alloc));
		PUTW(GET_BLOCKHDR(prev_block), PACK(size, prev_alloc));
		bp = prev_block;
	}

	else if (!prev_alloc && !next_alloc) { /* Case 4: Both blocks are free */
		remove_from_list(next_block, calc_list_index(GET_THISSIZE(next_block)));
		remove_from_list(prev_block, calc_list_index(GET_THISSIZE(prev_block)));

		/* Need to update the size and prev_alloc field */
		size += GET_THISSIZE(prev_block) + GET_THISSIZE(next_block);
		prev_alloc = GET_PREVALLOC(prev_block);

		PUTW(GET_BLOCKHDR(GET_PREVBLOCK(bp)), PACK(size, prev_alloc));
		PUTW(GET_BLOCKFTR(GET_NEXTBLOCK(bp)), PACK(size, prev_alloc));
		bp = GET_PREVBLOCK(bp);
	}

	/* coalesce() is always called after a block is marked free
		so it needs to add the block to the appropriate free list */
	add_to_list(bp, calc_list_index(size));
	TRACE("<<<---Leaving coalesce()\n");
	return bp;
}


/**
 * free_block - Mark block at specified address as free.
 * Payload remains intact until later overwritten.
 */
static void free_block(void *bp, size_t adjusted_size)
{
	size_t size;
	size_t is_prev_alloc;

	TRACE(">>>Entering free_block(bp=0x%X, adjusted_size=%u)\n", (unsigned int)bp, adjusted_size);

	is_prev_alloc = GET_PREVALLOC(bp);
	size = GET_THISSIZE(bp);

	PUTW(GET_BLOCKHDR(bp), PACK(size, is_prev_alloc));
	PUTW(GET_BLOCKFTR(bp), PACK(size, is_prev_alloc));

	TRACE("<<<---Leaving free_block()\n");
}

/**
 * allocate - Place block, i.e. write header and footer.
 */
static void allocate(void *bp, size_t adjusted_size)
{
	size_t csize = GET_THISSIZE(bp);
	size_t is_prev_alloc = GET_PREVALLOC(bp);

	TRACE(">>>Entering allocate(bp=0x%X, adjusted_size=%u)\n", (unsigned int)bp, adjusted_size);

	/* We will always need to remove tshi block from the free list */
	remove_from_list(bp, calc_list_index(csize));

	/* See if there's room to split this block into two */
	if ((csize - adjusted_size) >= (MIN_SIZE)) {
		PUTW(GET_BLOCKHDR(bp), PACK(adjusted_size, THISALLOC | is_prev_alloc));
		PUTW(GET_BLOCKFTR(bp), PACK(adjusted_size, THISALLOC | is_prev_alloc));

		/* Using the new header info, mark the newly created block as free */
		bp = GET_NEXTBLOCK(bp);
		PUTW(GET_BLOCKHDR(bp), PACK(csize - adjusted_size, PREVALLOC));
		PUTW(GET_BLOCKFTR(bp), PACK(csize - adjusted_size, PREVALLOC));

		/* And add it to the appropriate free list */
		add_to_list(bp, calc_list_index(csize - adjusted_size));
	}
	else {/* If there's not room to create split the block, just extend the
		 	amount to allocated */
		PUTW(GET_BLOCKHDR(bp), PACK(csize, THISALLOC | is_prev_alloc));
		PUTW(GET_BLOCKFTR(bp), PACK(csize, THISALLOC | is_prev_alloc));

		/* Make sure the next block's header has the prevalloc field marked */
		bp = GET_BLOCKHDR(GET_NEXTBLOCK(bp));
		PUTW(bp, GETW(bp) | PREVALLOC);
	}
	TRACE("<<<---Leaving allocate()\n");
}

/**
 * find_fit - Find a free block of memory of size block_size.
 *
 * This function assumes that block_size has already been adjusted
 * to fit alignment and header requirements.
 */
static void * find_fit(size_t block_size, int *result_index)
{
	int list_index,
		/* Make sure we search according to size & alignment requirements */
		min_index = calc_list_index(block_size);
	void *fitptr;

	TRACE(">>>Entering find_fit(block_size=%u, [retval result_index])\n", block_size);

	/* Look at the free list with the minimum size needed to hold block_size */
	for (list_index = min_index; list_index < FREELIST_COUNT; list_index++) {
		fitptr = free_lists[list_index];

		/* If the head of the list is not null, we can use it */
		if (fitptr != NULL && GET_THISSIZE(fitptr) >= block_size) {
			*result_index = list_index;
			TRACE("<<<---Leaving find_fit, result_index=%d\n", *result_index);
			return (void *)fitptr;
		}
		/* Otherwise, we can't */
	}
	TRACE("<<<---Leaving find_fit()\n");
	return NULL;
}



/**
 * find_end_of_list - Return a pointer to the payload of the last element of free_lists[list_index].
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
 * get_node_listindex - Given a pointer, determine which free list it is in.
 */
static int get_node_listindex(void *bp)
{
	mem_header *header = MEMHEADER_FROM_PAYLOAD(bp);
	int i;
	TRACE(">>>Entering get_node_listindex(bp=0x%X)\n", (unsigned int)bp);

	while(header->prev_free != NULL) {
		header = MEMHEADER_FROM_PAYLOAD(header->prev_free);
	}

	for (i = 0; i < FREELIST_COUNT; i++) {
		if (free_lists[i] == PAYLOAD_FROM_MEMHEADER(header)) {
			TRACE("<<<---Leaving get_node_listindex(), returning %d (found list index)", i);
			return i;
		}
	}
	TRACE("<<<---Leaving get_node_listindex(), returning -1 (did NOT find list index)");
	return -1;
}


/**
 * calc_list_index - Determine which free list to look in, given a size.
 */
static int calc_list_index(size_t size)
{
	int bits = 0;
	TRACE(">>>Entering calc_list_index(size=%u)\n", size);
	while ((size >> bits > 1) && (bits + 1 < FREELIST_COUNT)) {
		bits++;
	}
	TRACE("<<<---Leaving calc_list_index(), returning %d\n", bits);
	return bits;
}



/**
 * remove_from_list - Remove the block from the specified free list
 */
static void remove_from_list(char *bp, int list_index)
{
	mem_header *header = MEMHEADER_FROM_PAYLOAD(bp);
	mem_header *next_header;
	mem_header *prev_header;

	TRACE(">>>Entering remove_from_list(bp=0x%X, list_index=%d)\n", (unsigned int)bp, list_index);
	TRACE("        Removing data block of size %u\n", header->size_alloc);
	TRACE("        header->next_free = %0x%X\n", header->next_free);
	TRACE("        header->prev_free = %0x%X\n", header->prev_free);

	if (header->next_free != NULL) {
		next_header = MEMHEADER_FROM_PAYLOAD(header->next_free);
		next_header->prev_free = header->prev_free;
	}

	if (header->prev_free != NULL) {
		prev_header = MEMHEADER_FROM_PAYLOAD(header->prev_free);
		prev_header->next_free = header->next_free;
	}

	if (free_lists[list_index] == bp) {
		free_lists[list_index] = header->next_free;
	}
	TRACE("<<<---Leaving remove_from_list()\n");
}



/**
 * add_to_list - Add the block to the specified free list.
 */
static void add_to_list(char *bp, int list_index)
{
	char *tail_payload;
	mem_header *tail_node;
	mem_header *current = MEMHEADER_FROM_PAYLOAD(bp);

	TRACE(">>>Entering add_to_list(bp=0x%X, list_index=%d)\n", (unsigned int)bp, list_index);

	current->next_free = NULL;

	tail_payload = find_end_of_list(list_index);

	if (tail_payload == NULL) {
		free_lists[list_index] = bp;
		current->prev_free = NULL;
		TRACE("<<<---Leaving add_to_list(), list's head pointer NULL, list empty\n");
		return;
	}

	tail_node = MEMHEADER_FROM_PAYLOAD(tail_payload);

	tail_node->next_free = bp;
	current->prev_free = tail_payload;

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



#ifdef DO_MM_CHECK
/**
 * mm_check - Check the consistency of the heap.
 */
void mm_check()
{
	int i;
	char *bp;

	/* First, make sure heap_end is set appropriately. */
	assert(heap_end == mem_heap_hi());

	/* Then, check to ensure that we have no data past MAX_HEAP. */
	assert(heap_end < heap_start + MAX_HEAP);


	#ifdef DO_HEAP_OVERWRITE_CHECK
	/* Next, make sure we haven't written past mem_heap_hi(). */
	bp = heap_end + 1;
	while (bp < heap_start + MAX_HEAP) {
		assert((*bp) == 0);
		bp++;
	}
	#endif

	/* Then, make sure the blocks in our free lists are actually free. */
	for (i = 0; i < FREELIST_COUNT; i++) {
		bp = free_lists[i];
		while (bp != NULL) {
			assert(!GET_THISALLOC(bp));
			assert(GET_THISSIZE(bp) < MAX_BLOCK_ALLOCSIZE);
			bp = MEMHEADER_FROM_PAYLOAD(bp)->next_free;
		}
	}
	/* Finally, make sure we haven't misaligned our headers and payload.
		If a payload is misinterpreted as a header, its size will be
		over 1 million (discounting the first block which is all zeroes). */
	bp = heap_start + 4 * WSIZE;

	while (bp < heap_end) {
		assert(GET_THISSIZE(bp) < MAX_BLOCK_ALLOCSIZE);
		bp = GET_NEXTBLOCK(bp);
	}
}
#endif



#ifdef _DEBUG
/**
 * debuggable_memset
 *
 * Our own memset(), existing solely so we can watch each byte get set
 */
void debuggable_memset(void* addr, unsigned char value, size_t len)
{
	unsigned char *byte_pointer = addr;

	TRACE("------ YOU ARE NOW IN DEBUGGABLE_MEMSET. LOVE IT, FEAR IT, HATE IT. ------\n");

	/* I don't know if standard memset does any safety checks, but this one
		sure as hell doesn't */
	while (byte_pointer < addr + len) {
		PUTB(byte_pointer, value);
		byte_pointer++;
	}
	TRACE("...... ok, dun memsetting ......\n");
}



/*********************************************************************
 *                           test_main                               *
 *     Function for testing mm_malloc in a controlled environment    *
 *********************************************************************/
static  char *arr[] = {NULL, NULL, NULL, NULL, NULL};


int test_main(int argc, char* argv[])
{
	mm_init();
/*
	arr[0] = mm_malloc(2040);
	TRACE("Got pointer to memory from malloc, 0x%X.\n Memsetting to 0xFE\n", (unsigned int)arr[0]);
	debuggable_memset(arr[0], 0xFE, 2040);

	arr[1] = mm_malloc(2040);
	TRACE("Got pointer to memory from malloc, 0x%X.\n. Memsetting to 0xF1\n", (unsigned int)arr[1]);
	debuggable_memset(arr[1], 0xF1, 2040);

	arr[2] = mm_malloc(48);
	TRACE("Got pointer to memory from malloc, 0x%X.\n Memsetting to 0xF2\n", (unsigned int)arr[2]);
	debuggable_memset(arr[2], 0xF2, 48);

	arr[3] = mm_malloc(4072);
	TRACE("Got pointer to memory from malloc, 0x%X.\n Memsetting to 0xF3\n", (unsigned int)arr[3]);
	debuggable_memset(arr[3], 0xF3, 4072);
*/
	int *temp = mm_malloc(100);

	mm_free(temp);

	return 0;
}
#endif
