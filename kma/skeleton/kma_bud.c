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
#define IS_ALLOCATED(hdr) ((*((kma_size_t*) hdr)) & ((kma_size_t) 0x1))
#define ALLOCATE(hdr) *((kma_size_t*) hdr) |= (kma_size_t) 0x1
#define DEALLOCATE(hdr) *((kma_size_t*) hdr) &= ~((kma_size_t) 0x1)
#define NEXT(hdr) (*((void**) ((void*) hdr + sizeof(kma_size_t))))
#define SIZE(hdr) ((*((kma_size_t*) hdr)) & ~((kma_size_t) 0x1))
#define SET_SIZE(hdr, size) *((kma_size_t*) hdr) = \
                            (size & ~((kma_size_t) 0x1)) | \
                            ((*((kma_size_t*) hdr)) & ((kma_size_t) 0x1))
#define FIRST_FREE_NODE(class_id) ((((kma_root_t*) root_page->ptr)[class_id]).first)
#define MASK(class_id) ((((kma_root_t*) root_page->ptr)[class_id]).mask)
#define PAYLOAD(hdr) ((void*) hdr + sizeof(kma_size_t))
#define HEADER(payload) ((void*) payload - sizeof(kma_size_t))

#define VERBOSE

 typedef struct {
  void* first;
  void* mask;
 } kma_root_t;

/************Global Variables*********************************************/
static kma_page_t* root_page = NULL;

/************Function Prototypes******************************************/
void create_block(void* hdr, kma_size_t size, int allocated);

/************External Declaration*****************************************/

/**************Implementation***********************************************/

void dump() {
  kma_size_t class_size = PAGESIZE;
  int i = 0;

  while (class_size >= ALLOC_HEADER_SIZE) {
    printf("CLASS SIZE %#x\n", class_size);
    void* node = FIRST_FREE_NODE(i);
    while (node != NULL) {
      printf("%10s %p | BASEADDR(hdr) = %p; SIZE(hdr) = %#6x; NEXT(hdr) = %p;\n",
             IS_ALLOCATED(node) ? "ALLOCATED" : "FREE",
             node,
             BASEADDR(node),
             SIZE(node),
             NEXT(node));
      node = NEXT(node);
    }
    i++;
    class_size >>= 1;
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

    /* Figure out how many size classes there should be */

    kma_size_t class_size = PAGESIZE;
    int num_classes = 0;

    while (class_size >= FREE_HEADER_SIZE) {
      FIRST_FREE_NODE(num_classes) = NULL;
      MASK(num_classes) = NULL;
      MASK(num_classes) += class_size;
      num_classes++;
      class_size >>= 1;
    }

    kma_size_t control_block_size = 0x1;
    while (control_block_size < num_classes * sizeof(kma_root_t) ||
           control_block_size < ALLOC_HEADER_SIZE) {
      control_block_size <<= 1;
    }

    total_size = control_block_size;
    while (total_size < PAGESIZE) {
      create_block(root_page->ptr + total_size, total_size, FALSE);
      total_size <<= 1;
    }
  }

  printf("SIZE = %#x\n", size);

  kma_size_t optimal_size = PAGESIZE;
  int i = 0;

  while ((optimal_size >> 1) >= size + ALLOC_HEADER_SIZE &&
         (optimal_size >> 1) >= FREE_HEADER_SIZE) {
    i++;
    optimal_size >>= 1;
  }

  printf("optimal_size = %#x; class_id = %d\n", optimal_size, i);

  void* node = FIRST_FREE_NODE(i);

  while (node == NULL && i >= 0) {
    i--;
    node = FIRST_FREE_NODE(i);
  }

  if (node == NULL) {
    printf("nothing fits\n");
    kma_page_t* page = get_page();
    create_block(page->ptr, PAGESIZE, FALSE);
    node = page->ptr;
    i = 0;
  }

  kma_size_t target_size = SIZE(node);

  printf("target_size = %#x\n", target_size);

  /* Remove old block from list */

  FIRST_FREE_NODE(i) = NEXT(node);

  create_block(node, optimal_size, TRUE);

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
  ;
}

void create_block(void* hdr, kma_size_t size, int allocated) {
  kma_size_t class_size = PAGESIZE;
  int i = 0;

  while (class_size != size) {
    i++;
    class_size >>= 1;
  }

  SET_SIZE(hdr, class_size);
  if (allocated) {
    ALLOCATE(hdr);
  } else {
    DEALLOCATE(hdr);
    NEXT(hdr) = FIRST_FREE_NODE(i);
    FIRST_FREE_NODE(i) = hdr;
  }
}

#endif // KMA_BUD
