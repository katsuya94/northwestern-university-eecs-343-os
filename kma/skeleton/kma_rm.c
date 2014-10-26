/***************************************************************************
 *  Title: Kernel Memory Allocator
 * -------------------------------------------------------------------------
 *    Purpose: Kernel memory allocator based on the resource map algorithm
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
#ifdef KMA_RM
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

#define CONTROL_BLOCK_SIZE (sizeof(void*)) // The size of the control block in each page
#define CONTROL_BLOCK_FIRST_NODE(ctrl) (*((void**) ctrl))

#define ALLOC_HEADER_SIZE (sizeof(kma_size_t))
#define FREE_HEADER_SIZE (sizeof(kma_size_t) + sizeof(void*) * 2)
#define PAYLOAD(hdr) ((void*) hdr + sizeof(kma_size_t))
#define SIZE(hdr) (*((kma_size_t*) hdr))
#define PREV(hdr) (*((void**) ((void*) hdr + sizeof(kma_size_t))))
#define NEXT(hdr) (*((void**) ((void*) hdr + sizeof(kma_size_t) + sizeof(void*))))
#define HEADER(payload) ((void*) payload - sizeof(kma_size_t))

/************Global Variables*********************************************/
static kma_page_t* root_page = NULL;

/************Function Prototypes******************************************/
void add_and_insert_free_header(void* target, kma_size_t size);
void coalesce(void* hdr);

/************External Declaration*****************************************/

/**************Implementation***********************************************/

void print_sequential_allocated(void* node, void** next_hdr) {
  int flag = FALSE;
  void* page_ptr = BASEADDR(node);

  while (BASEADDR(node) == page_ptr) {
    void* checker = CONTROL_BLOCK_FIRST_NODE(root_page->ptr);
    while (checker != NULL) {
      if (checker == node) {
        flag = TRUE;
        break;
      }
      checker = NEXT(checker);
    }
    if (flag == TRUE) {
      break;
    }

    int page_id;
    if ((void*) BASEADDR(node) == root_page->ptr) {
      page_id = root_page->id;
    } else {
      page_id = (*((kma_page_t**) BASEADDR(node)))->id; 
    }

    printf("%2d ALLOCATED | hdr = %11p; size %4x; next_hdr = %11p\n",
           page_id,
           node,
           SIZE(node),
           node + ALLOC_HEADER_SIZE + SIZE(node));
    *next_hdr = node + ALLOC_HEADER_SIZE + SIZE(node);
    node += ALLOC_HEADER_SIZE + SIZE(node);
  }
}

void dump() {
  if (root_page == NULL) {
    printf("       EMPTY |\n");
    return;
  }

  void* prev = NULL;
  void* node = CONTROL_BLOCK_FIRST_NODE(root_page->ptr);
  void* next_hdr = NULL;
  kma_page_t* page = NULL;
  kma_page_t* prev_page = NULL;

  while (node != NULL) {
    prev_page = page;

    int page_id;

    if ((void*) BASEADDR(node) == root_page->ptr) {
      page = root_page;
      page_id = root_page->id;
    } else {
      page = *((kma_page_t**) BASEADDR(node));
      page_id = (*((kma_page_t**) BASEADDR(node)))->id; 
    }

    if (prev == NULL) {
      if (node > root_page->ptr + CONTROL_BLOCK_SIZE) {
        print_sequential_allocated(root_page->ptr + CONTROL_BLOCK_SIZE, &next_hdr);
      }
    } else if (prev_page != page) {
      print_sequential_allocated(page->ptr + CONTROL_BLOCK_SIZE, &next_hdr);
    } else {
      kma_size_t diff = node - (prev + ALLOC_HEADER_SIZE + SIZE(prev));
      if (diff > 0) {
        print_sequential_allocated(prev + ALLOC_HEADER_SIZE + SIZE(prev), &next_hdr);
      }
    }

    // if (prev_page == page && next_hdr != NULL) {
    //   assert(next_hdr == node);
    // }

    printf("%2d     FREED | hdr = %11p; size %4x; next_hdr = %11p; PREV = %11p; NEXT = %11p\n",
           page_id,
           node,
           SIZE(node),
           node + ALLOC_HEADER_SIZE + SIZE(node),
           PREV(node),
           NEXT(node));

    next_hdr = node + ALLOC_HEADER_SIZE + SIZE(node);

    prev = node;
    node = NEXT(node);
  }
}

