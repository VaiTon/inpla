#include "ruletable.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef RULETABLE_SIMPLE

RuleList *RuleTable[RULEHASH_SIZE];

RuleList *RuleList_new(void) {
  RuleList *alist = malloc(sizeof(RuleList));
  if (alist == NULL) {
    fprintf(stderr, "Error: RuleList_new() failed: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }
  alist->available = 0;
  return alist;
}

void RuleList_set_code(RuleList *at, int sym, void **code, int byte,
                       RuleList *next) {
  at->sym = sym;
  at->available = 1;
  CmEnv_copy_VMCode(byte, code, at->code);
  at->next = next;
}

void RuleList_inavailable(RuleList *at) { at->available = 0; }

void RuleTable_init(void) {
  for (int i = 0; i < RULEHASH_SIZE; i++) {
    RuleTable[i] = NULL;
  }
}

void RuleTable_record(int symlID, int symrID, void **code, int byte) {

  RuleList *add;

  if (RuleTable[symlID] == NULL) {
    // No entry for symlID

    add = RuleList_new();

    // Make a new linear list whose node is (symrID, code, byte)
    RuleList_set_code(add, symrID, code, byte, NULL);

    // Set the linear list to RuleTable[symlID]
    RuleTable[symlID] = add;
    return;
  }

  // Linear Search
  RuleList *at = RuleTable[symlID]; // Set the top of the list to `at'.

  while (at != NULL) {
    if (at->sym == symrID) {
      // already exists

      // overwrite
      CmEnv_copy_VMCode(byte, code, at->code);
      return;
    }
    at = at->next;
  }

  // No entry for symlID in the linear list

  // Make a new linear list
  // and add it as the top of the list (that is RuleTable[symlID]).
  add = RuleList_new();
  RuleList_set_code(add, symrID, code, byte, RuleTable[symlID]);
  RuleTable[symlID] = add;
}
void RuleTable_delete(int symlID, int symrID) {

  if (RuleTable[symlID] == NULL) {
    // No entry for symlID
    return;
  }

  // Linear Search
  RuleList *at = RuleTable[symlID];

  while (at != NULL) {
    if (at->sym == symrID) {
      // already exists

      // Make it void
      RuleList_inavailable(at);
      return;
    }
    at = at->next;
  }
}

void *RuleTable_get_code(int syml, int symr, int *result) {
  // returns:
  //   *code  :  for syml><symr
  //   result :  1 for success, otherwise 0.

  //  int syml = AGENT(heap_syml)->basic.id;
  //  int symr = AGENT(heap_symr)->basic.id;

  // RuleList *add;

  if (RuleTable[syml] == NULL) {
    // When ResultTable for syml is empty

    *result = 0;
    return NULL;
  }

  // Linear search for the entry RuleTable[syml]
  RuleList *at = RuleTable[syml]; // set the top
  while (at != NULL) {
    if (at->sym != symr) {
      at = at->next;
      continue;
    }

    // already exists
    if (at->available == 0) {
      *result = 0;
      return NULL;
    }

    *result = 1;
    return at->code;
  }

  // no entry

  *result = 0;
  return NULL;
}
void RuleTable_get_code_for_Int(const VALUE heap_syml, void ***code) {
  const int syml = AGENT(heap_syml)->basic.id;

  if (RuleTable[syml] == NULL) {
    return;
  }

  RuleList *at = RuleTable[syml];
  while (at != NULL) {
    if (at->sym != ID_INT) {
      at = at->next;
      continue;
    }
    if (at->available == 0) {
      return;
    }

    *code = at->code;
    return;
  }
}

#else

void *RuleTable[NUM_AGENTS][NUM_AGENTS];

void RuleTable_init(void) {
  for (int i = 0; i < NUM_AGENTS; i++) {
    for (int j = 0; j < NUM_AGENTS; j++) {
      RuleTable[i][j] = NULL;
    }
  }
}
void RuleTable_record(int symlID, int symrID, void **code, int byte) {
  if (RuleTable[symrID][symlID] == NULL) {
    RuleTable[symrID][symlID] = malloc(MAX_VMCODE_SEQUENCE * sizeof(void *));
  }
  CmEnv_copy_VMCode(byte, code, RuleTable[symrID][symlID]);
}
void *RuleTable_get_code(int symlID, int symrID, int *result) {
  //  int symlID = AGENT(heap_syml)->basic.id;
  //  int symrID = AGENT(heap_symr)->basic.id;

  void *code = RuleTable[symrID][symlID];
  if (code == NULL) {
    *result = 0;
  } else {
    *result = 1;
  }

  return code;
}
void RuleTable_get_code_for_Int(VALUE heap_syml, void ***code) {
  int symlID = AGENT(heap_syml)->basic.id;
  *code = RuleTable[ID_INT][symlID];
}

#endif
