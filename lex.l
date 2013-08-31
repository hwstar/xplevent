%{
#include <stdio.h>
#include <talloc.h>
#include "defs.h"
#include "types.h"
#include "parser.h"
#include "notify.h"
#include "grammar.h"


#define yyterminate() return(0)

static void *lexContext = NULL;

%}



%option yylineno noyywrap noyyalloc noyyrealloc noyyfree 

digit       [0-9]
letter      [a-zA-Z]
intlit		{digit}{digit}*
ucn			((\\u([[:xdigit:]]{4}))|(\\U([[:xdigit:]]{8})))
bareword	([_[:alpha:]]|{ucn})([_[:alnum:]]|{ucn})*
stringlit	L?\"([^\"\\\n]|(\\['\"?\\abfnrtv])|(\\([0123456]{1,3}))|(\\x[[:xdigit:]]+)|(\\u([[:xdigit:]]{4}))|(\\U([[:xdigit:]]{8})))*\"
comment		#.*\n

%%		/* Rules */

"("						{return (TOK_OPAREN);}
")"						{return (TOK_CPAREN);}
"{"						{return (TOK_OBRACE);}
"}"						{return (TOK_CBRACE);}
"="						{return (TOK_EQUALS);}
";"						{return (TOK_SEMI);}
","						{return (TOK_COMMA);}
"$"						{return (TOK_DOLLAR);}
"%"						{return (TOK_PERCENT);}
"\\"					{return (TOK_BACKSLASH);}
"xplcmd"				{return (TOK_XPLCMD);}
"args"					{return (TOK_ARGS);}
"xplout"				{return (TOK_XPLOUT);}

{bareword}				{return (TOK_BAREWORD);}
{intlit}				{return (TOK_INTLIT);}
{stringlit}				{return (TOK_STRINGLIT);}
{comment}				/* skip comment */
[ \t\n\r]       		/* skip whitespace */
.						{return (TOK_BADCHAR);}
%%


void *yyalloc(size_t size)
{
	void *p;
	
	ASSERT_FAIL(lexContext)
	
	p = talloc_size(lexContext, size);
	ASSERT_FAIL(p);

	return p;
}

void *yyrealloc (void * ptr, size_t bytes)
{
	void *p;
	
	ASSERT_FAIL(lexContext)

	p = talloc_realloc_size(lexContext, ptr, bytes);
	ASSERT_FAIL(p);
	return p;
}

void yyfree (void * ptr)
{
	talloc_free(ptr);
}


void yyInit(void * context)
{
	lexContext = context;
}


void yyScanString(const char *s)
{   
	ASSERT_FAIL(s);
}

void yyDeleteBuffer(void)
{
	yy_delete_buffer( YY_CURRENT_BUFFER );
}

int yyLex(void)
{
	return yylex();
}

tokenPtr_t yyTokenVal(void *context)
{
	String res = NULL;
	tokenPtr_t token;
	
	/* Context must be valid */
	ASSERT_FAIL(context)
	
	/* Allocate a token */
	token = talloc_zero(context, token_t);
	ASSERT_FAIL(token);
	

	/* copy string from lexer */
	res = talloc_strndup(token, yytext, yyleng + 1);
	ASSERT_FAIL(res);

	res[yyleng] = 0;
	token->stringVal = res;
	token->lineNo = yylineno;
	

	return token;
	
}

void yySetInputFile(FILE * f)
{
	ASSERT_FAIL(f)
	yyin = f;
}

