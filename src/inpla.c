#include "inpla.h"

#include "timer.h"

#include "ast.h"
#include "cmenv.h"
#include "heap.h"
#include "id_table.h"
#include "imcode.h"
#include "name_table.h"
#include "opt.h"
#include "ruletable.h"
#include "types.h"
#include "vm.h"

#include "config.h"

#include <assert.h>
#include <errno.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "linenoise/linenoise.h"

// ----------------------------------------------

#define VERSION    "0.13.0-2"
#define BUILT_DATE "12 September 2024"

// For global options  ---------------------------------

#ifndef THREAD
typedef struct {
  int verbose_memory_use; // default is 0 (NOT enable)
} GlobalOptions_t;

static GlobalOptions_t GlobalOptions = {
    .verbose_memory_use = 0,
};
#endif

// For threads  ---------------------------------

#if !defined(EXPANDABLE_HEAP) && !defined(FLEX_EXPANDABLE_HEAP)
// The fixed heap requires it to devide heaps by threads.
static int MaxThreadsNum = 1;
#endif

#ifdef THREAD

#  if defined(EXPANDABLE_HEAP) || defined(FLEX_EXPANDABLE_HEAP)
static int MaxThreadsNum = 1;
#  endif

#  include <pthread.h>
extern int pthread_setconcurrency(int concurrency);

static int SleepingThreadsNum = 0;

// for cas spinlock
#  include "cas_spinlock.h"

#endif

// ----------------------------------------------

#include "parser.tab.h"

extern FILE *yyin;
extern int   yylineno;
extern void  pushFP(FILE *fp);
extern int   popFP();

// ----------------------------------------------

#ifndef THREAD
#  define MYPUSH(vm, a1, a2)                                                   \
    if ((!IS_FIXNUM(a1)) && (IS_NAMEID(BASIC(a1)->id)) &&                      \
        (NAME(a1)->port == (VALUE)NULL)) {                                     \
      NAME(a1)->port = a2;                                                     \
    } else if ((!IS_FIXNUM(a2)) && (IS_LOCAL_NAMEID(BASIC(a2)->id)) &&         \
               (NAME(a2)->port == (VALUE)NULL)) {                              \
      NAME(a2)->port = a1;                                                     \
    } else {                                                                   \
      VM_EQStack_Push(vm, a1, a2);                                             \
    }
#else
#  define MYPUSH(vm, a1, a2)                                                   \
    if ((!IS_FIXNUM(a1)) && (IS_NAMEID(BASIC(a1)->id)) &&                      \
        (NAME(a1)->port == (VALUE)NULL)) {                                     \
      if (!(__sync_bool_compare_and_swap(&(NAME(a1)->port), NULL, a2))) {      \
        VM_EQStack_Push(vm, a1, a2);                                           \
      }                                                                        \
    } else if ((!IS_FIXNUM(a2)) && (IS_NAMEID(BASIC(a2)->id)) &&               \
               (NAME(a2)->port == (VALUE)NULL)) {                              \
      if (!(__sync_bool_compare_and_swap(&(NAME(a2)->port), NULL, a1))) {      \
        VM_EQStack_Push(vm, a1, a2);                                           \
      }                                                                        \
    } else {                                                                   \
      VM_EQStack_Push(vm, a1, a2);                                             \
    }
#endif

/*
  ===TODO===
  2021/9/18
  a2->t であるところに a1->a2 が与えられても a2->t, a1->a2 のままにしてある。
  a2 がここで解放できればメモリ利用が助かるのでは？
  仮に助からないとしても、グローバル環境で与えられたネットの場合には
  単純に eval_equation へ持ち込んだ方が、indirection を生成しなくて済むのでは？

  ==>
  DONE 21 September 2021
  誤差程度しか変わらない。

 */
#ifndef THREAD
#  define PUSH(vm, a1, a2)                                                     \
    if ((!IS_FIXNUM(a1)) && (IS_NAMEID(BASIC(a1)->id)) &&                      \
        (NAME(a1)->port == (VALUE)NULL)) {                                     \
      NAME(a1)->port = a2;                                                     \
    } else if ((!IS_FIXNUM(a2)) && (IS_NAMEID(BASIC(a2)->id)) &&               \
               (NAME(a2)->port == (VALUE)NULL)) {                              \
      NAME(a2)->port = a1;                                                     \
    } else {                                                                   \
      VM_EQStack_Push(vm, a1, a2);                                             \
    }
#else
#  define PUSH(vm, a1, a2)                                                     \
    if ((!IS_FIXNUM(a1)) && (IS_NAMEID(BASIC(a1)->id)) &&                      \
        (NAME(a1)->port == (VALUE)NULL)) {                                     \
      if (!(__sync_bool_compare_and_swap(&(NAME(a1)->port), NULL, a2))) {      \
        if (SleepingThreadsNum == 0) {                                         \
          VM_EQStack_Push(vm, NAME(a1)->port, a2);                             \
          free_Name(a1);                                                       \
        } else {                                                               \
          GlobalEQStack_Push(NAME(a1)->port, a2);                              \
          free_Name(a1);                                                       \
        }                                                                      \
      }                                                                        \
    } else if ((!IS_FIXNUM(a2)) && (IS_NAMEID(BASIC(a2)->id)) &&               \
               (NAME(a2)->port == (VALUE)NULL)) {                              \
      if (!(__sync_bool_compare_and_swap(&(NAME(a2)->port), NULL, a1))) {      \
        if (SleepingThreadsNum == 0) {                                         \
          VM_EQStack_Push(vm, a1, NAME(a2)->port);                             \
          free_Name(a2);                                                       \
        } else {                                                               \
          GlobalEQStack_Push(a1, NAME(a2)->port);                              \
          free_Name(a2);                                                       \
        }                                                                      \
      }                                                                        \
    } else {                                                                   \
      if (SleepingThreadsNum == 0) {                                           \
        VM_EQStack_Push(vm, a1, a2);                                           \
      } else {                                                                 \
        GlobalEQStack_Push(a1, a2);                                            \
      }                                                                        \
    }
#endif

void puts_memory_stat(void) {
#ifndef THREAD
  print_memory_usage(&VM.agentHeap, &VM.nameHeap);
#else
  puts("Not supported in the multi-threaded version.");
#endif
}

// -----------------------------------------------------
// The global equation stack
// -----------------------------------------------------

#ifdef THREAD
static pthread_cond_t  EQStack_not_empty = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t Sleep_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t AllSleep_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

// GlobalEQStack for execution with threads
#ifdef THREAD
typedef struct {
  EQ          *stack;
  int          nextPtr;
  int          size;
  volatile int lock; // for lightweight spin lock
} EQStack;
static EQStack GlobalEQS;
#endif

#ifdef THREAD
void GlobalEQStack_Init(int size) {
  GlobalEQS.nextPtr = -1;
  GlobalEQS.stack = malloc(sizeof(EQ) * size);
  GlobalEQS.size = size;
  if (GlobalEQS.stack == NULL) {
    printf("Malloc error\n");
    exit(-1);
  }
  // for cas_lock
  GlobalEQS.lock = 0;
}
#endif

#ifdef THREAD
void GlobalEQStack_Push(VALUE l, VALUE r) {

  lock(&GlobalEQS.lock);

  GlobalEQS.nextPtr++;
  if (GlobalEQS.nextPtr >= GlobalEQS.size) {
    GlobalEQS.size += GlobalEQS.size;
    GlobalEQS.stack = realloc(GlobalEQS.stack, sizeof(EQ) * GlobalEQS.size);

#  ifdef VERBOSE_EQSTACK_EXPANSION
    puts("(Global EQStack is expanded)");
#  endif
  }

  GlobalEQS.stack[GlobalEQS.nextPtr].l = l;
  GlobalEQS.stack[GlobalEQS.nextPtr].r = r;

  unlock(&GlobalEQS.lock);

  if (SleepingThreadsNum > 0) {
    pthread_mutex_lock(&Sleep_lock);
    pthread_cond_signal(&EQStack_not_empty);
    pthread_mutex_unlock(&Sleep_lock);
  }
}
#endif

int EQStack_Pop(VirtualMachine *vm, VALUE *l, VALUE *r) {

  if (vm->nextPtr_eqStack >= 0) {
    *l = vm->eqStack[vm->nextPtr_eqStack].l;
    *r = vm->eqStack[vm->nextPtr_eqStack].r;
    vm->nextPtr_eqStack--;

    /*
#ifdef DEBUG
    puts("");
    puts("====================================");
    puts("POP");
    puts("====================================");
    puts_term(*l);
    puts("");
    puts("><");
    puts_term(*r);
    puts("");
    puts("====================================");
#endif
    */

    return 1;
  }
#ifndef THREAD
  return 0;
#else

  lock(&GlobalEQS.lock);

  if (GlobalEQS.nextPtr < 0) {
    // When GlobalEQStack is empty
    unlock(&GlobalEQS.lock);
    return 0;
  }

  *l = GlobalEQS.stack[GlobalEQS.nextPtr].l;
  *r = GlobalEQS.stack[GlobalEQS.nextPtr].r;
  GlobalEQS.nextPtr--;

  unlock(&GlobalEQS.lock);
  return 1;

#endif
}

#ifdef DEBUG
void VM_EQStack_allputs(VirtualMachine *vm) {
  int i;
  if (vm->nextPtr_eqStack == -1)
    return;
  for (i = 0; i <= vm->nextPtr_eqStack + 1; i++) {
    printf("%02d: ", i);
    puts_term(vm->eqStack[i].l);
    puts("");
    printf("    ");
    puts_term(vm->eqStack[i].r);
    puts("");
  }
}
#endif

// The real addresses for the `Code':
void *CodeAddr[OP_NOP + 1];

void CodeAddr_init(void) {
  // Set CodeAddr
  void **table;
  table = exec_code(0, NULL, NULL);
  for (int i = 0; i < OP_NOP; i++) {
    CodeAddr[i] = table[i];
  }
}

void print_memory_usage(Heap *agent_heap, Heap *name_heap) {
  fprintf(stderr, "Using %lu agent nodes and %lu name nodes.\n\n",
          Heap_GetNum_Usage_forAgent(agent_heap),
          Heap_GetNum_Usage_forName(name_heap));
}

//-----------------------------------------------------------
// Pretty printing for terms
//-----------------------------------------------------------

static VALUE ShowNameHeap =
    (VALUE)NULL; // showname で表示するときの cyclic 防止
// showname 時に、呼び出し変数の heap num を入れておく。
// showname 呼び出し以外は NULL に。

void puts_name(VALUE ptr) {

  if (ptr == (VALUE)NULL) {
    printf("[NULL]");
    return;
  }

  if (IS_GNAMEID(BASIC(ptr)->id)) {
    printf("%s", IdTable_get_name(BASIC(ptr)->id));
  } else if (IS_LOCAL_NAMEID(BASIC(ptr)->id)) {
    printf("<var%lu>", (unsigned long)ptr);
  } else {
    puts_term(ptr);
  }
}

#define PRETTY_VAR
#ifdef PRETTY_VAR
typedef struct PrettyList_tag {
  VALUE                  id;
  char                  *name;
  struct PrettyList_tag *next;
} PrettyList;

typedef struct {
  PrettyList *list;
  int         alphabet;
  int         index;
  char        namebuf[10];
} PrettyStruct;

PrettyStruct Pretty;

#  define MAX_PRETTY_ALPHABET 26
void Pretty_init(void) {
  Pretty.alphabet = -1;
  Pretty.list = NULL;
  Pretty.index = 1;
}

PrettyList *PrettyList_new(void) {
  PrettyList *alist;
  alist = malloc(sizeof(PrettyList));
  if (alist == NULL) {
    printf("[PrettyList] Malloc error\n");
    exit(-1);
  }
  return alist;
}

char *Pretty_newName(void) {
  Pretty.alphabet++;
  if (Pretty.alphabet >= MAX_PRETTY_ALPHABET) {
    Pretty.alphabet = 0;
    Pretty.index++;
  }
  sprintf(Pretty.namebuf, "%c%d", 'a' + Pretty.alphabet, Pretty.index);
  return Pretty.namebuf;
}

PrettyList *PrettyList_recordName(VALUE a) {
  PrettyList *alist;
  alist = PrettyList_new();
  alist->id = a;
  alist->name = strdup(Pretty_newName());
  alist->next = Pretty.list;
  Pretty.list = alist;
  return alist;
}

char *Pretty_Name(VALUE a) {
  PrettyList *alist;
  if (Pretty.list == NULL) {
    alist = PrettyList_recordName(a);
    Pretty.list = alist;
    return alist->name;
  } else {
    PrettyList *at = Pretty.list;
    while (at != NULL) {
      if (at->id == a) {
        return at->name;
      }
      at = at->next;
    }

    alist = PrettyList_recordName(a);
    Pretty.list = alist;
    return alist->name;
  }
}
#endif

// #define PUTS_ELEMENTS_NUM 12
#define PUTS_ELEMENTS_NUM 30
static int Puts_list_element = 0;

static int PutIndirection = 1; // indirected term t for x is put as x->t
void       puts_term(VALUE ptr) {
  if (IS_FIXNUM(ptr)) {
    printf("%ld", FIX2INT(ptr));
    return;
  } else if (BASIC(ptr) == NULL) {
    printf("<NULL>");
    return;
  }

  if (IS_NAMEID(BASIC(ptr)->id)) {
    if (NAME(ptr)->port == (VALUE)NULL) {
      if (IS_GNAMEID(BASIC(ptr)->id)) {
        printf("%s", IdTable_get_name(BASIC(ptr)->id));

      } else {
#ifndef PRETTY_VAR
        printf("<var%lu>", (unsigned long)(ptr));
#else
        printf("<%s>", Pretty_Name(ptr));
#endif
      }
    } else {
      if (ptr == ShowNameHeap) {
        printf("<Warning:%s is cyclic>", IdTable_get_name(BASIC(ptr)->id));
        return;
      }

      if (PutIndirection && IdTable_get_name(BASIC(ptr)->id) != NULL) {
        printf("%s", IdTable_get_name(BASIC(ptr)->id));
      } else {
        puts_term(NAME(ptr)->port);
      }
    }

  } else if (IS_TUPLEID(BASIC(ptr)->id)) {
    int i, arity;
    arity = GET_TUPLEARITY(BASIC(ptr)->id);
    printf("(");
    for (i = 0; i < arity; i++) {
      puts_term(AGENT(ptr)->port[i]);
      if (i != arity - 1) {
        printf(",");
      }
    }
    printf(")");

  } else if (BASIC(ptr)->id == ID_NIL) {
    printf("[]");

  } else if (BASIC(ptr)->id == ID_CONS) {
    printf("[");

    while (ptr != (VALUE)NULL) {
      puts_term(AGENT(ptr)->port[0]);

      ptr = AGENT(ptr)->port[1];
      while (IS_NAMEID(BASIC(ptr)->id) && NAME(ptr)->port != (VALUE)NULL) {
        ptr = NAME(ptr)->port;
      }
      if (BASIC(ptr)->id == ID_NIL) {
        printf("]");
        Puts_list_element = 0;
        break;
      }

      if (IS_NAMEID(BASIC(ptr)->id) && NAME(ptr)->port == (VALUE)NULL) {
        // for WHNF
        printf(",");
        puts_term(ptr);
        printf("...");
        break;
      }

      if (!(IS_FIXNUM(ptr)) && BASIC(ptr)->id != ID_CONS) {
        printf(":");
        puts_term(ptr);
        break;
      }

      printf(",");

      Puts_list_element++;
      if (Puts_list_element > PUTS_ELEMENTS_NUM) {
        printf("...]");
        Puts_list_element = 0;
        break;
      }
    }

  } else if (BASIC(ptr)->id == ID_PERCENT) {
    printf("%%");
    printf("%s", IdTable_get_name(FIX2INT(AGENT(ptr)->port[0])));

  } else if (BASIC(ptr)->id == ID_WILDCARD) {
    printf("Wildcard");

  } else {
    // Agent
    int   i, arity;
    int   with_parenthses = 1;
    char *agent_name;

    arity = IdTable_get_arity(AGENT(ptr)->basic.id);
    agent_name = IdTable_get_name(AGENT(ptr)->basic.id);
    printf("%s", agent_name);

    if (agent_name[0] >= 'A' && agent_name[0] <= 'Z' && arity == 0) {
      // If the first letter of agent_name is not capital and the arity is 0
      // then parentheses is not needed.
      with_parenthses = 0;
    }

    if (with_parenthses) {
      printf("(");
    }

    for (i = 0; i < arity; i++) {
      puts_term(AGENT(ptr)->port[i]);
      if (i != arity - 1) {
        printf(",");
      }
    }

    if (with_parenthses) {
      printf(")");
    }
  }
}

// void puts_Name_port0_nat(VALUE a1) {
void puts_Name_port0_nat(char *sym) {
  int result = 0;
  int idS, idZ;

  int sym_id = NameTable_get_id(sym);
  if (!IS_GNAMEID(sym_id)) {
    printf("<NOT-DEFINED>\n");
    fflush(stdout);
    return;
  }

  VALUE a1 = IdTable_get_heap(sym_id);

  idS = NameTable_get_set_id_with_IdTable_forAgent("S");
  idZ = NameTable_get_set_id_with_IdTable_forAgent("Z");

  if (a1 == (VALUE)NULL) {
    printf("<NUll>");
  } else if (!IS_NAMEID(BASIC(a1)->id)) {
    printf("<NON-NAME>");
  } else {
    if (NAME(a1)->port == (VALUE)NULL) {
      printf("<EMPTY>");
    } else {

      a1 = NAME(a1)->port;
      while (BASIC(a1)->id != idZ) {
        if (BASIC(a1)->id == idS) {
          result++;
          a1 = AGENT(a1)->port[0];
        } else if (IS_NAMEID(BASIC(a1)->id)) {
          a1 = NAME(a1)->port;
        } else {
          puts("ERROR: it is not unary number.");
          exit(-1);
        }
      }
      printf("%d\n", result);
      fflush(stdout);
    }
  }
}

void puts_eqlist(EQList *at) {
  while (at != NULL) {
    puts_term(at->eq.l);
    printf("~");
    puts_term(at->eq.r);
    printf(",");
    at = at->next;
  }
}

// ------------------------------------------------------------
//  VIRTUAL MACHINE
// ------------------------------------------------------------

// static inline
VALUE make_Agent(VirtualMachine *restrict vm, int id) {
  VALUE ptr;
  ptr = myalloc_Agent(&vm->agentHeap);

#ifdef COUNT_MKAGENT
  NumberOfMkAgent++;
#endif

  AGENT(ptr)->basic.id = id;
  return ptr;
}

// static inline
VALUE make_Name(VirtualMachine *restrict vm) {
  VALUE ptr;

  ptr = myalloc_Name(&vm->nameHeap);
  SET_LOCAL_NAMEID(AGENT(ptr)->basic.id);
  NAME(ptr)->port = (VALUE)NULL;

  return ptr;
}

// ------------------------------------------------------------
// Evaluation of equations
// ------------------------------------------------------------

