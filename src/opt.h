#ifndef INPLA_OPT_H
#define INPLA_OPT_H

#include "ast.h"

void Ast_RewriteOptimisation_eqlist(Ast *eqlist);
int  Ast_subst_eqlist(int nth, char *sym, Ast *aterm, Ast *eqlist);
Ast *Ast_subst_term(char *sym, Ast *aterm, Ast *target, int *result);

#endif // INPLA_OPT_H
