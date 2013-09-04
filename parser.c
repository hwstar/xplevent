/*
 * parser.c
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


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <talloc.h>
#include <ctype.h>
#include <string.h>
#include <limits.h>
#include <xPL.h>

#include "defs.h"
#include "types.h"
#include "notify.h"
#include "grammar.h"
#include "parser.h"
#include "parse.h"
#include "lex.h"

/* Definitions */


#define KE_MAGIC	0x4F7B127A
#define AL_MAGIC	0x689C9A2F
#define PC_MAGIC	0x9A905437
#define SE_MAGIC	0x59F593AC






static ParseCtrlPtr_t parseCtrl; /* Not re-entrant because of this */



/*
* Hash a string
*/

static uint32_t hash(const String key)
{
	int len = strlen(key);
	register uint32_t hash, i;

	if(!key)
		return 0;

	for(hash = i = 0; i < len; ++i){
		hash += key[i];
		hash += (hash << 10);
		hash ^= (hash >> 6);
 	}
	hash += (hash << 3);
	hash ^= (hash >> 11);
	hash += (hash << 15);
	return hash;
}

			


/*
 * Malloc for parser
 */

static void *parserMalloc(size_t size)
{	
	void *p;
	p = talloc_size(parseCtrl, size);
	ASSERT_FAIL(p)
	return p;
}

/*
 * Free for parser
 */


static void parserFree(void *ctx)
{
	talloc_free(ctx);
}

/*
* Print P-code info
*/

static void printOpcode(pcodePtr_t p)
{
	String op ,data1, data2;
	
	
	switch(p->opcode){
		case OP_NOP:
			op = "Nop";
			break;
		
		case OP_PUSH:
			op = "Push";
			break;
		
		case OP_ASSIGN:
			op = "Assign";
			break;
			
		case OP_FUNC:
			op = "Func";
			break;
				
		case OP_BLOCK:
			op = "Block";
			break;
				
		case OP_IF:
			op = "If";
			break;	
				
		case OP_TEST:
			op = "Test";
			break;
			

		default:
			op = "UNK";
			break;
	}	
	data1 = (p->data1) ? p->data1 : "(nil)";
	data2 = (p->data2) ? p->data2 : "(nil)";
				
	debug(DEBUG_EXPECTED,"Seq: %d CBRC: %d, Opcode: %s, Operand: %d, Data1: %s, Data2: %s, Skip: %p", 
	p->seq, p->ctrlStructRefCount, op, p->operand, data1, data2, p->skip);	
}


