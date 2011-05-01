/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
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
	"",
	/* Second member's email address (leave blank if none) */
	""
};

/* single word (4) or double word (8) alignment */
#define WSIZE 4
#define DSIZE 8
#define ALIGNMENT DSIZE

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

/* Read and write word value at address 'p' */
#define GETW(p) (*(unsigned int *)(p))
#define PUTW(p, val) (*(unsigned int *)(p) = (val))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read size field */
#define GET_SIZE(p) (GETW(p) & 0xFFFFFFF8)
/* Read <allocated?> field */
#define GET_ALLOC(p) (GETW(p) & 0x1)

/* Get address of header and footer */
#define GET_BLOCKHDR(bp) ((char *)(bp) - WSIZE)
#define GET_BLOCKFTR(bp) ((char *)(bp) + GET_SIZE(GET_BLOCKHDR(bp)) - DSIZE)

/*  */
#define GET_NEXTBLOCK(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define GET_PREVBLOCK(bp) ((char *)(bp) - GETSIZE(((char *)(bp) - DSIZE)))

#define FREELIST_COUNT 13


/* Using size segregated explicit free lists */
static CHUNK * free_lists[FREELIST_COUNT] /* Segregate by word size power of 2, up to 4096 words */

typedef struct {
	unsigned int size_alloc;
	char * successor;
} mem_header;

static unsigned int PAGE_SIZE;
static unsigned int PAGE_WORDS;

static unsigned char * heap_start;
static unsigned char * heap_end;

static int calc_min_bits(size_t size);
static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void place(void *bp, size_t adjusted_size);
static void * find_fit(size_t block_size);

/**
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
	mem_init();
	
	memset(free_lists, NULL, sizeof(free_lists));
	
	PAGE_SIZE = mem_pagesize();
	PAGE_WORDS = PAGE_SIZE / ALIGNMENT;
	
	/* Initially allocate 1 page of memory plus room for
		the prologue and epilogue blocks and free block header */
	if((heap_start = mem_sbrk(PAGE_SIZE + (3 * ALIGNMENT))) == NULL)
		return -1;
	
	heap_end = mem_heap_high();
	
	/* Alignment word */
	PUTW(heap_start, 0);
	
	/* Prologue Block header */
	PUTW(heapstart + (1 * WSIZE), PACK(DISZE, 1));
	PUTW(heapstart + (2 * WSIZE), PACK(DSIZE, 1));
	
	/* Epilogue Block header */
	PUTW(heap_end - WSIZE + 1, PACK(0, 1));
	
	
	
	block_addr = heap_start + (2 * ALIGNMENT);
	num_bytes = PAGE_SIZE;
	
	PUTW(GET_BLOCKHDR(chunk_addr), PACK(num_bytes, 0));
	PUTW(GET_BLOCKFTR(chunk)addr), PACK(num_bytes, 0));
	/* mm_maloc should check the freelists instead of traverse a free list
	coalesce shouldn't change. Should pull this segment of code out into
	either mm_free or a mark_free() function or macro.
	*/
	/* Ensure first block of free memory is aligned to */
	free_lists[calc_min_bits(PAGE_WORDS)] = heap_start + ALIGNMENT;
	
	
	return 0;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
	size_t adjusted_size; /* Adjusted (aligned) block size */
	size_t extendsize	  /* Amount to extend heap if no fit */
	char *bp;
	
	/* Ignore stupid/ugly programmers */
	if (size == 0)
		return NULL;
	
	/* Adjust block size to allow for header and match alignment */
	if (size <= DSIZE)
		adjusted_size = 2 * DSIZE;
	else
		adjusted_size = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);
	
	/* Search for a best fit */
	if ((bp = find_fit(adjusted_size)) != NULL) {
		place(bp, adjusted_size);
		return bp;
	}
	
	/* No fit found, extend the heap */
	extendsize = MAX(adjusted_size, PAGE_SIZE);
	if ((bp = extend_heap(extendsize)) == NULL) {
		return NULL;
	place(bp, adjusted_size);
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


/**
 * Calculate bits needed to store a value
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
 * Extend the heap by number of words
 */
static void *extend_heap(size_t words)
{
	char *bp;
	size_t size;
	
	/* Allocate an even number of words to maintain alignment */
	size = (words %2) ? (words + 1) * WSIZE : words * WSIZE;
	if ((long)(bp = mem_sbrk(size)) == -1)
		return NULL;
	
	/* Initialize free block header/footer and the epilogue header */
	PUT(GET_BLOCKHDR(bp), PACK(size, 0));			  /* Free block header */
	PUT(GET_BLOCKFTR(bp), PACK(size), 0));			  /* Free block header */
	PUT(GET_BLOCKHDR(GET_NEXTBLOCK(bp)), PACK(0, 1)); /* New epilogue header */
	
	/* Coalesce if the previous block was free */
	return coalesce(bp);
}

