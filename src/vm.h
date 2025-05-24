#ifndef INPLA_VM_H
#define INPLA_VM_H

#include "heap.h"
#include "mytype.h"
#include "name_table.h"

#define VM_REG_SIZE 64

#define MAX_VMCODE_SEQUENCE 1024

// reg0 is used to store comparison results
// so, others have to be used from reg1
/*
#define VM_OFFSET_R0            (0)
#define VM_OFFSET_METAVAR_L(a)  (a)
#define VM_OFFSET_METAVAR_R(a)  (MAX_PORT+(a))
#define VM_OFFSET_ANNOTATE_L    (MAX_PORT*2)
#define VM_OFFSET_ANNOTATE_R    (MAX_PORT*2+1)
*/

#define VM_OFFSET_R0           (0)
#define VM_OFFSET_METAVAR_L(a) (1 + a)
#define VM_OFFSET_METAVAR_R(a) (1 + MAX_PORT + (a))
#define VM_OFFSET_ANNOTATE_L   (1 + MAX_PORT * 2)
#define VM_OFFSET_ANNOTATE_R   (1 + MAX_PORT * 2 + 1)
#define VM_OFFSET_LOCALVAR     (VM_OFFSET_ANNOTATE_R + 1)

typedef struct {
  // Heaps for agents and names
  Heap agentHeap, nameHeap;

  // EQStack
  EQ *eqStack;
  int nextPtr_eqStack;
  int eqStack_size;

#ifdef COUNT_INTERACTION
  unsigned long count_interaction;
#endif

  // register
  //  VALUE reg[VM_REG_SIZE+(MAX_PORT*2 + 2)];
  //  VALUE reg[VM_REG_SIZE];
  VALUE *reg;

#ifdef THREAD
  unsigned int id;
#endif

} VirtualMachine;

// -----------------------------------------------------------------
// BYTECODE and Compilation
// -----------------------------------------------------------------
// *** The occurrence order must be the same in labels in ExecCode. ***
typedef enum {
  OP_PUSH = 0,
  OP_PUSHI,
  OP_MYPUSH,

  OP_MKNAME,
  OP_MKGNAME,

  OP_MKAGENT,

  OP_RET,
  OP_RET_FREE_LR,
  OP_RET_FREE_L,
  OP_RET_FREE_R,

  OP_LOADI,
  OP_LOAD,
  OP_LOADP,
  OP_LOADP_L,
  OP_LOADP_R,
  OP_CHID_L,
  OP_CHID_R,

  OP_ADD,
  OP_SUB,
  OP_ADDI,
  OP_SUBI,
  OP_MUL,
  OP_DIV,
  OP_MOD,
  OP_LT,
  OP_LE,
  OP_EQ,
  OP_EQI,
  OP_NE,
  OP_UNM,
  OP_RAND,
  OP_INC,
  OP_DEC,

  OP_LT_R0,
  OP_LE_R0,
  OP_EQ_R0,
  OP_EQI_R0,
  OP_NE_R0,

  OP_JMPEQ0,
  OP_JMPEQ0_R0,
  OP_JMP,
  OP_JMPNEQ0,

  OP_JMPCNCT_CONS,
  OP_JMPCNCT,
  OP_LOOP,
  OP_LOOP_RREC,
  OP_LOOP_RREC1,
  OP_LOOP_RREC2,
  OP_LOOP_RREC_FREE_R,
  OP_LOOP_RREC1_FREE_R,
  OP_LOOP_RREC2_FREE_R,

  // Connection operation for global names of given nets in the interactive
  // mode.
  OP_CNCTGN,
  OP_SUBSTGN,

  // This corresponds to the last code in CodeAddr.
  // So ones after this are not used for execution by virtual machines.
  OP_NOP,

  // These are used for translation from intermediate codes to Bytecodes
  OP_LABEL,
  OP_DEAD_CODE,
  OP_BEGIN_BLOCK,
  OP_BEGIN_JMPCNCT_BLOCK,
  OP_LOAD_META,   // keep the `dest' in `OP_LOAD_META src dest' as it is
  OP_LOADI_SHARED // The same register will be assigned for the `dest`.
} Code;

#ifdef COUNT_INTERACTION
#  define COUNTUP_INTERACTION(vm) vm->count_interaction++
#else
#  define COUNTUP_INTERACTION(vm)
#endif

#ifdef COUNT_MKAGENT
unsigned int NumberOfMkAgent;
#endif

#ifndef THREAD
extern VirtualMachine VM;
#endif

// -----------------------------------------------------
// Mark and Sweep for error recovery
// -----------------------------------------------------
#ifndef THREAD
void sweep_AgentHeap(Heap *hp);
void sweep_NameHeap(Heap *hp);
void mark_and_sweep(void);
#endif

#if defined(EXPANDABLE_HEAP) || defined(FLEX_EXPANDABLE_HEAP)
void VM_Buffer_Init(VirtualMachine *restrict vm);
#else
void VM_InitBuffer(VirtualMachine *restrict vm, int size);
#endif

void VM_EQStack_Init(VirtualMachine *restrict vm, int size);
void VM_EQStack_Push(VirtualMachine *restrict vm, VALUE l, VALUE r);
int  VM_EQStack_Pop(VirtualMachine *restrict vm, VALUE *l, VALUE *r);
void VMCode_puts(void **code, int n);

#ifdef COUNT_INTERACTION
unsigned long VM_Get_InteractionCount(VirtualMachine *vm);
void          VM_Clear_InteractionCount(VirtualMachine *vm);
#endif

#if defined(EXPANDABLE_HEAP) || defined(FLEX_EXPANDABLE_HEAP)
inline void VM_Init(VirtualMachine *restrict vm, unsigned int eqStackSize) {
  VM_Buffer_Init(vm);
  VM_EQStack_Init(vm, eqStackSize);
}
#else
inline void VM_Init(VirtualMachine *restrict vm, unsigned int agentBufferSize,
                    unsigned int eqStackSize) {
  VM_InitBuffer(vm, agentBufferSize);
  VM_EQStack_Init(vm, eqStackSize);
}
#endif

#endif // INPLA_VM_H
