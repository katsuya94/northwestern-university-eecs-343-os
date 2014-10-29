/***************************************************************************
 *  Title: Kernel Memory Allocator
 * -------------------------------------------------------------------------
 *    Purpose: Kernel memory allocator based on the buddy algorithm
 *    Author: Stefan Birrer
 *    Copyright: 2004 Northwestern University
 ***************************************************************************/
/***************************************************************************
 *  ChangeLog:
 * -------------------------------------------------------------------------
 *    Revision 1.2  2009/10/31 21:28:52  jot836
 *    This is the current version of KMA project 3.
 *    It includes:
 *    - the most up-to-date handout (F'09)
 *    - updated skeleton including
 *        file-driven test harness,
 *        trace generator script,
 *        support for evaluating efficiency of algorithm (wasted memory),
 *        gnuplot support for plotting allocation and waste,
 *        set of traces for all students to use (including a makefile and README of the settings),
 *    - different version of the testsuite for use on the submission site, including:
 *        scoreboard Python scripts, which posts the top 5 scores on the course webpage
 *
 *    Revision 1.1  2005/10/24 16:07:09  sbirrer
 *    - skeleton
 *
 *    Revision 1.2  2004/11/05 15:45:56  sbirrer
 *    - added size as a parameter to kma_free
 *
 *    Revision 1.1  2004/11/03 23:04:03  sbirrer
 *    - initial version for the kernel memory allocator project
 *
 ***************************************************************************/
#ifdef KMA_BUD
#define __KMA_IMPL__

/************System include***********************************************/
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/************Private include**********************************************/
#include "kma_page.h"
#include "kma.h"

/************Defines and Typedefs*****************************************/
/*  #defines and typedefs should have their names in all caps.
 *  Global variables begin with g. Global constants with k. Local
 *  variables should be in all lower case. When initializing
 *  structures and arrays, line everything up in neat columns.
 */

#define FREE_HEADER_SIZE (sizeof(kma_size_t) + sizeof(void*))
#define ALLOC_HEADER_SIZE (sizeof(kma_size_t))

/* Since sizes never use the least significant bit,
   instead, use it to mark its state of allocation */

#define IS_ALLOCATED(hdr) ((*((kma_size_t*) hdr)) & ((kma_size_t) 0x1))
#define ALLOCATE(hdr) *((kma_size_t*) hdr) |= (kma_size_t) 0x1
#define DEALLOCATE(hdr) *((kma_size_t*) hdr) &= ~((kma_size_t) 0x1)
#define NEXT(hdr) (*((void**) ((void*) hdr + sizeof(kma_size_t))))
#define SIZE(hdr) ((*((kma_size_t*) hdr)) & ~((kma_size_t) 0x1))
#define SET_SIZE(hdr, size) *((kma_size_t*) hdr) = \
                            (size & ~((kma_size_t) 0x1)) | \
                            ((*((kma_size_t*) hdr)) & ((kma_size_t) 0x1))

/* The mask component is used to XOR and get the
   buddy */

#define FIRST_FREE_NODE(class_id) (((kma_root_t*) \
  ((void*) root_page->ptr + sizeof(kma_page_ptr_t*) * 2))[class_id].first)
#define MASK(class_id) (((kma_root_t*) \
  ((void*) root_page->ptr + sizeof(kma_page_ptr_t*) * 2))[class_id].mask)

#define PAYLOAD(hdr) ((void*) hdr + sizeof(kma_size_t))
#define HEADER(payload) ((void*) payload - sizeof(kma_size_t))

/* Gives access to the page data structure */

#define PAGE_PTR_LIST_ROOT (*((kma_page_ptr_t**) root_page->ptr))
#define PAGE_PTR_FREE_LIST_ROOT (*((kma_page_ptr_t**) ((void*) root_page->ptr + sizeof(kma_page_ptr_t*))))

// #define VERBOSE

 typedef struct {
  void* first;
  kma_size_t mask;
 } kma_root_t;

 typedef struct kma_page_ptr_t {
  kma_page_t* page;
  struct kma_page_ptr_t* next;
  struct kma_page_ptr_t* prev;
  int allocated;
 } kma_page_ptr_t;

/************Global Variables*********************************************/
static kma_page_t* root_page = NULL;

/************Function Prototypes******************************************/
void create_block(void* hdr, kma_size_t size, int allocated);

/************External Declaration*****************************************/

/**************Implementation***********************************************/

void dump_page(void* node) {
  void* base = BASEADDR(node);
  while (BASEADDR(node) == base) {
    printf("%10s %p | SIZE(hdr) = %#6x; NEXT(hdr) = %p;\n",
           IS_ALLOCATED(node) ? "ALLOCATED" : "FREE",
           node,
           SIZE(node),
           NEXT(node));
    node += SIZE(node);
  }
}

