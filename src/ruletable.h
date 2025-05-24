#ifndef INPLA_RULETABLE_H
#define INPLA_RULETABLE_H

#include "cmenv.h"
#include "vm.h"

// ------------------------------------------------------------
// TABLE for RULES
// ------------------------------------------------------------

#ifndef RULETABLE_SIMPLE
#  define RULEHASH_SIZE NUM_AGENTS
typedef struct RuleList {
  int sym;
  int available;
  void *code[MAX_VMCODE_SEQUENCE];
  struct RuleList *next;
} RuleList;

extern RuleList *RuleTable[RULEHASH_SIZE];
#else
// ------------------------------------------
// RuleTable: simple realisation with arrays
// ------------------------------------------
//
// codes for alpha><beta is stored in
// RuleTable[id_Beta][id_alpha]

extern void *RuleTable[NUM_AGENTS][NUM_AGENTS];
#endif

void RuleTable_init(void);
void *RuleTable_get_code(int syml, int symr, int *result);
void RuleTable_get_code_for_Int(VALUE heap_syml, void ***code);
void RuleTable_record(int symlID, int symrID, void **code, int byte);

#endif // INPLA_RULETABLE_H
