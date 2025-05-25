%{

#include "ast.h"
#include "name_table.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int make_rule(Ast *ast);

void free_Agent_recursively(VALUE ptr);
void free_Name(VALUE ptr);
void free_Agent(VALUE ptr);

void puts_Names_ast(Ast *ast);
void free_Names_ast(Ast *ast);

void puts_Name_port0_nat(char *sym);

void puts_term(VALUE ptr);
void puts_aplist(EQList *at);

int exec(Ast *st);
int destroy(void);


void puts_memory_stat(void);


//#define YYDEBUG 1
extern FILE *yyin;
extern int yylex();
int yyerror();
#define YY_NO_INPUT
extern int yylineno, yycolumn;

// For error message when nested source files are specified.
#define MY_YYLINENO
#ifdef MY_YYLINENO
 typedef struct InfoLinenoType_tag {
   char *fname;
   int yylineno;
   struct InfoLinenoType_tag *next;
 } InfoLinenoType;
static InfoLinenoType *InfoLineno;

#define InfoLineno_Init() InfoLineno = NULL;

void InfoLineno_Push(char *fname, int lineno) {
  InfoLinenoType *aInfo;
  aInfo = (InfoLinenoType *)malloc(sizeof(InfoLinenoType));
  if (aInfo == NULL) {
    printf("[InfoLineno]Malloc error\n");
    exit(-1);
  }
  aInfo->next = InfoLineno;
  aInfo->yylineno = lineno+1;
  aInfo->fname = strdup(fname);

  InfoLineno = aInfo;
}

void InfoLineno_Free() {
  InfoLinenoType *aInfo;
  free(InfoLineno->fname);
  aInfo = InfoLineno;
  InfoLineno = InfoLineno->next;
  free(aInfo);
}

void InfoLineno_AllDestroy() {
  //  InfoLinenoType *aInfo;
  while (InfoLineno != NULL) {
    InfoLineno_Free();
  }
}

#endif


extern void pushFP(FILE *fp);
extern int popFP();


// Messages from yyerror will be stored here.
// This works to prevent puting the message.
static char *Errormsg = NULL;


%}
%union{
  long longval;
  char *chval;
  Ast *ast;
}

%token <chval> NAME "NAME_LITERAL"
%token <chval> AGENT "AGENT_LITERAL"
%token <longval> NUMERAL_LITERAL
%token <chval> STRING_LITERAL

%token CROSS "><"
%token ABR "<<"
%token ARROW "=>"
%token PIPE "|"

%token ANNOTATE_L "(*L)"
%token ANNOTATE_R "(*R)"

%token NE "!="
%token LD "="
%token EQUAL "=="
%token LE "<="
%token GE ">="

%token NOT AND OR
%token INT LET IN END IF THEN ELSE WHERE RAND DEF
%token INTERFACE IFCE PRNAT FREE EXIT MEMSTAT
%token END_OF_FILE USE

%type <ast> body astterm astterm_item nameterm agentterm astparam astparams
val_declare
rule
ap aplist
stm stmlist_nondelimiter
stmlist
expr additive_expr equational_expr logical_expr relational_expr unary_expr
multiplicative_expr primary_expr agent_tuple agent_list agent_cons
agent_percent
bodyguard bd_else bd_elif bd_compound
if_sentence if_compound
name_params
abr_annotate

%type <chval> abr_agent_name
 //body

%nonassoc REDUCE
%nonassoc ')'

%right ':'
 //%right LD
 //%right EQ
 //%left NE GE GT LT
 //%left ADD SUB
 //%left MULT DIV

       // For error message information
       %define parse.error verbose
       %locations


%%
s
: error ';' {
  yyclearin;
  yyerrok;
  puts(Errormsg);
  free(Errormsg);
  ast_heapReInit();
  if (yyin == stdin) yylineno=0;
  yycolumn=1;
  //  YYACCEPT;
  YYABORT;
}
| ';' {
  if (yyin == stdin) yylineno=0;
  yycolumn=1;
  YYACCEPT;
}
| body ';'
{
  exec($1); // $1 is a list such as [stmlist, aplist]
  ast_heapReInit();
  if (yyin == stdin) yylineno=0;
  yycolumn=1;
  YYACCEPT;
}
| rule ';' {
  if (make_rule($1)) {
    if (yyin == stdin) yylineno=0;
    YYACCEPT;
  } else {
    if (yyin == stdin) yylineno=0;
    YYABORT;
  }
  yycolumn=1;
}
| command {
  if (yyin == stdin) yylineno=0;
  yycolumn=1;
  YYACCEPT;
}
;