// It seems better WITHOUT `static inline'
// static inline
void eval_equation(VirtualMachine *restrict vm, VALUE a1, VALUE a2) {

loop:

  // a2 is fixnum
  if (IS_FIXNUM(a2)) {
  loop_a2IsFixnum:
    if (IS_FIXNUM(a1)) {

      printf("Runtime ERROR: ");
      puts_term(a1);
      printf("~");
      puts_term(a2);
      printf("\nInteger %ld >< %ld can not be used as an active pair\n",
             FIX2INT(a1), FIX2INT(a2));
#ifndef THREAD
      mark_and_sweep();
      return;
#else
      printf("Retrieve is not supported in the multi-threaded version.\n");
      exit(-1);
#endif
    }

    // a1 is an agent
    if (IS_AGENTID(BASIC(a1)->id)) {

      void **code = NULL;

      RuleTable_get_code_for_Int(a1, &code);

      if (code == NULL) {
        // built-in: BUILT-IN_OP >< int

        switch (BASIC(a1)->id) {
        case ID_ADD: {
          COUNTUP_INTERACTION(vm);

          BASIC(a1)->id = ID_ADD2;
          VALUE a1port1 = AGENT(a1)->port[1];
          AGENT(a1)->port[1] = a2;
          a2 = a1port1;
          goto loop;
        }
        case ID_ADD2: {
          COUNTUP_INTERACTION(vm);

          // r << Add(m,n)
          long n = FIX2INT(AGENT(a1)->port[1]);
          long m = FIX2INT(a2);
          a2 = INT2FIX(m + n);
          VALUE a1port0 = AGENT(a1)->port[0];
          free_Agent(a1);
          a1 = a1port0;
          goto loop;
        }
        case ID_SUB: {
          COUNTUP_INTERACTION(vm);

          BASIC(a1)->id = ID_SUB2;
          VALUE a1port1 = AGENT(a1)->port[1];
          AGENT(a1)->port[1] = a2;
          a2 = a1port1;
          goto loop;
        }
        case ID_SUB2: {
          COUNTUP_INTERACTION(vm);

          // r << Sub(m,n)
          long n = FIX2INT(AGENT(a1)->port[1]);
          long m = FIX2INT(a2);
          a2 = INT2FIX(m - n);
          VALUE a1port0 = AGENT(a1)->port[0];
          free_Agent(a1);
          a1 = a1port0;
          goto loop;
        }
        case ID_MUL: {
          COUNTUP_INTERACTION(vm);

          BASIC(a1)->id = ID_MUL2;
          VALUE a1port1 = AGENT(a1)->port[1];
          AGENT(a1)->port[1] = a2;
          a2 = a1port1;
          goto loop;
        }
        case ID_MUL2: {
          COUNTUP_INTERACTION(vm);

          // r << Mult(m,n)
          long n = FIX2INT(AGENT(a1)->port[1]);
          long m = FIX2INT(a2);
          a2 = INT2FIX(m * n);
          VALUE a1port0 = AGENT(a1)->port[0];
          free_Agent(a1);
          a1 = a1port0;
          goto loop;
        }
        case ID_DIV: {
          COUNTUP_INTERACTION(vm);

          BASIC(a1)->id = ID_DIV2;
          VALUE a1port1 = AGENT(a1)->port[1];
          AGENT(a1)->port[1] = a2;
          a2 = a1port1;
          goto loop;
        }
        case ID_DIV2: {
          COUNTUP_INTERACTION(vm);

          // r << DIV(m,n)
          long n = FIX2INT(AGENT(a1)->port[1]);
          long m = FIX2INT(a2);
          a2 = INT2FIX(m / n);
          VALUE a1port0 = AGENT(a1)->port[0];
          free_Agent(a1);
          a1 = a1port0;
          goto loop;
        }
        case ID_MOD: {
          COUNTUP_INTERACTION(vm);

          BASIC(a1)->id = ID_MOD2;
          VALUE a1port1 = AGENT(a1)->port[1];
          AGENT(a1)->port[1] = a2;
          a2 = a1port1;
          goto loop;
        }
        case ID_MOD2: {
          COUNTUP_INTERACTION(vm);

          // r << MOD(m,n)
          long n = FIX2INT(AGENT(a1)->port[1]);
          long m = FIX2INT(a2);
          a2 = INT2FIX(m % n);
          VALUE a1port0 = AGENT(a1)->port[0];
          free_Agent(a1);
          a1 = a1port0;
          goto loop;
        }
        case ID_ERASER: {
          COUNTUP_INTERACTION(vm);

          // Eps ~ (int n)
          free_Agent(a1);
          return;
        }

        case ID_DUP: {
          COUNTUP_INTERACTION(vm);

          // Dup(x,y) ~ (int n) --> x~n, y~n;
          VALUE a1port = AGENT(a1)->port[0];
          PUSH(vm, a1port, a2);

          a1port = AGENT(a1)->port[1];
          free_Agent(a1);
          a1 = a1port;
          goto loop;
        }
        }

        printf("Runtime Error: There is no interaction rule for the following "
               "pair:\n  ");
        puts_term(a1);
        printf("~");
        puts_term(a2);
        puts("");

        if (yyin != stdin)
          exit(-1);

#ifndef THREAD
        mark_and_sweep();
        return;
#else
        printf("Retrieve is not supported in the multi-threaded version.\n");
        exit(-1);
#endif
      }

      /* JIT experimentation
      if (BASIC(a1)->id == 28) {
        vm->reg[VM_OFFSET_METAVAR_L(0)] = AGENT(a1)->port[0];

        vm->reg[VM_OFFSET_ANNOTATE_L] = a1;
        vm->reg[VM_OFFSET_ANNOTATE_R] = a2;

        COUNTUP_INTERACTION(vm);
        exec_code_fib(vm);

        return;
      }
      */

      int           i;
      unsigned long arity;
      arity = (unsigned long)code[0];

      switch (arity) {
      case 0:
        break;

      case 1:
        vm->reg[VM_OFFSET_METAVAR_L(0)] = AGENT(a1)->port[0];
        break;

      case 2:
        vm->reg[VM_OFFSET_METAVAR_L(0)] = AGENT(a1)->port[0];
        vm->reg[VM_OFFSET_METAVAR_L(1)] = AGENT(a1)->port[1];
        break;

      case 3:
        vm->reg[VM_OFFSET_METAVAR_L(0)] = AGENT(a1)->port[0];
        vm->reg[VM_OFFSET_METAVAR_L(1)] = AGENT(a1)->port[1];
        vm->reg[VM_OFFSET_METAVAR_L(2)] = AGENT(a1)->port[2];
        break;

      default:
        for (i = 0; i < arity; i++) {
          vm->reg[VM_OFFSET_METAVAR_L(i)] = AGENT(a1)->port[i];
        }
      }

      vm->reg[VM_OFFSET_ANNOTATE_L] = a1;
      vm->reg[VM_OFFSET_ANNOTATE_R] = a2;

      COUNTUP_INTERACTION(vm);

      exec_code(1, vm, &code[2]);
      return;

    } else {
      // a1 is name, a2 is Fixint
      if (NAME(a1)->port != (VALUE)NULL) {
        VALUE a1p0;
        a1p0 = NAME(a1)->port;
        free_Name(a1);
        a1 = a1p0;
        goto loop_a2IsFixnum;
      } else {
#ifndef THREAD
        NAME(a1)->port = a2;
#else
        if (!(__sync_bool_compare_and_swap(&(NAME(a1)->port), NULL, a2))) {
          VALUE a1p0;
          a1p0 = NAME(a1)->port;
          free_Name(a1);
          a1 = a1p0;
          goto loop_a2IsFixnum;
        }
#endif
      }
    }
    return;
  }

  // a2 is agent
  if (IS_AGENTID(BASIC(a2)->id)) {
  loop_a2IsAgent:

    // a1 is Fixnum
    if (IS_FIXNUM(a1)) {
      // Fixnum >< agent
      VALUE tmp = a1;
      a1 = a2;
      a2 = tmp;
      goto loop_a2IsFixnum;
    }

    // a1 is agent
    if (IS_AGENTID(BASIC(a1)->id)) {
      /* for the case of  Agent - Agent  */

#ifdef DEBUG
      puts("");
      puts("--------------------------------------");
      puts("execActive");
      puts("--------------------------------------");
      puts_term(a1);
      puts("");
      printf("><");
      puts("");
      puts_term(a2);
      puts("");
      puts("--------------------------------------");
      puts("");
#endif

      /*
      if (BASIC(a1)->id < BASIC(a2)->id) {
        VALUE tmp;
        tmp=a1;
        a1=a2;
        a2=tmp;
      }
      */

      int    result;
      void **code;

      code = RuleTable_get_code(BASIC(a1)->id, BASIC(a2)->id, &result);

      if (result == 0) {
        // there is no user-defined rule.

      loop_agent_a1_a2_this_order:

        // suppose that a2 is a constructor (So, ID(a1) >= ID(a2))
        switch (BASIC(a1)->id) {

        case ID_ERASER: {
          // Eps ~ Alpha(a1,...,a5)
          COUNTUP_INTERACTION(vm);

          int arity = IdTable_get_arity(BASIC(a2)->id);
          switch (arity) {
          case 0: {
            free_Agent2(a1, a2);
            return;
          }

          case 1: {
            VALUE a2p0 = AGENT(a2)->port[0];
            free_Agent(a2);
            a2 = a2p0;
            goto loop;
          }

          default:
            for (int i = 1; i < arity; i++) {
              VALUE a2port = AGENT(a2)->port[i];
              VALUE eps = make_Agent(vm, ID_ERASER);
              PUSH(vm, eps, a2port);
            }

            VALUE a2p0 = AGENT(a2)->port[0];
            free_Agent(a2);
            a2 = a2p0;
            goto loop;
          }
        } break;

        case ID_DUP: {
          // Dup(p1,p2) ~ Alpha(b1,...,b5)
          COUNTUP_INTERACTION(vm);

          if (BASIC(a2)->id == ID_DUP) {
            // Dup(p1,p2) >< Dup(b1,b2) => p1~b1, b2~b2;
            VALUE a1p = AGENT(a1)->port[0];
            VALUE a2p = AGENT(a2)->port[0];
            PUSH(vm, a1p, a2p);

            a1p = AGENT(a1)->port[1];
            a2p = AGENT(a2)->port[1];

            free_Agent2(a1, a2);
            a1 = a1p;
            a2 = a2p;
            goto loop;
          }

          int arity = IdTable_get_arity(BASIC(a2)->id);
          switch (arity) {
          case 0: {
            // Dup(p0,p1) >< A => p0~A, p1~A;
            VALUE a1p = AGENT(a1)->port[0];
            VALUE new_a2 = make_Agent(vm, BASIC(a2)->id);
            PUSH(vm, a1p, new_a2);

            a1p = AGENT(a1)->port[1];
            free_Agent(a1);
            a1 = a1p;
            goto loop;
          }

            // a2p0 などが fixint の場合は即座に複製させる
          case 1: {
            // Dup(p0,p1) >< A(a2p0) => p1~A(w2), p0~A(w1),
            //                          Dup(w1,w2)~a2p0;
            // Dup(p0,p1) >< A(int a2p0) => p1~A(a2p0), p0~A(a2p0);

            VALUE a2p0 = AGENT(a2)->port[0];

            int a2id = BASIC(a2)->id;

            // p1
            VALUE new_a2 = make_Agent(vm, a2id);

            if (IS_FIXNUM(a2p0)) {
              AGENT(new_a2)->port[0] = a2p0;
              PUSH(vm, AGENT(a1)->port[1], new_a2);
              AGENT(a2)->port[0] = a2p0;

              VALUE a1p0 = AGENT(a1)->port[0];
              free_Agent(a1);

              // PUSH(vm, a1p0, a2);
              a1 = a1p0;
              goto loop;

            } else {
              VALUE w = make_Name(vm);
              AGENT(new_a2)->port[0] = w;
              PUSH(vm, AGENT(a1)->port[1], new_a2);
              AGENT(a1)->port[1] = w; // for (*L)dup

              w = make_Name(vm);
              AGENT(a2)->port[0] = w;
              PUSH(vm, AGENT(a1)->port[0], a2);

              AGENT(a1)->port[0] = w; // for (*L)dup

              a2 = a2p0;
              goto loop;
            }

          } break;

          case 2: {
            // Here we only manage the following rule,
            // and the other will be done at the next case:
            //   Dup(p0,p1) >< Cons(int i, a2p1) =>
            //     p0~Cons(i,w), p1~(*R)Cons(i,ww), (*L)Dup(w,ww)~a2p1;

            VALUE a2p0 = AGENT(a2)->port[0];
            if (IS_FIXNUM(a2p0)) {
              int   a2id = BASIC(a2)->id;
              VALUE new_a2 = make_Agent(vm, a2id);

              AGENT(new_a2)->port[0] = a2p0;

              VALUE w = make_Name(vm);
              AGENT(new_a2)->port[1] = w;

              PUSH(vm, AGENT(a1)->port[0], new_a2);
              AGENT(a1)->port[0] = w;

              VALUE a2p1 = AGENT(a2)->port[1];
              VALUE ww = make_Name(vm);
              AGENT(a2)->port[1] = ww;

              PUSH(vm, AGENT(a1)->port[1], a2);
              AGENT(a1)->port[1] = ww;

              a2 = a2p1;
              goto loop;
            }

            // otherwise: goto default
            // So, DO NOT put break.
          }

          default: {
            // Dup(p0,p1) >< A(a2p0, a2p1) =>
            //      p0~A(w0,w1),        (*L)Dup(w0,ww0)~a2p0
            //      p1~(*R)A(ww0,ww1),      Dup(w1,ww1)~a2p1,

            // Dup(p0,p1) >< A(a2p0, a2p1, a2p2) =>
            //      p0~A(w0,w1,w2),        (*L)Dup(w0,ww0)~a2p0
            //      p1~(*R)A(ww0,ww1,ww2),     Dup(w1,ww1)~a2p1,
            //                                 Dup(w2,ww2)~a2p2;

            // Dup(p0,p1) >< A(a2p0, a2p1, a2p2 a2p3) =>
            //      p0~A(w0,w1,w2,w3),         (*L)Dup(w0,ww0)~a2p0
            //      p1~(*R)A(ww0,ww1,ww2,ww3),     Dup(w1,ww1)~a2p1,
            //                                     Dup(w2,ww2)~a2p2,
            //                                     Dup(w3,ww3)~a2p3;

            // Dup(p0,p1) >< A(a2p0, int a2p1, a2p2 a2p3) =>
            //      p0~A(w0,a2p1,w2,w3),         (*L)Dup(w0,ww0)~a2p0
            //      p1~(*R)A(ww0,a2p1,ww2,ww3),
            //                                     Dup(w2,ww2)~a2p2,
            //                                     Dup(w3,ww3)~a2p3;

            // newA = mkAgent(A);
            // for (i=0; i<arity; i++) {
            //   if (!IS_FIXNUM(a2->p[i])) {
            //      newA->p[i] = w_i;
            //   } else {
            //      newA->p[i] = a2->p[i];
            //   }
            // }
            // for (i=1; i<arity; i++) {
            //   if (!IS_FIXNUM(a2->p[i])) {
            //     newDup( newA->p[i], newWW_i) ~ a2[i]
            //     a2[i] = newWW_i;
            //   }
            // }
            // if (!IS_FIXNUM(a2->p[0])) {
            //   p0_preserve = p0;
            //   p1_preserve = p1;
            //   (*L)Dup(w0, newWW0) ~ a2[0] // this destroys the p0 and p1.
            //
            //   p0_preserve ~ newA
            //   a2[0] = newWW0;
            //
            //   p1_preserve ~ a2; <-- This will be `goto loop'.
            // } else {
            //   p0 ~ newA;
            //   p1_preserve = p1;
            //   free_Agent((*L)Dup);
            //   p1_preserve ~ a2; >-- This will be `goto loop'.
            // }

            int   a2id = BASIC(a2)->id;
            VALUE new_a2 = make_Agent(vm, a2id);
            for (int i = 0; i < arity; i++) {
              VALUE a2pi = AGENT(a2)->port[i];
              if (!IS_FIXNUM(a2pi)) {
                AGENT(new_a2)->port[i] = make_Name(vm);
              } else {
                AGENT(new_a2)->port[i] = a2pi;
              }
            }

            for (int i = 1; i < arity; i++) {
              VALUE a2pi = AGENT(a2)->port[i];

              if (!IS_FIXNUM(a2pi)) {

                VALUE new_dup = make_Agent(vm, ID_DUP);
                AGENT(new_dup)->port[0] = AGENT(new_a2)->port[i];

                VALUE new_ww = make_Name(vm);
                AGENT(new_dup)->port[1] = new_ww;

                PUSH(vm, new_dup, a2pi);

                AGENT(a2)->port[i] = new_ww;
              }
            }

            VALUE a2p0 = AGENT(a2)->port[0];
            if (!IS_FIXNUM(a2p0)) {

              VALUE a1p0 = AGENT(a1)->port[0];
              VALUE a1p1 = AGENT(a1)->port[1];

              AGENT(a1)->port[0] = AGENT(new_a2)->port[0];
              VALUE new_ww = make_Name(vm);
              AGENT(a1)->port[1] = new_ww;
              PUSH(vm, a1, a2p0);

              PUSH(vm, a1p0, new_a2);

              AGENT(a2)->port[0] = new_ww;

              a1 = a1p1;

              goto loop;
            } else {
              PUSH(vm, AGENT(a1)->port[0], new_a2);
              VALUE a1p1 = AGENT(a1)->port[1];
              free_Agent(a1);

              a1 = a1p1;
              goto loop;
            }
          }
          }
        } break;

        case ID_TUPLE0:
          if (BASIC(a2)->id == ID_TUPLE0) {
            // [] ~ [] --> nothing
            COUNTUP_INTERACTION(vm);

            //	      free_Agent(a1);
            //	      free_Agent(a2);
            free_Agent2(a1, a2);
            return;
          }

          break; // end ID_TUPLE0

        case ID_TUPLE2:
          if (BASIC(a2)->id == ID_TUPLE2) {
            // (x1,x2) ~ (y1,y2) --> x1~y1, x2~y2
            COUNTUP_INTERACTION(vm);

            VALUE a1p1 = AGENT(a1)->port[1];
            VALUE a2p1 = AGENT(a2)->port[1];
            PUSH(vm, a1p1, a2p1);

            //	      free_Agent(a1);
            //	      free_Agent(a2);
            free_Agent2(a1, a2);
            a1 = AGENT(a1)->port[0];
            a2 = AGENT(a2)->port[0];
            goto loop;
          }

          break; // end ID_TUPLE2

        case ID_TUPLE3:
          if (BASIC(a2)->id == ID_TUPLE3) {
            // (x1,x2,x3) ~ (y1,y2,y3) --> x1~y1, x2~y2, x3~y3
            COUNTUP_INTERACTION(vm);

            PUSH(vm, AGENT(a1)->port[2], AGENT(a2)->port[2]);
            PUSH(vm, AGENT(a1)->port[1], AGENT(a2)->port[1]);

            //	      free_Agent(a1);
            //	      free_Agent(a2);
            free_Agent2(a1, a2);
            a1 = AGENT(a1)->port[0];
            a2 = AGENT(a2)->port[0];
            goto loop;
          }

          break; // end ID_TUPLE2

        case ID_TUPLE4:
          if (BASIC(a2)->id == ID_TUPLE4) {
            // (x1,x2,x3) ~ (y1,y2,y3) --> x1~y1, x2~y2, x3~y3
            COUNTUP_INTERACTION(vm);

            PUSH(vm, AGENT(a1)->port[3], AGENT(a2)->port[3]);
            PUSH(vm, AGENT(a1)->port[2], AGENT(a2)->port[2]);
            PUSH(vm, AGENT(a1)->port[1], AGENT(a2)->port[1]);

            //	      free_Agent(a1);
            //	      free_Agent(a2);
            free_Agent2(a1, a2);
            a1 = AGENT(a1)->port[0];
            a2 = AGENT(a2)->port[0];
            goto loop;
          }

          break; // end ID_TUPLE2

        case ID_TUPLE5:
          if (BASIC(a2)->id == ID_TUPLE5) {
            // (x1,x2,x3) ~ (y1,y2,y3) --> x1~y1, x2~y2, x3~y3
            COUNTUP_INTERACTION(vm);

            PUSH(vm, AGENT(a1)->port[4], AGENT(a2)->port[4]);
            PUSH(vm, AGENT(a1)->port[3], AGENT(a2)->port[3]);
            PUSH(vm, AGENT(a1)->port[2], AGENT(a2)->port[2]);
            PUSH(vm, AGENT(a1)->port[1], AGENT(a2)->port[1]);

            //	      free_Agent(a1);
            //	      free_Agent(a2);
            free_Agent2(a1, a2);
            a1 = AGENT(a1)->port[0];
            a2 = AGENT(a2)->port[0];
            goto loop;
          }

          break; // end ID_TUPLE2

        case ID_NIL:
          if (BASIC(a2)->id == ID_NIL) {
            // [] ~ [] --> nothing
            COUNTUP_INTERACTION(vm);

            //	      free_Agent(a1);
            //	      free_Agent(a2);
            free_Agent2(a1, a2);

            return;
          }

          break; // end ID_NIL

        case ID_CONS:
          if (BASIC(a2)->id == ID_CONS) {
            // a:b ~ c:d --> a~c, b~d
            COUNTUP_INTERACTION(vm);

            VALUE a1p1 = AGENT(a1)->port[1];
            VALUE a2p1 = AGENT(a2)->port[1];
            PUSH(vm, a1p1, a2p1);

            //	      free_Agent(a1);
            //	      free_Agent(a2);
            free_Agent2(a1, a2);
            a1 = AGENT(a1)->port[0];
            a2 = AGENT(a2)->port[0];
            goto loop;
          }

          break; // end ID_CONS

          // built-in funcAgent for lists and tuples
        case ID_APPEND:
          switch (BASIC(a2)->id) {
          case ID_NIL: {
            // App(r,a) >< [] => r~a;
            COUNTUP_INTERACTION(vm);

            VALUE a1p0 = AGENT(a1)->port[0];
            VALUE a1p1 = AGENT(a1)->port[1];
            //		free_Agent(a1);
            //		free_Agent(a2);
            free_Agent2(a1, a2);
            a1 = a1p0;
            a2 = a1p1;
            goto loop;
          }

          case ID_CONS: {
            // App(r,a) >< x:xs => r~(*R)x:w, (*L)App(w,a)~xs;
            COUNTUP_INTERACTION(vm);

            VALUE a1p0 = AGENT(a1)->port[0];
            VALUE a2p1 = AGENT(a2)->port[1];
            VALUE w = make_Name(vm);

            AGENT(a2)->port[1] = w;
            PUSH(vm, a1p0, a2);
            // VM_EQStack_Push(vm, a1p1, a2);

            AGENT(a1)->port[0] = w;
            a2 = a2p1;
            goto loop;
          }
          }

          break; // end ID_APPEND

        case ID_ZIP:
          switch (BASIC(a2)->id) {
          case ID_NIL: {
            // Zip(r, blist) >< [] => r~[], blist~Eraser;
            COUNTUP_INTERACTION(vm);

            VALUE a1p0 = AGENT(a1)->port[0];
            VALUE a1p1 = AGENT(a1)->port[1];

            // Eraser
            BASIC(a1)->id = ID_ERASER;
            PUSH(vm, a1p1, a1);

            a1 = a1p0;
            goto loop;
          }

          case ID_CONS: {
            // Zip(r, blist) >< x:xs => Zip_Cons(r,x:xs)~blist;
            COUNTUP_INTERACTION(vm);

            VALUE a1p1 = AGENT(a1)->port[1];

            BASIC(a1)->id = ID_ZIPC;
            AGENT(a1)->port[1] = a2;
            a2 = a1p1;
            goto loop;
          }
          }

          break; // end ID_ZIP

        case ID_ZIPC:
          switch (BASIC(a2)->id) {
          case ID_NIL: {
            // Zip_Cons(r, x:xs)><[] => r~[], x:xs~Eraser;
            COUNTUP_INTERACTION(vm);

            VALUE a1p0 = AGENT(a1)->port[0];
            VALUE a1p1 = AGENT(a1)->port[1];

            // Eraser
            BASIC(a1)->id = ID_ERASER;
            PUSH(vm, a1p1, a1);

            a1 = a1p0;
            goto loop;
          }

          case ID_CONS: {
            // Zip_Cons(r, (*1)x:xs) >< y:ys =>
            //    r~(*R)((*1)(y,x):ws), (*L)Zip(ws,ys)~xs;
            COUNTUP_INTERACTION(vm);

            VALUE r = AGENT(a1)->port[0];
            VALUE inner_cons = AGENT(a1)->port[1];
            VALUE xs = AGENT(inner_cons)->port[1];
            VALUE ys = AGENT(a2)->port[1];

            // (*1)(y,x)
            BASIC(inner_cons)->id = ID_TUPLE2;
            VALUE x = AGENT(inner_cons)->port[0];
            VALUE y = AGENT(a2)->port[0];
            AGENT(inner_cons)->port[0] = y;
            AGENT(inner_cons)->port[1] = x;

            // (*R)((*1)(y,x):ws)
            VALUE ws = make_Name(vm);
            AGENT(a2)->port[0] = inner_cons;
            AGENT(a2)->port[1] = ws;

            PUSH(vm, r, a2);

            // (*L)Zip(ws,ys)~xs;
            BASIC(a1)->id = ID_ZIP;
            AGENT(a1)->port[0] = ws;
            AGENT(a1)->port[1] = ys;
            a2 = xs;
            goto loop;
          }
          }

          break; // end ID_ZIPC

        case ID_MAP:
          switch (BASIC(a2)->id) {
          case ID_NIL: {
            // Map(result, f) >< []   => result~[], Eraser~f;
            COUNTUP_INTERACTION(vm);

            VALUE a1p0 = AGENT(a1)->port[0];
            VALUE a1p1 = AGENT(a1)->port[1];

            // Eraser
            BASIC(a1)->id = ID_ERASER;
            PUSH(vm, a1, a1p1);

            a1 = a1p0;
            goto loop;
          }

          case ID_CONS: {
            COUNTUP_INTERACTION(vm);

            VALUE a1p0 = AGENT(a1)->port[0];
            VALUE a1p1 = AGENT(a1)->port[1];

            VALUE a2p0 = AGENT(a2)->port[0];
            VALUE a2p1 = AGENT(a2)->port[1];
            VALUE w = make_Name(vm);
            VALUE ws = make_Name(vm);

            VALUE pair = make_Agent(vm, ID_TUPLE2);

            AGENT(a2)->port[0] = w;
            AGENT(a2)->port[1] = ws;
            PUSH(vm, a1p0, a2);

            /*
            if ((IS_NAMEID(BASIC(a1p1)->id))
                && (NAME(a1p1)->port != NULL)) {
                  VALUE a1p1_agent = NAME(a1p1)->port;
                  free_Name(a1p1);
                  AGENT(a1)->port[1] = a1p1_agent;
            }
            */

            if (BASIC(a1p1)->id == ID_PERCENT) {
              // special case
              // Map(result, %f) >< x:xs =>
              //		  result~w:ws,
              //		  %f ~ (w, x), Map(ws, %f)~xs;

              AGENT(pair)->port[0] = w;
              AGENT(pair)->port[1] = a2p0;
              VALUE new_percent = make_Agent(vm, ID_PERCENT);
              AGENT(new_percent)->port[0] = AGENT(a1p1)->port[0];
              PUSH(vm, new_percent, pair);

              AGENT(a1)->port[0] = ws;
              a2 = a2p1;
              goto loop;
            }

            //     Map(result, f) >< x:xs => Dup(f1,f2)~f,
            //                            result~w:ws,
            //                            f1 ~ (w, x), map(ws, f2)~xs;

            VALUE dup = make_Agent(vm, ID_DUP);
            VALUE f1 = make_Name(vm);
            VALUE f2 = make_Name(vm);
            AGENT(dup)->port[0] = f1;
            AGENT(dup)->port[1] = f2;
            PUSH(vm, dup, a1p1);

            AGENT(pair)->port[0] = w;
            AGENT(pair)->port[1] = a2p0;
            PUSH(vm, f1, pair);

            AGENT(a1)->port[0] = ws;
            AGENT(a1)->port[1] = f2;
            a2 = a2p1;
            goto loop;
          }
          }

          break; // end ID_MAP

        case ID_MERGER:
          switch (BASIC(a2)->id) {
          case ID_TUPLE2: {
            // MG(r) ~ (a|b) => *MGp(*r)~a, *MGp(*r)~b
            COUNTUP_INTERACTION(vm);

            BASIC(a1)->id = ID_MERGER_P;
            AGENT(a1)->port[1] = (VALUE)NULL;
            AGENT(a1)->port[2] = (VALUE)NULL;
            PUSH(vm, a1, AGENT(a2)->port[1]);
            PUSH(vm, a1, AGENT(a2)->port[0]);
            free_Agent(a2);
            return;
          }
          }
          break; // end ID_MG

        case ID_MERGER_P:
          switch (BASIC(a2)->id) {
          case ID_NIL:
            // *MGP(r)~[]
#ifndef THREAD

            COUNTUP_INTERACTION(vm);

            if (AGENT(a1)->port[1] == (VALUE)NULL) {
              AGENT(a1)->port[1] = a2;
              return;
            } else {
              VALUE a1p0 = AGENT(a1)->port[0];
              //		free_Agent(AGENT(a1)->port[1]);
              //		free_Agent(a1);
              free_Agent2(AGENT(a1)->port[1], a1);

              a1 = a1p0;
              goto loop;
            }
#else
            // AGENT(a1)->port[2] is used as a lock for NIL case
            if (AGENT(a1)->port[2] == (VALUE)NULL) {
              if (!(__sync_bool_compare_and_swap(&(AGENT(a1)->port[2]), NULL,
                                                 a2))) {
                // something exists already
                goto loop;

              } else {
                return;
              }
            } else if ((AGENT(a1)->port[2] != (VALUE)NULL) &&
                       (BASIC(AGENT(a1)->port[2])->id == ID_NIL)) {

              COUNTUP_INTERACTION(vm);

              VALUE a1p0 = AGENT(a1)->port[0];
              //		free_Agent(AGENT(a1)->port[2]);
              //		free_Agent(a1);
              free_Agent2(AGENT(a1)->port[2], a1);
              a1 = a1p0;
              goto loop;
            } else {
              goto loop;
            }

#endif
          case ID_CONS:
            // *MGP(r)~x:xs => r~x:w, *MGP(w)~xs;
            {
#ifndef THREAD

              COUNTUP_INTERACTION(vm);

              VALUE a1p0 = AGENT(a1)->port[0];
              VALUE w = make_Name(vm);
              AGENT(a1)->port[0] = w;
              PUSH(vm, a1, AGENT(a2)->port[1]);

              AGENT(a2)->port[1] = w;
              a1 = a1p0;
              goto loop;
#else
              // AGENT(a1)->port[1] is used as a lock for CONS case
              // AGENT(a1)->port[2] is used as a lock for NIL case

              if (AGENT(a1)->port[2] != (VALUE)NULL) {
                // The other MGP finished still, so:
                // *MGP(r)~x:xs => r~x:xs;

                COUNTUP_INTERACTION(vm);

                VALUE a1p0 = AGENT(a1)->port[0];
                //		  free_Agent(AGENT(a1)->port[2]);   // free the
                // lock for NIL 		  free_Agent(a1);
                free_Agent2(AGENT(a1)->port[2], a1);
                a1 = a1p0;
                goto loop;

              } else if (AGENT(a1)->port[1] == (VALUE)NULL) {
                if (!(__sync_bool_compare_and_swap(&(AGENT(a1)->port[1]), NULL,
                                                   a2))) {
                  // Failure to be locked.
                  goto loop;
                }

                // Succeed the lock
                COUNTUP_INTERACTION(vm);

                VALUE a1p0 = AGENT(a1)->port[0];
                VALUE w = make_Name(vm);
                AGENT(a1)->port[0] = w;
                PUSH(vm, a1, AGENT(a2)->port[1]);

                AGENT(a2)->port[1] = w;

                AGENT(a1)->port[1] = (VALUE)NULL; // free the lock

                a1 = a1p0;

                goto loop;

              } else {
                // MPG works for the other now.

                goto loop;
              }

#endif
            }
          }
          break; // end ID_MERGER_P

        case ID_PERCENT:

          if (BASIC(a2)->id == ID_TUPLE2) {

            // %foo >< @(args, s)
            int percented_id = FIX2INT(AGENT(a1)->port[0]);
            int arity = IdTable_get_arity(percented_id);

            if (arity < 1) {
              printf("Rumtime error: `%s' has no arity.\n",
                     IdTable_get_name(percented_id));
              puts_term(a1);
              printf("~");
              puts_term(a2);
              puts("");

              if (yyin != stdin)
                exit(-1);
#ifndef THREAD
              mark_and_sweep();
              return;
#else
              printf(
                  "Retrieve is not supported in the multi-threaded version.\n");
              exit(-1);
#endif
            }

            COUNTUP_INTERACTION(vm);

            switch (arity) {
            case 1: {
              BASIC(a2)->id = percented_id;
              free_Agent(a1);
              a1 = a2;
              a2 = AGENT(a2)->port[1];
              goto loop;
            }

            default: {
              VALUE a2p0 = AGENT(a2)->port[0];
              VALUE a2p0_id = BASIC(a2p0)->id;
              if (IS_AGENTID(a2p0_id) && IS_TUPLEID(a2p0_id) &&
                  GET_TUPLEARITY(a2p0_id) == arity) {
                //    %foo2 ~ ((p1,p2),q) --> foo2(p1,p2)~q
                BASIC(a2p0)->id = percented_id;
                free_Agent(a1);
                VALUE preserved_q = AGENT(a2)->port[1];
                free_Agent(a2);

                a1 = a2p0;
                a2 = preserved_q;
                goto loop;

              } else {
                //    %foo2 ~ (p,q) --> foo2(p1,p2)~q, (p1,p2)~p
                VALUE preserved_p = AGENT(a2)->port[0];
                VALUE preserved_q = AGENT(a2)->port[1];
                VALUE tuple = a2;
                BASIC(a1)->id = percented_id;
                for (int i = 0; i < arity; i++) {
                  VALUE new_name = make_Name(vm);
                  AGENT(a1)->port[i] = new_name;
                  AGENT(tuple)->port[i] = new_name;
                }
                PUSH(vm, tuple, preserved_p);

                a2 = preserved_q;
                goto loop;
              }
            }
            }
          }
          break; // end ID_PERCENT

        } // end switch(BASIC(a1)->id)

        // Wildcard Agents
        code = RuleTable_get_code(BASIC(a1)->id, ID_WILDCARD, &result);
        if (result != 0) {
          //	  VMCode_puts(&code[0], 10);
          goto label_exec_agent_a1_a2;
        }

        if (BASIC(a1)->id < BASIC(a2)->id || BASIC(a2)->id == ID_ERASER ||
            BASIC(a2)->id == ID_DUP) {
          VALUE tmp;
          tmp = a1;
          a1 = a2;
          a2 = tmp;
          goto loop_agent_a1_a2_this_order;
        }

        printf("Runtime Error: There is no interaction rule for the following "
               "pair:\n");
        puts_term(a1);
        printf("~");
        puts_term(a2);
        puts("");

        //	printf("a1.id = %d, a2.id=%d\n", BASIC(a1)->id, BASIC(a2)->id);

        if (yyin != stdin)
          exit(-1);

#ifndef THREAD
        mark_and_sweep();
        return;
#else
        printf("Retrieve is not supported in the multi-threaded version.\n");
        exit(-1);
#endif
      }
      // normal op

      //      PutsCode(at);
      //      return;

    label_exec_agent_a1_a2:;
      int           i;
      unsigned long arity;
      arity = (unsigned long)code[0];

      switch (arity) {
      case 0:
        break;

      case 1:
        vm->reg[VM_OFFSET_METAVAR_L(0)] = AGENT(a1)->port[0];
        break;

      case 2:
        vm->reg[VM_OFFSET_METAVAR_L(0)] = AGENT(a1)->port[0];
        vm->reg[VM_OFFSET_METAVAR_L(1)] = AGENT(a1)->port[1];
        break;

      case 3:
        vm->reg[VM_OFFSET_METAVAR_L(0)] = AGENT(a1)->port[0];
        vm->reg[VM_OFFSET_METAVAR_L(1)] = AGENT(a1)->port[1];
        vm->reg[VM_OFFSET_METAVAR_L(2)] = AGENT(a1)->port[2];
        break;

      default:
        for (i = 0; i < arity; i++) {
          vm->reg[VM_OFFSET_METAVAR_L(i)] = AGENT(a1)->port[i];
        }
      }

      arity = (unsigned long)code[1];
      switch (arity) {
      case 0:
        break;

      case 1:
        vm->reg[VM_OFFSET_METAVAR_R(0)] = AGENT(a2)->port[0];
        break;

      case 2:
        vm->reg[VM_OFFSET_METAVAR_R(0)] = AGENT(a2)->port[0];
        vm->reg[VM_OFFSET_METAVAR_R(1)] = AGENT(a2)->port[1];
        break;

      case 3:
        vm->reg[VM_OFFSET_METAVAR_R(0)] = AGENT(a2)->port[0];
        vm->reg[VM_OFFSET_METAVAR_R(1)] = AGENT(a2)->port[1];
        vm->reg[VM_OFFSET_METAVAR_R(2)] = AGENT(a2)->port[2];
        break;

      default:
        for (i = 0; i < arity; i++) {
          vm->reg[VM_OFFSET_METAVAR_R(i)] = AGENT(a2)->port[i];
        }
      }

      vm->reg[VM_OFFSET_ANNOTATE_L] = a1;
      vm->reg[VM_OFFSET_ANNOTATE_R] = a2;

      // free_Agent(a1);
      // free_Agent(a2);

      //      vm->L = a1;
      //      vm->R = a2;

      COUNTUP_INTERACTION(vm);

      exec_code(1, vm, &code[2]);

      return;
    } else {

      // a1 is name
      // a2 is agent
      if (NAME(a1)->port != (VALUE)NULL) {

        VALUE a1p0;
        a1p0 = NAME(a1)->port;
        free_Name(a1);
        a1 = a1p0;
        goto loop_a2IsAgent;
      } else {
#ifndef THREAD
        NAME(a1)->port = a2;
#else
        if (!(__sync_bool_compare_and_swap(&(NAME(a1)->port), NULL, a2))) {
          VALUE a1p0;
          a1p0 = NAME(a1)->port;
          free_Name(a1);
          a1 = a1p0;
          goto loop_a2IsAgent;
        }
#endif
      }
    }
  } else {
    // a2 is name, a1 is unknown.

    if (NAME(a2)->port != (VALUE)NULL) {
      VALUE a2p0;
      a2p0 = NAME(a2)->port;
      free_Name(a2);
      a2 = a2p0;
      goto loop;
    } else {
#ifndef THREAD
      NAME(a2)->port = a1;
#else
      if (!(__sync_bool_compare_and_swap(&(NAME(a2)->port), NULL, a1))) {
        VALUE a2p0;
        a2p0 = NAME(a2)->port;
        free_Name(a2);
        a2 = a2p0;
        goto loop;
      }
#endif
    }
  }
}