void dump() {
  if (root_page == NULL) {
    printf("EMPTY\n");
    return;
  }

  printf("USED PAGE_PTRS\n");

  kma_page_ptr_t* node = PAGE_PTR_LIST_ROOT;

  while (node != NULL) {
    printf(" USED %p | id = %d; allocated = %d; prev = %p; next = %p;\n", node, node->page->id, node->allocated, node->prev, node->next);
    dump_page(node->page->ptr);
    node = node->prev;
  }

  printf("WASTE PAGE PTRS\n");

  node = PAGE_PTR_FREE_LIST_ROOT;

  while (node != NULL) {
    printf("WASTE %p | allocated = %d prev = %p; next = %p;\n", node, node->allocated, node->prev, node->next);
    node = node->prev;
  }
}

void*
kma_malloc(kma_size_t size)
{
  #ifdef VERBOSE
  printf("about to allocate %#x\n", size);
  #endif

  kma_size_t total_size;

  /* If first malloc, initialize necessary information */

  if (root_page == NULL) {
    root_page = get_page();

    /* Figure out how many size classes there should be
       and initialize pointer and mask for each class */

    kma_size_t class_size = PAGESIZE;
    int num_classes = 0;

    while (class_size >= FREE_HEADER_SIZE) {
      FIRST_FREE_NODE(num_classes) = NULL;
      MASK(num_classes) = class_size;
      num_classes++;
      class_size >>= 1;
    }

    /* Initialize pointers to the page data structure */

    PAGE_PTR_LIST_ROOT = NULL;
    PAGE_PTR_FREE_LIST_ROOT = NULL;
  }

  /* Find the optimal size for the requested block */

  kma_size_t optimal_size = PAGESIZE;
  int i = 0;

  while ((optimal_size >> 1) >= size + ALLOC_HEADER_SIZE &&
         (optimal_size >> 1) >= FREE_HEADER_SIZE) {
    i++;
    optimal_size >>= 1;
  }

  /* If there is no free blocks in the optimal size
     class, find the next largest class that does */

  void* node = FIRST_FREE_NODE(i);

  while (node == NULL && i > 0) {
    i--;
    node = FIRST_FREE_NODE(i);
  }

  /* If still no candidate is found, allocate a new page */

  if (node == NULL) {
    kma_page_t* page = get_page();

    /* Determine where to store the entry in the page data
       structure */

    kma_page_ptr_t* page_ptr;

    if (PAGE_PTR_FREE_LIST_ROOT != NULL) {
      /* If there are unused entries in the page data
         structure, remove it from the unused entry
         list */
      page_ptr = PAGE_PTR_FREE_LIST_ROOT;
      PAGE_PTR_FREE_LIST_ROOT = page_ptr->prev;
    } else {
      /* If not, allocate a new page for the page data
         structure */
      kma_page_t* ptr_page = get_page();

      /* Clear the page */
      memset(ptr_page->ptr, '\0', PAGESIZE);

      /* At the beginning of the page, point back to its page
         struct */
      (*((kma_page_t**) (ptr_page->ptr))) = ptr_page;

      /* Start scanning from after the page struct pointer
         and add it to the list of unused page data structure
         entries */

      kma_page_ptr_t* scanner = ptr_page->ptr + sizeof(kma_page_t*);

      while (BASEADDR(scanner + 1) == ptr_page->ptr) {
        scanner->allocated = FALSE;
        scanner->next = NULL;
        scanner->prev = PAGE_PTR_FREE_LIST_ROOT;
        if (scanner->prev != NULL) {
          scanner->prev->next = scanner;
        }
        PAGE_PTR_FREE_LIST_ROOT = scanner;
        scanner++;
      }

      /* Take the first entry of the new page data structure
         page and remove it */

      page_ptr = (kma_page_ptr_t*) (ptr_page->ptr + sizeof(kma_page_t*));

      if (page_ptr->next != NULL) {
        page_ptr->next->prev = page_ptr->prev;
      } else {
        PAGE_PTR_FREE_LIST_ROOT = page_ptr;
      }
      if (page_ptr->prev != NULL) {
        page_ptr->prev->next = page_ptr;
      }
    }

    /* Point the entry to the newly allocated page's page struct,
       mark the entry as allocated and add it to the used list */

    page_ptr->page = page;

    page_ptr->allocated = TRUE;
    page_ptr->next = NULL;
    page_ptr->prev = PAGE_PTR_LIST_ROOT;
    if (page_ptr->prev != NULL) {
      page_ptr->prev->next = page_ptr;
    }
    PAGE_PTR_LIST_ROOT = page_ptr;

    /* Create a block spanning the page and use it */

    create_block(page->ptr, PAGESIZE, FALSE);
    node = page->ptr;
    i = 0;
  }

  kma_size_t target_size = SIZE(node);

  /* Remove the target block from its free list */

  FIRST_FREE_NODE(i) = NEXT(node);

  create_block(node, optimal_size, TRUE);

  /* Fill up the remaining space with power of 2 blocks */

  total_size = optimal_size;
  while (total_size < target_size) {
    create_block(node + total_size, total_size, FALSE);
    total_size <<= 1;
  }

  #ifdef VERBOSE
  dump();
  #endif

  return PAYLOAD(node);
}