// body is a list such as [stmlist, aplist]
// ==> changed into (AST_BODY stmlist aplist)
body
: aplist { $$ = ast_makeAST(AST_BODY, NULL, $1); }
| aplist WHERE stmlist_nondelimiter { $$ = ast_makeAST(AST_BODY, $3, $1);}
| aplist WHERE  { $$ = ast_makeAST(AST_BODY, NULL, $1);}
| LET stmlist IN aplist END { $$ = ast_makeAST(AST_BODY, $2, $4);}
| LET stmlist ';' IN aplist END { $$ = ast_makeAST(AST_BODY, $2, $5);}
| LET  IN aplist END { $$ = ast_makeAST(AST_BODY, NULL, $3);}
| '{' stmlist '}'  aplist { $$ = ast_makeAST(AST_BODY, $2, $4);}
| '{' stmlist ';' '}' aplist { $$ = ast_makeAST(AST_BODY, $2, $5);}
;

// rule is changed as follows:
//    (ASTRULE (AST_CNCT agentL agentR)
//      <if-sentence> | <body>)
//
//      WHERE
//      <if-sentence> ::= (AST_IF guard (AST_BRANCH <then> <else>))
//                      | <body>
//                      | NULL
//      <then> ::= <if-sentence>
//      <else> ::= <if-sentence>


rule
: astterm CROSS astterm ARROW
{ $$ = ast_makeAST(AST_RULE, ast_makeAST(AST_CNCT, $1, $3),
                 	     NULL); }

| astterm CROSS astterm ARROW body
{ $$ = ast_makeAST(AST_RULE, ast_makeAST(AST_CNCT, $1, $3),
		             $5); }


| astterm CROSS astterm ARROW if_sentence
{ $$ = ast_makeAST(AST_RULE, ast_makeAST(AST_CNCT, $1, $3),
                 	     $5); }


| astterm CROSS astterm bodyguard
{ $$ = ast_makeAST(AST_RULE, ast_makeAST(AST_CNCT, $1, $3),
		   $4); }
;


name_params
: NAME
{ $$ = ast_makeList1(ast_makeSymbol($1)); }
| name_params NAME
{ $$ = ast_addLast($1, ast_makeSymbol($2)); }
;

command:
| FREE name_params ';'
{
  free_Names_ast($2);
}
| FREE ';'
| FREE IFCE ';'
{
  NameTable_free_all();
}
| FREE INTERFACE ';'
{
  NameTable_free_all();
}
| name_params ';'
{
  puts_Names_ast($1);
}

| PRNAT NAME ';'
{
  puts_Name_port0_nat($2);
}
| INTERFACE ';'
{
  NameTable_puts_all();
}
| IFCE ';'
{
  NameTable_puts_all();
}
| EXIT ';' {destroy(); exit(0);}
| USE STRING_LITERAL ';' {
  // http://flex.sourceforge.net/manual/Multiple-Input-Buffers.html
  yyin = fopen($2, "r");
  if (!yyin) {
    printf("Error: The file `%s' does not exist.\n", $2);
    free($2);
    yyin = stdin;

  } else {
#ifdef MY_YYLINENO
    InfoLineno_Push($2, yylineno+1);
    yylineno = 0;
#endif

    pushFP(yyin);
  }
}
| error END_OF_FILE {}
| END_OF_FILE {
  if (!popFP()) {
    destroy(); exit(-1);
  }
#ifdef MY_YYLINENO
  yylineno = InfoLineno->yylineno;
  InfoLineno_Free();
  destroy();
#endif


}
| DEF AGENT LD NUMERAL_LITERAL ';' {
  int entry=ast_recordConst($2,$4);

  if (!entry) {
    printf("`%s' has been already bound to a value `%d' as immutable.\n\n",
	   $2, ast_getRecordedVal(entry));
    fflush(stdout);
  }
 }
