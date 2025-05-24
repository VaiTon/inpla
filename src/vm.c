#include "vm.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void *CodeAddr[OP_NOP + 1];

#ifndef THREAD
VirtualMachine VM;
#endif

// -----------------------------------------------------
// Mark and Sweep for error recovery
// -----------------------------------------------------
#ifndef THREAD

#  ifdef EXPANDABLE_HEAP
void sweep_AgentHeap(Heap *hp) {

  HoopList *hoop_list = hp->last_alloc_list;
  Agent    *hoop;

  do {
    hoop = (Agent *)(hoop_list->hoop);
    for (int i = 0; i < HOOP_SIZE; i++) {

      if (!IS_FLAG_MARKED(hoop[i].basic.id)) {
        SET_HOOPFLAG_READYFORUSE(hoop[i].basic.id);
      } else {
        TOGGLE_FLAG_MARKED(hoop[i].basic.id);
      }
    }
    hoop_list = hoop_list->next;
  } while (hoop_list != hp->last_alloc_list);
}

void sweep_NameHeap(Heap *hp) {
  HoopList *hoop_list = hp->last_alloc_list;
  Name     *hoop;

  do {
    hoop = (Name *)(hoop_list->hoop);
    for (int i = 0; i < HOOP_SIZE; i++) {

      if (!IS_FLAG_MARKED(hoop[i].basic.id)) {
        SET_HOOPFLAG_READYFORUSE(hoop[i].basic.id);
      } else {
        TOGGLE_FLAG_MARKED(hoop[i].basic.id);
      }
    }
    hoop_list = hoop_list->next;
  } while (hoop_list != hp->last_alloc_list);
}

#  elif defined(FLEX_EXPANDABLE_HEAP)

void sweep_AgentHeap(Heap *hp) {

  HoopList *hoop_list = hp->last_alloc_list;

  do {
    Agent *hoop = (Agent *)hoop_list->hoop;
    for (int i = 0; i < hoop_list->size; i++) {

      if (!IS_FLAG_MARKED(hoop[i].basic.id)) {
        SET_HOOPFLAG_READYFORUSE(hoop[i].basic.id);
      } else {
        TOGGLE_FLAG_MARKED(hoop[i].basic.id);
      }
    }
    hoop_list = hoop_list->next;
  } while (hoop_list != hp->last_alloc_list);
}

void sweep_NameHeap(Heap *hp) {
  HoopList *hoop_list = hp->last_alloc_list;
  Name     *hoop;

  do {
    hoop = (Name *)hoop_list->hoop;
    for (int i = 0; i < hoop_list->size; i++) {

      if (!IS_FLAG_MARKED(hoop[i].basic.id)) {
        SET_HOOPFLAG_READYFORUSE(hoop[i].basic.id);
      } else {
        TOGGLE_FLAG_MARKED(hoop[i].basic.id);
      }
    }
    hoop_list = hoop_list->next;
  } while (hoop_list != hp->last_alloc_list);
}

#  else

// v0.5.6
void sweep_AgentHeap(Heap *hp) {
  int i;
  for (i = 0; i < hp->size; i++) {
    if (!IS_FLAG_MARKED(((Agent *)hp->heap)[i].basic.id)) {
      SET_HEAPFLAG_READYFORUSE(((Agent *)hp->heap)[i].basic.id);
    } else {
      TOGGLE_FLAG_MARKED(((Agent *)hp->heap)[i].basic.id);
    }
  }
}
void sweep_NameHeap(Heap *hp) {
  int i;
  for (i = 0; i < hp->size; i++) {
    if (!IS_FLAG_MARKED(((Name *)hp->heap)[i].basic.id)) {
      SET_HEAPFLAG_READYFORUSE(((Name *)hp->heap)[i].basic.id);
    } else {
      TOGGLE_FLAG_MARKED(((Name *)hp->heap)[i].basic.id);
    }
  }
}

#  endif

void mark_and_sweep(void) {
  mark_allHash();
  sweep_AgentHeap(&VM.agentHeap);
  sweep_NameHeap(&VM.nameHeap);
  VM.nextPtr_eqStack = -1;
}

#endif

#ifdef EXPANDABLE_HEAP
void VM_Buffer_Init(VirtualMachine *restrict vm) {
  // Agent Heap
  vm->agentHeap.last_alloc_list = HoopList_new_forAgent();
  vm->agentHeap.last_alloc_list->next = vm->agentHeap.last_alloc_list;
  vm->agentHeap.last_alloc_idx = 0;

  // Name Heap
  vm->nameHeap.last_alloc_list = HoopList_new_forName();
  vm->nameHeap.last_alloc_list->next = vm->nameHeap.last_alloc_list;
  vm->nameHeap.last_alloc_idx = 0;

  // Register
  vm->reg = malloc(sizeof(VALUE) * VM_REG_SIZE);
}