void*
kma_malloc(kma_size_t size)
{
  // printf("about to allocate %x\n", size);

  /* If first malloc, initialize necessary information */
  if (root_page == NULL) {
    root_page = get_page();

    /* Create a free block for the rest of the first page */
    void* hdr = root_page->ptr + CONTROL_BLOCK_SIZE;
    SIZE(hdr) = root_page->size - CONTROL_BLOCK_SIZE - ALLOC_HEADER_SIZE;
    PREV(hdr) = NULL;
    NEXT(hdr) = NULL;

    /* Set the list root in the control block to point to the free block */

    CONTROL_BLOCK_FIRST_NODE(root_page->ptr) = hdr;
  }

  void* node = CONTROL_BLOCK_FIRST_NODE(root_page->ptr);

  /* If the request is too small, allocate more */

  if (size < FREE_HEADER_SIZE - ALLOC_HEADER_SIZE) {
    size = FREE_HEADER_SIZE - ALLOC_HEADER_SIZE;
  }

  /* First fit */

  while (node != NULL) {
    if (SIZE(node) >= size) {
        break;
    }
    node = NEXT(node);
  }

  /* If no node was found, allocate a new page */

  if (node == NULL) {
    kma_page_t* page = get_page();

    /* At the beginning of the page, store a pointer to the page struct */
    *((kma_page_t**) page->ptr) = page;

    /* Create a new free block after the page struct pointer spanning the
       rest of the page */
    
    node = (void*) page->ptr + CONTROL_BLOCK_SIZE;
    add_and_insert_free_header(node, page->size - CONTROL_BLOCK_SIZE - ALLOC_HEADER_SIZE);
  }

  kma_size_t target_size = SIZE(node);
  void* ptr = PAYLOAD(node);

  if (target_size - size < FREE_HEADER_SIZE) {
    size = target_size;
  }

  SIZE(node) = size;

  /* Remove old free block */

  void* prev = PREV(node);
  void* next = NEXT(node);

  if (prev != NULL) {
    NEXT(prev) = next;
  } else {
    CONTROL_BLOCK_FIRST_NODE(root_page->ptr) = next;
  }

  if (next != NULL) {
    PREV(next) = prev;
  }

  if (size != target_size) {
    add_and_insert_free_header(node + ALLOC_HEADER_SIZE + size,
                               target_size - ALLOC_HEADER_SIZE - size);
  }

  // dump();

  return ptr;
}

void
kma_free(void* ptr, kma_size_t size)
{
  // printf("about to free %x at %p\n", size, ptr);

  assert(SIZE(HEADER(ptr)) >= size);
  void* hdr = HEADER(ptr);
  add_and_insert_free_header(hdr, SIZE(hdr));

  coalesce(hdr);

  // dump();
}

void add_and_insert_free_header(void* target, kma_size_t size) {
  SIZE(target) = size;

  void* node = CONTROL_BLOCK_FIRST_NODE(root_page->ptr);

  if (node == NULL) {
    CONTROL_BLOCK_FIRST_NODE(root_page->ptr) = target;
    PREV(target) = NULL;
    NEXT(target) = NULL;
  } else {
    void* prev = NULL;
    while (node < target && node != NULL) {
      prev = node;
      node = NEXT(node);
    }

    if (node != NULL) {
      PREV(node) = target;
    }

    if (prev == NULL) {
      CONTROL_BLOCK_FIRST_NODE(root_page->ptr) = target;
    } else {
      NEXT(prev) = target;
    }

    NEXT(target) = node;
    PREV(target) = prev;
  }
}

void coalesce(void* hdr) {
  int again = FALSE;
  kma_size_t size = SIZE(hdr);

  void* prev = PREV(hdr);
  void* next = NEXT(hdr);

  if (prev != NULL &&
      BASEADDR(prev) == BASEADDR(hdr) &&
      prev + ALLOC_HEADER_SIZE + SIZE(prev) == hdr) {
    size += SIZE(prev) + ALLOC_HEADER_SIZE;
    NEXT(prev) = next;
    if (next != NULL) {
      PREV(next) = prev;
    }
    SIZE(prev) = size;
    hdr = prev;
    again = TRUE;
  }

  if (next != NULL &&
      BASEADDR(hdr) == BASEADDR(next) &&
      hdr + ALLOC_HEADER_SIZE + SIZE(hdr) == next) {
    size += SIZE(next) + ALLOC_HEADER_SIZE;
    NEXT(hdr) = NEXT(next);
    if (NEXT(next) != NULL) {
      PREV(NEXT(next)) = hdr;
    }
    SIZE(hdr) = size;
    again = TRUE;
  }

  if (again == TRUE) {
    coalesce(hdr);
  } else {
    kma_size_t page_size;

    if (BASEADDR(hdr) == root_page->ptr) {
      page_size = root_page->size;
    } else {
      page_size = (*((kma_page_t**) BASEADDR(hdr)))->size;
    }

    if (SIZE(hdr) == page_size - CONTROL_BLOCK_SIZE - ALLOC_HEADER_SIZE) {
      if (PREV(hdr) != NULL) {
        NEXT(PREV(hdr)) = NEXT(hdr);
      } else {
        CONTROL_BLOCK_FIRST_NODE(root_page->ptr) = NEXT(hdr);
      }
      if (NEXT(hdr) != NULL) {
        PREV(NEXT(hdr)) = PREV(hdr);
      }
      if (BASEADDR(hdr) == root_page->ptr) {
        free_page(root_page);
        if (NEXT(hdr) != NULL) {
          root_page = *((kma_page_t**) BASEADDR(NEXT(hdr)));
          CONTROL_BLOCK_FIRST_NODE(root_page->ptr) = NEXT(hdr);
        } else {
          root_page = NULL;
        }
      } else {
        free_page(*((kma_page_t**) BASEADDR(hdr)));
      }
    }
  }
}

#endif // KMA_RM
