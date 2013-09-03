


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
start ::= ifelseconst .
start ::= ifconst .

/*
* If construct
*/


ifconst ::= iftest block .


/*
* Else construct
*/

ifelseconst ::= iftest block elsekw(A) block .
{
	ParserUpdateIf(parseCtrl, A);
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
* If keyword
*/

iftest	::= ifkw OPAREN test CPAREN .

ifkw(A)	::= IF(B) .
{
	ParserPcodeEmit(parseCtrl, OP_IF, 0, "if statement", NULL);
	A = B;
}

elsekw(A)	::= ELSE(B) .
{
	A = B;
}


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

test ::= rhash EQUALS EQUALS rvalue .
{
	ParserPcodeEmit(parseCtrl, OP_TEST, OPRT_NUMEQUALITY, "test numeric equality", "rhash eq eq rvalue");
}

test ::= rvalue EQUALS EQUALS rhash .
{
	ParserPcodeEmit(parseCtrl, OP_TEST, OPRT_NUMEQUALITY, "test numeric equality", "rvalue eq eq rhash");
}

test ::= lhash EQUALS EQUALS rhash .
{
	ParserPcodeEmit(parseCtrl, OP_TEST, OPRT_NUMEQUALITY, "test numeric equality", "lhash eq eq rhash");
}


test ::= rhash EQUALS EQUALS lhash .
{
	ParserPcodeEmit(parseCtrl, OP_TEST, OPRT_NUMEQUALITY, "test numeric equality", "rhash eq eq lhash");
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
	
	/* Allocate a string of the same size as the token value */
	s = talloc_size(parseCtrl, strlen(A->stringVal));
	ASSERT_FAIL(s)

	
	
	/* Strip off double quotes */
	/* FIXME: This should not strip escaped quotes */
	for(i = 0, p = A->stringVal; *p; p++){ /* Strip quotes */
		if(*p == '"')
			continue;
		s[i++] = *p;
	}
	/* NUL terminate new string */
	s[i] = 0;		
	
	/* Emit pcode */
	ParserPcodeEmit(parseCtrl, OP_PUSH, OPRD_STRINGLIT, s, "string literal");

	/* Free string */
	talloc_free(s);
	
	/* Free original token */
	talloc_free(A);
	
	 
}





	