void 
kma_free(void* ptr, kma_size_t size)
{
  void* hdr = HEADER(ptr);

  #ifdef VERBOSE
  printf("about to free %#x where hdr = %p\n", size, hdr);
  #endif

  /* Find the appropriate size class */

  kma_size_t class_size = PAGESIZE;
  int i = 0;

  while (class_size != SIZE(hdr)) {
    i++;
    class_size >>= 1;
  }

  DEALLOCATE(hdr);

  /* Attempt to find buddy, ensure that the buddy is in
     the same page, has the same size, and is free */

  void* buddy = (void*) ((long) hdr ^ MASK(i));

  while (!IS_ALLOCATED(buddy) &&
         SIZE(buddy) == class_size &&
         BASEADDR(buddy) == BASEADDR(hdr)) {
    if (buddy == root_page->ptr) {
      break;
    }

    /* Find the node previous to buddy */

    void* prev = NULL;
    void* node = FIRST_FREE_NODE(i);

    while (node != buddy) {
      prev = node;
      node = NEXT(node);
    }

    /* Remove buddy from list */

    if (prev == NULL) {
      FIRST_FREE_NODE(i) = NEXT(buddy);
    } else {
      NEXT(prev) = NEXT(buddy);
    }

    /* The beginning of the coalesced block should be
       the lesser one */

    if (buddy < hdr) {
      hdr = buddy;
    }

    /* Set up for next iteration, repeat until no
       buddy is found */

    i--;
    class_size <<= 1;
    buddy = (void*) ((long) hdr ^ MASK(i));
  }

  /* If a free block spans an entire page, free the page */

  if (class_size == PAGESIZE) {
    /* Find the page data structure entry corresponding to
       this page */

    kma_page_ptr_t* node = PAGE_PTR_LIST_ROOT;

    while (hdr != node->page->ptr) {
      node = node->prev;
    }

    /* Remove page data structure entry from the used list */

    if (node->next != NULL) {
      node->next->prev = node->prev;
    } else {
      PAGE_PTR_LIST_ROOT = node->prev;
    }

    if (node->prev != NULL) {
      node->prev->next = node->next;
    }

    /* Add it to the unused list */

    node->allocated = FALSE;
    node->next = NULL;
    node->prev = PAGE_PTR_FREE_LIST_ROOT;
    if (node->prev != NULL) {
      node->prev->next = node;
    }
    PAGE_PTR_FREE_LIST_ROOT = node;

    /* Free the page */

    free_page(node->page);

    /* Scan the page of the page data structure to see if
       any entries are in use */

    void* base = BASEADDR(node);
    kma_page_ptr_t* scanner = base + sizeof(kma_page_t*);

    int used = FALSE;

    while (BASEADDR(scanner + 1) == base) {
      if (scanner->allocated == TRUE) {
        used = TRUE;
        break;
      }
      scanner++;
    }

    if (used == FALSE) {
      /* If not, remove them all from the unused list */

      scanner = base + sizeof(kma_page_t*);
      while (BASEADDR(scanner) == base) {
        if (scanner->next != NULL) {
          scanner->next->prev = scanner->prev;
        } else {
          PAGE_PTR_FREE_LIST_ROOT = scanner->prev;
        }
        if (scanner->prev != NULL) {
          scanner->prev->next = scanner->next;
        }
        scanner++;
      }

      /* Then, free the page data structure page */

      free_page(*((kma_page_t**) base));

      /* If there are no unused or used page data structure
         entries */

      if (PAGE_PTR_LIST_ROOT == NULL && PAGE_PTR_FREE_LIST_ROOT == NULL) {
        /* Free the root page, next time everything must
           be re-initialized */
        free_page(root_page);
        root_page = NULL;
      }
    }
  } else {
    /* Add the coalesced block to the appropriate free
       list */

    SET_SIZE(hdr, class_size);
    NEXT(hdr) = FIRST_FREE_NODE(i);
    FIRST_FREE_NODE(i) = hdr;
  }

  #ifdef VERBOSE
  dump();
  #endif
}

void create_block(void* hdr, kma_size_t size, int allocated) {
  /* Find the appropriate size class */

  kma_size_t class_size = PAGESIZE;
  int i = 0;

  while (class_size != size) {
    i++;
    class_size >>= 1;
  }

  /* Set size */
  SET_SIZE(hdr, class_size);

  /* Simply mark as allocated or mark as unallocated
     and add to free list */

  if (allocated) {
    ALLOCATE(hdr);
  } else {
    DEALLOCATE(hdr);
    NEXT(hdr) = FIRST_FREE_NODE(i);
    FIRST_FREE_NODE(i) = hdr;
  }
}

#endif // KMA_BUD