| DEF AGENT LD '-' NUMERAL_LITERAL ';' {
  int entry=ast_recordConst($2,$5*(-1));

  if (!entry) {
    printf("`%s' has been already bound to a value `%d' as immutable.\n\n",
	   $2, ast_getRecordedVal(entry));
    fflush(stdout);
  }
 }

| MEMSTAT ';'
{
  puts_memory_stat();
}
;


bodyguard
: PIPE expr ARROW bd_compound bd_elif
{ $$ = ast_makeAST(AST_IF, $2, ast_makeAST(AST_THEN_ELSE, $4, $5));}
;

bd_compound
: body
| bodyguard
;

bd_elif
: bd_else
| PIPE expr ARROW bd_compound bd_elif
{ $$ = ast_makeAST(AST_IF, $2, ast_makeAST(AST_THEN_ELSE, $4, $5));}
;

bd_else
: PIPE '_' ARROW bd_compound
{ $$ = $4; }
;



// if_sentence
if_sentence
: IF expr THEN if_compound ELSE if_compound
{ $$ = ast_makeAST(AST_IF, $2, ast_makeAST(AST_THEN_ELSE, $4, $6));}
;

if_compound
: if_sentence
| body
;



// AST -----------------
astterm
: '(' ANNOTATE_L ')' astterm_item
{ $$=ast_makeAST(AST_ANNOTATION_L, $4, NULL); }
| '(' ANNOTATE_R ')' astterm_item
{ $$=ast_makeAST(AST_ANNOTATION_R, $4, NULL); }
//| astterm_item ':' astterm_item
| astterm_item ':' astterm   // h:t
{$$ = ast_makeAST(AST_OPCONS, NULL, ast_makeList2($1, $3)); }
| astterm_item
;


astterm_item
      : agentterm
      | agent_tuple
      | agent_list
      | agent_cons
      | val_declare
      | agent_percent
      | expr %prec REDUCE
;

agent_percent
: '%' AGENT
{ $$=ast_makeAST(AST_PERCENT, ast_makeSymbol($2), NULL); }
| '%' NAME
{ $$=ast_makeAST(AST_PERCENT, ast_makeSymbol($2), NULL); }
;

val_declare
: INT NAME { $$ = ast_makeAST(AST_INTVAR, ast_makeSymbol($2), NULL); }
;

agent_cons
: '[' astterm PIPE astterm ']'
{ $$ = ast_makeAST(AST_OPCONS, NULL, ast_makeList2($2, $4)); }
;

agent_list
: '[' ']' { $$ = ast_makeAST(AST_NIL, NULL, NULL); }
| '[' astparams ']' { $$ = ast_paramToCons($2); }
;

agent_tuple
: astparam { $$ = ast_makeTuple($1);}
;

nameterm
: NAME {$$=ast_makeAST(AST_NAME, ast_makeSymbol($1), NULL);}

agentterm
: AGENT astparam
{ $$=ast_makeAST(AST_AGENT, ast_makeSymbol($1), $2); }
| NAME astparam
{ $$=ast_makeAST(AST_AGENT, ast_makeSymbol($1), $2); }
;


astparam
: '(' ')' { $$ = NULL; }
| '(' astparams ')' { $$ = $2; }
;

astparams
: astterm { $$ = ast_makeList1($1); }
| astparams ',' astterm { $$ = ast_addLast($1, $3); }
;

ap
: astterm '~' astterm { $$ = ast_makeAST(AST_CNCT, $1, $3); }
//
// param << A(...)
| astparams ABR abr_agent_name '(' astparams ')'
{ $$ = ast_unfoldABR($1, $3, $5, NULL); }
// << A(...)
| ABR abr_agent_name '(' astparams ')'
{ $$ = ast_unfoldABR(NULL, $2, $4, NULL); }
// param << (*L)A(...)
| astparams ABR abr_annotate abr_agent_name  '(' astparams ')'
{ $$ = ast_unfoldABR($1, $4, $6, $3); }
// << (*L)A(...)
| ABR abr_annotate abr_agent_name '(' astparams ')'
{ $$ = ast_unfoldABR(NULL, $3, $5, $2); }
//
//| astparams ABR NAME '(' astparams ')'
//{ $$ = ast_unfoldABR($1, $3, $5, NULL); }
//| ABR NAME '(' astparams ')'
//{ $$ = ast_unfoldABR(NULL, $2, $4, NULL); }
;