/*
* Move string from one context to another
*/
String ParserMoveString(void *newCtx, String oldStr, int offset)
{
	String newStr;
	
	ASSERT_FAIL(newCtx)
	ASSERT_FAIL(oldStr)
	
	if((newStr = talloc_strdup(newCtx, OldStr + offset){
		talloc_free(oldStr);
	}
	
	return newStr;
}

/*
 * Split an xPL tag into its constituent parts.
 * 
 * Check parts for length
 * 
 * To do a validation without returning the strings, set vendor, device, and instance to NULL
 * 
 * Strings must be freed unless set to NULL
 * 
 */

int ParserSplitXPLTag(TALLOC_CTX *ctx, const String tag, String *vendor, String *device, String *instance)
{
	int res = PASS;
	int done = FALSE;
	int i,j,state, begin;
	
	for(i = 0, j = 0, state = 0, begin = 0; !done; i++,j++){

		switch(state){
			case 0: /* Search for hyphen */
				if(!tag[i]){
					done = TRUE;
					res = FAIL;
					break;
				}
				if('-' == tag[i]){
					if((!j) || (j > 9)){
						done = TRUE;
						res = FAIL;
						break;
					}
					else{
						if(vendor){
							*vendor = talloc_strndup(ctx, tag, j);
							ASSERT_FAIL(*vendor)
						}
						begin = i + 1;
						j = 0;
						state++;
					}
				}
				break;
				
			case 1: /* Search for dot */
				if(!tag[i]){
					done = TRUE;
					res = FAIL;
					break;
				}
				if('.' == tag[i]){
					if((!j) || (j > 9)){
						done = TRUE;
						res = FAIL;
						break;
					}
					else{
						if(device){
								*device = talloc_strndup(ctx, tag + begin, j - 1);
								ASSERT_FAIL(*device);
						}
						begin = i + 1;
						j = 0;
						state++;
					}
				}
				
			case 2: /* Search for end of string and measure length */
				if(!tag[i]){
					if((!j) || (j > 9)){
						res = FAIL;
						
					}
					if(res != FAIL && device){
							*instance = talloc_strndup(ctx, tag + begin, j - 1);
							ASSERT_FAIL(*device);
					}
					done = TRUE;
				}
				break;

			default:
				ASSERT_FAIL(FALSE);	
		}
		
		
	}
	return res;	
}



/*
 * Get a value from the hash
 */

const String ParserHashGetValue(ParseHashKVPtr_t pHead, const String key)
{
	unsigned kh;
	ParseHashKVPtr_t ke;

	ASSERT_FAIL(pHead && (pHead->magic == KE_MAGIC) && key)

	/* Hash the section string passed in */
	kh = hash(key);
	for(ke = pHead; (ke); ke = ke->next){ /* Traverse key list */
		ASSERT_FAIL(KE_MAGIC == ke->magic)
		/* Compare hashes, and if they match, compare strings */
		if((kh == ke->hash) && (!strcmp(ke->key, key)))
			return ke->value;
	}
	return NULL; /* No match found */
}

/*
 * Add a value to the hash
 * Replace any entry with a matching key which is already there
 */
 
void ParserHashAddKeyValue(ParseHashKVPtrPtr_t ppHead, void *tallocContext, const String key, const String value)
{
	ParseHashKVPtrPtr_t pph = (ParseHashKVPtrPtr_t) ppHead;
	ParseHashKVPtr_t ke, keNew, kePrev;
	

	ASSERT_FAIL(ppHead && key && value)
		
	/* Initialize a new list entry */
	 
	keNew = talloc_zero(tallocContext, ParseHashKV_t);
	ASSERT_FAIL(keNew)
	keNew->magic = KE_MAGIC;
	keNew->key = talloc_strdup(keNew, key);
	ASSERT_FAIL(keNew->key)
	keNew->value = talloc_strdup(keNew, value);
	ASSERT_FAIL(keNew->value);
	keNew->hash = hash(key);

	if(!*pph){
		/* First entry */
		debug(DEBUG_ACTION, "First entry in hash: %s", key);
		*pph = keNew;
	}
	else{
		for(ke = *pph, kePrev = NULL; (ke); ke = ke->next){ /* Traverse key list */
			ASSERT_FAIL(KE_MAGIC == ke->magic);
			/* Compare hashes, and if they match, compare strings */
			if((keNew->hash == ke->hash) && (!strcmp(ke->key, key))){
				/* Key already exists, need to replace it */
				keNew->next = ke->next;
				if(!kePrev){
					debug(DEBUG_ACTION, "Replacing first entry in hash: %s", key);
					*pph = keNew; /* Change first entry */
				}
				else{
					debug(DEBUG_ACTION, "Replacing other than the first entry in hash: %s", key);
					kePrev->next = keNew; /* Change subsequent entry */
				}	
				talloc_free(ke); /* Free the old entry and its strings */
				break;
			}
			else if(!ke->next){
				debug(DEBUG_ACTION, "Appending entry to end of hash: %s", key);
				/* At end of list, need to append it */
				ke->next = keNew;
				break;	
			}
			kePrev = ke;	
		}
	}	
}

/*
 * Walk the list calling a callback function with each key/value
 */
 
void ParserHashWalk(ParseHashKVPtr_t pHead, void (*parseHashWalkCallback)(const String key, const String value))
{
	ParseHashKVPtr_t ke;
	
	ASSERT_FAIL(pHead)
	
	/* NULL function pointer is fatal */
	ASSERT_FAIL(parseHashWalkCallback)
		
	for(ke = pHead; (ke); ke = ke->next){ /* Traverse key list */
		ASSERT_FAIL(KE_MAGIC == ke->magic)
		(*parseHashWalkCallback)(ke->key, ke->value);
	}
	
}

/*
* Add a hash to the symbol table
*
* If the hash already exists, return FAIL, else return PASS
*/

int ParserHashAdd(pcodeHeaderPtr_t ph, String name, Bool writable)
{
	ParseHashSTEPtr_t hNew, se;
	ASSERT_FAIL(ph)
	ASSERT_FAIL(name)
		
	/* Initialize a new list entry */
	 
	hNew = talloc_zero(ph, ParseHashSTE_t);
	ASSERT_FAIL(hNew)
	hNew->magic = SE_MAGIC;
	hNew->name = talloc_strdup(hNew, name);
	ASSERT_FAIL(hNew->name)
	hNew->hash = hash(name);
	hNew->writable = writable;

	if(!ph->steHead){
		/* First entry */
		ph->steHead = hNew;
	}
	else{
		for(se = ph->steHead; (se); se = se->next){ /* Traverse symbol list */
			ASSERT_FAIL(SE_MAGIC == se->magic);
			/* Compare hashes, and if they match, compare strings */
			if((hNew->hash == se->hash) && (!strcmp(se->name, name))){
				talloc_free(hNew);
				return FAIL;
			}
			else if(!se->next){
				/* At end of list, need to append it */
				se->next = hNew;
				return PASS;	
			}
		}
	}
	
}



/*
 * Return the value for the push instruction passed in
 */
 

Bool ParserPcodeGetValue(pcodeHeaderPtr_t ph, pcodePtr_t instr, String *pValue)
{
	String value = NULL;
	
	ASSERT_FAIL(ph);
	ASSERT_FAIL(instr)
	ASSERT_FAIL(pValue)
	
	/* Do something based on the operand */
	switch(instr->operand){
		
		case OPRD_STRINGLIT: /* Literals */
		case OPRD_INTLIT:
			value = instr->data1;
			break;
			
		case OPRD_HASHKV:  /* Assoc array key/value */
			if(!strcmp("args", instr->data1)){
				if(ph->argsHead){
					value = ParserHashGetValue(ph->argsHead, instr->data2);
				}
			}
			else if(!strcmp("xplout", instr->data1)){
				if(ph->xplOutHead){
					value = ParserHashGetValue(ph->xplOutHead, instr->data2);
				}
			}
			break;
			
		case OPRD_HASHREF: /* Hash reference */
			value = instr->data1;
			break;
			
		default:
			ASSERT_FAIL(0)
	}
	if(!value)
		return FAIL;
	else{
		*pValue = value;
		return PASS;
	}
}

/*
 * Put a value in a variable
 */

Bool ParserPcodePutValue(pcodeHeaderPtr_t ph, pcodePtr_t instr, String value)
{
	ASSERT_FAIL(ph)
	ASSERT_FAIL(instr)
	ASSERT_FAIL(value)
	
	if(instr->operand != OPRD_HASHKV){
		return FAIL;
	}
	if(strcmp("xplout", instr->data1)){
		return FAIL;
	}
	ASSERT_FAIL(ph->xplOutContext);
	
	ParserHashAddKeyValue(&ph->xplOutHead, ph->xplOutContext, instr->data2, value);
	return PASS;
}

/*
 * This is called by the parser when a pcode structure has to be added to the list
 */

void ParserPcodeEmit(ParseCtrlPtr_t pc, opType_t op, int operand, String data1, String data2)
{
	pcodeHeaderPtr_t ph;
	pcodePtr_t new;
	
	ASSERT_FAIL(pc)
	
	ph = pc->pcodeHeader;
	
	if(ph){
		
		new = talloc_zero(ph, pcode_t);
		ASSERT_FAIL(new);
		
		/* Initialize new list entry */
		new->magic = PC_MAGIC;
		new->opcode = op;
		new->operand = operand;
		new->lineNo = pc->lineNo;
		new->seq = ph->seq++;
		new->ctrlStructRefCount = ph->ctrlStructRefCount;
		
		if(data1){
			/* Make a copy of the string */
			new->data1 = talloc_strdup(new, data1);
			ASSERT_FAIL(new->data1);
		}
		if(data2){
			/* Make a copy of the string */
			new->data2 = talloc_strdup(new, data2);
			ASSERT_FAIL(new->data2);
		}
		
		if(!ph->head){
			/* First entry */
			ph->head = ph->tail = new;
			return;
		}
		else{
			/* Subsequent entry */
			ph->tail->next = new;
			new->prev = ph->tail;
			ph->tail = new; 
		}
	}
}

/*
 * Debug function.
 * 
 * Dump pcode list
 */

void ParserPcodeDumpList(pcodeHeaderPtr_t ph)
{

	pcodePtr_t p;
	int count;
	
	if(!ph)
		return;
	debug(DEBUG_EXPECTED, "*** begin p-code dump ***");
	for(count = 0, p = ph->head; p; p = p->next, count++){
		printOpcode(p);
	}
	debug(DEBUG_EXPECTED, "*** end p-code dump ***");
	
}

	
/*
 * Execute a built-in function
 */
 
#if(0)
void ParserExecFunction(ParseCtrlPtr_t this, int tokenID)
{
	pcodeHeaderPtr_t ph;
	
	ASSERT_FAIL(this)
	
	ph = this->pcodeHeader;
	
	ASSERT_FAIL(ph);
	
	
	debug(DEBUG_ACTION,"Executing function with token id: %d, number of args: %d", tokenID, ph->numFuncArgs);
	
	
	switch(tokenID){
		case TOK_XPLCMD:
			sendXPLCommand(this); 
		break;
		
		default:
			ASSERT_FAIL(FALSE);
	}	
}
#endif

/*
 * Set Jumps for control statements
 */
 
void ParserSetJumps(ParseCtrlPtr_t this, int tokenID)
{
	int myCBRC;
	pcodeHeaderPtr_t ph;
	pcodePtr_t tail, p, elseblock = NULL;
	
	ASSERT_FAIL((tokenID == TOK_ELSE) || (tokenID == TOK_IF))
	
	ASSERT_FAIL(this)
	
	ASSERT_FAIL(ph = this->pcodeHeader)
	
	ASSERT_FAIL(tail = ph->tail)
	
	/* Must be currently on a close block instr */
	ASSERT_FAIL((tail->opcode == OP_BLOCK) && (tail->operand == OPRB_END))
	
	p = tail->prev;
	
	ASSERT_FAIL(p) /* Previous instruction must exist */

	myCBRC = p->ctrlStructRefCount;
	
	if(tokenID == TOK_ELSE){
		debug(DEBUG_ACTION,"Set Jumps TOK_ELSE");
		/* Now, go back in the pcode and find the prior close block instr */
		for(; (p) ; p = p->prev){
			ASSERT_FAIL(p->ctrlStructRefCount >= myCBRC);
			if((p->ctrlStructRefCount == myCBRC) && (p->opcode == OP_BLOCK) && (p->operand == OPRB_END)){
				p->skip = tail; /* Install jump over second block */
				elseblock = p->next; /* Note the beginning of the else block */
				debug(DEBUG_ACTION, "tail seq = %d, elseblock seq = %d", tail->seq, elseblock->seq);
				break;
			}
		}
		ASSERT_FAIL(p)
		ASSERT_FAIL(elseblock)
		
	}
	/* Look for test instruction */		
	for(; (p); p = p->prev){
		ASSERT_FAIL(p->ctrlStructRefCount >= myCBRC);
		if((p->ctrlStructRefCount == myCBRC) && (p->opcode == OP_TEST)){
			if(tokenID == TOK_IF){
				debug(DEBUG_ACTION,"Set Jumps TOK_IF");
				p->skip = tail; /* Note end of block for if test */	
			}
			else{
				p->skip = elseblock; /* Note start of else block for if-else */
			}
			break;	
		}
	}
	ASSERT_FAIL(p);	
}

/*
 * Send xPL command if everything looks good
 */
#if(0)
static void sendXPLCommand(ParseCtrlPtr_t this)
{
	String tag;
	String hash;
	String class;
	String schema;
	TALLOC_CTX *ctx;
	String vendor, device, instance;
	ParseHashKVPtr_t kvp;
	pcodeHeaderPtr_t ph;
	xPL_MessagePtr msg;

	ASSERT_FAIL(this)
	ph = this->pcodeHeader;
	ASSERT_FAIL(ph);
	
	ctx = talloc_new(ph);
	ASSERT_FAIL(ctx)
			
	if(ph->numFuncArgs != 4){
		this->failReason = talloc_asprintf(this, "Incorrect number of arguments passed to xplcmd, requires 4, got %d",
		ph->numFuncArgs);
		goto end;
	}
	tag = ParserFunctionArg(ph->argListHead, 0);
	ASSERT_FAIL(tag)
	class = ParserFunctionArg(ph->argListHead, 1);
	ASSERT_FAIL(class)
	schema = ParserFunctionArg(ph->argListHead, 2);
	ASSERT_FAIL(schema)
	hash = ParserFunctionArg(ph->argListHead, 3);
	ASSERT_FAIL(hash)
	
	if(ParserSplitXPLTag(ctx, tag, &vendor, &device, &instance)){
		this->failReason = talloc_asprintf(this, "Bad xPL Tag: %s", tag);
		goto end;
	}
	if(strcmp("xplout", hash)){
		this->failReason = talloc_asprintf(this, "Hash must be named xplout");
		goto end;
	}

	if(ph->xplServicePtr){
	
			
		/* Create xpl command message */
		msg = xPL_createTargetedMessage(ph->xplServicePtr, xPL_MESSAGE_COMMAND, vendor, device, instance);
		ASSERT_FAIL(msg)
		
		/* Set message schema */
		xPL_setSchema(msg, class, schema); 
		
		/* Clear name/value pairs */
		xPL_clearMessageNamedValues(msg);
	}
	else{
		/* For dry run, just print the vendor, device and instance */
		debug(DEBUG_EXPECTED, "Vendor: %s", vendor);
		debug(DEBUG_EXPECTED, "Device: %s", device);
		debug(DEBUG_EXPECTED, "Instance: %s", instance);
	}		

					
	for(kvp = ph->xplOutHead; kvp; kvp = kvp->next){
		
		if(!ph->xplServicePtr){
			debug(DEBUG_EXPECTED,"Adding Key: %s, Value: %s", kvp->key, kvp->value);
		}
		else{
			xPL_addMessageNamedValue(msg, kvp->key, kvp->value);
		}		
	}
	
	debug(DEBUG_ACTION, "***Sending xPL command***");
	
	if(ph->xplServicePtr){
		xPL_sendMessage(msg);
		xPL_releaseMessage(msg);
	}

end:
	/* Free the xplout hash */
	
	talloc_free(ph->xplOutContext);
	ph->xplOutContext = ph->xplOutHead = NULL;
	
	/* Free the context used here */
	
	talloc_free(ctx);
	
}
#endif

/*
 * Exec p-code generated by parser
 */
 
int ParserExecPcode(pcodeHeaderPtr_t ph)
{
	int res = PASS;
	pcodePtr_t pe;
	String value;
	int leftNum, rightNum;
	Bool testRes;
	
	ASSERT_FAIL(ph)
	
	ASSERT_FAIL(ph->head)
	

	/* Execution loop */
	for(pe = ph->head;(pe) && (!ph->failReason); pe = pe->next){
		printOpcode(pe); // DEBUG

		/* If not a push opcode reset first push location */
		if(pe->opcode != OP_PUSH){
			ph->firstPush = NULL;
		}
		/* Act on op code */	
		switch(pe->opcode){
			
			case OP_NOP:
				break;
				
			case OP_BLOCK:
				if((pe->operand == OPRB_END) && pe->skip){
					debug(DEBUG_ACTION, "Else Block skip");
					pe = pe->skip; /* Skip over next block */
				}
				break;
				
			case OP_PUSH:
				if(!ph->firstPush){
					ph->pushCount = 0;
					ph->firstPush = pe; /* Note first push */
				}
				ph->pushCount++;
				break;
				
			case OP_ASSIGN: /* Assignment */
				if(ph->pushCount != 2){
					ph->failReason = talloc_asprintf(ph, "Assignment requires 2 operands on line %d",pe->lineNo);
					break;
				}
				
				if(ParserPcodeGetValue(ph, pe->prev, &value)){
					ph->failReason = talloc_asprintf(ph, "Can't read variable on line number %d", pe->lineNo);
				}
			    if(ParserPcodePutValue(ph, pe->prev->prev, value)){
					ph->failReason = talloc_asprintf(ph, "Can't change destination variable on line %d", pe->lineNo);
					break;
				}	
				debug(DEBUG_ACTION,"Assign successful on line %d", pe->lineNo);
				break;
				
			case OP_TEST: /* Variable test */
				ASSERT_FAIL(ph->pushCount == 2)
				
				if(ParserPcodeGetValue(ph, pe->prev->prev, &value)){ /* Left */
					ph->failReason = talloc_asprintf(ph, "Can't read left variable on line number %d", pe->lineNo);
					break;
				}
				debug(DEBUG_ACTION,"Left string: %s", value);				
				leftNum = atoi(value);
				
				if(ParserPcodeGetValue(ph, pe->prev, &value)){ /* Right */
					ph->failReason = talloc_asprintf(ph, "Can't read right variable on line number %d", pe->lineNo);
					break;
				}	
				debug(DEBUG_ACTION,"Right string: %s", value);					
				rightNum = atoi(value);
				debug(DEBUG_ACTION,"leftNum = %d, rightNum = %d", leftNum, rightNum);
				
				switch(pe->operand){
					case OPRT_NUMEQUALITY:
						testRes = (leftNum == rightNum);
						break;
						
					case OPRT_NUMINEQUALITY:
						testRes = (leftNum != rightNum);
						break;
					
					default:
						ASSERT_FAIL(0);
				}
				debug(DEBUG_ACTION, "Test result: %d", testRes);
				if(!testRes){
					debug(DEBUG_ACTION,"Test Skip");
					pe = pe->skip;

				}
				break;
				
			case OP_FUNC:
				break;
				
			case OP_IF:
				break;
			
			default:
				debug(DEBUG_UNEXPECTED,"Unrecognized op-code: %d", pe->opcode);
				ASSERT_FAIL(0);
			
		}	
	}
	
	
	return res;	
}


/*
* Lex, parse and execute event code
*/

int ParserParseHCL(ParseCtrlPtr_t this, int fileMode, const String str)
{
	int tokenID;
	int res = FAIL;
	tokenPtr_t pToken;
	void *pParser;
	FILE *file;
	pcodeHeaderPtr_t ph;
	
	ASSERT_FAIL(this);
	
	ph = this->pcodeHeader;
	
	ASSERT_FAIL(ph);
	
	
	/* Initialize parser control variables */
	
	
	/* Create an xplout context for the output variable */
	/* This makes it easy to delete the hash and start over */
	
	ph->xplOutContext = talloc_new(ph);
	ASSERT_FAIL(ph->xplOutContext)
	
	/* Allocate memory for parser */
	pParser = ParseAlloc(parserMalloc);
	
	/* Check for error */
	ASSERT_FAIL(pParser)
	
	/* Give parse control context to lexer */	
	yyInit(this); 
	
	/* Set up flex input mode */
	if(!fileMode)
		yyScanString(str);
	else{
		file = fopen(str, "r");
		if(file)
			yySetInputFile(file);
		else{
			this->failReason = talloc_asprintf(this, "Can't open file: \"%s\". %s", str, strerror(errno));
			return res;
		}
	}

	/* Set lexer up for string input */
	
	while((!this->failReason) && (tokenID = yyLex())){
		pToken = yyTokenVal(this);
		this->lineNo = pToken->lineNo;
		pToken->tokenID = tokenID;
		Parse(pParser, tokenID, pToken, this);
	}

	
	/* Force parser to terminate */
	if(!this->failReason){
		Parse(pParser, 0, NULL, this);
		res = PASS;
	}
	
	/* Free flex buffer if string mode */
	if(!fileMode)
		yyDeleteBuffer();	
	
	/* Reset the lexer */
	
	yyLexDestroy();
	
	/* Free parser */
	ParseFree(pParser, parserFree);
	
	return res;
}