/**
 * Concatenate adjacent blocks
 */
static void *coalesce(void *bp)
{
	size_t prev_alloc = GET_ALLOC(GET_BLOCKFTR(GET_PREVBLOCK(bp)));
	size_t next_alloc = GET_ALLOC(GET_BLOCKHDR(GET_NEXTBLOCK(bp)));
	size_t size = GET_SIZE(GET_BLOCKHDR(GET_NEXTBLOCK(bp)));
	
	if (prev_alloc && !next_alloc) {
		return bp;
	}
	
	else if (prev_alloc && !next_alloc) {
		size += GET_SIZE(GET_BLOCKHDR(GET_NEXTBLOCK(bp)));
		PUTW(GET_BLOCKHDR(bp), PACK(size, 0));
		PUT(GET_BLOCKFTR(bp), PACK(size, 0));
	}
	
	else if (!prev_alloc && next_alloc) {
		size += GET_SIZE(GET_BLOCKHDR(GET_PREVBLOCK(bp)));
		PUTW(GET_BLOCKFTR(bp), PACK(size, 0));
		PUTW(GET_BLOCKHDR(GET_PREVBLOCK(b)), PACK(size, 0));
		bp = GET_PREVBLOCK(bp);
	}
	
	else {
		size += GET_SIZE(GET_BLOCKHDR(GET_PREVBLOCK(bp))) + 
		  GET_SIZE(GET_BLOCKFTR(GET_NEXTBLOCK(bp)));
		PUTW(GET_BLOCKHDR(GET_PREVBLOCK(b)), PACK(size, 0));
		PUTW(GET_BLOCKFTR(GET_NEXTBLOCK(bp)), PACK(size, 0));
		bp = GET_PREVBLOCK(bp);
	}
	
	return bp;
}

/**
 * place block (Write header & footer)
 */
static void place(void *bp, size_t adjusted_size)
{
	size_t csize = GET_SIZE(GET_BLOCKHDR(bp));
	
	if ((csize - adjusted_size) >= (2 * DSIZE)) {
		PUTW(GET_BLOCKHDR(bp), PACK(adjusted_size, 1));
		PUTW(GET_BLOCKFTR(bp), PACK(adjusted_size, 1));
		bp = GET_NEXTBLOCK(bp);
		PUTW(GET_BLOCKHDR(bp), PACK(csize - adjusted_size, 0));
		PUTW(GET_BLOCKFTR(bp), PACK(csize - adjusted_size, 0));
	}
	
	else {
		PUTW(GET_BLOCKHDR(bp), PACK(csize, 1));
		PUTW(GET_BLOCKFTR(bp), PACK(cisze, 1));
	}
}

/**
 * Find a free block of memory of size block_size
 */
static void * find_fit(size_t block_size)
{
	int list_index,
		min_index = calc_min_bits(block_size);
	
	/* Look at the free list with the minimum size needed to hold block_size */
	for (list_index = min_index; list_index < FREELIST_COUNT; list_index++) {
		/* If the head of the list is not null, we can use it */
		if (free_lists[list_index] != NULL && !GET_ALLOC(free_lists[list_index])) {
			return (void *)free_lists[list_index];
		}
		/* Otherwise,  */
	}
	return NULL;
}