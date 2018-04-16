/**
 * Buddy Allocator
 *
 * For the list library usage, see http://www.mcs.anl.gov/~kazutomo/list/
 */

/**************************************************************************
 * Conditional Compilation Options
 **************************************************************************/
#define USE_DEBUG 0

/**************************************************************************
 * Included Files
 **************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "buddy.h"
#include "list.h"

/**************************************************************************
 * Public Definitions
 **************************************************************************/
#define MIN_ORDER 12
#define MAX_ORDER 20

#define PAGE_SIZE (1<<MIN_ORDER)
/* page index to address */
#define PAGE_TO_ADDR(page_idx) (void *)((page_idx*PAGE_SIZE) + g_memory)

/* address to page index */
#define ADDR_TO_PAGE(addr) ((unsigned long)((void *)addr - (void *)g_memory) / PAGE_SIZE)

/* find buddy address */
#define BUDDY_ADDR(addr, o) (void *)((((unsigned long)addr - (unsigned long)g_memory) ^ (1<<o)) \
									 + (unsigned long)g_memory)

#if USE_DEBUG == 1
#  define PDEBUG(fmt, ...) \
	fprintf(stderr, "%s(), %s:%d: " fmt,			\
		__func__, __FILE__, __LINE__, ##__VA_ARGS__)
#  define IFDEBUG(x) x
#else
#  define PDEBUG(fmt, ...)
#  define IFDEBUG(x)
#endif

/***************************************************  ***********************
 * Public Types
 **************************************************************************/
typedef struct {
	struct list_head list;
	int free;
	int order;
	char* addr;
	/* TODO: DECLARE NECESSARY MEMBER VARIABLES */
} page_t;

/**************************************************************************
 * Global Variables
 **************************************************************************/
/* free lists*/
struct list_head free_area[MAX_ORDER+1];

/* memory area */
char g_memory[1<<MAX_ORDER];

/* page structures */
page_t g_pages[(1<<MAX_ORDER)/PAGE_SIZE];

/**************************************************************************
 * Public Function Prototypes
 **************************************************************************/

/**************************************************************************
 * Local Functions
 **************************************************************************/

/**
 * Initialize the buddy system
 */
void buddy_init()
{
	int n_pages = (1<<MAX_ORDER) / PAGE_SIZE;
	for (int i = 0; i < n_pages; i++) {
		INIT_LIST_HEAD(&g_pages[i].list);
		g_pages[i].free = 1;
		g_pages[i].order = MAX_ORDER;
		g_pages[i].addr = (char*)PAGE_TO_ADDR(i);
		//&g_pages[i];
	}

	/* initialize freelist */
	for (int i = MIN_ORDER; i <= MAX_ORDER; i++) {
		INIT_LIST_HEAD(&free_area[i]);
	}

	/* add the entire memory as a freeblock */
	list_add(&g_pages[0].list, &free_area[MAX_ORDER]);
}

/**
 * Allocate a memory block.
 *
 * On a memory request, the allocator returns the head of a free-list of the
 * matching size (i.e., smallest block that satisfies the request). If the
 * free-list of the matching block size is empty, then a larger block size will
 * be selected. The selected (large) block is then splitted into two smaller
 * blocks. Among the two blocks, left block will be used for allocation or be
 * further splitted while the right block will be added to the appropriate
 * free-list.
 *
 * @param size size in bytes
 * @return memory block address
 */
void *buddy_alloc(int size)
{
	int order = ceil(log2(size));
	if(order<MIN_ORDER){
		order = MIN_ORDER;
	}
	page_t* curr = NULL;
	struct list_head* head;
	//find a free page in the list of blocks of the smallest size that can hold the allocation.
	list_for_each(head, &free_area[order]){
		curr = list_entry(head, page_t, list);
		if(curr==NULL) break;
	}
	//if we found a free page at requested order, just return it
	if (curr != NULL){
		//printf("returning free page\n");
		list_del(&curr->list);
		curr->free = 0;
		//max order
		return curr->addr;
	}
	//otherwise try larger orders, we must be able to find at least one free page
	else{
		int newOrder = order+1;
		while (curr == NULL){
			if (newOrder>MAX_ORDER) return NULL;
			list_for_each(head, &free_area[newOrder]){
				curr = list_entry(head, page_t, list);
				if(curr->free == 1) break;
			}
			if(curr == NULL) newOrder++;
		}
		if(curr==NULL) return NULL;
		//remove the larger page from the freelist it's in
		list_del(&curr->list);
		//split curr in two and only use half - put the 'right half' in the freelist for the order below
		while(newOrder>order){
			char* lowerAddr = BUDDY_ADDR(curr->addr, (newOrder-1));
			page_t* rightBlock = &g_pages[ADDR_TO_PAGE(lowerAddr)];
			rightBlock->order = newOrder-1;
			rightBlock->free = 1;
			rightBlock->addr = lowerAddr;
			list_add(&rightBlock->list, &free_area[newOrder-1]);
			newOrder--;
		}
		//curr has now been split down to the proper size, return it to allocate
		curr->order = order;
		curr->free = 0;
		return curr->addr;
	}


}
//finding free pages in the g_pages table is weird sometimes?
//probably my fault somewhere
//just check the freelist manually
page_t* findFreeBuddyManually(char* addr, int order){
	page_t* ret = NULL;
	struct list_head* head;
	list_for_each(head, &free_area[order]){
		ret = list_entry(head, page_t, list);
		if(ret->addr == addr){
			return ret;
		}
	}
	return NULL;
}

/**
 * Free an allocated memory block.
 *
 * Whenever a block is freed, the allocator checks its buddy. If the buddy is
 * free as well, then the two buddies are combined to form a bigger block. This
 * process continues until one of the buddies is not free.
 *
 * @param addr memory block address to be freed
 */
void buddy_free(void *addr)
{

	page_t* freedBlock = &g_pages[ADDR_TO_PAGE(addr)];
	if(freedBlock == NULL) return;
	int order = freedBlock->order;

	//if the page to be freed was split it might have buddies at various sizes
	//we can merge them with it to consolidate memory if they're free
	char* buddyAddr = BUDDY_ADDR(freedBlock->addr, order);
	page_t* buddy = &g_pages[ADDR_TO_PAGE(buddyAddr)];



	if(buddy->free){
		while(buddy != NULL && buddy->free && order<MAX_ORDER){
			//remove buddy from its freelist, we can combine with the freed page for a bigger block
			list_del(&buddy->list);

			//switch which page is buddy and which is freedPage
			if(freedBlock->addr< buddy->addr){
				buddy = NULL;
			}
			else{
				freedBlock = buddy;
				buddy = NULL;
			}

			//update order of current page, move up a list
			freedBlock->order++;
			order++;

			//look for a buddy we can free at the next order up
	 		buddyAddr = BUDDY_ADDR(freedBlock->addr, order);
			buddy = findFreeBuddyManually(buddyAddr, order);
		}
	}
	//add a single free block of the highest order we were able to merge up to
	//or just free freedBlock if we weren't able to merge at all
	list_add(&freedBlock->list, &free_area[order]);
	freedBlock->free = 1;
}

/**
 * Print the buddy system status---order oriented
 *
 * print free pages in each order.
 */
void buddy_dump()
{
	int o;
	for (o = MIN_ORDER; o <= MAX_ORDER; o++) {
		struct list_head *pos;
		int cnt = 0;
		list_for_each(pos, &free_area[o]) {
			cnt++;
		}
		printf("%d:%dK ", cnt, (1<<o)/1024);
	}
	printf("\n");
}
