#ifndef INPLA_HEAP_H
#define INPLA_HEAP_H

#include "id_table.h"
#include "types.h"

#include <stddef.h>

#ifdef EXPANDABLE_HEAP

// HOOP_SIZE must be power of two
// #define INIT_HOOP_SIZE (1 << 10)
// #define HOOP_SIZE (1 << 18)
#  define HOOP_SIZE_MASK ((HOOP_SIZE) - 1)

typedef struct HoopList_tag {
  VALUE *hoop;
  struct HoopList_tag *next;
} HoopList;

// Nodes with '1' on the 31bit in hoops are ready for use and
// '0' are occupied, that is to say, using now.
#  define HOOPFLAG_READYFORUSE                0x01 << 31
#  define IS_READYFORUSE(a)                   ((a) & HOOPFLAG_READYFORUSE)
#  define SET_HOOPFLAG_READYFORUSE(a)         ((a) = ((a) | HOOPFLAG_READYFORUSE))
#  define RESET_HOOPFLAG_READYFORUSE_AGENT(a) ((a) = (HOOPFLAG_READYFORUSE))
#  define RESET_HOOPFLAG_READYFORUSE_NAME(a)                                   \
    ((a) = ((ID_NAME) | (HOOPFLAG_READYFORUSE)))
#  define TOGGLE_HOOPFLAG_READYFORUSE(a) ((a) = ((a) ^ HOOPFLAG_READYFORUSE))

typedef struct Heap_tag {
  HoopList *last_alloc_list;
  int last_alloc_idx;
} Heap;

HoopList *HoopList_new_forName(void);
HoopList *HoopList_new_forAgent(void);

/* ------------------------------------------------------------
   FLEX EXPANDABLE HEAP
 ------------------------------------------------------------ */
#elif defined(FLEX_EXPANDABLE_HEAP)
extern unsigned int Hoop_init_size;
extern unsigned int Hoop_increasing_magnitude;

typedef struct HoopList_tag {
  VALUE *hoop;
  struct HoopList_tag *next;
  unsigned int size; // NOTE: Be power of 2!
} HoopList;

#  define HOOPFLAG_READYFORUSE                0
#  define IS_READYFORUSE(a)                   ((a) == HOOPFLAG_READYFORUSE)
#  define SET_HOOPFLAG_READYFORUSE(a)         ((a) = HOOPFLAG_READYFORUSE)
#  define RESET_HOOPFLAG_READYFORUSE_AGENT(a) ((a) = HOOPFLAG_READYFORUSE)
#  define RESET_HOOPFLAG_READYFORUSE_NAME(a)  ((a) = HOOPFLAG_READYFORUSE)

typedef struct Heap_tag {
  HoopList *last_alloc_list;
  unsigned int last_alloc_idx;
} Heap;

HoopList *HoopList_new_forName(unsigned int size);
HoopList *HoopList_new_forAgent(unsigned int size);

#else
// v0.5.6 -------------------------------------
// Fixed size buffer
// --------------------------------------------
typedef struct Heap_tag {
  VALUE *heap;
  int lastAlloc;
  unsigned int size;
} Heap;

// Heap cells having '1' on the 31bit are ready for use and
// '0' are occupied, that is to say, using now.
#  define HEAPFLAG_READYFORUSE                0x01 << 31
#  define IS_READYFORUSE(a)                   ((a) & HEAPFLAG_READYFORUSE)
#  define SET_HEAPFLAG_READYFORUSE(a)         ((a) = ((a) | HEAPFLAG_READYFORUSE))
#  define RESET_HEAPFLAG_READYFORUSE_AGENT(a) ((a) = (HEAPFLAG_READYFORUSE))
#  define RESET_HEAPFLAG_READYFORUSE_NAME(a)                                   \
    ((a) = ((ID_NAME) | (HEAPFLAG_READYFORUSE)))
#  define TOGGLE_HEAPFLAG_READYFORUSE(a) ((a) = ((a) ^ HEAPFLAG_READYFORUSE))

VALUE *MakeAgentHeap(int size) {
  int i;
  VALUE *heap;

  // Agent Heap
  heap = (VALUE *)malloc(sizeof(Agent) * size);
  if (heap == (VALUE *)NULL) {
    printf("[Heap]Malloc error\n");
    exit(-1);
  }
  for (i = 0; i < size; i++) {
    RESET_HEAPFLAG_READYFORUSE_AGENT(((Agent *)(heap))[i].basic.id);
  }

  return heap;
}

VALUE *MakeNameHeap(int size) {
  int i;
  VALUE *heap;

  // Name Heap
  heap = (VALUE *)malloc(sizeof(Name) * size);
  if (heap == (VALUE *)NULL) {
    printf("[Name]Malloc error\n");
    exit(-1);
  }
  for (i = 0; i < size; i++) {
    //    ((Name *)(heap))[i].basic.id = ID_NAME;
    RESET_HEAPFLAG_READYFORUSE_NAME(((Name *)(heap))[i].basic.id);
  }

  return heap;
}

//---------------------------------------------

#endif

VALUE myalloc_Agent(Heap *hp);
VALUE myalloc_Name(Heap *hp);

void myfree(VALUE ptr);
void myfree2(VALUE ptr, VALUE ptr2);

unsigned long Heap_GetNum_Usage_forName(Heap *hp);
unsigned long Heap_GetNum_Usage_forAgent(Heap *hp);

#endif // INPLA_HEAP_H
