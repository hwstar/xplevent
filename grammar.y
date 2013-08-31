


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
	parseCtrl->failReason = talloc_asprintf(parseCtrl, "Syntax error on line: %d", parseCtrl->lineNo);
}

%nonassoc EQUALS .
%left OPAREN CPAREN .


/*
* Top of parse tree
*/

start ::= statementlist .

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

/*
* Expression
*/

expression ::= function .

/*
* Function
*/

function ::= builtinFunction(A) OPAREN argumentlist CPAREN .
{
	debug(DEBUG_ACTION, "Builtin function exec Token ID: %d", A->tokenID);
	/* Call builtin function handler callback if enabled */
	ParserExecFunction(parseCtrl, A->tokenID);
	/* Call cleanup */
	ParserPostFunctionCleanup(parseCtrl);
	

}

/*
* Builtin xplcmd()
*/

builtinFunction(A) ::= XPLCMD(B) .
{
	debug(DEBUG_ACTION, "Builtin function xplcmd");
	A = B;
}


/*
* Argument to argument list (sucessive)
*/

argumentlist ::= argumentlist COMMA argument .
{
	debug(DEBUG_ACTION, "Add to arg list");
}

/*
* Argument to argument list (initial)
*/

argumentlist ::= argument .
{
    debug(DEBUG_ACTION, "Start arg list");
}

/*
* rvalue to argument
*/

argument ::= rvalue(A) .
{
	debug(DEBUG_ACTION, "Add string to argument list: %s", A->stringVal);
	ParserAddFunctionArg(parseCtrl, A->stringVal, ATYPE_STRING);
}

/*
* xplout hash
*/


argument ::= BACKSLASH PERCENT XPLOUT(A) .
{
	debug(DEBUG_ACTION, "Add hash: %s to argument list", A->stringVal);
	ParserAddFunctionArg(parseCtrl, A->stringVal, ATYPE_HASH);
}

/*
* rvalue to hash assignment
*/

expression ::= lhash(A) EQUALS rvalue(B) .
{
	debug(DEBUG_ACTION, "Lhash to rvalue assignment");
	ParserHashAddKeyValue(&parseCtrl->xplOutHead, parseCtrl->xplOutContext, A->stringVal, B->stringVal);
}

/*
* hash to hash assignment
*/

expression ::= lhash(A) EQUALS rhash(B) .
{	
	const char *val;
	
	if(!(val = ParserHashGetValue(parseCtrl->argsHead, B->stringVal))){
		debug(DEBUG_UNEXPECTED, "Cannot find key %s in args", B->stringVal);
		parseCtrl->failReason = talloc_asprintf(parseCtrl, "Key %s does not exist in hash %s on line %d",
		B->stringVal, A->stringVal, parseCtrl->lineNo);
		ASSERT_FAIL(parseCtrl->failReason);
	}
	else{
		debug(DEBUG_ACTION,"Adding key %s to $xplout", val); 
		ParserHashAddKeyValue(&parseCtrl->xplOutHead, parseCtrl->xplOutContext, A->stringVal, val); 
	}
	
}

/*
* rvalue hash
*/

rhash(A) ::= DOLLAR ARGS OBRACE BAREWORD(B) CBRACE .
{
	debug(DEBUG_ACTION, "$args found, BAREWORD = %s", B->stringVal);
	A = B;
}

/*
* lvalue hash
*/

lhash(A) ::= DOLLAR XPLOUT OBRACE BAREWORD(B) CBRACE .
{
	debug(DEBUG_ACTION, "$xplout found, BAREWORD = %s", B->stringVal);	
	A = B;
}

/*
* Integer literal to rvalue
*/

rvalue(A) ::= INTLIT(B) .
{
	ASSERT_FAIL(B)
	debug(DEBUG_ACTION, "rvalue is an intlit, value = %s", B->stringVal);
	A = B;
}

/*
* String literal to rvalue
*/

rvalue(A) ::= STRINGLIT(B) .
{
	tokenPtr_t token;
	char *s = NULL;
	char *p;
	int i;
	
	ASSERT_FAIL(B)
	
	debug(DEBUG_ACTION, "rvalue is a stringlit, value = %s", B->stringVal);
	
	/* Allocate a token */
	token = talloc_zero(parseCtrl, token_t);
	ASSERT_FAIL(token)
	if(token){
		/* Allocate a string of the same size as the token value */
		s = talloc_size(token, strlen(B->stringVal));
		ASSERT_FAIL(s)
	}
	
	
	/* Strip off double quotes */
	/* FIXME: This should not strip escaped quotes */
	for(i = 0, p = B->stringVal; *p; p++){ /* Strip quotes */
		if(*p == '"')
			continue;
		s[i++] = *p;
	}
	/* NUL terminate new string */
	s[i] = 0;
		
	/* Initialize token */
	token->stringVal = s;	

	
		
	/* Assign token pointer */
	A = token;
	
	/* Free original token */
	talloc_free(B);
}

/*
* Catch-all
*/

start ::= BADCHAR .

	