#ifndef THREAD

#  define WHNF_UNUSED_STACK_SIZE 100
typedef struct {
  // EQStack
  EQ *eqs;
  int eqs_index;
  int size;
  int enable; // 0: not Enable, 1: Enable.
} WHNF_Info;
WHNF_Info WHNFinfo;

void Init_WHNFinfo(void) {
  WHNFinfo.eqs_index = 0;
  WHNFinfo.size = WHNF_UNUSED_STACK_SIZE;
  WHNFinfo.enable = 0; // not Enable

  WHNFinfo.eqs = malloc(sizeof(EQ) * WHNF_UNUSED_STACK_SIZE);
  if (WHNFinfo.eqs == NULL) {
    printf("WHNFinfo.eqs: Malloc error\n");
    exit(-1);
  }
}

void WHNFInfo_push_equation(VALUE t1, VALUE t2) {
  WHNFinfo.eqs[WHNFinfo.eqs_index].l = t1;
  WHNFinfo.eqs[WHNFinfo.eqs_index].r = t2;
  WHNFinfo.eqs_index++;
  if (WHNFinfo.eqs_index >= WHNF_UNUSED_STACK_SIZE) {
    printf("ERROR: WHNFinfo.eqs stack becomes full.\n");
    exit(-1);
  }
}

void WHNF_execution_loop(void) {
  VALUE t1, t2;

  while (EQStack_Pop(&VM, &t1, &t2)) {

    puts_term(t1);
    printf("~");
    puts_term(t2);
    puts("");

    if (NameTable_check_if_term_has_gname(t1) == 1 ||
        NameTable_check_if_term_has_gname(t2) == 1) {

      eval_equation(&VM, t1, t2);

    } else {

      WHNFInfo_push_equation(t1, t2);
    }
  }
}

int exec(Ast *at) {
  // Ast at: (AST_BODY stmlist aplist)

  unsigned long long t, time;
  void              *code[MAX_VMCODE_SEQUENCE];

  start_timer(&t);

  CmEnv_clear_all();

  // for `where' expression
  if (!Compile_stmlist_on_ast(at->left))
    return 0;

  // aplist
  at = at->right;

  /*
  //   for aplists
            puts(""); ast_puts(at); puts("");
            Ast_RewriteOptimisation_eqlist(at);
            puts(""); ast_puts(at); puts("");
                    exit(1);
  */

  // Ast_RewriteOptimisation_eqlist(at);

  // Syntax error check
  {
    Ast *tmp_at = at;

    if (Ast_eqs_has_agentID(tmp_at, AST_ANNOTATION_L)) {
      puts("Error: Given nets contain `(*L)'.");
      // invalid case
      if (yyin != stdin)
        exit(-1);
      return 0;
    }
    if (Ast_eqs_has_agentID(tmp_at, AST_ANNOTATION_R)) {
      puts("Error: Given nets contain `(*R)'.");
      // invalid case
      if (yyin != stdin)
        exit(-1);
      return 0;
    }

    while (tmp_at != NULL) {

      if (!check_ast_arity(tmp_at->left->left) ||
          !check_ast_arity(tmp_at->left->right) ||
          !check_invalid_occurrence(tmp_at->left->left) ||
          !check_invalid_occurrence(tmp_at->left->right)) {

        // invalid case
        if (yyin != stdin)
          exit(-1);
        return 0;
      }

      tmp_at = ast_getTail(tmp_at);
    }
  }

  // Reset the counter of compilation errors
  CmEnv.count_compilation_errors = 0;

  while (at != NULL) {
    int  p1, p2;
    Ast *left, *right;

    left = at->left->left;
    right = at->left->right;
    p1 = Compile_term_on_ast(left, -1);
    p2 = Compile_term_on_ast(right, -1);

    // Check whether compilation errors arise
    if (CmEnv.count_compilation_errors != 0) {
      return 0;
    }

    if (left->id == AST_NAME) {
      select_kind_of_push(left, p1, p2);

    } else if (right->id == AST_NAME) {
      select_kind_of_push(right, p2, p1);

    } else {
      IMCode_genCode2(OP_PUSH, p1, p2);
    }

    at = ast_getTail(at);
  }
  IMCode_genCode0(OP_RET);

  // checking whether names occur more than twice
  if (!CmEnv_check_name_reference_times()) {
    if (yyin != stdin)
      exit(-1);
    return 0;
  }

#  ifndef DEBUG_NETS
  // for regular operation
  CmEnv_retrieve_MKGNAME();
  CmEnv_generate_VMCode(code);

#  else
  // for debug
  CmEnv_retrieve_MKGNAME();
  IMCode_puts(0); // exit(1);

  int codenum = CmEnv_generate_VMCode(code);
  VMCode_puts(code, codenum - 2); // exit(1);
  // end for debug
#  endif

#  ifdef COUNT_MKAGENT
  NumberOfMkAgent = 0;
#  endif

  // WHNF: Unused equations are stacked to be execution targets again.
  if (WHNFinfo.enable) {
    for (int i = 0; i < WHNFinfo.eqs_index; i++) {
      MYPUSH(&VM, WHNFinfo.eqs[i].l, WHNFinfo.eqs[i].r);
    }
    WHNFinfo.eqs_index = 0;
  }

  exec_code(1, &VM, code);

#  ifdef COUNT_INTERACTION
  VM_Clear_InteractionCount(&VM);
#  endif

  // EXECUTION LOOP

  if (!WHNFinfo.enable) {
    // no-stategy execution

    VALUE t1, t2;
    while (EQStack_Pop(&VM, &t1, &t2)) {
      eval_equation(&VM, t1, t2);
    }

  } else {
    // WHNF stragety
    WHNF_execution_loop();
  }

  time = stop_timer(&t);
#  ifdef COUNT_INTERACTION
  printf("(%lu interactions, %.2f sec)\n", VM_Get_InteractionCount(&VM),
         (double)time / 1000000);
#  else
  printf("(%.2f sec)\n", (double)(time) / 1000000);
#  endif

#  ifdef COUNT_MKAGENT
  printf("(%d mkAgent calls)\n", NumberOfMkAgent);
#  endif

  if (GlobalOptions.verbose_memory_use) {
    print_memory_usage(&VM.agentHeap, &VM.nameHeap);
  }

#  ifdef COUNT_CNCT
  printf("JMP_CNCT:%d true:%d ratio:%.2f%%\n", Count_cnct, Count_cnct_true,
         Count_cnct_true * 100.0 / Count_cnct);
  printf("  ind_op:%d ratio(ind/JMP_CNCT):%.2f%%\n", Count_cnct_indirect_op,
         Count_cnct_indirect_op * 100.0 / Count_cnct);
#  endif

  return 1;
}

#else

// For setting the number of CPUs.
// it is calculated by using sysconf(_SC_NPROSSEORS_CONF) in tpool_init
static int CpuNum = 1;

static pthread_cond_t   ActiveThread_all_sleep = PTHREAD_COND_INITIALIZER;
static pthread_t       *Threads;
static VirtualMachine **VMs;

void *tpool_thread(void *arg) {

  VirtualMachine *vm;

  vm = (VirtualMachine *)arg;

#  ifdef CPU_ZERO
  cpu_set_t mask;
  CPU_ZERO(&mask);
  CPU_SET((vm->id) % CpuNum, &mask);
  if (sched_setaffinity(0, sizeof(mask), &mask) == -1) {
    printf("WARNING:");
    printf("Thread%d works on Core%d/%d\n", vm->id, (vm->id) % CpuNum,
           CpuNum - 1);
  }
  //  printf("Thread%d works on Core%d/%d\n", vm->id, (vm->id)%CpuNum,
  //  CpuNum-1);
#  endif

  while (1) {

    VALUE t1, t2;
    while (!EQStack_Pop(vm, &t1, &t2)) {

      // Not sure, but it works well. Perhaps it can reduce race condition.
      usleep(CAS_LOCK_USLEEP);
      usleep(CAS_LOCK_USLEEP);
      usleep(CAS_LOCK_USLEEP);

      pthread_mutex_lock(&Sleep_lock);
      SleepingThreadsNum++;

      if (SleepingThreadsNum == MaxThreadsNum) {
        pthread_mutex_lock(&AllSleep_lock);
        pthread_cond_signal(&ActiveThread_all_sleep);
        pthread_mutex_unlock(&AllSleep_lock);
      }

      //            printf("[Thread %d is slept.]\n", vm->id);
      pthread_cond_wait(&EQStack_not_empty, &Sleep_lock);
      SleepingThreadsNum--;
      pthread_mutex_unlock(&Sleep_lock);
      //            printf("[Thread %d is waked up.]\n", vm->id);
    }

    eval_equation(vm, t1, t2);
  }

  return (void *)NULL;
}

