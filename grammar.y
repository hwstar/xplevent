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
#include "util.h"
#include "notify.h"
#include "grammar.h"
#include "parser.h"
#include "xplevent.h"

}

%token_prefix TOK_
%extra_argument { ParseCtrlPtr_t parseCtrl }
%token_type {tokenPtr_t}
%default_type {tokenPtr_t}



%syntax_error
{ 
	parseCtrl->failReason = talloc_asprintf(parseCtrl, "Syntax error on or near line: %d", parseCtrl->lineNo);
}

%nonassoc EQEQ NEQ .
%nonassoc EQUALS .
%nonassoc OPAREN CPAREN .

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
	ParserPcodeEmit(parseCtrl, OP_FUNC, A->tokenID, A->stringVal, NULL); 
}

/*
* Builtin xplcmd()
*/

builtinFunction(A) ::= XPLCMD(B) .
{
	A = B;
}

/*
* Builtin command spawn another program
*/

builtinFunction(A) ::= SPAWN(B) .
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
* hash reference
*/


argument ::= BACKSLASH HASH(A) .
{
	A->stringVal = UtilMoveString(A, A->stringVal, 1); /* Chop off percent */
	MALLOC_FAIL(A->stringVal);
	ParserPcodeEmit(parseCtrl, OP_PUSH, OPRD_HASHREF, A->stringVal, NULL); 
}

/*
* assignment
*/

assignment ::= hash EQUALS rvalue .
{

	ParserPcodeEmit(parseCtrl, OP_ASSIGN, 0, "hash eq rvalue", NULL);
}


assignment ::= hash EQUALS hash .
{	

	ParserPcodeEmit(parseCtrl, OP_ASSIGN, 0, "hash eq rhash", NULL);
		
}

/*
* Numeric Equality test
*/

test ::= hash testop(A) rvalue .
{
	ParserPcodeEmit(parseCtrl, OP_TEST2, A->operand, "test", A->anno);
}

test ::= rvalue testop(A) hash .
{
	ParserPcodeEmit(parseCtrl, OP_TEST2, A->operand, "test", A->anno);
}

test ::= hash testop(A) hash .
{
	ParserPcodeEmit(parseCtrl, OP_TEST2, A->operand, "test", A->anno);
}

test ::= rvalue testop(A) rvalue .
{
	ParserPcodeEmit(parseCtrl, OP_TEST2, A->operand, "test", A->anno);
}

/*
* Array key exists test
*/

test ::= EXISTS OPAREN hash CPAREN .
{
	ParserPcodeEmit(parseCtrl, OP_EXISTS, 0, "test", "exists"); 
}



testop(A) ::= EQEQ . /* Numeric Equality */
{
	MALLOC_FAIL(A = talloc_zero(parseCtrl, token_t)) 
	A->operand = OPRT_NUMEQUALITY;
	A->anno = "Num eq.";
}

testop(A) ::= NGT . /* Numeric Greater Than */
{
	MALLOC_FAIL(A = talloc_zero(parseCtrl, token_t)) 
	A->operand = OPRT_NUMGTRTHAN;
	A->anno = "Num gtr. than";
}

testop(A) ::= NLT . /* Numeric Less than */
{
	MALLOC_FAIL(A = talloc_zero(parseCtrl, token_t)) 
	A->operand = OPRT_NUMLESSTHAN;
	A->anno = "Num less than";
}


testop(A) ::= NGET . /* Numeric Greater Than */
{
	MALLOC_FAIL(A = talloc_zero(parseCtrl, token_t)) 
	A->operand = OPRT_NUMGTREQTHAN;
	A->anno = "Num gtr. eq. than";
}

testop(A) ::= NLET . /* Numeric Less than */
{
	MALLOC_FAIL(A = talloc_zero(parseCtrl, token_t)) 
	A->operand = OPRT_NUMLESSEQTHAN;
	A->anno = "Num less eq. than";
}

testop(A) ::= NEQ . /* Numeric Not Equal */
{
	MALLOC_FAIL(A = talloc_zero(parseCtrl, token_t)) 
	A->operand = OPRT_NUMINEQUALITY;
	A->anno = "Num ineq.";
}




testop(A) ::= SEQ . /* String Equality */
{
	MALLOC_FAIL(A = talloc_zero(parseCtrl, token_t)) 
	A->operand = OPRT_STREQUALITY;
	A->anno = "String eq.";
}


/*
*  hash
*/

hash ::= SCALAR(B) OBRACE BAREWORD(A) CBRACE .
{
	B->stringVal = UtilMoveString(B, B->stringVal, 1); /* Chop off dollar sign */
	MALLOC_FAIL(B->stringVal);
	
	ParserPcodeEmit(parseCtrl, OP_PUSH, OPRD_HASHKV, B->stringVal, A->stringVal);
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

rvalue ::= FLOATLIT(A) .
{
	ASSERT_FAIL(A)
	ParserPcodeEmit(parseCtrl, OP_PUSH, OPRD_FLOATLIT, A->stringVal, "float literal"); 
	talloc_free(A);
}

/*
* String literal to rvalue
*/

rvalue ::= STRINGLIT(A) .
{

	ASSERT_FAIL(A)
	
	/* Strip off double quotes */
	
	A->stringVal[strlen(A->stringVal) - 1] = 0;
	A->stringVal = UtilMoveString(A, A->stringVal, 1);
	MALLOC_FAIL(A->stringVal);
	
	
	/* Emit pcode */
	ParserPcodeEmit(parseCtrl, OP_PUSH, OPRD_STRINGLIT, A->stringVal, "string literal");

	 
}





	





