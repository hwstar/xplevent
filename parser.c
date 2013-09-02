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
 * Send xPL command if everything looks good
 */

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
 * Add a function argument to the argument list
 */

void ParserAddFunctionArg(ParseCtrlPtr_t this, void *arg, argType_t type)
{
	argListEntryPtr_t newale; 
	pcodeHeaderPtr_t ph;
	
	debug(DEBUG_ACTION,"Adding function argument: %s, with type: %d ", arg, type);
	
	ASSERT_FAIL(this && arg)
	
	ph = this->pcodeHeader;
	
	ASSERT_FAIL(ph)
	

	
	
	
	if(ph->argListHead){
		/* List empty, create talloc context for this list */
	    ph->argListContext = talloc_new(ph);
		ASSERT_FAIL(ph->argListContext);
	}
	
	/* Create the new list entry */
			
	newale = talloc_zero(ph->argListContext, argListEntry_t);
	ASSERT_FAIL(newale)
	newale->magic = AL_MAGIC;
	newale->arg = talloc_strdup(newale, arg);
	ASSERT_FAIL(newale->arg)
	newale->type = type;
	ph->numFuncArgs++;	
	
	if(!ph->argListHead){
		/* First entry */
		ph->argListHead = newale;
	}
	else{
		/* Subsequent entries */
		argListEntryPtr_t ale, endale;
		/* Traverse list to end */
		for(ale = ph->argListHead; ale; ale = ale->next){
			ASSERT_FAIL(AL_MAGIC == ale->magic)
			endale = ale;
		}
		/* Add it on to end */
		endale->next = newale;
	}
	
		
		

}

/*
 * Return the nth function argument
 */


const String ParserFunctionArg(argListEntryPtr_t ale, int argNum)
{
	int i;
	
	ASSERT_FAIL(ale)
	
	for(i = 0; ale && i < argNum; i++, ale = ale->next);
	
	if(ale)
		return ale->arg;
	else
		return NULL;
}

	
/*
 * Execute a built-in function
 */
 
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

/*
 * Parser Post Function Cleanup
 */

void ParserPostFunctionCleanup(ParseCtrlPtr_t this)
{
	pcodeHeaderPtr_t ph;
	
	debug(DEBUG_ACTION,"Entered post function cleanup");
	
	ASSERT_FAIL(this)
	ph = this->pcodeHeader;
	ASSERT_FAIL(ph);
	
	/* Free the argument list */
	if(ph->argListHead){
		talloc_free(ph->argListContext); /* Free the list context */
		ph->argListContext = ph->argListHead = NULL;
		ph->numFuncArgs = 0;
	}
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
* Lex, parse and execute event code
*/

int ParserHCLScan(ParseCtrlPtr_t this, int fileMode, const String str)
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
		
		new = talloc_zero(ph->pcodeCTX, pcode_t);
		ASSERT_FAIL(new);
		
		/* Initialize new list entry */
		new->magic = PC_MAGIC;
		new->opcode = op;
		new->operand = operand;
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

void ParserDumpPcodeList(pcodeHeaderPtr_t ph)
{
	String op,data1,data2;
	pcodePtr_t p;
	
	if(!ph)
		return;
	debug(DEBUG_EXPECTED, "*** begin p-code dump ***");
	for(p = ph->head; p; p = p->next){
		switch(p->opcode){
			case OP_NOP:
				op = "Nop";
				break;
		
			case OP_PUSH:
				op = "Push";
				break;
			
			case OP_POP:
				op = "Pop";
				break;
		
			case OP_ASSIGN:
				op = "Assign";
				break;
			
			case OP_FUNC:
				op = "Func";
				break;
			
			default:
				op = "UNK";
				break;
		}	
		data1 = (p->data1) ? p->data1 : "NULL";
		data2 = (p->data2) ? p->data2 : "NULL";
				
		debug(DEBUG_EXPECTED,"Opcode: %s, Operand: %d, Data1: %s, Data2: %s", op, p->operand, data1, data2);	
	}

	debug(DEBUG_EXPECTED, "*** end p-code dump ***");
	
}