#elif defined(FLEX_EXPANDABLE_HEAP)
void VM_Buffer_Init(VirtualMachine *restrict vm) {
  // Agent Heap
  /*
  vm->agentHeap.last_alloc_list = HoopList_new_forAgent(Hoop_init_size);
  vm->agentHeap.last_alloc_list->next = vm->agentHeap.last_alloc_list;
  vm->agentHeap.last_alloc_idx = 0;
  */

  // Agent and Name heaps start from two hoops:
  //     +-----------------------+
  //     |    new        new     |
  //     v    hoop       hoop    |
  // init-->|......|-->|......|--+
  //        last_alloc  next
  //        idx=0

  vm->agentHeap.last_alloc_list = HoopList_new_forAgent(Hoop_init_size);
  vm->agentHeap.last_alloc_list->next = HoopList_new_forAgent(Hoop_init_size);
  vm->agentHeap.last_alloc_list->next->next = vm->agentHeap.last_alloc_list;
  vm->agentHeap.last_alloc_idx = 0;

  // Name Heap
  /*
  vm->nameHeap.last_alloc_list = HoopList_new_forName(Hoop_init_size);
  vm->nameHeap.last_alloc_list->next = vm->nameHeap.last_alloc_list;
  vm->nameHeap.last_alloc_idx = 0;
  */

  vm->nameHeap.last_alloc_list = HoopList_new_forName(Hoop_init_size);
  vm->nameHeap.last_alloc_list->next = HoopList_new_forName(Hoop_init_size);
  vm->nameHeap.last_alloc_list->next->next = vm->nameHeap.last_alloc_list;
  vm->nameHeap.last_alloc_idx = 0;

  // Register
  vm->reg = malloc(sizeof(VALUE) * VM_REG_SIZE);
}
#else

void VM_InitBuffer(VirtualMachine *restrict vm, int size) {

  // Name Heap
  vm->agentHeap.heap = MakeAgentHeap(size);
  vm->agentHeap.lastAlloc = 0;
  vm->agentHeap.size = size;

  // size = size/2;
  vm->nameHeap.heap = MakeNameHeap(size);
  vm->nameHeap.lastAlloc = size - 1;
  // vm->nameHeap.lastAlloc = 0;
  vm->nameHeap.size = size;

  // Register
  vm->reg = malloc(sizeof(VALUE) * VM_REG_SIZE);
}

#endif

void VM_EQStack_Init(VirtualMachine *vm, int size) {
  vm->nextPtr_eqStack = -1;
  vm->eqStack = malloc(sizeof(EQ) * size);
  vm->eqStack_size = size;
  if (vm->eqStack == NULL) {
    fprintf(stderr, "ERROR: VM_EQStack_Init: could not allocate memory: %s\n",
            strerror(errno));
    exit(EXIT_FAILURE);
  }
}

void VM_EQStack_Push(VirtualMachine *vm, VALUE l, VALUE r) {

  vm->nextPtr_eqStack++;

  if (vm->nextPtr_eqStack >= vm->eqStack_size) {
    vm->eqStack_size += vm->eqStack_size;

    void *newStack = realloc(vm->eqStack, sizeof(EQ) * vm->eqStack_size);

    if (newStack == NULL) {
      fprintf(stderr, "ERROR: VM_EQStack_Push: could not allocate memory: %s\n",
              strerror(errno));
      free(vm->eqStack);
      exit(EXIT_FAILURE);
    }

    vm->eqStack = newStack;

#ifdef VERBOSE_EQSTACK_EXPANSION
    puts("(EQStack is expanded)");
#endif
  }
  vm->eqStack[vm->nextPtr_eqStack].l = l;
  vm->eqStack[vm->nextPtr_eqStack].r = r;

#ifdef DEBUG
  // DEBUG
  printf(" PUSH:");
  puts_term(l);
  puts("");
  puts("      ><");
  printf("      ");
  puts_term(r);
  puts("");
  //  printf("VM%d:pushed\n", vm->id);
#endif
}

