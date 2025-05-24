#include "opt.h"

#include "ast.h"
#include "cmenv.h"

#include <string.h>

Ast *Ast_subst_term(char *sym, Ast *aterm, Ast *target, int *result) {
  // target is astterm: (AST asttermL asttermR)
  // this function replaces sym with aterm in target, i.e. tgs[aterm/sym].

  switch (target->id) {
  case AST_NAME:
    if (strcmp(target->left->sym, sym) == 0) {
      *result = 1;
      return aterm;
    }
    return target;

  case AST_ANNOTATION_L:
  case AST_ANNOTATION_R:

  case AST_OPCONS:
  case AST_TUPLE:
  case AST_AGENT: {
    Ast *port;
    if (target->id == AST_ANNOTATION_L || target->id == AST_ANNOTATION_R) {

      // (AST_ANNOTATION (AST_AGENT (ID_SYM sym NULL) paramlist) NULL)
      port = target->left->right;

    } else {

      // (AST_AGENT (ID_SYM sym NULL) paramlist)
      port = target->right;
    }

    for (int i = 0; i < MAX_PORT; i++) {
      if (port == NULL)
        break;

      port->left = Ast_subst_term(sym, aterm, port->left, result);
      if (*result)
        break;

      port = ast_getTail(port);
    }
  }
    return target;

  default:
    return target;
  }
}

int Ast_subst_eqlist(int nth, char *sym, Ast *aterm, Ast *eqlist) {
  // eqlist[aterm/sym] except for n-th eq

  Ast *eq, *at = eqlist;
  int  ct = 0;
  int  result;

  while (at != NULL) {
    eq = at->left;

    if (ct == nth) {
      ct++;
      at = ast_getTail(at);
      continue;
    }

    result = 0;
    eq->left = Ast_subst_term(sym, aterm, eq->left, &result);
    if (result) {
      return 1;
    }

    result = 0;
    eq->right = Ast_subst_term(sym, aterm, eq->right, &result);
    if (result) {
      return 1;
    }

    ct++;
    at = ast_getTail(at);
  }

  return 0;
}

void Ast_RewriteOptimisation_eqlist(Ast *eqlist) {
  // Every eq such as x~t in eqlist is replaced as eqlist[t/x].
  //
  // Structure
  // eqlist : (AST_LIST eq1 (AST_LIST eq2 (AST_LIST eq3 NULL)))
  // eq : (AST_CNCT astterm1 astterm2)

  Ast    *at, *prev, *term;
  char   *sym;
  int     nth = 0, exists_in_table;
  NB_TYPE type = NB_NAME;

  at = prev = eqlist;

  while (at != NULL) {
    // (id, left, right)
    // at : (AST_LIST, x1, NULL)
    // at : (AST_LIST, x1, (AST_LIST, x2, NULL))
    Ast *target_eq = at->left;

    //            printf("\n[target_eq] "); ast_puts(target_eq);
    //            printf("\n%dnth in ", nth); ast_puts(eqlist); printf("\n\n");

    Ast *name_term = target_eq->left;
    if (name_term->id == AST_NAME) {

      sym = name_term->left->sym;
      exists_in_table = CmEnv_gettype_forname(sym, &type);

      if (!exists_in_table || type == NB_NAME) {
        // When no entry in CmEnv table or its type is NB_NAME,
        // it is a local name, so it is the candidate.

        term = target_eq->right;
        //	printf("%s~",sym); ast_puts(term); puts("");

        if (Ast_subst_eqlist(nth, sym, term, eqlist)) {
          //		 printf("=== hit %dth\n", nth);

          if (prev != at) {
            // 前のリストの接続先を、現在地を省いて、その次の要素に変更。
            prev->right = at->right;
            at = at->right;
          } else {
            // 先頭の要素が代入に使われたので、
            // body 内の eqlist から先頭を削除
            // (AST_LIST, x1, (AST_LIST, x2, *next)) ==> (AST_LIST, x2, *next)
            eqlist->left = eqlist->right->left;
            eqlist->right = eqlist->right->right;
            eqlist = eqlist->right;
            // 対象 at を次の要素に更新し、prev も at とする
            prev = at = at->right;
          }

          continue;
        }
      }
    }

    name_term = target_eq->right;
    if (name_term->id == AST_NAME) {
      sym = name_term->left->sym;
      exists_in_table = CmEnv_gettype_forname(sym, &type);

      if (!exists_in_table || type == NB_NAME) {
        // When no entry in CmEnv table or its type is NB_NAME

        term = target_eq->left;
        //	ast_puts(term); printf("~%s",sym);puts("");

        if (Ast_subst_eqlist(nth, sym, term, eqlist)) {
          //	 printf("=== hit %dth\n", nth);

          if (prev != at) {
            // 前のリストの接続先を、現在地を省いて、その次の要素に変更。
            prev->right = at->right;
            at = at->right;
          } else {
            // 先頭の要素が代入に使われたので、
            // body 内の eqlist から先頭を削除
            // (AST_LIST, x1, (AST_LIST, x2, *next)) ==> (AST_LIST, x2, *next)
            eqlist->left = eqlist->right->left;
            eqlist->right = eqlist->right->right;
            eqlist = eqlist->right;
            // 対象 at を次の要素に更新し、prev も at とする
            prev = at = at->right;
          }

          continue;
        }
      }
    }

    nth++;
    prev = at;
    at = ast_getTail(at);
  }
}