#  if defined(EXPANDABLE_HEAP) || defined(FLEX_EXPANDABLE_HEAP)
void tpool_init(unsigned int eqstack_size) {
#  else
// v0.5.6
void tpool_init(unsigned int agentBufferSize, unsigned int eqstack_size) {
#  endif

  int i, status;
  //  static int id[100];

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);

  CpuNum = sysconf(_SC_NPROCESSORS_CONF);

  pthread_setconcurrency(CpuNum);

  SleepingThreadsNum = 0;
  Threads = (pthread_t *)malloc(sizeof(pthread_t) * MaxThreadsNum);
  if (Threads == NULL) {
    printf("the thread pool could not be created.");
    exit(-1);
  }

  VMs = malloc(sizeof(VirtualMachine *) * MaxThreadsNum);
  if (VMs == NULL) {
    printf("the thread pool could not be created.");
    exit(-1);
  }

  for (i = 0; i < MaxThreadsNum; i++) {
    VMs[i] = malloc(sizeof(VirtualMachine));
    VMs[i]->id = i;

    //    usleep(i*2);

#  if defined(EXPANDABLE_HEAP) || defined(FLEX_EXPANDABLE_HEAP)
    VM_Init(VMs[i], eqstack_size);
#  else
    VM_Init(VMs[i], agentBufferSize, eqstack_size);
#  endif

    status = pthread_create(&Threads[i], &attr, tpool_thread, (void *)VMs[i]);
    if (status != 0) {
      printf("ERROR: Thread%d could not be created.", i);
      exit(-1);
    }
  }
}

void tpool_destroy(void) {
  for (int i = 0; i < MaxThreadsNum; i++) {
    pthread_join(Threads[i], NULL);
  }
  free(Threads);
}

int exec(Ast *at) {
  // Ast at: (AST_BODY stmlist aplist)

  unsigned long long t, time;

  void *code[MAX_VMCODE_SEQUENCE];
  int   eqsnum = 0;

#  ifdef COUNT_INTERACTION
  for (int i = 0; i < MaxThreadsNum; i++) {
    VM_Clear_InteractionCount(VMs[i]);
  }
#  endif

  start_timer(&t);

  CmEnv_clear_all();

  // for `where' expression
  if (!Compile_stmlist_on_ast(at->left))
    return 0;

  // aplist
  at = at->right;

  Ast_RewriteOptimisation_eqlist(at);

  // Syntax error check
  {
    Ast *tmp_at = at;
    while (tmp_at != NULL) {

      if (!(check_ast_arity(tmp_at->left->left)) ||
          !(check_ast_arity(tmp_at->left->right)) ||
          !(check_invalid_occurrence(tmp_at->left->left)) ||
          !(check_invalid_occurrence(tmp_at->left->right))) {

        // invalid case
        if (yyin != stdin)
          exit(-1);
        return 0;
      }

      tmp_at = ast_getTail(tmp_at);
    }
  }

  // Reset the counter of compilation errors
  CmEnv.count_compilation_errors = 0;

  while (at != NULL) {
    int  p1, p2;
    Ast *left, *right;
    left = at->left->left;
    right = at->left->right;
    p1 = Compile_term_on_ast(left, -1);
    p2 = Compile_term_on_ast(right, -1);

    // Check whether compilation errors arise
    if (CmEnv.count_compilation_errors != 0) {
      return 0;
    }

    if (left->id == AST_NAME) {
      select_kind_of_push(left, p1, p2);

    } else if (right->id == AST_NAME) {
      select_kind_of_push(right, p2, p1);

    } else {
      IMCode_genCode2(OP_PUSH, p1, p2);
    }

    eqsnum++; // for distrubution
    at = ast_getTail(at);
  }
  IMCode_genCode0(OP_RET);

  // checking whether names occur more than twice
  if (!CmEnv_check_name_reference_times()) {
    return 0;
  }

#  ifndef DEBUG_NETS
  // for regular
  CmEnv_retrieve_MKGNAME();
  CmEnv_generate_VMCode(code);

#  else
  // for debug
  CmEnv_retrieve_MKGNAME();
  IMCode_puts(0); // exit(1);

  int codenum = CmEnv_generate_VMCode(code);
  VMCode_puts(code, codenum - 2); // exit(1);
  // end for debug
#  endif

  exec_code(1, VMs[0], code);

  // Distribute equations to virtual machines
  {
    int each_eqsnum = eqsnum / MaxThreadsNum;
    if (each_eqsnum == 0)
      each_eqsnum = 1;

    VALUE t1, t2;
    for (int i = 1; i < MaxThreadsNum; i++) {
      for (int j = 0; j < each_eqsnum; j++) {
        if (!VM_EQStack_Pop(VMs[0], &t1, &t2))
          goto endloop;
        VM_EQStack_Push(VMs[i], t1, t2);
      }
    }
  }
endloop:

  pthread_mutex_lock(&Sleep_lock);
  pthread_cond_broadcast(&EQStack_not_empty);
  pthread_mutex_unlock(&Sleep_lock);

  // a little wait until all threads start.
  // usleep(CAS_LOCK_USLEEP);
  usleep(10000); // 0.01 sec wait

  // if some threads are working, wait for all of these to sleep.
  if (SleepingThreadsNum < MaxThreadsNum) {
    pthread_mutex_lock(&AllSleep_lock);
    pthread_cond_wait(&ActiveThread_all_sleep, &AllSleep_lock);
    pthread_mutex_unlock(&AllSleep_lock);
  }

  usleep(10000); // 0.01 sec wait
  //  usleep(CAS_LOCK_USLEEP);
  for (int i = 0; i < MaxThreadsNum; i++) {
    //    printf("VM[%d]: nextPtr_eqStack=%d\n", i, VMs[i]->nextPtr_eqStack);
    if (VMs[i]->nextPtr_eqStack != -1)
      goto endloop;
  }

  time = stop_timer(&t);

#  ifdef COUNT_INTERACTION
  {
    unsigned long total = 0;
    for (int i = 0; i < MaxThreadsNum; i++) {
      total += VM_Get_InteractionCount(VMs[i]);
    }
    printf("(%lu interactions by %d threads, %.2f sec)\n", total, MaxThreadsNum,
           (double)(time) / 1000000.0);
  }

#  else
  printf("(%.2f sec by %d threads)\n", (double)(time) / 1000000.0,
         MaxThreadsNum);
#  endif

  return 0;
}
#endif

int make_rule(Ast *ast) {
  //      (ASTRULE
  //       (AST_CNCT agentL agentR)
  //       <if-sentence> | <body>)

  //  ast_puts(ast);puts("");

  Ast *ruleAgent_L = ast->left->left;
  Ast *ruleAgent_R = ast->left->right;

  /*
  if (ruleAgent_L->id == AST_NAME) {
    printf("%d:ERROR: The name `%s' was specified as the left-hand side of rule
  agents. It should be an agent.\n", yylineno, ruleAgent_L->left->sym); return
  0;
  }
  if (ruleAgent_R->id == AST_NAME) {
    printf("%d:ERROR: The name `%s' was specified as the right-hand side of rule
  agents. It should be an agent.\n", yylineno, ruleAgent_R->left->sym); return
  0;
  }
  */

  ast->left->left = ast_remove_tuple1(ruleAgent_L);
  ast->left->right = ast_remove_tuple1(ruleAgent_R);

  Ast *rule_mainbody = ast->right;
  Ast_remove_tuple1_in_mainbody(rule_mainbody);

  set_annotation_LR(VM_OFFSET_ANNOTATE_L, VM_OFFSET_ANNOTATE_R);

  if (!make_rule_oneway(ast)) {
    return 0;
  }

  // another way
  int preserve_CmEnv_put_compiled_codes = CmEnv.put_compiled_codes;
  CmEnv.put_compiled_codes = 0; // without outputting codes

  CmEnv.put_warning_for_cnct_property = 0; // without warning

  Ast *tmp;
  tmp = ast->left->left;
  ast->left->left = ast->left->right;
  ast->left->right = tmp;

  /*
  ruleAgent_L = ast->left->left;
  ruleAgent_R = ast->left->right;

  ast->left->left = ruleAgent_R;
  ast->left->right = ruleAgent_L;
  */

  set_annotation_LR(VM_OFFSET_ANNOTATE_R, VM_OFFSET_ANNOTATE_L);

  //  ast_puts(ast);puts("");
  int result_make_rule = make_rule_oneway(ast);

  CmEnv.put_warning_for_cnct_property = 1; // retrieve warning
  CmEnv.put_compiled_codes = preserve_CmEnv_put_compiled_codes; // retrieve

  return result_make_rule;
}

void IMCode_puts(int n) {

  static char *string_opcode[] = {"PUSH",
                                  "PUSHI",
                                  "MYPUSH",
                                  "MKNAME",
                                  "MKGNAME",

                                  "MKAGENT",

                                  "RET",
                                  "RET_FREE_LR",
                                  "RET_FREE_L",
                                  "RET_FREE_R",

                                  "LOADI",
                                  "LOAD",
                                  "LOADP",
                                  "LOADP_L",
                                  "LOADP_R",
                                  "CHID_L",
                                  "CHID_R",

                                  "ADD",
                                  "SUB",
                                  "ADDI",
                                  "SUBI",
                                  "MUL",
                                  "DIV",
                                  "MOD",
                                  "LT",
                                  "LE",
                                  "EQ",
                                  "EQI",
                                  "NE",
                                  "UNM",
                                  "RAND",
                                  "INC",
                                  "DEC",

                                  "LT_R0",
                                  "LE_R0",
                                  "EQ_R0",
                                  "EQI_R0",
                                  "NE_R0",

                                  "JMPEQ0",
                                  "JMPEQ0_R0",
                                  "JMP",
                                  "JMPNEQ0",

                                  "JMPCNCT_CONS",
                                  "JMPCNCT",
                                  "LOOP",
                                  "LOOP_RREC",
                                  "LOOP_RREC1",
                                  "LOOP_RREC2",
                                  "LOOP_RREC_FREE_R",
                                  "LOOP_RREC1_FREE_R",
                                  "LOOP_RREC2_FREE_R",

                                  "CNCTGN",
                                  "SUBSTGN",

                                  "NOP",

                                  "LABEL",
                                  "__DEAD_CODE",
                                  "BEGIN_BLOCK",
                                  "BEGIN_CNCT_BLOCK",
                                  "LOAD_META",
                                  "LOADI_SHARED"

  };

  struct IMCode_tag *imcode;

  puts("[IMCode_puts]");
  if (n == -1)
    return;

  for (int i = n; i < IMCode_n; i++) {
    imcode = &IMCode[i];
    printf("%2d: ", i);

    switch (imcode->opcode) {
      // for indentation
    case OP_LABEL:
    case OP_BEGIN_BLOCK:
    case OP_BEGIN_JMPCNCT_BLOCK:
      break;

    default:
      printf("\t");
    }

    switch (imcode->opcode) {
    case OP_MKNAME:
      printf("%s var%ld\n", string_opcode[imcode->opcode], imcode->operand1);
      break;

    case OP_MKGNAME:
      printf("%s id%ld var%ld; \"%s\"\n", string_opcode[imcode->opcode],
             imcode->operand1, imcode->operand2,
             IdTable_get_name(imcode->operand1));
      break;

    case OP_MKAGENT:
      printf("%s id%ld var%ld\n", string_opcode[imcode->opcode],
             imcode->operand1, imcode->operand2);
      break;

    case OP_LABEL:
      printf("%s%ld:\n", string_opcode[imcode->opcode], imcode->operand1);
      break;

      // 0 arity: opcode
    case OP_RET:
    case OP_RET_FREE_L:
    case OP_RET_FREE_R:
    case OP_RET_FREE_LR:
    case OP_LOOP:
    case OP_NOP:
    case OP_DEAD_CODE:
      printf("%s\n", string_opcode[imcode->opcode]);
      break;

    case OP_BEGIN_BLOCK:
    case OP_BEGIN_JMPCNCT_BLOCK:
      printf("%s:\n", string_opcode[imcode->opcode]);
      break;

      // 1 arity: opcode var1
    case OP_LOOP_RREC1:
    case OP_LOOP_RREC2:
    case OP_LOOP_RREC1_FREE_R:
    case OP_LOOP_RREC2_FREE_R:
      printf("%s var%ld\n", string_opcode[imcode->opcode], imcode->operand1);
      break;

      // arity is 2: opcode var1 var2
    case OP_UNM:
    case OP_INC:
    case OP_DEC:
    case OP_RAND:
    case OP_PUSH:
    case OP_MYPUSH:
    case OP_LOAD:
    case OP_LOAD_META:
    case OP_LT_R0:
    case OP_LE_R0:
    case OP_EQ_R0:
    case OP_NE_R0:
    case OP_CNCTGN:
    case OP_SUBSTGN:
      printf("%s var%ld var%ld\n", string_opcode[imcode->opcode],
             imcode->operand1, imcode->operand2);
      break;

    case OP_LOADP:
      printf("%s var%ld $%ld var%ld\n", string_opcode[imcode->opcode],
             imcode->operand1, imcode->operand2, imcode->operand3);
      break;

    case OP_LOADP_L:
      printf("%s var%ld $%ld\n", string_opcode[imcode->opcode],
             imcode->operand1, imcode->operand2);
      break;
    case OP_LOADP_R:
      printf("%s var%ld $%ld\n", string_opcode[imcode->opcode],
             imcode->operand1, imcode->operand2);
      break;
    case OP_CHID_L:
      printf("%s $%ld\n", string_opcode[imcode->opcode], imcode->operand1);
      break;
    case OP_CHID_R:
      printf("%s $%ld\n", string_opcode[imcode->opcode], imcode->operand1);
      break;

      // arity is 2: opcode var1 $2
    case OP_PUSHI:
    case OP_LOADI:
    case OP_EQI_R0:
    case OP_LOADI_SHARED:
      printf("%s $%ld var%ld\n", string_opcode[imcode->opcode],
             imcode->operand1, imcode->operand2);
      break;

    case OP_LOOP_RREC:
    case OP_LOOP_RREC_FREE_R:
      printf("%s var%ld $%ld\n", string_opcode[imcode->opcode],
             imcode->operand1, imcode->operand2);
      break;

      // arity is 3: opcode var1 var2 var3
    case OP_ADD:
    case OP_SUB:
    case OP_MUL:
    case OP_DIV:
    case OP_MOD:
    case OP_LT:
    case OP_LE:
    case OP_EQ:
    case OP_NE:
      printf("%s var%ld var%ld var%ld\n", string_opcode[imcode->opcode],
             imcode->operand1, imcode->operand2, imcode->operand3);
      break;

      // arity is 3: opcode var1 var2 $3
    case OP_ADDI:
    case OP_SUBI:
    case OP_EQI:
      printf("%s var%ld $%ld var%ld\n", string_opcode[imcode->opcode],
             imcode->operand1, imcode->operand2, imcode->operand3);
      break;

      // arity is 2: opcode var1 LABEL2
    case OP_JMPEQ0:
    case OP_JMPNEQ0:
    case OP_JMPCNCT_CONS:
      printf("%s var%ld %s%ld\n", string_opcode[imcode->opcode],
             imcode->operand1, string_opcode[OP_LABEL], imcode->operand2);
      break;

      // arity is 3: opcode var1 id2 LABEL3
    case OP_JMPCNCT:
      printf("%s var%ld id%ld %s%ld\n", string_opcode[imcode->opcode],
             imcode->operand1, imcode->operand2, string_opcode[OP_LABEL],
             imcode->operand3);
      break;

      // others
    case OP_JMPEQ0_R0:
      printf("%s %s%ld\n", string_opcode[imcode->opcode],
             string_opcode[OP_LABEL], imcode->operand1);
      break;

    case OP_JMP:
      printf("%s %s%ld\n", string_opcode[imcode->opcode],
             string_opcode[OP_LABEL], imcode->operand1);
      break;

    default:
      printf("CODE %ld %ld %ld %ld %ld %ld\n", imcode->operand1,
             imcode->operand2, imcode->operand3, imcode->operand4,
             imcode->operand5, imcode->operand6);
    }
  }
}

int get_ruleagentID(Ast *ruleAgent) {
  // Returns ID of the given Ast.
  // This is called by make_rule to get IDs of rule agents.

  int id;

  if (ruleAgent->id == AST_TUPLE) {
    id = GET_TUPLEID(ruleAgent->intval);

  } else if (ruleAgent->id == AST_OPCONS) {
    id = ID_CONS;

  } else if (ruleAgent->id == AST_NIL) {
    id = ID_NIL;

  } else if (ruleAgent->id == AST_INTVAR) {
    id = ID_INT;

  } else if (ruleAgent->id == AST_NAME) {
    id = ID_WILDCARD;

  } else if (strcmp((char *)ruleAgent->left->sym, "Int") == 0) {
    id = ID_INTAGENT;
  } else {

    id = IdTable_getid_builtin_funcAgent(ruleAgent);
    if (id == -1) {
      id = NameTable_get_set_id_with_IdTable_forAgent(ruleAgent->left->sym);
    }
  }

  return id;
}

void print_name_port0(VALUE ptr) {
  if (ptr == (VALUE)NULL) {
    fprintf(stderr, "<NON-DEFINED>");
    return;
  }

  if (!(IS_NAMEID(BASIC(ptr)->id))) {
    puts("ERROR: it is not a name.");
    return;
  }

  if (NAME(ptr)->port == (VALUE)NULL) {
    printf("<EMPTY>");
    return;
  }

  ShowNameHeap = ptr;
  puts_term(NAME(ptr)->port);
  ShowNameHeap = (VALUE)NULL;
}

int make_rule_oneway(Ast *ast) {
  //    (ASTRULE
  //      (AST_CNCT agentL agentR)
  //      <if-sentence> | <body>)
  //
  //      WHERE
  //      <if-sentence> ::= (AST_IF guard (AST_BRANCH <then> <else>))
  //                      | <body>
  //      <then> ::= <if-sentence>
  //      <else> ::= <if-sentence>
  //
  //      <body> ::= (AST_BODY stmlist aplist)

  int  idL, idR;
  Ast *ruleAgent_L, *ruleAgent_R, *rule_mainbody;

  void *code[MAX_VMCODE_SEQUENCE];
  int   gencode_num = 0;

  CmEnv_clear_all();

  //  #define PUT_RULE_COMPILATION_TIME
#ifdef PUT_RULE_COMPILATION_TIME
  unsigned long long t, time;
  start_timer(&t);
#endif

  ruleAgent_L = ast->left->left;
  ruleAgent_R = ast->left->right;

  //          #define MYDEBUG1
#ifdef MYDEBUG1
  printf("MYDEBUG1");
  ast_puts(ruleAgent_L);
  puts("");
  printf("MYDEBUG1");
  ast_puts(ruleAgent_R);
  puts("");
  //  ast_puts(bodies); exit(1);
#endif

  // Check if every argument is a kind of names.

  // Decide IDs for rule agents.
  // Note that agentR should be checked earlier
  // because it could be a constructor.
  // IDs of Constructors must be smaller than destructors.
  idR = get_ruleagentID(ruleAgent_R);

  // Peel the tuple1 such as `(int i)'
  // int i は (AST_INTVAR (AST_SYM sym NULL) NULL) として構築されているので
  // ID_TUPLE1 である間は読み飛ばす
  while (idR == ID_TUPLE1) {
    ruleAgent_R = ruleAgent_R->right->left;
    idR = get_ruleagentID(ruleAgent_R);
  }

  idL = get_ruleagentID(ruleAgent_L);
  while (idL == ID_TUPLE1) {
    ruleAgent_L = ruleAgent_L->right->left;
    idL = get_ruleagentID(ruleAgent_L);
  }

  /*
  // Annotation (*L)、(*R) の処理があるため
  // 単純に ruleAgentL、ruleAgentR を入れ替えれば良いわけではない。

  if (idL > idR) {
    // Generally the order should be as Append(xs,r) >< y:ys,
    // so idL should be greater than idR.

    // JAPANESE: 標準では Append(xs, r) >< [y|ys] なので
    // idL の方が大きくなって欲しい


    // (*L) is interpreted as the left-hand side agent, (*R) is the right one.
    // JAPANESE: annotation が (*L) なら左側、(*R) なら右側を指すものとする
    set_annotation_LR(VM_OFFSET_ANNOTATE_L, VM_OFFSET_ANNOTATE_R);

  } else {
    Ast *tmp;
    tmp = ruleAgent_L;
    ruleAgent_L = ruleAgent_R;
    ruleAgent_R = tmp;

    int idTmp;
    idTmp = idL;
    idL = idR;
    idR = idTmp;

    // Keep the occurrence (*L) and (*R) in aplist,
    // change the interpretation of (*L) as (*R), vice versa.

    // JAPANESE: aplist 内の (*L) (*R) はそのままにしておき、
    // annotation が (*L) なら右側、(*R) なら左側を指すものとする
    set_annotation_LR(VM_OFFSET_ANNOTATE_R, VM_OFFSET_ANNOTATE_L);
  }
  */

  int arity;

  // IMPORTANT:
  // The first two codes stores arities of idL and idR, respectively.
  if (idL == ID_INT || idL == ID_WILDCARD) {
    arity = 0;
  } else {
    arity = get_arity_on_ast(ruleAgent_L);
  }
  if (arity > MAX_PORT) {
    printf("%d:ERROR: Too many arguments of `%s'. It should be MAX_PORT(=%d) "
           "or less.\n",
           yylineno, ruleAgent_L->left->sym, MAX_PORT);
    return 0;
  }
  IdTable_set_arity(idL, arity);
  code[0] = (void *)(unsigned long)arity;

  if (idR == ID_INT || idR == ID_WILDCARD) {
    arity = 0;
  } else {
    arity = get_arity_on_ast(ruleAgent_R);
  }
  if (arity > MAX_PORT) {
    printf("%d:ERROR: Too many arguments of `%s'. It should be MAX_PORT(=%d) "
           "or less.\n",
           yylineno, ruleAgent_R->left->sym, MAX_PORT);
    return 0;
  }
  IdTable_set_arity(idR, arity);
  code[1] = (void *)(unsigned long)arity;

  gencode_num = 2;

  if (idL == ID_INT) {
    set_metaL_as_IntName(ruleAgent_L);
    CmEnv.annotateL = ANNOTATE_INT_MODIFIER; // to prevent putting Free_L
  } else if (idL == ID_WILDCARD) {
    set_metaL_as_AnyAgent(ruleAgent_L);
    CmEnv.annotateL = ANNOTATE_WILDCARD; // to prevent putting Free_L
  } else {
    if (!set_metaL(ruleAgent_L)) {
      return 0;
    }
  }

  if (idR == ID_INT) {
    set_metaR_as_IntName(ruleAgent_R);
    CmEnv.annotateR = ANNOTATE_INT_MODIFIER; // to prevent putting Free_R
  } else if (idR == ID_WILDCARD) {
    set_metaR_as_AnyAgent(ruleAgent_R);
    CmEnv.annotateR = ANNOTATE_WILDCARD; // to prevent putting Free_R
  } else {
    if (!set_metaR(ruleAgent_R)) {
      return 0;
    }
  }

  CmEnv.idL = idL;
  CmEnv.idR = idR;

  rule_mainbody = ast->right;

  // puts("Before:"); ast_puts(rule_mainbody); puts("");

  int is_tco = 0;

  // #ifdef OPTIMISE_IMCODE_TCO
  if (CmEnv.tco) {

    if (!Ast_mainbody_has_agentID(rule_mainbody, AST_ANNOTATION_L)) {

      is_tco = Ast_make_annotation_TailCallOptimisation(rule_mainbody);

    } else {

      is_tco = 0;
    }

#ifdef VERBOSE_TCO
    if (is_tco) {
      printf("=== Tail Call Optimisation is applied to ");
      printf("Rule: %s(id:%d) >< %s(id:%d). ===\n", IdTable_get_name(idL), idL,
             IdTable_get_name(idR), idR);
    }
#endif

  } // #endif

  if (!Compile_rule_mainbody_on_ast(rule_mainbody))
    return 0;

  // #ifdef OPTIMISE_IMCODE_TCO
  if (CmEnv.tco) {

    if (is_tco) {

      //    puts("Before:"); ast_puts(rule_mainbody); puts("");

      Ast_undo_TCO_annotation(rule_mainbody);

      //    puts("After:"); ast_puts(rule_mainbody); puts("");
    }
  } // #endif

  //              #define DEBUG_MKRULE
#ifndef DEBUG_MKRULE
  gencode_num += CmEnv_generate_VMCode(&code[2]);
#else

  // int here_flag = is_tco;
  int here_flag = 1;
  if (here_flag) {
    puts("[1]. --- RAW codes --------------------------------");
    printf("Rule: %s(id:%d) >< %s(id:%d).\n", IdTable_get_name(idL), idL,
           IdTable_get_name(idR), idR);
    IMCode_puts(0);
  }

  gencode_num += CmEnv_generate_VMCode(&code[2]);

  if (here_flag) {
    puts("[2].--- OPTMISED codes --------------------------------");
    IMCode_puts(0);
    puts("[3].---- Genarated bytecodes  -------------------------------");
    VMCode_puts(&code[2], gencode_num - 2);
    puts("-----------------------------------");
  }

  //  exit(1);

  //    printf("Rule: %s(id:%d) >< %s(id:%d).\n",
  //	   IdTable_get_name(idL), idL,
  //	   IdTable_get_name(idR), idR);
  //
  //    ast_puts(ruleAgent_L); printf("><");
  //    ast_puts(ruleAgent_R); puts("");
#endif

  if (CmEnv.put_compiled_codes) {
    printf("Rule: %s(id:%d,arity:%lu) >< %s(id:%d,arity:%lu).\n",
           IdTable_get_name(idL), idL, (unsigned long)code[0],
           IdTable_get_name(idR), idR, (unsigned long)code[1]);
    VMCode_puts(&code[2], gencode_num - 2);
    if (yyin != stdin) {
      puts("");
    }
  }

  // Record the rule code for idL >< idR
  RuleTable_record(idL, idR, code, gencode_num);

  /*
  if (idL != idR) {
    // Delete the rule code for idR >< idL
    // because we need only the rule idL >< idR.
    RuleTable_delete(idR, idL);
  }
  */

  //    #define DEBUG_PUT_RULE_CODE
#ifdef DEBUG_PUT_RULE_CODE
  if (((strcmp(IdTable_get_name(idL), "Fib") == 0) && (idR == ID_INT)) ||
      ((strcmp(IdTable_get_name(idL), "fib") == 0) && (idR == ID_INT)) ||
      ((strcmp(IdTable_get_name(idL), "Ack") == 0) && (idR == ID_INT)) ||
      ((strcmp(IdTable_get_name(idL), "Ackm") == 0) && (idR == ID_INT)) ||
      ((strcmp(IdTable_get_name(idL), "MergeCC") == 0) && (idR == ID_CONS))) {

    printf("Rule: %s >< %s.\n", IdTable_get_name(idL), IdTable_get_name(idR));

    IMCode_puts(0);
    VMCode_puts(code, gencode_num);
    //      exit(1);
  }
#endif

#ifdef PUT_RULE_COMPILATION_TIME
  time = stop_timer(&t);
  printf("(Compilation of %s><%s takes %.6f sec)\n", IdTable_get_name(idL),
         IdTable_get_name(idR), (double)(time) / 1000000.0);
#endif

  return 1;
}

