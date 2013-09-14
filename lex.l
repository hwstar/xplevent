/*
 * lex.l
 * 
 *  Copyright (C) 2013  Stephen Rodgers
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 * 
 * 
 * Stephen "Steve" Rodgers <hwstar@rodgers.sdcoxmail.com>
 *
 */

%{
#include <stdio.h>
#include <talloc.h>
#include "defs.h"
#include "types.h"
#include "util.h"
#include "parser.h"
#include "notify.h"
#include "grammar.h"
#include "xplevent.h"


#define yyterminate() return(0)

static void *lexContext = NULL;

%}



%option yylineno noyywrap noyyalloc noyyrealloc noyyfree 

digit       [0-9]
letter      [a-zA-Z]
floatlit	{digit}+"."*{digit}+
intlit		{digit}{digit}*
ucn			((\\u([[:xdigit:]]{4}))|(\\U([[:xdigit:]]{8})))
bareword	([_[:alpha:]]|{ucn})([_[:alnum:]]|{ucn})*
scalar		[\$]{bareword}
hash		%{bareword}
stringlit	L?\"([^\"\\\n]|(\\['\"?\\abfnrtv])|(\\([0123456]{1,3}))|(\\x[[:xdigit:]]+)|(\\u([[:xdigit:]]{4}))|(\\U([[:xdigit:]]{8})))*\"
comment		#.*\n

%%		/* Rules */
">="					{return (TOK_NGET);}
"<="					{return (TOK_NLET);}
"=="					{return (TOK_EQEQ);}
"!="					{return (TOK_NEQ);}
"("						{return (TOK_OPAREN);}
")"						{return (TOK_CPAREN);}
"{"						{return (TOK_OBRACE);}
"}"						{return (TOK_CBRACE);}
"="						{return (TOK_EQUALS);}
";"						{return (TOK_SEMI);}
","						{return (TOK_COMMA);}
"\\"					{return (TOK_BACKSLASH);}
">"						{return (TOK_NGT);}
"<"						{return (TOK_NLT);}
"eq"					{return (TOK_SEQ);}
"xplcmd"				{return (TOK_XPLCMD);}
"spawn"					{return (TOK_SPAWN);}
"if"					{return (TOK_IF);}
"else"					{return (TOK_ELSE);}
"exists"				{return (TOK_EXISTS);}

{scalar}				{return (TOK_SCALAR);}
{hash}					{return (TOK_HASH);}
{bareword}				{return (TOK_BAREWORD);}
{floatlit}				{return (TOK_FLOATLIT);}
{intlit}				{return (TOK_INTLIT);}
{stringlit}				{return (TOK_STRINGLIT);}
{comment}				/* skip comment */
[ \t\n\r]       			/* skip whitespace */
.					{return (TOK_BADCHAR);}
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
	yy_scan_string(s);
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
	MALLOC_FAIL(token);
	

	/* copy string from lexer */
	res = talloc_strndup(token, yytext, yyleng + 1);
	MALLOC_FAIL(res);

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

void yyLexDestroy(void)
{
	yylex_destroy();
}

