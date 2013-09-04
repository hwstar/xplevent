/*
 * grammar.y 
 * 
 *  Copyright (C) 2012,2013  Stephen Rodgers
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

%include{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <talloc.h>
#include "defs.h"
#include "types.h"
#include "notify.h"
#include "grammar.h"
#include "parser.h"

}

%token_prefix TOK_
%extra_argument { ParseCtrlPtr_t parseCtrl }
%token_type {tokenPtr_t}
%default_type {tokenPtr_t}



%syntax_error
{ 
	parseCtrl->failReason = talloc_asprintf(parseCtrl, "Syntax error on or near line: %d", parseCtrl->lineNo);
}

%nonassoc EQUALS .
%left OPAREN CPAREN .

/*
* Top of parse tree
*/

start ::= statementlist .
start ::= BADCHAR .


/*
* Statement list (successive)
*/

statementlist ::= statementlist statement .

/*
* Statement list (initial)
*/

statementlist ::= statement .

/*
* Statement
*/

statement ::= expression SEMI .
statement ::= assignment SEMI .
statement ::= ifelseconst .
statement ::= ifconst .


/*
* If construct
*/


ifconst ::= iftest block .
{
	
	ParserSetJumps(parseCtrl, TOK_IF);
	parseCtrl->pcodeHeader->ctrlStructRefCount--;
}



/*
* Else construct
*/

ifelseconst ::= iftest block ELSE block .
{
	ParserSetJumps(parseCtrl, TOK_ELSE);
	parseCtrl->pcodeHeader->ctrlStructRefCount--;
}


/*
* Block
*/


block ::= blockstart statementlist blockend .

/*
* Block start
*/

blockstart ::= OBRACE .
{
	ParserPcodeEmit(parseCtrl, OP_BLOCK, OPRB_BEGIN, "***BLOCK START***", NULL); 
}

/*
* Block end
*/

blockend ::= CBRACE .
{
	ParserPcodeEmit(parseCtrl, OP_BLOCK, OPRB_END, "***BLOCK END***", NULL); 
}

/*
* If test
*/

iftest	::= ifkw OPAREN test CPAREN .

ifkw ::= IF .
{
	parseCtrl->pcodeHeader->ctrlStructRefCount++;
}



/*
* Expression
*/

expression ::= function .


/*
* Function
*/

function ::= builtinFunction(A) OPAREN argumentlist CPAREN .
{
	ParserPcodeEmit(parseCtrl, OP_FUNC, 0, A->stringVal, NULL); 
}

/*
* Builtin xplcmd()
*/

builtinFunction(A) ::= XPLCMD(B) .
{
	A = B;
}


/*
* Argument to argument list (sucessive)
*/

argumentlist ::= argumentlist COMMA argument .
{
}

/*
* Argument to argument list (initial)
*/

argumentlist ::= argument .
{
}

/*
* rvalue to argument
*/

argument ::= rvalue .
{
}

/*
* xplout hash
*/


argument ::= BACKSLASH PERCENT XPLOUT(A) .
{
	ParserPcodeEmit(parseCtrl, OP_PUSH, OPRD_HASHREF, A->stringVal, NULL); 
}

/*
* assignment
*/

assignment ::= lhash EQUALS rvalue .
{

	ParserPcodeEmit(parseCtrl, OP_ASSIGN, 0, "lhash eq rvalue", NULL);
}


assignment ::= lhash EQUALS rhash .
{	

	ParserPcodeEmit(parseCtrl, OP_ASSIGN, 0, "lhash eq rhash", NULL);
		
}

/*
* Numeric Equality test
*/

test ::= rhash testop(A) rvalue .
{
	ParserPcodeEmit(parseCtrl, OP_TEST, A->operand, "test", A->anno);
}

test ::= rvalue testop(A) rhash .
{
	ParserPcodeEmit(parseCtrl, OP_TEST, A->operand, "test", A->anno);
}

test ::= lhash testop(A) rhash .
{
	ParserPcodeEmit(parseCtrl, OP_TEST, A->operand, "test", A->anno);
}


test ::= rhash testop(A) lhash .
{
	ParserPcodeEmit(parseCtrl, OP_TEST, A->operand, "test", A->anno);	
}

testop(A) ::= EQEQ . /* Numeric Equality */
{
	ASSERT_FAIL(A = talloc_zero(parseCtrl, token_t)) 
	A->operand = OPRT_NUMEQUALITY;
	A->anno = "Num eq.";
}

testop(A) ::= NEQ . /* Numeric Not Equal */
{
	ASSERT_FAIL(A = talloc_zero(parseCtrl, token_t)) 
	A->operand = OPRT_NUMINEQUALITY;
	A->anno = "Num ineq.";
}



/*
* rvalue hash
*/

rhash ::= DOLLAR ARGS OBRACE BAREWORD(A) CBRACE .
{
	if(NULL == ParserHashGetValue(parseCtrl->pcodeHeader->argsHead, A->stringVal)){
		parseCtrl->failReason = talloc_asprintf(parseCtrl,
		"Hash $args contains no key named %s on or near line %d", A->stringVal, parseCtrl->lineNo);
	}
	ParserPcodeEmit(parseCtrl, OP_PUSH, 2, "args", A->stringVal);
}

/*
* lvalue hash
*/

lhash ::= DOLLAR XPLOUT OBRACE BAREWORD(A) CBRACE .
{
	ParserPcodeEmit(parseCtrl, OP_PUSH, OPRD_HASHKV, "xplout", A->stringVal);
}

/*
* Integer literal to rvalue
*/

rvalue ::= INTLIT(A) .
{
	ASSERT_FAIL(A)
	ParserPcodeEmit(parseCtrl, OP_PUSH, OPRD_INTLIT, A->stringVal, "integer literal"); 
	talloc_free(A);
}

/*
* String literal to rvalue
*/

rvalue ::= STRINGLIT(A) .
{
	String s = NULL;
	String p;
	int i;
	
	ASSERT_FAIL(A)
	
	/* Strip off double quotes */
	
	A->stringVal[strlen(A->stringVal) - ] = 0;
	A->stringVal = ParserMoveString(A, A->stringVal, 1);
	ASSERT_FAIL(A->stringVal);
	
	
	/* Emit pcode */
	ParserPcodeEmit(parseCtrl, OP_PUSH, OPRD_STRINGLIT, s, "string literal");

	 
}





	





