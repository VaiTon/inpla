#ifndef _AST_
#define _AST_

// http://www.hpcs.cs.tsukuba.ac.jp/~msato/lecture-note/comp-lecture/tiny-c/AST.h
#include "types.h"

#include <stdbool.h>

typedef enum {
  // clang-format off
  AST_SYM=0, AST_NAME, AST_INTVAR, AST_AGENT,
  AST_CNCT, AST_CNCT_TCO_INTVAR, AST_CNCT_TCO_CONS, AST_CNCT_TCO,
  AST_RULE, AST_BODY, AST_IF, AST_THEN_ELSE,

  // this is for ASTLIST
  AST_LIST,

  // annotation
  AST_ANNOTATION_L, AST_ANNOTATION_R,

  // builtin tuple
  AST_TUPLE,

  // operation
  AST_INT, AST_LD, AST_PLUS, AST_SUB, AST_MUL, AST_DIV, AST_MOD,
  AST_LT, AST_LE, AST_EQ, AST_NE, AST_UNM, AST_AND, AST_OR, AST_NOT,

  // for built-in lists
  AST_OPCONS, AST_NIL,

  // for built-in agents
  AST_RAND, AST_SRAND,

  // for PERCENT
  AST_PERCENT,
  // clang-format on
} AST_ID;

typedef struct abstract_syntax_tree {
  AST_ID                       id;
  int                          intval;
  long                         longval;
  char                        *sym;
  struct abstract_syntax_tree *left, *right;
} Ast;

void ast_heapInit(void);
void ast_heapReInit(void);

Ast *ast_makeSymbol(char *name);
Ast *ast_makeInt(long num);
Ast *ast_makeAST(AST_ID id, Ast *left, Ast *right);
Ast *ast_makeTuple(Ast *tuple);

Ast *ast_addLast(Ast *l, Ast *p);
Ast *ast_getNth(Ast *p, int nth);
Ast *ast_getTail(Ast *p);
int  ast_getRecordedVal(int entry);
Ast *ast_paramToCons(Ast *ast);
Ast *ast_remove_tuple1(Ast *p);
Ast *ast_unfoldABR(Ast *left_params, char *sym, Ast *paramlist, Ast *annotate);
int  ast_recordConst(char *name, int val);

void ast_puts(Ast *p);

void Ast_undo_TCO_annotation(Ast *mainbody);
void Ast_remove_tuple1_in_mainbody(Ast *mainbody);
void Ast_undo_TCO_annotation(Ast *mainbody);
int  Ast_make_annotation_TailCallOptimisation(Ast *mainbody);

bool Ast_mainbody_has_agentID(Ast *mainbody, IDTYPE id);
bool Ast_eqs_has_agentID(Ast *eqs, IDTYPE id);
bool Ast_has_the_ID(Ast *ptr, IDTYPE id);
bool Ast_is_agent(const Ast *ptr);
bool Ast_is_expr(const Ast *ptr);

#define ast_makeCons(x1, x2) ast_makeAST(AST_LIST, x1, x2)
#define ast_makeList1(x1)    ast_makeAST(AST_LIST, x1, NULL)
#define ast_makeList2(x1, x2)                                                  \
  ast_makeAST(AST_LIST, x1, ast_makeAST(AST_LIST, x2, NULL))
#define ast_makeList3(x1, x2, x3)                                              \
  ast_makeAST(AST_LIST, x1,                                                    \
              ast_makeAST(AST_LIST, x2, ast_makeAST(AST_LIST, x3, NULL)))
#define getFirst(p) getNth(p, 0)

#endif
