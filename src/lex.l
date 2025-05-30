%{
#include <stdio.h>
#include <stdlib.h>
#include "linenoise/linenoise.h"
#include "ast.h"
#include "parser.tab.h"


extern int yylex(void);
extern int yyerror(char *s);

#undef YY_INPUT
#define YY_INPUT(buf,result,max_size) result = mygetinput(buf, max_size);

static int mygetinput(char *buf, int size) {
    char *line;
    if (feof(yyin))  return YY_NULL;

    if (yyin!=stdin) {
      int len;
      char ch;
      for (len = 0; len < size; len++) {
	ch = getc(yyin);
	if (ch == EOF)
	    break;
	buf[len] = ch;
      }
      return len;
    }

    linenoiseSetMultiLine(1);
    if (yylineno == 1) {
      line = linenoise(">>> ");
    } else {
      line = linenoise("... ");
    }
    if(!line)        return YY_NULL;
    if(strlen(line) > size-2){
      fprintf(stderr,"input line too long\n");
      return YY_NULL;
    }
    sprintf(buf, "%s\n", line);
    linenoiseHistoryAdd(line);
    linenoiseHistorySave(".inpla.history.txt");
    free(line);
    return strlen(buf);
}


//#undef YY_INPUT
//#define YYINPUT(buf, result, max_size) result = (c == EOF) ? YY_NULL : (buf[0] = c, 1);


/************************************
 Strings
*************************************/
#define MAX_STRING_LENGTH 1024
static char ParseStringBuffer[MAX_STRING_LENGTH];
static int nextPtr_ParseStringBuffer = 0;
void Parse_StringBuffer_reInit() {
  nextPtr_ParseStringBuffer = 0;
}

void Parse_addString(char ch) {
  ParseStringBuffer[nextPtr_ParseStringBuffer] = ch;
  nextPtr_ParseStringBuffer++;
}

char *Parse_closeStringBuffer() {
  char *ret;
  ParseStringBuffer[nextPtr_ParseStringBuffer]='\0';
  ret = ParseStringBuffer;
  return ret;
}

// https://stackoverflow.com/questions/26854374/how-do-i-use-yy-bs-lineno-and-yy-bs-column-in-flex
int yycolumn = 1;
#define YY_USER_ACTION \
  yylloc.first_line = yylloc.last_line = yylineno; \
  yylloc.first_column = yycolumn; \
  yylloc.last_column = yycolumn + yyleng - 1; \
  yycolumn += yyleng; \
  //yylval.str = strdup(yytext);


%}

%option nounput
%option noyywrap
%option yylineno
%x STRING_STATE COM_STATE
%%
<INITIAL>"><" return (CROSS);
<INITIAL>"=>" return (ARROW);
<INITIAL>"|" return(PIPE);
<INITIAL>"&&" return(AND);
<INITIAL>"and" return(AND);
<INITIAL>"||" return(OR);
<INITIAL>"or" return(OR);
<INITIAL>"!" return(NOT);
<INITIAL>"not" return(NOT);
<INITIAL>"=" return(LD);
<INITIAL>"==" return(EQUAL);
<INITIAL>"!=" return(NE);
<INITIAL>">=" return(GE);
<INITIAL>"<=" return(LE);
<INITIAL>"<<" return (ABR);

<INITIAL>"int" return(INT);

<INITIAL>"let" return(LET);
<INITIAL>"in" return(IN);
<INITIAL>"end" return(END);

<INITIAL>"where" return(WHERE);

<INITIAL>"if" return(IF);
<INITIAL>"then" return(THEN);
<INITIAL>"else" return(ELSE);

<INITIAL>"interface" return(INTERFACE);
<INITIAL>"ifce" return(IFCE);

<INITIAL>"prnat" return(PRNAT);
<INITIAL>"free" return(FREE);
<INITIAL>"memstat" return(MEMSTAT);
<INITIAL>"exit" return(EXIT);

<INITIAL>"rand" return(RAND);

<INITIAL>"const" return(DEF);

<INITIAL>"use" return(USE);

<INITIAL>"*L" return(ANNOTATE_L);
<INITIAL>"*R" return(ANNOTATE_R);
<INITIAL>[a-z][a-zA-Z0-9_'\.]* { yylval.chval = strdup(yytext);
         return (NAME);}
<INITIAL>[A-Z][a-zA-Z0-9_'\.]* { yylval.chval = strdup(yytext);
         return (AGENT);}
<INITIAL>[0-9]+ { yylval.longval = atol(yytext);
         return (NUMERAL_LITERAL);}

<INITIAL>"//"[^\n]* ; //comment
<INITIAL>"/*" BEGIN(COM_STATE);
<COM_STATE>. ;
<COM_STATE>\n ;
<COM_STATE>"*/" BEGIN INITIAL; //comment

<INITIAL>\r\n { yycolumn=1; };
<INITIAL>[\n|\r] { yycolumn=1; };
<INITIAL>[ \t]+ ;
<INITIAL><<EOF>> {return END_OF_FILE;}
<INITIAL>\" {
    Parse_StringBuffer_reInit();
    BEGIN STRING_STATE;
}

<INITIAL>. {return yytext[0];}




<STRING_STATE>\"        {
    yylval.chval = strdup(Parse_closeStringBuffer());
    BEGIN INITIAL;
    return STRING_LITERAL;
}
<STRING_STATE>\n        {
//    Parse_addString('\n');
yyerror("Unterminated string");
BEGIN(INITIAL);
return ';';
}
<STRING_STATE>\\\"      Parse_addString('"');
<STRING_STATE>\\n       Parse_addString('\n');
<STRING_STATE>\\t       Parse_addString('\t');
<STRING_STATE>\\\\      Parse_addString('\\');
<STRING_STATE>.         Parse_addString(yytext[0]);
%%

void pushFP(FILE *fp) {
  yypush_buffer_state(yy_create_buffer(fp, YY_BUF_SIZE));
}

int popFP() {
  yypop_buffer_state();
  if (! YY_CURRENT_BUFFER ) {
    yyterminate();
    return 0;
  }
  return 1;
}