// ------------------------------------------------------
// Code Execution
// ------------------------------------------------------

#ifdef COUNT_CNCT
int Count_cnct = 0;
int Count_cnct_true = 0;
int Count_cnct_indirect_op = 0;
#endif

void *exec_code(int mode, VirtualMachine *restrict vm, void *restrict *code) {

  // http://magazine.rubyist.net/?0008-YarvManiacs
  static const void *table[] = {
      &&E_PUSH,
      &&E_PUSHI,
      &&E_MYPUSH,
      &&E_MKNAME,
      &&E_MKGNAME,
      &&E_MKAGENT,
      &&E_RET,
      &&E_RET_FREE_LR,
      &&E_RET_FREE_L,
      &&E_RET_FREE_R,
      &&E_LOADI,
      &&E_LOAD,
      &&E_LOADP,
      &&E_LOADP_L,
      &&E_LOADP_R,
      &&E_CHID_L,
      &&E_CHID_R,
      &&E_ADD,
      &&E_SUB,
      &&E_ADDI,
      &&E_SUBI,
      &&E_MUL,
      &&E_DIV,
      &&E_MOD,
      &&E_LT,
      &&E_LE,
      &&E_EQ,
      &&E_EQI,
      &&E_NE,
      &&E_UNM,
      &&E_RAND,
      &&E_INC,
      &&E_DEC,
      &&E_LT_R0,
      &&E_LE_R0,
      &&E_EQ_R0,
      &&E_EQI_R0,
      &&E_NE_R0,

      &&E_JMPEQ0,
      &&E_JMPEQ0_R0,
      &&E_JMP,
      &&E_JMPNEQ0,
      &&E_JMPCNCT_CONS,
      &&E_JMPCNCT,

      &&E_LOOP,
      &&E_LOOP_RREC,
      &&E_LOOP_RREC1,
      &&E_LOOP_RREC2,
      &&E_LOOP_RREC_FREE_R,
      &&E_LOOP_RREC1_FREE_R,
      &&E_LOOP_RREC2_FREE_R,

      &&E_CNCTGN,
      &&E_SUBSTGN,
      &&E_NOP,
  };

  // To create the table.
  // mode=0: Create table (only for initialise)
  // mode=1: Execute codes (the normal operation)
  if (mode == 0) {
    return table;
  }

  unsigned long inst;
  int           pc = 0;
  VALUE         a1;
  VALUE        *reg = vm->reg;

  goto *code[0];

E_MKNAME:
  //    puts("mkname dest");
  reg[(unsigned long)code[++pc]] = make_Name(vm);
  goto *code[++pc];

E_MKGNAME:
  //    puts("mkgname id dest");

  inst = (unsigned long)code[++pc];
  a1 = IdTable_get_heap(inst);
  if (a1 == (VALUE)NULL) {
    a1 = make_Name(vm);

    // set GID
    BASIC(a1)->id = inst;
    IdTable_set_heap(inst, a1);
  }

  reg[(unsigned long)code[++pc]] = a1;
  goto *code[++pc];

E_MKAGENT:
  //    puts("mkagent id dest");
  inst = (unsigned long)code[++pc];
  reg[(unsigned long)code[++pc]] = make_Agent(vm, inst);
  goto *code[++pc];

E_PUSH:
  //    puts("push reg reg");
  {
    VALUE a1 = reg[(unsigned long)code[++pc]];
    VALUE a2 = reg[(unsigned long)code[++pc]];

#ifdef DEBUG
    puts("");
    puts("E_PUSH is operating:");
    puts_term(a1);
    puts("");
    puts_term(a2);
    puts("");
    puts("");
#endif

    PUSH(vm, a1, a2);
  }
  goto *code[++pc];

E_PUSHI:
  //      puts("pushi reg fixint");
  {
    VALUE a1 = reg[(unsigned long)code[++pc]];
    VALUE a2 = (unsigned long)code[++pc];
    PUSH(vm, a1, a2);
  }
  goto *code[++pc];

E_MYPUSH:
  //    puts("mypush reg reg");
  {
    VALUE a1 = reg[(unsigned long)code[++pc]];
    VALUE a2 = reg[(unsigned long)code[++pc]];
    MYPUSH(vm, a1, a2);
  }
  goto *code[++pc];

E_LOADI:
  //    puts("loadi num dest");
  inst = (long)code[++pc];
  reg[(unsigned long)code[++pc]] = inst;
  goto *code[++pc];

E_RET:
  //    puts("ret");
  return NULL;

E_RET_FREE_LR:
  //    puts("ret_free_LR");

  // free_Agent(reg[VM_OFFSET_ANNOTATE_L]);
  // free_Agent(reg[VM_OFFSET_ANNOTATE_R]);
  free_Agent2(reg[VM_OFFSET_ANNOTATE_L], reg[VM_OFFSET_ANNOTATE_R]);
  return NULL;

E_RET_FREE_L:
  //    puts("ret_free_L");
  free_Agent(reg[VM_OFFSET_ANNOTATE_L]);
  return NULL;

E_RET_FREE_R:
  //    puts("ret_free_R");
  free_Agent(reg[VM_OFFSET_ANNOTATE_R]);
  return NULL;

E_LOOP:
  //    puts("loop");
  pc = 0;
  goto *code[0];

E_LOOP_RREC_FREE_R:
  free_Agent(reg[VM_OFFSET_ANNOTATE_R]);

E_LOOP_RREC:
  //      puts("looprrec reg ar");

  a1 = reg[(unsigned long)code[pc + 1]];
  for (int i = 0; i < (unsigned long)code[pc + 2]; i++) {
    reg[VM_OFFSET_METAVAR_R(i)] = AGENT(a1)->port[i];
  }

  reg[VM_OFFSET_ANNOTATE_R] = a1;

  pc = 0;
  goto *code[0];

E_LOOP_RREC1_FREE_R:
  free_Agent(reg[VM_OFFSET_ANNOTATE_R]);

E_LOOP_RREC1:
  //      puts("looprrec1 reg");

  a1 = reg[(unsigned long)code[pc + 1]];

  reg[VM_OFFSET_METAVAR_R(0)] = AGENT(a1)->port[0];

  reg[VM_OFFSET_ANNOTATE_R] = a1;

  pc = 0;
  goto *code[0];

E_LOOP_RREC2_FREE_R:
  free_Agent(reg[VM_OFFSET_ANNOTATE_R]);

E_LOOP_RREC2:
  //      puts("looprrec2 reg);

#ifdef DEBUG
  puts("E_LOOP_REC2 is operating");
#endif

  a1 = reg[(unsigned long)code[pc + 1]];

  reg[VM_OFFSET_METAVAR_R(0)] = AGENT(a1)->port[0];
  reg[VM_OFFSET_METAVAR_R(1)] = AGENT(a1)->port[1];

  reg[VM_OFFSET_ANNOTATE_R] = a1;

  pc = 0;
  goto *code[0];

E_LOAD:
  //    puts("load src dest");
  a1 = reg[(unsigned long)code[++pc]];
  reg[(unsigned long)code[++pc]] = a1;
  goto *code[++pc];

E_LOADP:
  //    puts("loadp src port dest");
  a1 = reg[(unsigned long)code[++pc]];
  inst = (unsigned long)code[++pc];
  AGENT(reg[(unsigned long)code[++pc]])->port[inst] = a1;
  goto *code[++pc];

E_LOADP_L:
  //    puts("loadp_L src port");
  a1 = reg[(unsigned long)code[++pc]];
  inst = (unsigned long)code[++pc];
  AGENT(reg[VM_OFFSET_ANNOTATE_L])->port[inst] = a1;
  goto *code[++pc];

E_LOADP_R:
  //    puts("loadp_R src port");
  a1 = reg[(unsigned long)code[++pc]];
  inst = (unsigned long)code[++pc];
  AGENT(reg[VM_OFFSET_ANNOTATE_R])->port[inst] = a1;
  goto *code[++pc];

E_CHID_L:
  //    puts("chid_L id");
  BASIC(reg[VM_OFFSET_ANNOTATE_L])->id = (unsigned long)code[++pc];
  goto *code[++pc];

E_CHID_R:
  //    puts("chid_R id");
  BASIC(reg[VM_OFFSET_ANNOTATE_R])->id = (unsigned long)code[++pc];
  goto *code[++pc];

E_ADD:
  //    puts("ADD src1 src2 dest");
  {
    long i = FIX2INT(reg[(unsigned long)code[++pc]]);
    long j = FIX2INT(reg[(unsigned long)code[++pc]]);

    reg[(unsigned long)code[++pc]] = INT2FIX(i + j);
  }
  goto *code[++pc];

E_SUB:
  //    puts("SUB src1 src2 dest");
  {
    long i = FIX2INT(reg[(unsigned long)code[++pc]]);
    long j = FIX2INT(reg[(unsigned long)code[++pc]]);

    reg[(unsigned long)code[++pc]] = INT2FIX(i - j);
  }
  goto *code[++pc];

E_ADDI:
  //  puts("ADDI src int dest");
  {
    long i = FIX2INT(reg[(unsigned long)code[++pc]]);
    long j = (unsigned long)code[++pc];

    reg[(unsigned long)code[++pc]] = INT2FIX(i + j);
  }
  goto *code[++pc];

E_SUBI:
  //  puts("SUBI src int dest");
  {
    long i = FIX2INT(reg[(unsigned long)code[++pc]]);
    long j = (unsigned long)code[++pc];

    reg[(unsigned long)code[++pc]] = INT2FIX(i - j);
  }
  goto *code[++pc];

E_MUL:
  //    puts("MUL src1 src2 dest");
  {
    long i = FIX2INT(reg[(unsigned long)code[++pc]]);
    long j = FIX2INT(reg[(unsigned long)code[++pc]]);

    reg[(unsigned long)code[++pc]] = INT2FIX(i * j);
  }
  goto *code[++pc];

E_DIV:
  //    puts("DIV src1 src2 dest");
  {
    long i = FIX2INT(reg[(unsigned long)code[++pc]]);
    long j = FIX2INT(reg[(unsigned long)code[++pc]]);

    reg[(unsigned long)code[++pc]] = INT2FIX(i / j);
  }
  goto *code[++pc];

E_MOD:
  //    puts("MOD src1 src2 dest");
  {
    long i = FIX2INT(reg[(unsigned long)code[++pc]]);
    long j = FIX2INT(reg[(unsigned long)code[++pc]]);

    reg[(unsigned long)code[++pc]] = INT2FIX(i % j);
  }
  goto *code[++pc];

E_LT:
  //    puts("LT src1 src2 dest");
  {
    //    int i = FIX2INT(reg[(unsigned long)code[pc++]]);
    //    int j = FIX2INT(reg[(unsigned long)code[pc++]]);
    long i = reg[(unsigned long)code[++pc]];
    long j = reg[(unsigned long)code[++pc]];

    if (i < j) {
      reg[(unsigned long)code[++pc]] = INT2FIX(1);
    } else {
      reg[(unsigned long)code[++pc]] = INT2FIX(0);
    }
  }
  goto *code[++pc];

E_LE:
  //    puts("LT src1 src2 dest");
  {
    long i = reg[(unsigned long)code[++pc]];
    long j = reg[(unsigned long)code[++pc]];

    if (i <= j) {
      reg[(unsigned long)code[++pc]] = INT2FIX(1);
    } else {
      reg[(unsigned long)code[++pc]] = INT2FIX(0);
    }
  }
  goto *code[++pc];

E_EQ:
  //    puts("EQ src1 src2 dest");
  {
    long i = reg[(unsigned long)code[++pc]];
    long j = reg[(unsigned long)code[++pc]];

    if (i == j) {
      reg[(unsigned long)code[++pc]] = INT2FIX(1);
    } else {
      reg[(unsigned long)code[++pc]] = INT2FIX(0);
    }
  }
  goto *code[++pc];

E_EQI:
  //    puts("EQI src1 fixint dest");
  {
    long i = reg[(unsigned long)code[++pc]];
    long j = (unsigned long)code[++pc];

    if (i == j) {
      reg[(unsigned long)code[++pc]] = INT2FIX(1);
    } else {
      reg[(unsigned long)code[++pc]] = INT2FIX(0);
    }
  }
  goto *code[++pc];

E_NE:
  //    puts("NE src1 src2 dest");
  {
    long i = reg[(unsigned long)code[++pc]];
    long j = reg[(unsigned long)code[++pc]];

    if (i != j) {
      reg[(unsigned long)code[++pc]] = INT2FIX(1);
    } else {
      reg[(unsigned long)code[++pc]] = INT2FIX(0);
    }
  }
  goto *code[++pc];

E_LT_R0:
  //    puts("LT_R0 src1 src2");
  {
    long i = reg[(unsigned long)code[++pc]];
    long j = reg[(unsigned long)code[++pc]];

    if (i < j) {
      reg[0] = 1;
    } else {
      reg[0] = 0;
    }
  }
  goto *code[++pc];

E_LE_R0:
  //    puts("LE_R0 src1 src2");
  {
    long i = reg[(unsigned long)code[++pc]];
    long j = reg[(unsigned long)code[++pc]];

    if (i <= j) {
      reg[0] = 1;
    } else {
      reg[0] = 0;
    }
  }
  goto *code[++pc];

E_EQ_R0:
  //    puts("EQ_R0 src1 src2");
  {
    long i = reg[(unsigned long)code[++pc]];
    long j = reg[(unsigned long)code[++pc]];

    if (i == j) {
      reg[0] = 1;
    } else {
      reg[0] = 0;
    }
  }
  goto *code[++pc];

E_EQI_R0:
  //      puts("EQI_R0 src1 int");
  {
    long i = reg[(unsigned long)code[++pc]];
    long j = (unsigned long)code[++pc];

    if (i == j) {
      reg[0] = 1;
    } else {
      reg[0] = 0;
    }
  }
  goto *code[++pc];

E_NE_R0:
  //    puts("NE_R0 src1 src2");
  {
    long i = reg[(unsigned long)code[++pc]];
    long j = reg[(unsigned long)code[++pc]];

    if (i != j) {
      reg[0] = 1;
    } else {
      reg[0] = 0;
    }
  }
  goto *code[++pc];

E_JMPEQ0:
  //    puts("JMPEQ0 reg pc");
  //    the pc is a relative address, not absolute one!
  {
    long i = reg[(unsigned long)code[++pc]];
    if (!FIX2INT(i)) {
      int j = (unsigned long)code[++pc];
      pc += j;
    } else {
      ++pc;
    }
  }
  goto *code[++pc];

E_JMPEQ0_R0:
  //      puts("JMPEQ0_R0 pc");
  {
    long i = reg[0];
    if (!i) {
      int j = (unsigned long)code[++pc];
      pc += j;
    } else {
      ++pc;
    }
  }

  goto *code[++pc];

E_JMPNEQ0:
  //      puts("JMPNEQ0 reg pc");
  {
    long i = reg[(unsigned long)code[++pc]];
    if (FIX2INT(i)) {
      int j = (unsigned long)code[++pc];
      pc += j;
    } else {
      ++pc;
    }
  }

  goto *code[++pc];

E_JMPCNCT_CONS:
  //    puts("JMPCNCT_CONS reg pc");
#ifdef COUNT_CNCT
  Count_cnct++;
#endif

#ifdef THREAD
  if (SleepingThreadsNum > 0) {
    pc += 3;
    goto *code[pc];
  }
#endif

  a1 = reg[(unsigned long)code[pc + 1]];

#ifdef DEBUG
  puts("");
  printf("E_JMPCNCT_CONS is now operating for reg%lu:\n",
         (unsigned long)code[pc + 1]);
  puts_term(a1);
  puts("");
  puts("");
#endif

  if (IS_FIXNUM(a1)) {
    pc += 3;
    goto *code[pc];
  }

  while (IS_NAMEID(BASIC(a1)->id)) {

    if (NAME(a1)->port == (VALUE)NULL) {
      pc += 3;
      goto *code[pc];
    }

#ifdef COUNT_CNCT
    Count_cnct_indirect_op++;
#endif
    VALUE a2 = NAME(a1)->port;

    free_Name(a1);
    a1 = a2;
    reg[(unsigned long)code[pc + 1]] = a2;
  }

#ifdef DEBUG
  puts("===> ");
  puts_term(a1);
  printf(" reg%lu\n", (unsigned long)code[pc + 1]);
  puts_term(reg[(unsigned long)code[pc + 1]]);
  puts("");
#endif

  if (BASIC(a1)->id == ID_CONS) {
#ifdef COUNT_CNCT
    Count_cnct_true++;
#endif

#ifdef DEBUG
    puts("");
    printf("Success: E_JMPCNCT_CONS increased PC by %lu.\n",
           (unsigned long)code[pc + 2]);
    puts_term(reg[(unsigned long)code[pc + 1]]);
    puts("");
    puts("");
#endif

    pc += (unsigned long)code[pc + 2];
    pc += 3;
    goto *code[pc];
  }

  pc += 3;
  goto *code[pc];

E_JMPCNCT:
  //      puts("JMPCNCT reg id pc");
#ifdef COUNT_CNCT
  Count_cnct++;
#endif

  a1 = reg[(unsigned long)code[pc + 1]];
  if (IS_FIXNUM(a1)) {
    pc += 4;
    goto *code[pc];
  }

  while (IS_NAMEID(BASIC(a1)->id)) {

    if (NAME(a1)->port == (VALUE)NULL) {
      pc += 4;
      goto *code[pc];
    }

#ifdef COUNT_CNCT
    Count_cnct_indirect_op++;
#endif
    VALUE a2 = NAME(a1)->port;
    free_Name(a1);
    a1 = a2;
    reg[(unsigned long)code[pc + 1]] = a1;
  }

  if (BASIC(a1)->id == (unsigned long)code[pc + 2]) {
#ifdef COUNT_CNCT
    Count_cnct_true++;
#endif
    pc += (unsigned long)code[pc + 3];
    pc += 4;
    goto *code[pc];
  }

  pc += 4;
  goto *code[pc];

E_JMP:
  //      puts("JMP pc");
  pc += (unsigned long)code[pc + 1];
  pc += 2;
  goto *code[pc];

E_UNM:
#if !defined(OPTIMISE_TWO_ADDRESS) || !defined(OPTIMISE_TWO_ADDRESS_UNARY)
  //    puts("UNM src dest");
  {
    long i = FIX2INT(reg[(unsigned long)code[++pc]]);
    reg[(unsigned long)code[++pc]] = INT2FIX(-1 * i);
  }
#else
  //    puts("UNM dest");
  {
    long i = FIX2INT(reg[(unsigned long)code[++pc]]);
    reg[(unsigned long)code[pc]] = INT2FIX(-1 * i);
  }
#endif
  goto *code[++pc];

E_RAND:
#if !defined(OPTIMISE_TWO_ADDRESS) || !defined(OPTIMISE_TWO_ADDRESS_UNARY)
  //    puts("RAND src dest");
  {
    long i = FIX2INT(reg[(unsigned long)code[++pc]]);
    reg[(unsigned long)code[++pc]] = INT2FIX(rand() % i);
  }
#else
  //    puts("RAND dest");
  {
    long i = FIX2INT(reg[(unsigned long)code[++pc]]);
    reg[(unsigned long)code[pc]] = INT2FIX(rand() % i);
  }
#endif
  goto *code[++pc];

E_INC:
#if !defined(OPTIMISE_TWO_ADDRESS) || !defined(OPTIMISE_TWO_ADDRESS_UNARY)
  //    puts("INC src dest");
  {
    long i = FIX2INT(reg[(unsigned long)code[++pc]]);
    reg[(unsigned long)code[++pc]] = INT2FIX(++i);
  }
#else
  //    puts("INC src");
  {
    long i = FIX2INT(reg[(unsigned long)code[++pc]]);
    reg[(unsigned long)code[pc]] = INT2FIX(++i);
  }
#endif
  goto *code[++pc];

E_DEC:
#if !defined(OPTIMISE_TWO_ADDRESS) || !defined(OPTIMISE_TWO_ADDRESS_UNARY)
  //    puts("DEC src dest");
  {
    long i = FIX2INT(reg[(unsigned long)code[++pc]]);
    reg[(unsigned long)code[++pc]] = INT2FIX(--i);
  }
#else
  //    puts("DEC dest");
  {
    long i = FIX2INT(reg[(unsigned long)code[++pc]]);
    reg[(unsigned long)code[pc]] = INT2FIX(--i);
  }

#endif
  goto *code[++pc];

E_CNCTGN:
  // puts("CNCTGN reg reg");
  // "x"~s, "x"->t     ==> push(s,t), free("x") where "x" is a global name.
  {
    VALUE x = reg[(unsigned long)code[pc + 1]];
    a1 = NAME(x)->port;
    free_Name(x);
    VALUE t = reg[(unsigned long)code[pc + 2]];

    //    puts("============================================");
    //    puts_term(t);printf("\n~");puts_term(a1);puts("");
    //    puts("============================================");

    PUSH(vm, t, a1);
  }
  pc += 3;
  goto *code[pc];

E_SUBSTGN:
  //    puts("SUBSTGN reg reg");
  // "x"~s, t->u("x")  ==> t->u(s), free("x") where "x" is a global name.
  {
    pc++;
    VALUE x = reg[(unsigned long)code[pc++]];
    global_replace_keynode_in_another_term(x, reg[(unsigned long)code[pc++]]);
    free_Name(x);
  }
  goto *code[pc];

  // extended codes should be ended here.

E_NOP:
  goto *code[++pc];
}