abr_agent_name
: AGENT { $$ = $1; }
| NAME { $$ = $1; }
;

abr_annotate
: '(' ANNOTATE_L ')' { $$ = ast_makeAST(AST_ANNOTATION_L, NULL, NULL); }
| '(' ANNOTATE_R ')' { $$ = ast_makeAST(AST_ANNOTATION_R, NULL, NULL); }
;


aplist
: ap { $$ = ast_makeList1($1); }
| aplist ',' ap { $$ = ast_addLast($1, $3); }
;

stm
: nameterm LD expr { $$ = ast_makeAST(AST_LD, $1, $3); }

stmlist
: stm { $$ = ast_makeList1($1); }
| stmlist ';' stm { $$ = ast_addLast($1, $3); }

stmlist_nondelimiter
: stm { $$ = ast_makeList1($1); }
| stmlist_nondelimiter stm { $$ = ast_addLast($1, $2); }


expr
: equational_expr
;

equational_expr
: logical_expr
| equational_expr EQUAL logical_expr { $$ = ast_makeAST(AST_EQ, $1, $3); }
| equational_expr NE logical_expr { $$ = ast_makeAST(AST_NE, $1, $3); }

logical_expr
: relational_expr
| NOT relational_expr { $$ = ast_makeAST(AST_NOT, $2, NULL); }
| logical_expr AND relational_expr { $$ = ast_makeAST(AST_AND, $1, $3); }
| logical_expr OR relational_expr { $$ = ast_makeAST(AST_OR, $1, $3); }
;

relational_expr
: additive_expr
| relational_expr '<' additive_expr { $$ = ast_makeAST(AST_LT, $1, $3); }
| relational_expr LE additive_expr { $$ = ast_makeAST(AST_LE, $1, $3); }
| relational_expr '>' additive_expr { $$ = ast_makeAST(AST_LT, $3, $1); }
| relational_expr GE additive_expr { $$ = ast_makeAST(AST_LE, $3, $1); }
;

additive_expr
: multiplicative_expr
| additive_expr '+' multiplicative_expr { $$ = ast_makeAST(AST_PLUS, $1, $3); }
| additive_expr '-' multiplicative_expr { $$ = ast_makeAST(AST_SUB, $1, $3); }
;

multiplicative_expr
: unary_expr
| multiplicative_expr '*' primary_expr { $$ = ast_makeAST(AST_MUL, $1, $3); }
| multiplicative_expr '/' primary_expr { $$ = ast_makeAST(AST_DIV, $1, $3); }
| multiplicative_expr '%' primary_expr { $$ = ast_makeAST(AST_MOD, $1, $3); }


unary_expr
: primary_expr
| '-' primary_expr { $$ = ast_makeAST(AST_UNM, $2, NULL); }
| RAND '(' primary_expr ')' { $$ = ast_makeAST(AST_RAND, $3, NULL); }
;

primary_expr
: nameterm { $$ = $1;}
| NUMERAL_LITERAL { $$ = ast_makeInt($1); }
| AGENT { $$=ast_makeAST(AST_AGENT, ast_makeSymbol($1), NULL); }
| '(' expr ')' { $$ = $2; }

;

%%



int yyerror(char *s) {
  extern char *yytext;
  char msg[256];

#ifdef MY_YYLINENO
  if (InfoLineno != NULL) {
    sprintf(msg, "%s:%d:%d: %s near token `%s'.\n",
	    InfoLineno->fname, yylineno+1, yycolumn, s, yytext);
  } else {
    sprintf(msg, "%d:%d: %s near token `%s'.\n",
	    yylineno, yycolumn, s, yytext);
  }
#else
  sprintf(msg, "%d: %s near token `%s'.\n", yylineno, s, yytext);
#endif

  Errormsg = strdup(msg);

  if (yyin != stdin) {
    //    puts(Errormsg);
    destroy();
    //    exit(0);
  }

  return 0;
}




int destroy() {
  return 0;
}

int yywrap() {
  return 1;
}
