#ifndef _INPLA_
#define _INPLA_

#include "ast.h"
#include "mytype.h"
#include "vm.h"

void *exec_code(int mode, VirtualMachine *restrict vm, void *restrict *code);

void flush_name_port0(VALUE ptr);

void print_name_port0(VALUE ptr);
void print_memory_usage(Heap *agent_heap, Heap *name_heap);

int make_rule_oneway(Ast *ast);

void mark_and_sweep(void);

void select_kind_of_push(Ast *ast, int p1, int p2);

int get_arity_on_ast(Ast *ast);
int check_ast_arity(Ast *ast);
int check_invalid_occurrence_as_rule(Ast *ast);
int check_invalid_occurrence(Ast *ast);

int Compile_rule_mainbody_on_ast(Ast *mainbody);
int Compile_stmlist_on_ast(Ast *at);
int Compile_term_on_ast(Ast *ptr, int target);
int Compile_expr_on_ast(Ast *ptr, int target);

void set_metaL_as_IntName(Ast *ast);
void set_metaL_as_AnyAgent(Ast *ast);
void set_metaR_as_IntName(Ast *ast);
void set_metaR_as_AnyAgent(Ast *ast);

int set_metaR(Ast *ast);
int set_metaL(Ast *ast);

void set_annotation_LR(int left, int right);



void free_Names_ast(Ast *ast);
void free_Agent2(VALUE ptr1, VALUE ptr2);
#endif