void select_kind_of_push(Ast *ast, int p1, int p2) {
  int sym_id = NameTable_get_id(ast->left->sym);

  if (IS_GNAMEID(sym_id)) {
    // aheap already exists as a global
    VALUE aheap = IdTable_get_heap(sym_id);

    // aheap is connected with something such as aheap->t
    // ==> p2 should be conncected with t, so OP_CNCTGN(p1,p2)
    if (NAME(aheap)->port != (VALUE)NULL) {
      IMCode_genCode2(OP_CNCTGN, p1, p2);

    } else {
      // aheap occurs somewhere, so it should be replaced by OP_SUBSTGN(p1,p2)
      IMCode_genCode2(OP_SUBSTGN, p1, p2);
    }
  } else {
    IMCode_genCode2(OP_PUSH, p1, p2);
  }
}

void Compile_gen_RET_for_rulebody(void) {

  if (CmEnv.annotateL == ANNOTATE_TCO) {
    return;
  }

  if (CmEnv.annotateL == ANNOTATE_NOTHING &&
      CmEnv.annotateR == ANNOTATE_NOTHING) {
    IMCode_genCode0(OP_RET_FREE_LR);

  } else if (CmEnv.annotateL == ANNOTATE_NOTHING &&
             CmEnv.annotateR != ANNOTATE_NOTHING) {
    // FreeL
    if (CmEnv.reg_agentL == VM_OFFSET_ANNOTATE_L) {
      IMCode_genCode0(OP_RET_FREE_L);

    } else {
      IMCode_genCode0(OP_RET_FREE_R);
    }

  } else if (CmEnv.annotateL != ANNOTATE_NOTHING &&
             CmEnv.annotateR == ANNOTATE_NOTHING) {
    // FreeR
    if (CmEnv.reg_agentL == VM_OFFSET_ANNOTATE_L) {
      IMCode_genCode0(OP_RET_FREE_R);

    } else {
      IMCode_genCode0(OP_RET_FREE_L);
    }
  } else {
    IMCode_genCode0(OP_RET);
  }
}

int Compile_eqlist_on_ast_in_rulebody(Ast *at) {
  NB_TYPE type;
  Ast    *at_preserved = at;

  // Occurrence check
  if (CmEnv.put_warning_for_cnct_property) {
    while (at != NULL) {
      Ast *eq = at->left;

      // It does not work for now
      if (!check_invalid_occurrence_as_rule(eq->left) ||
          !check_invalid_occurrence_as_rule(eq->right)) {
        return 0;
      }

      // Check for: x:int~expr
      if (eq->left->id == AST_NAME) {
        int result = CmEnv_gettype_forname(eq->left->left->sym, &type);
        if (result != 0 && type == NB_INTVAR) {
          // the left term is a variable on property

          if (Ast_is_expr(eq->right)) {
            printf("%d:Warning: The variable `%s' is connected to an "
                   "expression. It may cause runtime error.\n",
                   yylineno, eq->left->left->sym);

          } else if (eq->right->id == AST_NAME) {
            result = CmEnv_gettype_forname(eq->right->left->sym, &type);
            if (result != 0 && type == NB_INTVAR) {
              printf("%d:Warning: The variable `%s' is connected to a variable "
                     "%s. It may cause runtime error.\n",
                     yylineno, eq->left->left->sym, eq->right->left->sym);
            }
          }
        }
      }

      // Check for: expr~x:int
      if (eq->right->id == AST_NAME) {
        int result = CmEnv_gettype_forname(eq->right->left->sym, &type);
        if (result != 0 && type == NB_INTVAR) {
          // the right term is a variable on property

          if (Ast_is_expr(eq->left)) {
            printf("%d:Warning: The variable `%s' is connected to an "
                   "expression. It may cause runtime error.\n",
                   yylineno, eq->right->left->sym);

          } else {
            if (eq->left->id == AST_NAME) {
              result = CmEnv_gettype_forname(eq->left->left->sym, &type);
              if (result != 0 && type == NB_INTVAR) {
                printf("%d:Warning: The variable `%s' is connected to a "
                       "variable %s. It may cause runtime error.\n",
                       yylineno, eq->right->left->sym, eq->left->left->sym);
              }
            }
          }
        }
      }

      at = ast_getTail(at);
    }
  }

  at = at_preserved;

  // Reset the counter of compilation errors
  CmEnv.count_compilation_errors = 0;

  while (at != NULL) {
    Ast *eq = at->left;
    Ast *next = ast_getTail(at);

    // #ifndef OPTIMISE_IMCODE_TCO
    if (!CmEnv.tco) {

      // Without Tail Call Recursion Optimisation

      // 2021/9/6: It seems, always EnvAddCodePUSH is selected
      // because at is not NULL in this step. Should be (next == NULL)?
      //
      // 2021/9/12: It is faster
      // when we choose OP_PUSH always in parallel execution,
      // though I am not sure the reason.

      int var1 = Compile_term_on_ast(eq->left, -1);
      int var2 = Compile_term_on_ast(eq->right, -1);

      // Check whether compilation errors arise
      if (CmEnv.count_compilation_errors != 0) {
        return 0;
      }

      /*
        if (next == NULL) {
        IMCode_genCode2(OP_MYPUSH, var1, var2);
        } else {
        IMCode_genCode2(OP_PUSH, var1, var2);
        }
      */

      IMCode_genCode2(OP_PUSH, var1, var2);

      at = next;

    } else {

      // WITH Tail Call Optimisation

      if (next != NULL || (next == NULL && eq->id == AST_CNCT)) {
        int var1 = Compile_term_on_ast(eq->left, -1);
        int var2 = Compile_term_on_ast(eq->right, -1);

        // Check whether compilation errors arise
        if (CmEnv.count_compilation_errors != 0) {
          return 0;
        }

        IMCode_genCode2(OP_PUSH, var1, var2);

      } else {
        // operation for the last placed equation.

        if (eq->id == AST_CNCT_TCO_INTVAR) {
          // rule_left_agent ~ expression

          // It always becomes loop operation
          // because of the form `(*L) ~ expression'.

          int var2 = Compile_term_on_ast(eq->right, -1);
          // Check whether compilation errors arise
          if (CmEnv.count_compilation_errors != 0) {
            return 0;
          }

          int alloc[MAX_PORT];
          int arity = 0;

          // The `rule_left_aget' is annotated by (*L) manually
          // in the function `Ast_make_annotation_TailCallOptimisation'.

          // So, first
          // Pealing (*L)
          Ast *eq_lhs = eq->left;              // (*L)(AGENT(Fib, arglist))
          Ast *deconst_term = eq_lhs->left;    // (AGENT(Fib, arglist))
          Ast *arg_list = deconst_term->right; // arglist

          //		printf("TCO: %s><int\n", deconst_term->left->sym);

          for (int i = 0; i < MAX_PORT; i++) {
            if (arg_list == NULL)
              break;
            arity++;

            //	  ast_puts(arg_list->left); puts("");

            alloc[i] = Compile_term_on_ast(arg_list->left, -1);

            // Check whether compilation errors arise
            if (CmEnv.count_compilation_errors != 0) {
              return 0;
            }

            arg_list = ast_getTail(arg_list);
          }

          for (int i = 0; i < arity; i++) {
            if (alloc[i] == VM_OFFSET_ANNOTATE_R) {
              // VM_OFFSET_ANNOTATE_R should be preserved
              int newreg = CmEnv_newvar();
              IMCode_genCode2(OP_LOAD, VM_OFFSET_ANNOTATE_R, newreg);
              alloc[i] = newreg;
              break;
            }
          }
          IMCode_genCode2(OP_LOAD_META, var2, VM_OFFSET_ANNOTATE_R);

          for (int i = 0; i < arity; i++) {
            for (int j = i + 1; j < arity; j++) {
              if (alloc[j] == VM_OFFSET_METAVAR_L(i)) {
                // VM_OFFSET_METAVAR_L(i) should be preserved
                int newreg = CmEnv_newvar();
                IMCode_genCode2(OP_LOAD, VM_OFFSET_METAVAR_L(i), newreg);
                alloc[j] = newreg;
                break;
              }
            }

            IMCode_genCode2(OP_LOAD_META, alloc[i], VM_OFFSET_METAVAR_L(i));
          }

          IMCode_genCode0(OP_LOOP);

          // prevent putting FREE_L, and ignore counting up for name ref
          CmEnv.annotateL = ANNOTATE_TCO;

          //	IMCode_puts(0); exit(1);

        } else if (eq->id == AST_CNCT_TCO) {
          // rule_left_agent ~ name   where the name is meta_R

          // Pealing (*L)
          Ast *eq_lhs = eq->left;           // (*L)(AGENT(Fib, arglist))
          Ast *deconst_term = eq_lhs->left; // (AGENT(Fib, arglist))

          int eq_rhs_name_reg = CmEnv_find_var(eq->right->left->sym);

          int label1 = CmEnv_get_newlabel();
          if (CmEnv.idR == ID_CONS) {
            // JMPCNCT_CONS reg pc
            IMCode_genCode2(OP_JMPCNCT_CONS, eq_rhs_name_reg, label1);

          } else {
            // JMPCNCT reg id pc
            IMCode_genCode3(OP_JMPCNCT, eq_rhs_name_reg, CmEnv.idR, label1);
          }

#ifdef OPTIMISE_TWO_ADDRESS
          IMCode_genCode0(OP_BEGIN_JMPCNCT_BLOCK);
#endif

          int var1 = Compile_term_on_ast(deconst_term, -1);
          // int var1 = Compile_term_on_ast(eq_lhs, -1);

          // Check whether compilation errors arise
          if (CmEnv.count_compilation_errors != 0) {
            return 0;
          }

          IMCode_genCode2(OP_PUSH, var1, eq_rhs_name_reg);
          Compile_gen_RET_for_rulebody();

          // From now on,
          // prevent putting FREE_L, and ignore counting up for name ref
          CmEnv.annotateL = ANNOTATE_TCO;

#ifdef OPTIMISE_TWO_ADDRESS
          IMCode_genCode0(OP_BEGIN_JMPCNCT_BLOCK);
#endif

          IMCode_genCode1(OP_LABEL, label1);

          Ast *arg_list = deconst_term->right; // arglist
          int  arity = 0;
          int  alloc[MAX_PORT];

          for (int i = 0; i < MAX_PORT; i++) {
            if (arg_list == NULL)
              break;
            arity++;

            alloc[i] = Compile_term_on_ast(arg_list->left, -1);
            // Check whether compilation errors arise
            if (CmEnv.count_compilation_errors != 0) {
              return 0;
            }

            arg_list = ast_getTail(arg_list);
          }

          for (int i = 0; i < arity; i++) {
            if (alloc[i] == VM_OFFSET_METAVAR_L(i))
              continue;

            if (eq_rhs_name_reg == VM_OFFSET_METAVAR_L(i)) {
              // preserve eq_rhs_name_reg to be overwritten
              // because it is used for OP_LOOP_RREC.
              // Ex.
              // MergeCC(ret, int y, ys) >< (int x):xs
              // | _      => ret~(y:cnt), MergeCC(cnt, x, xs) ~ ys;
              // The `ys' is stored in reg(3), but it is overwritten by `xs',
              // though `ys' is required by LOOP_RREC2 `ys' placed later.
              // So, the `ys' must be preserved.

              int newreg = CmEnv_newvar();
              IMCode_genCode2(OP_LOAD, eq_rhs_name_reg, newreg);
              eq_rhs_name_reg = newreg;
            }

            for (int j = i + 1; j < arity; j++) {
              if (alloc[j] == VM_OFFSET_METAVAR_L(i)) {
                // preserve VM_OFFSET_METAVAR_L(i)
                // if it is used later.
                // Ex.
                // LD 3,1
                // LD 1,3  <-- 1 is disappeared.
                // ==>
                // It must be as follows:
                // LD 1, newreg
                // LD 3, 1
                // LD newreg, 3
                //
                // Ex.2  A(a,b,c)>< (int x):xs => A(c,a,b)~xs;
                //

                int newreg = CmEnv_newvar();
                IMCode_genCode2(OP_LOAD, VM_OFFSET_METAVAR_L(i), newreg);
                alloc[j] = newreg;
                break;
              }
            }

            IMCode_genCode2(OP_LOAD_META, alloc[i], VM_OFFSET_METAVAR_L(i));
          }

          int arityR = IdTable_get_arity(CmEnv.idR);
          if (CmEnv.annotateR == ANNOTATE_REUSE) {
            switch (arityR) {
            case 1:
              IMCode_genCode1(OP_LOOP_RREC1, eq_rhs_name_reg);
              break;
            case 2:
              IMCode_genCode1(OP_LOOP_RREC2, eq_rhs_name_reg);
              break;
            default:
              IMCode_genCode2(OP_LOOP_RREC, eq_rhs_name_reg, arityR);
            }
          } else {
            switch (arityR) {
            case 1:
              IMCode_genCode1(OP_LOOP_RREC1_FREE_R, eq_rhs_name_reg);
              break;
            case 2:
              IMCode_genCode1(OP_LOOP_RREC2_FREE_R, eq_rhs_name_reg);
              break;
            default:
              IMCode_genCode2(OP_LOOP_RREC_FREE_R, eq_rhs_name_reg, arityR);
            }
          }

          //		IMCode_puts(0); //exit(1);

        } else {
          // unknown AST id
          puts("Fatal ERROR in Compile_eqlist_on_ast_in_rulebody");
          exit(1);
        }
      }

      at = next;

    } // #endif
  }

  return 1;
}

int Compile_stmlist_on_ast(Ast *at) {
  Ast *ptr;
  int  toRegLeft;

  while (at != NULL) {
    ptr = at->left;

    // (AST_LD (AST_NAME sym NULL) some)
    if (ptr->id != AST_LD) {
      printf("%d:System ERROR: The given StmList contains something besides "
             "statements.",
             yylineno);
      exit(-1);
    }
    // operation of x=y:
    // for the x
    toRegLeft = CmEnv_find_var(ptr->left->left->sym);
    if (toRegLeft == -1) {
      // the sym is new
      toRegLeft = CmEnv_set_as_INTVAR(ptr->left->left->sym);
    } else {
      printf("%d:Warning: `%s' has been already defined.\n", yylineno,
             ptr->left->left->sym);
    }

    // for the y
    if (ptr->right->id == AST_NAME) {
      // y is a name
      int toRegRight = CmEnv_find_var(ptr->right->left->sym);
      if (toRegRight == -1) {
        toRegRight = CmEnv_set_as_INTVAR(ptr->right->left->sym);
      }
      IMCode_genCode2(OP_LOAD, toRegRight, toRegLeft);

    } else if (ptr->right->id == AST_INT) {
      // y is an integer
      IMCode_genCode2(OP_LOADI, ptr->right->longval, toRegLeft);

    } else {
      // y is an expression
      if (!Compile_expr_on_ast(ptr->right, toRegLeft))
        return 0;
    }

    at = ast_getTail(at);
  }

  return 1;
}

int Compile_body_on_ast(Ast *body) {
  Ast *stms, *eqs;

  if (body == NULL)
    return 1;

  stms = body->left;
  eqs = body->right;

  Ast_RewriteOptimisation_eqlist(eqs);

  if (!Compile_stmlist_on_ast(stms))
    return 0;
  if (!Compile_eqlist_on_ast_in_rulebody(eqs)) {
    printf("%d:ERROR: Compilation failure for %s >< %s.\n", yylineno,
           IdTable_get_name(CmEnv.idL), IdTable_get_name(CmEnv.idR));
    return 0;
  }

  return 1;
}

int Compile_rule_mainbody_on_ast(Ast *mainbody) {
  // return 1: success
  //        0: compile error
  //
  //      <if-sentence> ::= (AST_IF guard (AST_BRANCH <then> <else>))
  //                      | <body>
  //      <then> ::= <if-sentence>
  //      <else> ::= <if-sentence>
  //
  //      <body> ::= (AST_BODY stmlist aplist)

  CmEnv_clear_keeping_rule_properties();

  if (mainbody == NULL || mainbody->id == AST_BODY) {
    // Compilation without guards

    Ast *body = mainbody;

    if (!Compile_body_on_ast(body))
      return 0;

    if (!CmEnv_check_linearity_in_rule()) {
      printf("of the rule:\n  %s >< %s.\n", IdTable_get_name(CmEnv.idL),
             IdTable_get_name(CmEnv.idR));
      return 0;
    }

    Compile_gen_RET_for_rulebody();

    // Change MKNAME to MKGNAME if the name occurs just onece
    CmEnv_retrieve_MKGNAME();

    return 1;

  } else {
    // if_sentence

    int label; // jump label

    Ast *if_sentence = mainbody;
    Ast *guard = if_sentence->left;
    Ast *then_branch = if_sentence->right->left;
    Ast *else_branch = if_sentence->right->right;

    //    IMCode_genCode0(OP_BEGIN_BLOCK);

    // Compilation of Guard expressions
    int newreg = CmEnv_newvar();
    if (!Compile_expr_on_ast(guard, newreg))
      return 0;

#ifdef OPTIMISE_IMCODE
    // optimisation for R0
    // OP src1 src2 dest ==> OP_R0 src1 src2
    int                opt = 0;
    struct IMCode_tag *imcode = &IMCode[IMCode_n - 1];
    switch (imcode->opcode) {
    case OP_LT:
      imcode->opcode = OP_LT_R0;
      opt = 1;
      break;

    case OP_LE:
      imcode->opcode = OP_LE_R0;
      opt = 1;
      break;

    case OP_EQ:
      imcode->opcode = OP_EQ_R0;
      opt = 1;
      break;

    case OP_EQI:
      imcode->opcode = OP_EQI_R0;
      opt = 1;
      break;

    case OP_NE:
      imcode->opcode = OP_NE_R0;
      opt = 1;
    }
#endif

    // CmEnv_retrieve_MKGNAME();  // <- no need for guard expressions

    // Generate OP_JMPEQ0 for VM
    label = CmEnv_get_newlabel();
#ifdef OPTIMISE_IMCODE
    if (opt != 1) {
      IMCode_genCode2(OP_JMPEQ0, newreg, label);

    } else {
      IMCode_genCode1(OP_JMPEQ0_R0, label);
    }
#else
    IMCode_genCode2(OP_JMPEQ0, newreg, label);
#endif

    // Compilation of then_branch
    IMCode_genCode0(OP_BEGIN_BLOCK);
    CmEnv_clear_localnamePtr(); // 局所変数の割り当て番号を初期化
    if (!Compile_rule_mainbody_on_ast(then_branch))
      return 0;

    // Compilation of else_branch
    IMCode_genCode1(OP_LABEL, label);
    IMCode_genCode0(OP_BEGIN_BLOCK);

    CmEnv_clear_localnamePtr(); // 局所変数の割り当て番号を初期化
    if (!Compile_rule_mainbody_on_ast(else_branch))
      return 0;

    return 1;
  }
}

int check_ast_arity(Ast *ast) {
  int   i;
  Ast  *ptr;
  char *sym;

  if (ast == NULL) {
    return 1;
  }

  if (ast->id == AST_AGENT) {
    ptr = ast->right;
    sym = ast->left->sym;

    for (i = 0; i < MAX_PORT; i++) {
      if (ptr == NULL) {
        return 1;
      }
      if (!check_ast_arity(ptr->left)) {
        return 0;
      }
      ptr = ast_getTail(ptr);
    }

    if (ptr != NULL) {
      printf("Error!: `%s' has too many arguments. It should be MAX_PORT(=%d) "
             "or less.\n",
             sym, MAX_PORT);
      return 0;
    }

  } else if (ast->id == AST_TUPLE) {
    // We support upto 5-tuples.
    if (ast->intval <= MAX_PORT && ast->intval <= 5) {
      return 1;
    } else {
      if (MAX_PORT <= 5) {
        printf("Error: A tuple has too many arguments. It should be "
               "MAX_PORT(=%d) or less.\n",
               MAX_PORT);
      } else {
        printf(
            "Error: A tuple has too many arguments. It should be 5 or less.\n");
      }
      return 0;
    }
  }

  return 1;
}

