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






static ParseCtrlPtr_t parseCtrl; /* Not re-entrant because of this */



/*
* Hash a string
*/

static uint32_t hash(const char *key)
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
	const char *tag;
	const char *hash;
	TALLOC_CTX *ctx = talloc_new(this);
	char *vendor, *device, *instance;
	ParseHashKVPtr_t kvp;
	xPL_MessagePtr msg;


	ASSERT_FAIL(ctx)
			
	if(this->numFuncArgs != 2){
		this->failReason = talloc_asprintf(this, "Incorrect number of arguments passed to xplcmd, requires 2, got %d",
		this->numFuncArgs);
		goto end;
	}
	tag = ParserFunctionArg(this->argListHead, 0);
	ASSERT_FAIL(tag)
	hash = ParserFunctionArg(this->argListHead, 1);
	ASSERT_FAIL(hash)
	if(ParserSplitXPLTag(ctx, tag, &vendor, &device, &instance)){
		this->failReason = talloc_asprintf(this, "Bad xPL Tag: %s", tag);
		goto end;
	}
	if(strcmp("xplout", hash)){
		this->failReason = talloc_asprintf(this, "Hash must be named xplout");
		goto end;
	}

	if(this->xplServicePtr){
	
			
		/* Create xpl command message */
		msg = xPL_createTargetedMessage(this->xplServicePtr, xPL_MESSAGE_COMMAND, vendor, device, instance);
		ASSERT_FAIL(msg)
		xPL_clearMessageNamedValues(msg);
	}
	else{
		/* For dry run, just print the vendor, device and instance */
		debug(DEBUG_EXPECTED, "Vendor: %s", vendor);
		debug(DEBUG_EXPECTED, "Device: %s", device);
		debug(DEBUG_EXPECTED, "Instance: %s", instance);
	}		

					
	for(kvp = this->xplOutHead; kvp; kvp = kvp->next){
		
		if(!this->xplServicePtr){
			debug(DEBUG_EXPECTED,"Adding Key: %s, Value: %s", kvp->key, kvp->value);
		}
		else{
			xPL_addMessageNamedValue(msg, kvp->key, kvp->value);
		}		
	}
	
	debug(DEBUG_ACTION, "***Sending xPL command***");
	
	if(this->xplServicePtr){
		xPL_sendMessage(msg);
		xPL_releaseMessage(msg);
	}

end:
	/* Free the xplout hash */
	
	talloc_free(this->xplOutContext);
	this->xplOutContext = this->xplOutHead = NULL;
	
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

int ParserSplitXPLTag(TALLOC_CTX *ctx, const char *tag, char **vendor, char **device, char **instance)
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

void ParserAddFunctionArg(struct ParseCtrl_s *this, void *arg, argType_t type)
{
	argListEntryPtr_t head,newale; 
	
	debug(DEBUG_ACTION,"Adding function argument: %s, with type: %d ", arg, type);
	
	ASSERT_FAIL(this && arg)
	
	/* Cast to useable type */	
	head = (argListEntryPtr_t) this->argListHead;
	
	
	
	if(!head){
		/* List empty, create talloc context for this list */
	    this->argListContext = talloc_new(this);
		ASSERT_FAIL(this->argListContext);
	}
	
	/* Create the new list entry */
			
	newale = talloc_zero(this->argListContext, argListEntry_t);
	ASSERT_FAIL(newale)
	newale->magic = AL_MAGIC;
	newale->arg = talloc_strdup(newale, arg);
	ASSERT_FAIL(newale->arg)
	newale->type = type;
	this->numFuncArgs++;	
	
	if(!head){
		/* First entry */
		this->argListHead = newale;
	}
	else{
		/* Subsequent entries */
		argListEntryPtr_t ale, endale;
		/* Traverse list to end */
		for(ale = head; ale; ale = ale->next){
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


const char *ParserFunctionArg(argListEntryPtr_t ale, int argNum)
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
 
void ParserExecFunction(struct ParseCtrl_s *this, int tokenID)
{

	
	ASSERT_FAIL(this)
	
	debug(DEBUG_ACTION,"Executing function with token id: %d, number of args: %d", tokenID, this->numFuncArgs);
	
	
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

void ParserPostFunctionCleanup(struct ParseCtrl_s *this)
{
	debug(DEBUG_ACTION,"Entered post function cleanup");
	
	/* Free the argument list */
	if(this->argListHead){
		talloc_free(this->argListContext); /* Free the list context */
		this->argListContext = this->argListHead = NULL;
		this->numFuncArgs = 0;
	}
}

/*
 * Get a value from the hash
 */

const char *ParserHashGetValue(void *pHead, const char *key)
{
	unsigned kh;
	ParseHashKVPtr_t ke;
	ParseHashKVPtr_t ph = (ParseHashKVPtr_t) pHead;

	ASSERT_FAIL(ph && (ph->magic == KE_MAGIC) && key)

	/* Hash the section string passed in */
	kh = hash(key);
	for(ke = ph; (ke); ke = ke->next){ /* Traverse key list */
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
 
void ParserHashAddKeyValue(void **ppHead, void *tallocContext, const char *key, const char *value)
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
 
void ParserHashWalk(void *pHead, void (*parseHashWalkCallback)(const char *key, const char *value))
{
	ParseHashKVPtr_t ph = (ParseHashKVPtr_t) pHead;
	ParseHashKVPtr_t ke;
	
	/* NULL function pointer is fatal */
	ASSERT_FAIL(parseHashWalkCallback)
		
	for(ke = ph; (ke); ke = ke->next){ /* Traverse key list */
		ASSERT_FAIL(KE_MAGIC == ke->magic)
		(*parseHashWalkCallback)(ke->key, ke->value);
	}
	
}




/*
* Lex, parse and execute event code
*/

int ParserHCLScan(ParseCtrlPtr_t parseCtrl, int fileMode, const char *str)
{
	int tokenID;
	int res = FAIL;
	tokenPtr_t pToken;
	void *pParser;
	FILE *file;
	
	ASSERT_FAIL(parseCtrl);
	
	/* Initialize parser control variables */
	
	
	/* Create an xplout context for the output variable */
	/* This makes it easy to delete the hash and start over */
	
	parseCtrl->xplOutContext = talloc_new(parseCtrl);
	ASSERT_FAIL(parseCtrl->xplOutContext)
	
	/* Allocate memory for parser */
	pParser = ParseAlloc(parserMalloc);
	
	/* Check for error */
	ASSERT_FAIL(pParser)
	
	/* Give parse control context to lexer */	
	yyInit(parseCtrl); 
	
	/* Set up flex input mode */
	if(!fileMode)
		yyScanString(str);
	else{
		file = fopen(str, "r");
		if(file)
			yySetInputFile(file);
		else{
			parseCtrl->failReason = talloc_asprintf(parseCtrl, "Can't open file: \"%s\". %s", str, strerror(errno));
			return res;
		}
	}
	
	/* Set lexer up for string input */
	
	while((!parseCtrl->failReason) && (tokenID = yyLex())){
		
		pToken = yyTokenVal(parseCtrl);
		parseCtrl->lineNo = pToken->lineNo;
		pToken->tokenID = tokenID;
		Parse(pParser, tokenID, pToken, parseCtrl);
	}
	
	/* Force parser to terminate */
	if(!parseCtrl->failReason){
		Parse(pParser, 0, NULL, parseCtrl);
		res = PASS;
	}
	
	/* Free flex buffer if string mode */
	if(!fileMode)
		yyDeleteBuffer();	
	
	/* Free parser */
	ParseFree(pParser, parserFree);
	
	return res;
}