int VM_EQStack_Pop(VirtualMachine *vm, VALUE *l, VALUE *r) {
  if (vm->nextPtr_eqStack < 0) {
    return 0;
  }

  *l = vm->eqStack[vm->nextPtr_eqStack].l;
  *r = vm->eqStack[vm->nextPtr_eqStack].r;
  vm->nextPtr_eqStack--;
  return 1;
}

void VMCode_puts(void **code, int n) {
  int line = 0;

  // puts("[PutsCode]");
  if (n == -1)
    n = MAX_VMCODE_SEQUENCE;

  printf("Line:Addr.\n");
  for (int i = 0; i < n; i++) {
    line++;
    printf("%04d:%04d: ", line, i);

    if (code[i] == CodeAddr[OP_MKNAME]) {
      printf("mkname reg%lu\n", (unsigned long)code[i + 1]);
      i += 1;

    } else if (code[i] == CodeAddr[OP_MKGNAME]) {
      printf("mkgname id%lu reg%lu; \"%s\"\n", (unsigned long)code[i + 1],
             (unsigned long)code[i + 2],
             IdTable_get_name((unsigned long)code[i + 1]));
      i += 2;

    } else if (code[i] == CodeAddr[OP_MKAGENT]) {
      printf("mkagent id:%lu reg%lu\n", (unsigned long)code[i + 1],
             (unsigned long)code[i + 2]);

      i += 2;

    } else if (code[i] == CodeAddr[OP_PUSH]) {
      printf("push reg%lu reg%lu\n", (unsigned long)code[i + 1],
             (unsigned long)code[i + 2]);
      i += 2;

    } else if (code[i] == CodeAddr[OP_PUSHI]) {
      printf("pushi reg%lu $%ld\n", (unsigned long)code[i + 1],
             FIX2INT((unsigned long)code[i + 2]));
      i += 2;

    } else if (code[i] == CodeAddr[OP_MYPUSH]) {
      printf("mypush reg%lu reg%lu\n", (unsigned long)code[i + 1],
             (unsigned long)code[i + 2]);
      i += 2;

    } else if (code[i] == CodeAddr[OP_RET]) {
      puts("ret");

    } else if (code[i] == CodeAddr[OP_RET_FREE_L]) {
      puts("ret_free_l");

    } else if (code[i] == CodeAddr[OP_RET_FREE_R]) {
      puts("ret_free_r");

    } else if (code[i] == CodeAddr[OP_RET_FREE_LR]) {
      puts("ret_free_lr");

    } else if (code[i] == CodeAddr[OP_LOADI]) {
      printf("loadi $%ld reg%lu\n", FIX2INT((unsigned long)code[i + 1]),
             (unsigned long)code[i + 2]);
      i += 2;

    } else if (code[i] == CodeAddr[OP_LOADP]) {
      printf("loadp reg%lu $%ld reg%lu\n", (unsigned long)code[i + 1],
             (unsigned long)code[i + 2], (unsigned long)code[i + 3]);
      i += 3;

    } else if (code[i] == CodeAddr[OP_LOADP_L]) {
      printf("loadp_l reg%lu $%ld\n", (unsigned long)code[i + 1],
             (unsigned long)code[i + 2]);
      i += 2;

    } else if (code[i] == CodeAddr[OP_LOADP_R]) {
      printf("loadp_r reg%lu $%ld\n", (unsigned long)code[i + 1],
             (unsigned long)code[i + 2]);
      i += 2;

    } else if (code[i] == CodeAddr[OP_CHID_L]) {
      printf("chgid_l id:%ld\n", (unsigned long)code[i + 1]);
      i += 1;

    } else if (code[i] == CodeAddr[OP_CHID_R]) {
      printf("chgid_r id:%ld\n", (unsigned long)code[i + 1]);
      i += 1;

    } else if (code[i] == CodeAddr[OP_LOOP]) {
      puts("loop");

    } else if (code[i] == CodeAddr[OP_LOOP_RREC]) {
      printf("loop_rrec reg%lu $%lu\n", (unsigned long)code[i + 1],
             (unsigned long)code[i + 2]);
      i += 2;

    } else if (code[i] == CodeAddr[OP_LOOP_RREC_FREE_R]) {
      printf("loop_rrec_free_r reg%lu $%lu\n", (unsigned long)code[i + 1],
             (unsigned long)code[i + 2]);
      i += 2;

    } else if (code[i] == CodeAddr[OP_LOOP_RREC1]) {
      printf("loop_rrec1 reg%lu\n", (unsigned long)code[i + 1]);
      i += 1;

    } else if (code[i] == CodeAddr[OP_LOOP_RREC1_FREE_R]) {
      printf("loop_rrec1_free_r reg%lu\n", (unsigned long)code[i + 1]);
      i += 1;

    } else if (code[i] == CodeAddr[OP_LOOP_RREC2]) {
      printf("loop_rrec2 reg%lu\n", (unsigned long)code[i + 1]);
      i += 1;

    } else if (code[i] == CodeAddr[OP_LOOP_RREC2_FREE_R]) {
      printf("loop_rrec2_free_r reg%lu\n", (unsigned long)code[i + 1]);
      i += 1;

    } else if (code[i] == CodeAddr[OP_LOAD]) {
      printf("load reg%lu reg%lu\n", (unsigned long)code[i + 1],
             (unsigned long)code[i + 2]);
      i += 2;

    } else if (code[i] == CodeAddr[OP_ADD]) {
      printf("add reg%lu reg%lu reg%lu\n", (unsigned long)code[i + 1],
             (unsigned long)code[i + 2], (unsigned long)code[i + 3]);
      i += 3;

    } else if (code[i] == CodeAddr[OP_SUB]) {
      printf("sub reg%lu reg%lu reg%lu\n", (unsigned long)code[i + 1],
             (unsigned long)code[i + 2], (unsigned long)code[i + 3]);
      i += 3;

    } else if (code[i] == CodeAddr[OP_ADDI]) {
      printf("addi reg%lu $%lu reg%lu\n", (unsigned long)code[i + 1],
             (unsigned long)code[i + 2], (unsigned long)code[i + 3]);
      i += 3;
    } else if (code[i] == CodeAddr[OP_SUBI]) {
      printf("subi reg%lu $%lu reg%lu\n", (unsigned long)code[i + 1],
             (unsigned long)code[i + 2], (unsigned long)code[i + 3]);
      i += 3;

    } else if (code[i] == CodeAddr[OP_MUL]) {
      printf("mul reg%lu reg%lu reg%lu\n", (unsigned long)code[i + 1],
             (unsigned long)code[i + 2], (unsigned long)code[i + 3]);
      i += 3;

    } else if (code[i] == CodeAddr[OP_DIV]) {
      printf("div reg%lu reg%lu reg%lu\n", (unsigned long)code[i + 1],
             (unsigned long)code[i + 2], (unsigned long)code[i + 3]);
      i += 3;

    } else if (code[i] == CodeAddr[OP_MOD]) {
      printf("mod reg%lu reg%lu reg%lu\n", (unsigned long)code[i + 1],
             (unsigned long)code[i + 2], (unsigned long)code[i + 3]);
      i += 3;

    } else if (code[i] == CodeAddr[OP_LT]) {
      printf("lt reg%lu reg%lu reg%lu\n", (unsigned long)code[i + 1],
             (unsigned long)code[i + 2], (unsigned long)code[i + 3]);
      i += 3;

    } else if (code[i] == CodeAddr[OP_EQ]) {
      printf("eq reg%lu reg%lu reg%lu\n", (unsigned long)code[i + 1],
             (unsigned long)code[i + 2], (unsigned long)code[i + 3]);
      i += 3;

    } else if (code[i] == CodeAddr[OP_EQI]) {
      printf("eqi reg%lu $%ld reg%lu\n", (unsigned long)code[i + 1],
             FIX2INT((unsigned long)code[i + 2]), (unsigned long)code[i + 3]);
      i += 3;

    } else if (code[i] == CodeAddr[OP_NE]) {
      printf("ne reg%lu reg%lu reg%lu\n", (unsigned long)code[i + 1],
             (unsigned long)code[i + 2], (unsigned long)code[i + 3]);
      i += 3;

    } else if (code[i] == CodeAddr[OP_LT_R0]) {
      printf("lt_r0 reg%lu reg%lu\n", (unsigned long)code[i + 1],
             (unsigned long)code[i + 2]);
      i += 2;

    } else if (code[i] == CodeAddr[OP_LE_R0]) {
      printf("le_r0 reg%lu reg%lu\n", (unsigned long)code[i + 1],
             (unsigned long)code[i + 2]);
      i += 2;

    } else if (code[i] == CodeAddr[OP_EQ_R0]) {
      printf("eq_r0 reg%lu reg%lu\n", (unsigned long)code[i + 1],
             (unsigned long)code[i + 2]);
      i += 2;

    } else if (code[i] == CodeAddr[OP_EQI_R0]) {
      printf("eqi_r0 reg%lu $%ld\n", (unsigned long)code[i + 1],
             FIX2INT((unsigned long)code[i + 2]));
      i += 2;

    } else if (code[i] == CodeAddr[OP_NE_R0]) {
      printf("ne_r0 reg%lu reg%lu\n", (unsigned long)code[i + 1],
             (unsigned long)code[i + 2]);
      i += 2;

    } else if (code[i] == CodeAddr[OP_JMPNEQ0]) {
      printf("jmpneq0 reg%lu $%lu\n", (unsigned long)code[i + 1],
             (unsigned long)code[i + 2]);
      i += 2;

    } else if (code[i] == CodeAddr[OP_JMPEQ0]) {
      printf("jmpeq0 reg%lu $%lu\n", (unsigned long)code[i + 1],
             (unsigned long)code[i + 2]);
      i += 2;

    } else if (code[i] == CodeAddr[OP_JMPEQ0_R0]) {
      printf("jmpeq0_r0 $%lu\n", (unsigned long)code[i + 1]);
      i += 1;

    } else if (code[i] == CodeAddr[OP_JMPCNCT_CONS]) {
      printf("jmpcnct_cons reg%lu $%lu\n", (unsigned long)code[i + 1],
             (unsigned long)code[i + 2]);
      i += 2;

    } else if (code[i] == CodeAddr[OP_JMPCNCT]) {
      printf("jmpcnct reg%lu id%lu $%lu\n", (unsigned long)code[i + 1],
             (unsigned long)code[i + 2], (unsigned long)code[i + 3]);
      i += 3;

    } else if (code[i] == CodeAddr[OP_JMP]) {
      printf("jmp $%lu\n", (unsigned long)code[i + 1]);
      i += 1;

    } else if (code[i] == CodeAddr[OP_UNM]) {
#if !defined(OPTIMISE_TWO_ADDRESS) || !defined(OPTIMISE_TWO_ADDRESS_UNARY)
      printf("unm reg%lu reg%lu\n", (unsigned long)code[i + 1],
             (unsigned long)code[i + 2]);
      i += 2;
#else
      printf("unm reg%lu\n", (unsigned long)code[i + 1]);
      i += 1;
#endif

    } else if (code[i] == CodeAddr[OP_INC]) {
#if !defined(OPTIMISE_TWO_ADDRESS) || !defined(OPTIMISE_TWO_ADDRESS_UNARY)
      printf("inc reg%lu reg%lu\n", (unsigned long)code[i + 1],
             (unsigned long)code[i + 2]);
      i += 2;
#else
      printf("inc reg%lu\n", (unsigned long)code[i + 1]);
      i += 1;
#endif

    } else if (code[i] == CodeAddr[OP_DEC]) {
#if !defined(OPTIMISE_TWO_ADDRESS) || !defined(OPTIMISE_TWO_ADDRESS_UNARY)
      printf("dec reg%lu reg%lu\n", (unsigned long)code[i + 1],
             (unsigned long)code[i + 2]);
      i += 2;
#else
      printf("dec reg%lu\n", (unsigned long)code[i + 1]);
      i += 1;
#endif

    } else if (code[i] == CodeAddr[OP_RAND]) {
#if !defined(OPTIMISE_TWO_ADDRESS) || !defined(OPTIMISE_TWO_ADDRESS_UNARY)
      printf("rnd reg%lu reg%lu\n", (unsigned long)code[i + 1],
             (unsigned long)code[i + 2]);
      i += 2;
#else
      printf("rnd reg%lu\n", (unsigned long)code[i + 1]);
      i += 1;
#endif

    } else if (code[i] == CodeAddr[OP_CNCTGN]) {
      printf("cnctgn reg%lu reg%lu\n", (unsigned long)code[i + 1],
             (unsigned long)code[i + 2]);
      i += 2;

    } else if (code[i] == CodeAddr[OP_SUBSTGN]) {
      printf("substgn reg%lu reg%lu\n", (unsigned long)code[i + 1],
             (unsigned long)code[i + 2]);
      i += 2;

    } else if (code[i] == CodeAddr[OP_NOP]) {
      puts("nop");

    } else {
      printf("code %lu\n", (unsigned long)code[i]);
    }
  }
}

#ifdef COUNT_INTERACTION
// ------------------------------------------------------
//  Count for Interaction operation
// ------------------------------------------------------

unsigned long VM_Get_InteractionCount(VirtualMachine *vm) {
  return vm->count_interaction;
}

void VM_Clear_InteractionCount(VirtualMachine *vm) {
  vm->count_interaction = 0;
}
#endif