// MAIN ---------------------------

int main(int argc, char *argv[]) {
  int   i, param;
  char *fname = NULL;
  int   max_EQStack = 1 << 8; // 512
  bool  retrieve_flag = true; // 1: retrieve to interpreter even if error occurs

#if !defined(EXPANDABLE_HEAP) && !defined(FLEX_EXPANDABLE_HEAP)
  // v0.5.6
  unsigned int heap_size = 100000;
#endif

#ifdef MY_YYLINENO
  InfoLineno_Init();
#endif

  // Pritty printing for local variables
#ifdef PRETTY_VAR
  Pretty_init();
#endif

#ifndef THREAD
  Init_WHNFinfo();
#endif

  ast_heapInit();

#ifdef THREAD
  MaxThreadsNum = sysconf(_SC_NPROCESSORS_CONF);
#endif

  for (i = 1; i < argc; i++) {
    if (*argv[i] == '-') {
      switch (*(argv[i] + 1)) {
      case 'v':
        printf("Inpla version %s\n", VERSION);
        exit(-1);
        break;

      case '-':
      case 'h':
      case '?':
        printf("Inpla version %s\n", VERSION);
        puts("Usage: inpla [options]\n");
        puts("Options:");
        printf(" -f <filename>    Set input file name                     "
               "(Default:      STDIN)\n");
        printf(" -d <Name>=<val>  Bind <val> to <Name>\n");

#ifdef EXPANDABLE_HEAP
#elif defined(FLEX_EXPANDABLE_HEAP)
        printf(" -Xms <num>       Set initial heap size to 2^<num>        "
               "(Defalut: %2u (=%4u))\n",
               Hoop_init_size, 1 << Hoop_init_size);
        printf(" -Xmt <num>       Set multiple heap increment to 2^<num>  "
               "(Defalut: %2u (=%4u))\n",
               Hoop_increasing_magnitude, 1 << Hoop_increasing_magnitude);
        printf("                    0: the same (=2^0) size heap is inserted "
               "when it runs up.\n");
        printf("                    1: the heap size is twice (=2^1).\n");
        printf("                    2: the size is four times (=2^2).\n");
#else
        // v0.5.6
        printf(" -m <num>         Set size of heaps                       "
               "(Defalut: %10u)\n",
               heap_size);
#endif

        printf(" -Xes <num>       Set initial equation stack size         "
               "(Default: %10u)\n",
               max_EQStack);

#ifdef THREAD
        printf(" -t <num>         Set the number of threads               "
               "(Default: %10d)\n",
               MaxThreadsNum);

#else
        printf(" -w               Enable Weak Reduction strategy          "
               "(Default:    disable)\n");
#endif

        printf(" -c               Enable output of compiled codes         "
               "(Default:    disable)\n");

        printf(" -h               Print this help message\n");

        printf(" -foptimise-tail-calls   Enable tail call optimisation    "
               "(Default:    disable)\n");

#ifndef THREAD
        printf(" -fverbose-memory-usage  Show memory usage                "
               "(Default:    disable)\n");
#endif

        puts("");

        exit(-1);
        break;

      case 'X':
        if (!strcmp(argv[i], "-Xes")) {
          i++;
          if (i < argc) {
            param = atoi(argv[i]);
            if (param == 0) {
              printf("ERROR: `%s' is illegal parameter for -Xes\n", argv[i]);
              exit(-1);
            }
          } else {
            printf("ERROR: The option `-Xes' needs a natural number.");
            exit(-1);
          }
          max_EQStack = param;
        }

#ifdef FLEX_EXPANDABLE_HEAP
        else if (!strcmp(argv[i], "-Xms")) {
          i++;
          if (i < argc) {
            int   valid = 1;
            char *val = argv[i];

            for (int idx = 0; idx < strlen(val); idx++) {
              if (val[idx] < '0' || val[idx] > '9') {
                valid = 0;
                break;
              }
            }
            if (!valid) {
              printf("ERROR: `%s' is illegal parameter for -Xms\n", argv[i]);
              exit(-1);
            }

            param = atoi(argv[i]);
            if (param == 0) {
              printf("ERROR: `%s' is illegal parameter for -Xms\n", argv[i]);
              exit(-1);
            }
          } else {
            printf("ERROR: The option `-Xms' needs a number.");
            exit(-1);
          }
          Hoop_init_size = param;

        } else if (!strcmp(argv[i], "-Xmt")) {
          i++;
          if (i < argc) {

            int   valid = 1;
            char *val = argv[i];

            for (int idx = 0; idx < strlen(val); idx++) {
              if (val[idx] < '0' || val[idx] > '9') {
                valid = 0;
                break;
              }
            }
            if (!valid) {
              printf("ERROR: `%s' is illegal parameter for -Xmt\n", argv[i]);
              exit(-1);
            }

          } else {
            printf("ERROR: The option `-Xmt' needs a number.");
            exit(-1);
          }
          param = atoi(argv[i]);
          Hoop_increasing_magnitude = param;
        }
#endif
        break;

      case 'd':
        i++;
        if (i < argc) {
          char  varname[100], val[100];
          char *tp;

          tp = strtok(argv[i], "=");

          // parsing for an identifier
          int len = snprintf(varname, sizeof(varname) - 1, "%s", tp);
          if (len == 0 || varname[0] < 'A' || varname[0] > 'Z') {
            puts("ERROR: `id' in the format `id=value' must start from a "
                 "capital letter.");
            exit(-1);
          }

          tp = strtok(NULL, "=");

          // parsing for a number
          len = snprintf(val, sizeof(val) - 1, "%s", tp);
          if (len == 0) {
            puts("ERROR: `value' in the format `id=value' must an integer "
                 "value.");
            exit(-1);
          }

          int offset = 0;
          if (val[0] == '-') {
            offset = 1;
          }
          int valid = 1;
          for (int idx = offset; idx < strlen(val); idx++) {
            if (val[idx] < '0' || val[idx] > '9') {
              valid = 0;
              break;
            }
          }

          if (!valid) {
            puts("ERROR: `value' in the format `id=value' must an integer "
                 "value.");
            exit(-1);
          }

          ast_recordConst(varname, atoi(val));

        } else {
          puts("ERROR: The option `-d' needs a string such as VarName=value.");
          exit(-1);
        }
        break;

      case 'f':

        // flags
        if (!strcmp(argv[i], "-foptimise-tail-calls")) {
          CmEnv.tco = 1;
          break;
        }

#ifndef THREAD
        if (!strcmp(argv[i], "-fverbose-memory-usage")) {
          GlobalOptions.verbose_memory_use = 1;
          break;
        }
#endif

        // for files
        if (strcmp(argv[i], "-f") != 0) {
          printf("ERROR: Unknown option: `%s'\n", argv[i]);
          exit(-1);
        }

        i++;
        if (i < argc) {
          fname = argv[i];
          retrieve_flag = false;
        } else {
          printf(
              "ERROR: The option `-f' needs a string of an input file name.");
          exit(-1);
        }
        break;

#if !defined(EXPANDABLE_HEAP) && !defined(FLEX_EXPANDABLE_HEAP)
      case 'm':
        // v0.5.6
        i++;
        if (i < argc) {
          param = atoi(argv[i]);
          if (param == 0) {
            printf("ERROR: `%s' is illegal parameter for -m\n", argv[i]);
            exit(-1);
          }
        } else {
          printf("ERROR: The option `-m' needs a number as an argument.");
          exit(-1);
        }
        heap_size = param;
        break;
#endif

#ifdef THREAD
      case 't':
        i++;
        if (i < argc) {
          param = atoi(argv[i]);
          if (param == 0) {
            printf("ERROR: `%s' is illegal parameter for -t\n", argv[i]);
            exit(-1);
          }
        } else {
          printf("ERROR: The option `-t' needs a number of threads.");
          exit(-1);
        }

        MaxThreadsNum = param;
        break;
#else
      case 'w':
        WHNFinfo.enable = 1;
        break;
#endif

      case 'c':
        CmEnv.put_compiled_codes = 1;
        break;

      default:
        printf("ERROR: Unrecognized option %s\n", argv[i]);
        printf("Use -h option for getting more information.\n\n");
        exit(-1);
      }
    } else {
      printf("ERROR: Unrecognized option %s\n", argv[i]);
      printf("Use -h option for getting more information.\n\n");
      exit(-1);
    }
  }

  // input file source
  if (fname == NULL) {
    yyin = stdin;

  } else {
    if (!(yyin = fopen(fname, "r"))) {

      char *fname_in = malloc(sizeof(char *) * 256);
      snprintf(fname_in, 256, "%s.in", fname);
      if (!(yyin = fopen(fname_in, "r"))) {
        printf("Error: The file `%s' cannot be opened.\n", fname);
        exit(-1);
      }

      free(fname_in);
    }
  }

#ifndef THREAD
  if (WHNFinfo.enable) {
    printf(
        "Inpla %s (Weak Strategy) : Interaction nets as a programming language",
        VERSION);
    printf(" [%s]\n", BUILT_DATE);
  } else {
    printf("Inpla %s : Interaction nets as a programming language", VERSION);
    printf(" [built: %s]\n", BUILT_DATE);
  }
#else
  printf("Inpla %s : Interaction nets as a programming language", VERSION);
  printf(" [built: %s]\n", BUILT_DATE);
#endif

#ifdef EXPANDABLE_HEAP
#elif defined(FLEX_EXPANDABLE_HEAP)
  Hoop_init_size = 1 << Hoop_init_size;
  Hoop_increasing_magnitude = 1 << Hoop_increasing_magnitude;

#else
  // v0.5.6
  heap_size = heap_size / MaxThreadsNum;
#endif

  /*
  // check parameters
  printf("Hoop_init_size=%d\n", Hoop_init_size);
  printf("Hoop_increasing_magnitude=%d\n", Hoop_increasing_magnitude);
  printf("max_EQStack=%d\n", max_EQStack);
  exit(1);
  */

  IdTable_init();
  NameTable_init();
  RuleTable_init();
  CodeAddr_init();

#ifdef THREAD
  GlobalEQStack_Init(MaxThreadsNum * 8);
#endif

#if defined(EXPANDABLE_HEAP) || defined(FLEX_EXPANDABLE_HEAP)

#  ifndef THREAD
  VM_Init(&VM, max_EQStack);
#  else
  tpool_init(max_EQStack);
#  endif

#else
  // v0.5.6

#  ifndef THREAD
  VM_Init(&VM, heap_size, max_EQStack);
#  else
  tpool_init(heap_size, max_EQStack);
#  endif

#endif

#ifdef THREAD
  // if some threads invoked by the initialise are still working,
  // wait until these all sleep.
  if (SleepingThreadsNum < MaxThreadsNum) {
    pthread_mutex_lock(&AllSleep_lock);
    pthread_cond_wait(&ActiveThread_all_sleep, &AllSleep_lock);
    pthread_mutex_unlock(&AllSleep_lock);
  }
#endif

  linenoiseHistoryLoad(".inpla.history.txt");

  // the main loop of parsing and execution
  while (1) {

    // When errors occur during parsing
    if (yyparse() != 0) {

      if (!retrieve_flag) {
        exit(0);
      }

      if (yyin != stdin) {
        fclose(yyin);
        while (yyin != stdin) {
          popFP();
        }
#ifdef MY_YYLINENO
        InfoLineno_AllDestroy();
#endif
      }
    }
  }

  exit(0);
}

void puts_Names_ast(Ast *ast) {
  Ast *param = ast;

  const int preserve = PutIndirection;
  PutIndirection = 0;

  while (param != NULL) {
    char *sym = param->left->sym;
    int   sym_id = NameTable_get_id(sym);
    if (IS_GNAMEID(sym_id)) {
      print_name_port0(IdTable_get_heap(sym_id));
    } else {
      print_name_port0((VALUE)NULL);
    }

    param = ast_getTail(param);
    if (param != NULL)
      printf(" ");
  }
  puts("");

  PutIndirection = preserve;
}

// Checks for variable occurrences in equations in rules.
// When this function is called, meta names have been stored in CmEnv.

// Japanese:
// rule の中の eqs をコンパイルするときに使う
// この関数が呼ばれる前に、rule agents の meta names は
// Environment に積まれている。
int check_invalid_occurrence_as_rule(Ast *ast) {
  NB_TYPE type;

  if (ast->id == AST_NAME) {

    if (CmEnv_gettype_forname(ast->left->sym, &type)) {
      /*
      if (type == NB_INTVAR) {
        printf("ERROR: The variable '%s' for an integer cannot be used as an
      agent ", ast->left->sym); return 0;
      }
      */
    } else {
      //      printf("ERROR: The name '%s' is not bound ", ast->left->sym);
      //      return 0;
    }
  } else if (ast->id == AST_INT) {
    //    printf("ERROR: The integer '%d' is used as an agent ",
    //	   ast->intval);
    //    return 0;
  }

  return 1;
}

int check_invalid_occurrence(Ast *ast) {
  // communication with global names
  int count;

  if (ast->id == AST_NAME) {
    int sym_id = NameTable_get_id(ast->left->sym);

    if (IS_GNAMEID(sym_id)) {
      // already exists as a global
      VALUE aheap = IdTable_get_heap(sym_id);

      /*
      // it is connected with something.
      if (NAME(aheap)->port != (VALUE)NULL) {
        printf("ERROR: '%s' is already connected with ", sym);
        puts_term(NAME(aheap)->port);
        puts(".");
        return 1;
      }
      */

      count = keynode_exists_in_another_term(aheap, NULL);
      if (count == 2) {
        // already twice
        printf("%d:ERROR: `%s' occurs twice already. \n", yylineno,
               ast->left->sym);
        printf("Use `ifce' command to see the occurrence.\n");
        return 0;
      }
    }
  }

  return 1;
}

// -------------------------------------------------------------
// Make Rules
// -------------------------------------------------------------

int get_arity_on_ast(Ast *ast) {
  int  i;
  Ast *ptr;

  ptr = ast->right;
  for (i = 0; i < MAX_PORT; i++) {
    if (ptr == NULL)
      break;
    ptr = ast_getTail(ptr);
  }
  return i;
}

// setMetaL(Ast *ast)
// A(x1,x2) が ast に与えられれば、rule 内で x1, x2 が
// VM のレジスタである VM_OFFSET_METAVAR_L(0)番、VM_OFFSET_METAVAR_L(1)番を
// 参照できるようにする。
int set_metaL(Ast *ast) {
  Ast    *ptr;
  NB_TYPE type;

  ptr = ast->right;
  for (int i = 0; i < MAX_PORT; i++) {
    if (ptr == NULL)
      break;

    if (ptr->left->id == AST_NAME) {
      type = NB_META_L;
    } else if (ptr->left->id == AST_INTVAR) {
      type = NB_INTVAR;
    } else if (ptr->left->id == AST_AGENT) {
      printf("Error: `%s' isn't a name in the left-hand side of this rule "
             "active pair.\n",
             ptr->left->left->sym);
      return 0;
    } else {
      printf("Error: Something wrong at the argument %d in the left-hand side "
             "of this rule active pair. \n",
             i + 1);
      ast_puts(ptr->left);
      puts("");
      return 0;
    }

    CmEnv_set_symbol_as_meta(ptr->left->left->sym, VM_OFFSET_METAVAR_L(i),
                             type);

    ptr = ast_getTail(ptr);
  }
  return 1;
}

int set_metaR(Ast *ast) {
  Ast    *ptr;
  NB_TYPE type;

  ptr = ast->right;
  for (int i = 0; i < MAX_PORT; i++) {
    if (ptr == NULL)
      break;

    if (ptr->left->id == AST_NAME) {
      type = NB_META_R;
    } else if (ptr->left->id == AST_INTVAR) {
      type = NB_INTVAR;
    } else if (ptr->left->id == AST_AGENT) {
      printf("Error: `%s' isn't a name in the right-hand side of this rule "
             "active pair.\n",
             ptr->left->left->sym);
      return 0;
    } else {
      printf("Error: Something wrong at the argument %d in the right-hand side "
             "of this rule active pair. \n",
             i + 1);
      ast_puts(ptr->left);
      puts("");
      return 0;
    }

    CmEnv_set_symbol_as_meta(ptr->left->left->sym, VM_OFFSET_METAVAR_R(i),
                             type);
    ptr = ast_getTail(ptr);
  }

  return 1;
}

void set_metaL_as_IntName(Ast *ast) {
  CmEnv_set_symbol_as_meta(ast->left->sym, VM_OFFSET_ANNOTATE_L, NB_INTVAR);
}

void set_metaR_as_IntName(Ast *ast) {
  CmEnv_set_symbol_as_meta(ast->left->sym, VM_OFFSET_ANNOTATE_R, NB_INTVAR);
}

void set_metaL_as_AnyAgent(Ast *ast) {
  CmEnv_set_symbol_as_meta(ast->left->sym, VM_OFFSET_ANNOTATE_L, NB_WILDCARD);
}

void set_metaR_as_AnyAgent(Ast *ast) {
  CmEnv_set_symbol_as_meta(ast->left->sym, VM_OFFSET_ANNOTATE_R, NB_WILDCARD);
}

void set_annotation_LR(int left, int right) {
  CmEnv.reg_agentL = left;
  CmEnv.reg_agentR = right;
}

// Compilation functions ------------------------------------------
int Compile_expr_on_ast(Ast *ptr, int target) {

  if (ptr == NULL) {
    return 1;
  }
  switch (ptr->id) {
  case AST_INT:
    IMCode_genCode2(OP_LOADI, ptr->longval, target);
    return 1;
    break;

  case AST_NAME: {
    int result = CmEnv_find_var(ptr->left->sym);
    if (result == -1) {
      //      result=CmEnv_set_as_INTVAR(ptr->left->sym);

      // Increase the counter of compilation errors
      CmEnv.count_compilation_errors++;

      printf("%d:ERROR: `%s' is referred to as a property variable in an "
             "expression, although it has not yet been declared (as so).\n",
             yylineno, ptr->left->sym);
      return 0;
    }

    // Check for x:int
    if (CmEnv.put_warning_for_cnct_property) {
      NB_TYPE type;
      int     get_result;
      get_result = CmEnv_gettype_forname(ptr->left->sym, &type);
      if (get_result && type != NB_INTVAR) {
        printf("%d:Warning: `%s' is used as a property variable, although is "
               "is not declared as so.\n",
               yylineno, ptr->left->sym);
      }
    }

    IMCode_genCode2(OP_LOAD, result, target);
    return 1;
    break;
  }
  case AST_RAND: {
    int newreg = CmEnv_newvar();
    if (!Compile_expr_on_ast(ptr->left, newreg))
      return 0;

    IMCode_genCode2(OP_RAND, newreg, target);
    return 1;
    break;
  }
  case AST_UNM: {
    int newreg = CmEnv_newvar();
    if (!Compile_expr_on_ast(ptr->left, newreg))
      return 0;

    IMCode_genCode2(OP_UNM, newreg, target);
    return 1;
    break;
  }
  case AST_PLUS:
  case AST_SUB:
  case AST_MUL:
  case AST_DIV:
  case AST_MOD:
  case AST_LT:
  case AST_LE:
  case AST_EQ:
  case AST_NE: {
    int newreg = CmEnv_newvar();
    int newreg2 = CmEnv_newvar();
    if (!Compile_expr_on_ast(ptr->left, newreg))
      return 0;
    if (!Compile_expr_on_ast(ptr->right, newreg2))
      return 0;

    int opcode;
    switch (ptr->id) {
    case AST_PLUS:
      opcode = OP_ADD;
      break;
    case AST_SUB:
      opcode = OP_SUB;
      break;
    case AST_MUL:
      opcode = OP_MUL;
      break;
    case AST_DIV:
      opcode = OP_DIV;
      break;
    case AST_MOD:
      opcode = OP_MOD;
      break;
    case AST_LT:
      opcode = OP_LT;
      break;
    case AST_LE:
      opcode = OP_LE;
      break;
    case AST_EQ:
      opcode = OP_EQ;
      break;
    case AST_NE:
      opcode = OP_NE;
      break;
    default:
      opcode = OP_MOD;
    }

    IMCode_genCode3(opcode, newreg, newreg2, target);
    return 1;
    break;
  }
  case AST_AND: {
    // compilation of expr1 && expr2

    // if (expr1 == 0) goto L1;
    // if (expr2 == 0) goto L1;
    // target = 1;
    // goto L2;
    // L1:
    // target = 0;
    // L2;

    int newreg = CmEnv_newvar();
    if (!Compile_expr_on_ast(ptr->left, newreg))
      return 0;

    int label1 = CmEnv_get_newlabel();
    IMCode_genCode2(OP_JMPEQ0, newreg, label1);

    int newreg2 = CmEnv_newvar();
    if (!Compile_expr_on_ast(ptr->right, newreg2))
      return 0;
    IMCode_genCode2(OP_JMPEQ0, newreg2, label1);

    IMCode_genCode2(OP_LOADI_SHARED, 1, target);

    int label2 = CmEnv_get_newlabel();
    IMCode_genCode1(OP_JMP, label2);

    IMCode_genCode1(OP_LABEL, label1);
    IMCode_genCode2(OP_LOADI_SHARED, 0, target);

    IMCode_genCode1(OP_LABEL, label2);

    return 1;
    break;
  }
  case AST_OR: {
    // compilation of expr1 || expr2

    // calc expr1
    // if (expr1 != 0) goto L1;
    // if (expr2 != 0) goto L1;
    // target = 0;
    // goto L2;
    // L1:
    // target = 1;
    // L2;

    int newreg = CmEnv_newvar();
    if (!Compile_expr_on_ast(ptr->left, newreg))
      return 0;

    int label1 = CmEnv_get_newlabel();
    IMCode_genCode2(OP_JMPNEQ0, newreg, label1);

    int newreg2 = CmEnv_newvar();
    if (!Compile_expr_on_ast(ptr->right, newreg2))
      return 0;
    IMCode_genCode2(OP_JMPNEQ0, newreg2, label1);

    IMCode_genCode2(OP_LOADI_SHARED, 0, target);

    int label2 = CmEnv_get_newlabel();
    IMCode_genCode1(OP_JMP, label2);

    IMCode_genCode1(OP_LABEL, label1);
    IMCode_genCode2(OP_LOADI_SHARED, 1, target);

    IMCode_genCode1(OP_LABEL, label2);

    return 1;
    break;
  }
  case AST_NOT: {
    // compilation of !(expr1)
    // if (expr1 == 0) goto L1;
    // target = 0;
    // goto L2;
    // L1:
    // target = 1;
    // L2;

    int newreg = CmEnv_newvar();
    if (!Compile_expr_on_ast(ptr->left, newreg))
      return 0;

    int label1 = CmEnv_get_newlabel();
    IMCode_genCode2(OP_JMPEQ0, newreg, label1);

    IMCode_genCode2(OP_LOADI, 0, target);

    int label2 = CmEnv_get_newlabel();
    IMCode_genCode1(OP_JMP, label2);

    IMCode_genCode1(OP_LABEL, label1);
    IMCode_genCode2(OP_LOADI, 1, target);

    IMCode_genCode1(OP_LABEL, label2);

    return 1;
    break;
  }

  default:
    printf("%d:ERROR: Wrong AST was given to CompileExpr.\n\n", yylineno);
    return 0;
  }
}

int Compile_term_on_ast(Ast *ptr, int target) {
  // input:
  // target == -1  => a new node is allocated from localHeap.
  //   otherwise   => a node specified by the `target' is reused as a new node.
  //
  // output:
  // return: offset in localHeap

  int result, mkagent;
  int i, arity;

  int alloc;

  if (ptr == NULL) {
    return -1;
  }

  switch (ptr->id) {
  case AST_NAME:

    result = CmEnv_find_var(ptr->left->sym);
    if (result == -1) {
      result = CmEnv_set_symbol_as_name(ptr->left->sym);
      IMCode_genCode1(OP_MKNAME, result);
    }
    return result;
    break;

  case AST_INT:
    result = CmEnv_newvar();
    IMCode_genCode2(OP_LOADI, ptr->longval, result);
    return result;
    break;

  case AST_NIL:
    if (target == -1) {
      result = CmEnv_newvar();
      mkagent = OP_MKAGENT;
      IMCode_genCode2(mkagent, ID_NIL, result);
    } else {
      result = target;

      if (target == VM_OFFSET_ANNOTATE_L) {
        if (CmEnv.idL != ID_NIL) {
          IMCode_genCode1(OP_CHID_L, ID_NIL);
        }
      } else {
        if (CmEnv.idR != ID_NIL) {
          IMCode_genCode1(OP_CHID_R, ID_NIL);
        }
      }
    }

    return result;
    break;

  case AST_OPCONS:
    ptr = ptr->right;

    /* A register of the element
    alloc[0] = Compile_term_on_ast(ptr->left, -1);
    */

    if (target == -1) {
      result = CmEnv_newvar();

      // expanded operations
      IMCode_genCode2(OP_MKAGENT, ID_CONS, result);

      alloc = Compile_term_on_ast(ptr->left, -1);
      IMCode_genCode3(OP_LOADP, alloc, 0, result);

      alloc = Compile_term_on_ast(ptr->right->left, -1);
      IMCode_genCode3(OP_LOADP, alloc, 1, result);

    } else {
      result = target;

      if (target == VM_OFFSET_ANNOTATE_L) {
        if (CmEnv.idL != ID_CONS) {
          IMCode_genCode1(OP_CHID_L, ID_CONS);
        }
        for (i = 0; i < 2; i++) {
          alloc = Compile_term_on_ast(ptr->left, -1);
          ptr = ast_getTail(ptr);

          if (alloc != VM_OFFSET_METAVAR_L(i)) {
            IMCode_genCode2(OP_LOADP_L, alloc, i);
          }
        }
      } else {
        if (CmEnv.idR != ID_CONS) {
          IMCode_genCode1(OP_CHID_R, ID_CONS);
        }
        for (i = 0; i < 2; i++) {
          alloc = Compile_term_on_ast(ptr->left, -1);
          ptr = ast_getTail(ptr);

          if (alloc != VM_OFFSET_METAVAR_R(i)) {
            IMCode_genCode2(OP_LOADP_R, alloc, i);
          }
        }
      }
    }

    return result;
    break;

  case AST_TUPLE:
    arity = ptr->intval;

    if (arity == 1) {
      // The case of the single tuple such as `(A)'.
      // The `A' is recognised as not an argument, but as a first-class object.
      ptr = ptr->right;
      alloc = Compile_term_on_ast(ptr->left, target);
      result = alloc;

    } else {

      ptr = ptr->right;

      // normal case
      if (target == -1) {
        result = CmEnv_newvar();

        IMCode_genCode2(OP_MKAGENT, GET_TUPLEID(arity), result);

        for (i = 0; i < arity; i++) {
          alloc = Compile_term_on_ast(ptr->left, -1);
          ptr = ast_getTail(ptr);
          IMCode_genCode3(OP_LOADP, alloc, i, result);
        }

      } else {

        result = target;

        if (target == VM_OFFSET_ANNOTATE_L) {
          if (CmEnv.idL != GET_TUPLEID(arity)) {
            IMCode_genCode1(OP_CHID_L, GET_TUPLEID(arity));
          }
          for (i = 0; i < arity; i++) {
            alloc = Compile_term_on_ast(ptr->left, -1);
            ptr = ast_getTail(ptr);

            if (alloc != VM_OFFSET_METAVAR_L(i)) {
              IMCode_genCode2(OP_LOADP_L, alloc, i);
            }
          }

        } else {
          if (CmEnv.idR != GET_TUPLEID(arity)) {
            IMCode_genCode1(OP_CHID_R, GET_TUPLEID(arity));
          }
          for (i = 0; i < arity; i++) {
            alloc = Compile_term_on_ast(ptr->left, -1);
            ptr = ast_getTail(ptr);

            if (alloc != VM_OFFSET_METAVAR_R(i)) {
              IMCode_genCode2(OP_LOADP_R, alloc, i);
            }
          }
        }
      }
    }

    return result;
    break;

  case AST_PERCENT:
    result = CmEnv_newvar();

    {
      int id = IdTable_getid_builtin_funcAgent(ptr);

      if (id == -1) {
        char *sym = (char *)ptr->left->sym;
        id = NameTable_get_id(sym);
        if (id == -1) {
          printf("%d:Warning: `%s' is given to %%, although it hasn't yet been "
                 "defined.\n",
                 yylineno, sym);
          id = NameTable_get_set_id_with_IdTable_forAgent(sym);
        }
      }
      //      printf("id=%d\n", id);
      alloc = CmEnv_newvar();
      IMCode_genCode2(OP_LOADI, id, alloc);
      //
      IMCode_genCode2(OP_MKAGENT, ID_PERCENT, result);
      IMCode_genCode3(OP_LOADP, alloc, 0, result);
    }
    return result;
    break;

  case AST_AGENT: {
    int id = IdTable_getid_builtin_funcAgent(ptr);

    if (id == -1) {
      id = NameTable_get_set_id_with_IdTable_forAgent((char *)ptr->left->sym);
    }

    // Get the arity
    arity = 0;
    {
      Ast *arg = ptr->right;
      for (i = 0; i < MAX_PORT; i++) {
        if (arg == NULL)
          break;
        arity++;
        arg = ast_getTail(arg);
      }
    }

    /*
      ptr=ptr->right;
      for (i=0; i<MAX_PORT; i++) {
      if (ptr == NULL) break;
      alloc[i] = Compile_term_on_ast(ptr->left, -1);
      arity++;
      ptr = ast_getTail(ptr);
      }
    */

    IdTable_set_arity(id, arity);

    ptr = ptr->right;

    if (target == -1) {
      result = CmEnv_newvar();

      IMCode_genCode2(OP_MKAGENT, id, result);

      for (i = 0; i < arity; i++) {
        alloc = Compile_term_on_ast(ptr->left, -1);
        ptr = ast_getTail(ptr);

        IMCode_genCode3(OP_LOADP, alloc, i, result);
      }
    } else {

      result = target;

      if (target == VM_OFFSET_ANNOTATE_L) {
        if (CmEnv.idL != id) {
          IMCode_genCode1(OP_CHID_L, id);
        }
        for (i = 0; i < arity; i++) {
          alloc = Compile_term_on_ast(ptr->left, -1);
          ptr = ast_getTail(ptr);

          if (alloc != VM_OFFSET_METAVAR_L(i)) {
            IMCode_genCode2(OP_LOADP_L, alloc, i);
          }
        }

      } else {

        if (CmEnv.idR != id) {
          IMCode_genCode1(OP_CHID_R, id);
        }
        for (i = 0; i < arity; i++) {
          alloc = Compile_term_on_ast(ptr->left, -1);
          ptr = ast_getTail(ptr);

          if (alloc != VM_OFFSET_METAVAR_R(i)) {
            IMCode_genCode2(OP_LOADP_R, alloc, i);
          }
        }
      }
    }

    return result;
  } break;

  case AST_ANNOTATION_L:
  case AST_ANNOTATION_R:
    if (ptr->id == AST_ANNOTATION_L) {
      CmEnv.annotateL = ANNOTATE_REUSE; // turn *L_occurrence flag on
      result = CmEnv.reg_agentL;        // VM_OFFSET_ANNOTATE_L;
    } else {
      CmEnv.annotateR = ANNOTATE_REUSE; // turn *R_occurrence flag on
      result = CmEnv.reg_agentR;        // VM_OFFSET_ANNOTATE_R;
    }

    result = Compile_term_on_ast(ptr->left, result);
    return result;
    break;

  default:
    // expression case
    result = CmEnv_newvar();

#ifndef DEBUG_EXPR_COMPILE_ERROR
    Compile_expr_on_ast(ptr, result);
#else
    int compile_expr_result = Compile_expr_on_ast(ptr, result);
    if (compile_expr_result == 0) {
      printf("%d:ERROR: Something strange in Compile_term_on_ast.\n", yylineno);
      ast_puts(ptr);
      puts("");
      //      exit(1);
    }
#endif

    return result;
  }
}

void Ast_remove_tuple1_in_mainbody(Ast *mainbody) {

  if (mainbody == NULL)
    return;

  if (mainbody->id == AST_BODY) {
    Ast *body = mainbody;
    Ast *eqs = body->right;

    while (eqs != NULL) {
      Ast *eq = eqs->left;
      eq->left = ast_remove_tuple1(eq->left);
      eq->right = ast_remove_tuple1(eq->right);

      eqs = ast_getTail(eqs);
    }

  } else {
    Ast *if_sentence = mainbody;
    Ast *then_branch = if_sentence->right->left;
    Ast *else_branch = if_sentence->right->right;

    Ast_remove_tuple1_in_mainbody(then_branch);
    Ast_remove_tuple1_in_mainbody(else_branch);
  }
}

void Ast_undo_TCO_annotation(Ast *mainbody) {
  // Make any kinds of CNCTs changed into just AST_CNCT
  // in order to give it to compilation procedures safely.

  if (mainbody == NULL)
    return;

  if (mainbody->id == AST_BODY) {
    Ast *body = mainbody;
    Ast *eqs = body->right;

    if (eqs == NULL) {
      return;
    }

    // Move to the last placed equation
    while (ast_getTail(eqs) != NULL) {
      eqs = ast_getTail(eqs);
    }

    Ast *eq = eqs->left;
    if (eq->id != AST_CNCT) {
      // Undo is required.
      eq->id = AST_CNCT;

      // (AST_ANNOTATE Foo NULL) => Foo
      eq->left = eq->left->left;
    }

  } else {
    Ast *if_sentence = mainbody;
    Ast *then_branch = if_sentence->right->left;
    Ast *else_branch = if_sentence->right->right;

    Ast_undo_TCO_annotation(then_branch);
    Ast_undo_TCO_annotation(else_branch);
  }
}

int Ast_make_annotation_TailCallOptimisation(Ast *mainbody) {
  // Return 1 when annotated.
  //
  // When there is an equation for TCO, its ID is changed from AST_CNCT into
  // AST_CNCT_TCO_INTVAR or AST_CNCT_TCO.

  if (mainbody == NULL) {
    return 0;
  }

  if (mainbody->id == AST_BODY) {
    Ast *body = mainbody;
    Ast *eqs = body->right;

    if (eqs == NULL) {
      return 0;
    }

    // DEBUG: 13 Apr 2024
    // Improvement plan:
    // To check the syntax, the compilation should be done first.
    // Next, if the rewriting deals with at least one ap pair,
    // the compilation should be redo.
    // But, this rewriting optimisation is really needed?
    // This must be required for rules, but this is a top level program,
    // and it is executed just one time...
    //
    //    Ast_RewriteOptimisation_eqlist(eqs);

    //    ast_puts(eqs);puts("");

    if (Ast_eqs_has_agentID(eqs, AST_ANNOTATION_L)) {
      //          puts("has AST_ANNOTATION_L");
      return 0;
    }
    //        puts("has no AST_ANNOTATION_L");

    // Move to the last placed equation
    while (ast_getTail(eqs) != NULL) {
      eqs = ast_getTail(eqs);
    }

    Ast *eq = eqs->left;
    Ast *recursion_agent = eq->left;
    Ast *constructor_agent = eq->right;

    int idL = CmEnv.idL;

    int    available_TCO = 0;
    AST_ID astID_of_TCO; // = idL;  // idL is dummy

    // --- recursion_agent ~ constructor_agent
    if (recursion_agent->left->id == AST_SYM &&
        idL == NameTable_get_id(recursion_agent->left->sym)) {

      if (Ast_is_expr(constructor_agent)) {
        // --- CASE 1: rule_left_agent ~ expression
        available_TCO = 1;
        astID_of_TCO = AST_CNCT_TCO_INTVAR;

      } else if (constructor_agent->id == AST_NAME) {
        // --- CASE 2: rule_left_agent ~ name   where the name is meta_R
        char   *sym = constructor_agent->left->sym;
        NB_TYPE type = 0;
        int     exists_in_table = CmEnv_gettype_forname(sym, &type);
        if (exists_in_table && (type == NB_META_R || type == NB_META_L)) {

          available_TCO = 1;
          astID_of_TCO = AST_CNCT_TCO;
        }
      }
    }

    if (available_TCO) {
      //      puts("Target:");
      //      ast_puts(eq_TCO); puts("");

      eq->id = astID_of_TCO;

      // Foo ==> (*L)Foo
      Ast *annotationL = ast_makeAST(AST_ANNOTATION_L, recursion_agent, NULL);
      eq->left = annotationL;

      //      ast_puts(eq); puts("");
      return 1;
    }
    return 0;

  } else {
    // if_sentence

    Ast *if_sentence = mainbody;
    Ast *then_branch = if_sentence->right->left;
    Ast *else_branch = if_sentence->right->right;

    int ret_status = 0;
    ret_status += Ast_make_annotation_TailCallOptimisation(then_branch);
    ret_status += Ast_make_annotation_TailCallOptimisation(else_branch);

    if (ret_status == 0) {
      return 0;
    } else {
      return 1;
    }
  }
}

// Operation on Ast ----------------------------------------------
bool Ast_has_the_ID(Ast *ptr, IDTYPE id) {
  if (ptr == NULL) {
    return 0;
  }

  if (ptr->id == id) {
    return 1;
  }

  switch (ptr->id) {
  case AST_TUPLE:
  case AST_OPCONS:
  case AST_AGENT:
    ptr = ptr->right;

    for (int i = 0; i < MAX_PORT; i++) {
      if (ptr == NULL)
        return 0;

      if (Ast_has_the_ID(ptr->left, id))
        return 1;

      ptr = ast_getTail(ptr);
    }
    return 0;
    break;

  case AST_ANNOTATION_L:
  case AST_ANNOTATION_R:
    return Ast_has_the_ID(ptr->left, id);
    break;

  default:
    return 0;
  }
}

bool Ast_eqs_has_agentID(Ast *eqs, IDTYPE id) {
  Ast *at = eqs;

  while (at != NULL) {
    Ast *eq = at->left;
    if (Ast_has_the_ID(eq->left, id))
      return 1;

    if (Ast_has_the_ID(eq->right, id))
      return 1;

    at = ast_getTail(at);
  }
  return 0;
}

bool Ast_mainbody_has_agentID(Ast *mainbody, IDTYPE id) {

  if (mainbody == NULL)
    return 0;

  if (mainbody->id == AST_BODY) {

    Ast *body = mainbody;
    Ast *eqs = body->right;

    if (eqs == NULL) {
      return 0;
    }

    if (Ast_eqs_has_agentID(eqs, id)) {
      return 1;
    }

    return 0;
  }

  // if_sentence

  Ast *if_sentence = mainbody;
  Ast *then_branch = if_sentence->right->left;
  Ast *else_branch = if_sentence->right->right;

  if (Ast_mainbody_has_agentID(then_branch, id) ||
      Ast_mainbody_has_agentID(else_branch, id)) {
    return 1;
  }

  return 0;
}

bool Ast_is_agent(const Ast *ptr) {
  if (ptr == NULL) {
    return false;
  }

  switch (ptr->id) {
  case AST_NIL:
  case AST_OPCONS:
  case AST_AGENT:
    return 1;
    break;

  case AST_TUPLE: {
    const int arity = ptr->intval;
    if (arity == 1) {
      // The case of the single tuple such as `(A)'.
      // The `A' is recognised as not an argument, but as a first-class object.
      ptr = ptr->right;
      return Ast_is_agent(ptr->right);
    }

    return true;
    break;
  }
  case AST_ANNOTATION_L:
  case AST_ANNOTATION_R:
    return Ast_is_agent(ptr->left);
    break;

  default:
    return false;
  }
}

bool Ast_is_expr(const Ast *ptr) {
  if (ptr == NULL) {
    return 0;
  }

  switch (ptr->id) {
  case AST_INT:
    return 1;
    break;

  case AST_NAME: {
    NB_TYPE type;
    int     result = CmEnv_gettype_forname(ptr->left->sym, &type);
    if (result == 0) {
      return 0;
    }
    if (type != NB_INTVAR) {
      return 0;
    }

    return 1;
    break;
  }

  case AST_RAND:
  case AST_UNM: {
    if (!Ast_is_expr(ptr->left))
      return 0;

    return 1;
    break;
  }

  case AST_PLUS:
  case AST_SUB:
  case AST_MUL:
  case AST_DIV:
  case AST_MOD:
  case AST_LT:
  case AST_LE:
  case AST_EQ:
  case AST_NE: {
    if (!Ast_is_expr(ptr->left))
      return 0;
    if (!Ast_is_expr(ptr->right))
      return 0;

    return 1;
    break;
  }
  default:
    return 0;
  }
}

void free_Names_ast(Ast *ast) {
  Ast *param = ast;

  while (param != NULL) {
    int sym_id = NameTable_get_id(param->left->sym);
    if (IS_GNAMEID(sym_id)) {
      flush_name_port0(IdTable_get_heap(sym_id));
    } else {
      flush_name_port0((VALUE)NULL);
    }
    param = ast_getTail(param);
  }
}

void flush_name_port0(const VALUE ptr) {
  if (ptr == (VALUE)NULL) {
    return;
  }

  if (IS_FIXNUM(ptr)) {
    return;
  }

  // When the given name nodes (ptr) occurs somewhere also,
  // these are not freed.
  // JAPANESE: ptr の name nodes が他の場所で出現するなら flush しない。
  VALUE connected_from;
  if (keynode_exists_in_another_term(ptr, &connected_from) >= 1) {
    printf("Error: `%s' cannot be freed because it is referred to by `%s'.\n",
           IdTable_get_name(BASIC(ptr)->id),
           IdTable_get_name(BASIC(connected_from)->id));

    return;
  }

  if (NAME(ptr)->port == (VALUE)NULL) {
    free_Name(ptr);
  } else {
    ShowNameHeap = ptr;
    free_Agent_recursively(NAME(ptr)->port);
    free_Name(ptr);
    ShowNameHeap = (VALUE)NULL;
  }

#ifndef THREAD
  if (GlobalOptions.verbose_memory_use) {
    print_memory_usage(&VM.agentHeap, &VM.nameHeap);
  }
#endif
}

// ------------------------------------------------------
// Free allocated nodes
// ------------------------------------------------------

// static inline
void free_Agent(const VALUE ptr) {
  assert(ptr != (VALUE)NULL);
  myfree(ptr);
}

void free_Agent2(const VALUE ptr1, const VALUE ptr2) {
  assert(ptr1 != (VALUE)NULL);
  assert(ptr2 != (VALUE)NULL);
  myfree2(ptr1, ptr2);
}

// static inline
void free_Name(VALUE ptr) {
  if (ptr == (VALUE)NULL) {
    puts("ERROR: NULL is applied to free_Name.");
    return;
  }

  if (IS_LOCAL_NAMEID(BASIC(ptr)->id)) {
    myfree(ptr);
  } else {
    // Global name

    NameTable_erase_id(IdTable_get_name(BASIC(ptr)->id));
    IdTable_set_heap(BASIC(ptr)->id, (VALUE)NULL);

    SET_LOCAL_NAMEID(BASIC(ptr)->id);
    myfree(ptr);
  }
}

void free_Agent_recursively(VALUE ptr) {

loop:
  if (IS_FIXNUM(ptr) || ptr == (VALUE)NULL) {
    return;
  }

  if (IS_READYFORUSE(BASIC(ptr)->id)) {
    return;
  }

  if (IS_NAMEID(BASIC(ptr)->id)) {
    if (ptr == ShowNameHeap)
      return;

    if (NAME(ptr)->port != (VALUE)NULL) {
      VALUE port = NAME(ptr)->port;
      free_Name(ptr);
      ptr = port;
      goto loop;
    }

    // The name is kept living if it occurs anywhere as a global.
    if (keynode_exists_in_another_term(ptr, NULL) < 2) {
      free_Name(ptr);
    }
  } else {
    if (BASIC(ptr)->id == ID_CONS) {
      if (IS_FIXNUM(AGENT(ptr)->port[0])) {
        const VALUE port1 = AGENT(ptr)->port[1];
        free_Agent(ptr);
        ptr = port1;
        goto loop;
      }
    }

    if (BASIC(ptr)->id == ID_PERCENT) {
      free_Agent(ptr);
      return;
    }

    int arity = IdTable_get_arity(AGENT(ptr)->basic.id);
    if (arity == 1) {
      VALUE port1 = AGENT(ptr)->port[0];
      free_Agent(ptr);
      ptr = port1;
      goto loop;
    }

    free_Agent(ptr);
    //      printf(" .");
    for (int i = 0; i < arity; i++) {
      free_Agent_recursively(AGENT(ptr)->port[i]);
    }
  }
}
