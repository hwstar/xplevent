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


#include "defs.h"
#include "types.h"
#include "notify.h"
#include "util.h"
#include "grammar.h"
#include "parser.h"
#include "parse.h"
#include "lex.h"
#include "db.h"
#include "xplcore.h"
#include "xplevent.h"

/* Definitions */


#define KE_MAGIC	0x4F7B127A
#define AL_MAGIC	0x689C9A2F
#define PC_MAGIC	0x9A905437
#define SE_MAGIC	0x59F593AC






static ParseCtrlPtr_t parseCtrl; /* Not re-entrant because of this */



/*
 * Malloc for parser. This is called by Lemon when a block of memory is required.
 *
 * Arguments: 
 *
 * 1. Size of block to allocate
 *
 * Return value:
 *
 * Generic pointer to block of memory allocated.
 *
 */

static void *parserMalloc(size_t size)
{	
	void *p;
	p = talloc_size(parseCtrl, size);
	MALLOC_FAIL(p)
	return p;
}

/*
 * Free for parser. Called by Lemon when a memory block is no longer required.
 *
 * Arguments: 
 *
 * 1. Pointer to block of memory to free.
 *
 * Return value:
 *
 * None
 *
 */


static void parserFree(void *ctx)
{
	ASSERT_FAIL(ctx)
	
	talloc_free(ctx);
}

/*
 * Print P-code info
 *
 * Arguments: 
 *
 * 1. Pointer to pcode block.
 *
 * Return value:
 *
 * None
 */

static void printOpcode(PcodePtr_t p)
{
	String op ,data1, data2;
	
	ASSERT_FAIL(p)
	
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
		
				
		case OP_TEST2:
			op = "Test 2";
			break;
			
			
		case OP_EXISTS:
			op = "Exists";
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
 * Set undefined variable error message, or warn about it if the ignore assign error flag is set
 * in the pcode header.
 *
 * Arguments: 
 *
 * 1. Pointer to pcode header
 * 2. String containing variable name
 * 3. Line number where the veriable was referenced.
 *
 * Return value:
 *
 * None
 */

static void undefVar(PcodeHeaderPtr_t ph, String var, int lineNo)
{
	String res;
	
	ASSERT_FAIL(ph)
	ASSERT_FAIL(var)

	res = talloc_asprintf(ph, "Variable '%s' undefined on line number %d", var, lineNo);
	MALLOC_FAIL(res);
	if(!ph->ignoreAssignErrors){
		ph->failReason = res;
	}
	else{
		warn("%s", res); /* Warn about variable being undefined, but keep going */
	}
}

/*
* Remove ALL the keys from a hash
* Arguments: 
*
* 1. Pointer the hash to empty out.
*
* Return value:
*
* None
*/

static void deleteHashContents(ParseHashSTEPtr_t se)
{
	ASSERT_FAIL(se)
	debug(DEBUG_ACTION,"Deleting contents of hash: %s", se->name);
	talloc_free(se->context);
	se->context = se->head = NULL;
}


/*
 * Find a hash in the symbol table
 *
 * Arguments: 
 *
 * 1. Pointer the pcode header block
 * 2. String contining the name of the hash
 * 3. A pointer to a pointer variable where the end of the list can be stored if the search fails.
 *    this value is optional, and a NULL can be passed in if it is not required.
 *
 *
 * Return value:
 * 
 * Return the symbol table entry, or NULL if not found
 */

static ParseHashSTEPtr_t findHash(PcodeHeaderPtr_t ph, const String hashName, ParseHashSTEPtrPtr_t tail)
{
	unsigned hashVal;
	ParseHashSTEPtr_t se,p = NULL;
	
	ASSERT_FAIL(ph)
	ASSERT_FAIL(hashName)
	
	if(!ph->steHead){
		return NULL; /* Empty symbol table */
	}
	hashVal = UtilHash(hashName);
	
	for(se = ph->steHead; (se); se = se->next){
		p = se;
		ASSERT_FAIL(se->magic == SE_MAGIC)
		if((se->hash == hashVal) && (!strcmp(hashName, se->name))){
			break;
		}
	}
	
	if(tail){
		*tail = p; /* Return pointer to end of list or current entry if set */
	}
	
	return se;
}


/*
* Add a hash to the end of the symbol table
*
* Arguments: 
*
* 1. Talloc context to hang the new symbol table off of.
* 2. Pointer to the symbol table tail pointer.
* 3. The key to add to the new hash entry.
*
* Return value:
*
* None
*
* 
*/

static void hashAppend(TALLOC_CTX *ctx, ParseHashSTEPtrPtr_t ptail, String name)
{
	ParseHashSTEPtr_t hNew;

	ASSERT_FAIL(ctx)
	ASSERT_FAIL(ptail)
	ASSERT_FAIL(name)
		
	/* Initialize a new list entry */
	 
	hNew = talloc_zero(ctx, ParseHashSTE_t);
	MALLOC_FAIL(hNew)
	hNew->magic = SE_MAGIC;
	hNew->name = talloc_strdup(hNew, name);
	MALLOC_FAIL(hNew->name)
	hNew->hash = UtilHash(name);
	hNew->writable = TRUE;

	*ptail = hNew;
}

/*
 * Spawn another program
 *
 * Arguments: 
 *
 * 1. Pointer to the pcode header
 * 2. Pointer to the pcode instruction containing the spawn token.
 *
 * Return value:
 *
 * None
 *
 */
 
static void spawn(PcodeHeaderPtr_t ph, PcodePtr_t pi)
{
	PcodePtr_t pa;
	String command = NULL;
	TALLOC_CTX *ctx;
	
	ASSERT_FAIL(ph)
	ASSERT_FAIL(pi)
	
	/* Make dedicated context */
	ctx = talloc_new(ph);
	MALLOC_FAIL(ctx)
	
	/* Check for correct argument count */
	if(ph->pushCount != 1){
		ph->failReason = talloc_asprintf(ph, "Incorrect number of arguments passed to spawn, requires 1, got %d",
		ph->pushCount);
		goto end;
	}
	
	/* Retrieve parameter */
	pa = pi->prev;
	ASSERT_FAIL(ParserPcodeGetValue(ctx, ph, pa, &command) == PASS)
	if(/* ph->xplServicePtr */ 1){ /* Make sure this isn't a dry run */
		debug(DEBUG_ACTION, "Spawning command: %s", command);
		if(UtilSpawn(command, NULL) == FAIL){
			ph->failReason = talloc_asprintf(ph, "Spawn %s failed", command);
		}
	}
	
end:
	talloc_free(ctx);

}


/*
 * Send xPL command if everything looks good
 * Arguments: 
 *
 * 1. Pointer to the pcode header
 * 2. Pointer to the pcode instruction containing the spawn token.
 *
 * Return value:
 *
 * None
 */

static void sendXPLCommand(PcodeHeaderPtr_t ph, PcodePtr_t pi)
{
	String tag = NULL;
	String class = NULL;
	String type = NULL;
	String hash = NULL;
	TALLOC_CTX *ctx;
	String vendor, device, instance;
	ParseHashSTEPtr_t se = NULL;
	ParseHashKVPtr_t kvp;
	void *msg = NULL;
	PcodePtr_t pa;


	ASSERT_FAIL(ph)
	ASSERT_FAIL(pi)
	
	ctx = talloc_new(ph);
	MALLOC_FAIL(ctx)
			
	if(ph->pushCount != 4){
		ph->failReason = talloc_asprintf(ph, "Incorrect number of arguments passed to xplcmd, requires 4, got %d",
		ph->pushCount);
		goto end;
	}


	pa = pi->prev;
	ASSERT_FAIL(ParserPcodeGetValue(ctx, ph, pa, &hash) == PASS)
	pa = pa->prev;
	ASSERT_FAIL(ParserPcodeGetValue(ctx, ph, pa, &type) == PASS)
	pa = pa->prev;
	ASSERT_FAIL(ParserPcodeGetValue(ctx, ph, pa, &class) == PASS)
	pa = pa->prev;
	ASSERT_FAIL(ParserPcodeGetValue(ctx, ph, pa, &tag) == PASS)

	
	
	if(ParserSplitXPLTag(ctx, tag, &vendor, &device, &instance)){
		ph->failReason = talloc_asprintf(ph, "Bad xPL Tag: %s", tag);
		goto end;
	}


	if(ph->xplServicePtr){ /* if this is NULL, it is to be a dry run */
			
		/* Create xpl command message */
		msg = XplInitTargettedMessage(ph->xplServicePtr, XPL_MESSAGE_COMMAND, vendor, device, instance);
		ASSERT_FAIL(msg)
		
		/* Set message schema */
		XplSetMessageClassType(msg, class, type);
	
		
		/* Clear name/value pairs */
		XplClearNameValues(msg);
	}
	else{
		/* For dry run, just print the vendor, device and instance */
		debug(DEBUG_EXPECTED, "Vendor: %s", vendor);
		debug(DEBUG_EXPECTED, "Device: %s", device);
		debug(DEBUG_EXPECTED, "Instance: %s", instance);
	}		

	se = findHash(ph, hash, NULL);
	ASSERT_FAIL(se)
	/* Build xPL name value pairs from hash entries */
	for(kvp = se->head; kvp; kvp = kvp->next){
		ASSERT_FAIL(se->magic == SE_MAGIC)
		if(!ph->xplServicePtr){
			debug(DEBUG_EXPECTED,"Adding Key: %s, Value: %s", kvp->key, kvp->value);
		}
		else{
			XplAddNameValue(msg, kvp->key, kvp->value);
		}		
	}
	
	debug(DEBUG_ACTION, "***Sending xPL command***");
	
	if(ph->xplServicePtr){
		XplSendMessage(msg);
		XplDestroyMessage(msg);
	}

end:
	/* Clear out the xplout hash */
	
	if(se){
		deleteHashContents(se);
	}
	
	/* Free the context used here */
	
	talloc_free(ctx);
	
}


/*
 * Split an xPL tag into its constituent parts.
 * 
 * Check parts for length
 * 
 * To do just a validation without returning the strings, set vendor, device, and instance to NULL
 * 
 * Strings must be freed unless set to NULL
 *
 * Arguments: 
 *
 * 1. Talloc context to hang the strings off of.
 * 2. String containing xPL tag. 
 * 3. Pointer to string to store vendor name (or NULL)
 * 4. Pointer to string to store device name (or NULL)
 * 5. Pointer to string to store instance (or NULL)
 *
 * Return value:
 *
 * Boolean. PASS indicates success, FAIL indicates failure.
 * 
 */

Bool ParserSplitXPLTag(TALLOC_CTX *ctx, const String tag, String *vendor, String *device, String *instance)
{
	int res = PASS;
	int done = FALSE;
	int i,j,state, begin;
	
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(tag)
	
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
							MALLOC_FAIL(*vendor)
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
							MALLOC_FAIL(*instance);
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
 * Get a value from the hash. If the has name is "nvstate" and the database pointer is
 * in the pcode header, retreive the value from the database instead of memory.
 *
 * Arguments: 
 *
 * 1. Talloc context to hang the result off of.
 * 2. Pointer to pcode header block.
 * 3. String with the name of the hash.
 * 4. String with the key to look up.
 *
 * Return value:
 *
 * Value as a string. String must be freed when it is no longer required.
 *
 * 
 */

const String ParserHashGetValue(TALLOC_CTX *ctx, PcodeHeaderPtr_t ph, const String hashName, const String key)
{
	unsigned kh;
	String s;
	ParseHashSTEPtr_t h;
	ParseHashKVPtr_t ke;

	ASSERT_FAIL(ctx && ph && hashName && key)
	
	if((ph->DB) && (!strcmp(hashName, "nvstate"))){
		/* nvstate database hash */
		return DBReadNVState(ctx, ph->DB, key);
	}
	else{ /* In memory hash */
		h = findHash(ph, hashName, NULL);
		if(!h)
			return NULL;
		
		/* Hash the section string passed in */
		kh = UtilHash(key);
		for(ke = h->head; (ke); ke = ke->next){ /* Traverse key list */
			ASSERT_FAIL(KE_MAGIC == ke->magic)
			/* Compare hashes, and if they match, compare strings */
			if((kh == ke->hash) && (!strcmp(ke->key, key))){
				s = talloc_strdup(ctx, ke->value);
				MALLOC_FAIL(s)
				return s;
			}
		}
		return NULL; /* No match found */
	}
}

/*
 * Add a value to the hash
 * Replace any entry with a matching key which is already there
 * Add hash to the symbol table if it does not already exist.
 * If the hash name is "nvstate" and the database pointer is in the
 * pcode header block, store the key and value in the database instead
 * of in memory.
 * 
 *
 * Arguments: 
 *
 * 1. Talloc context to hang the result off of.
 * 2. Pointer to the pcode header block.
 * 3. String containing the name of the hash.
 * 4. String containing the key to add to the hash
 *
 *
 * Return value:
 *
 * Boolean. PASS, unless DB write fails, then FAIL.
 */
 
Bool ParserHashAddKeyValue(TALLOC_CTX *ctx, PcodeHeaderPtr_t ph, const String hashName, const String key, const String value)
{
	ParseHashSTEPtr_t h = NULL;
	ParseHashKVPtr_t ke, keNew, kePrev;
	

	ASSERT_FAIL(ph && hashName && key && value)
	
	if((ph->DB) && (!strcmp(hashName, "nvstate"))){
		/* nvstate database hash */
		return DBWriteNVState(ctx, ph->DB, key, value);
	}
	else{	/* In memory hash */	
		/* Attempt to find the hash in the symbol table */
		
		if(ph->steHead){ /* If symbol table isn't empty */
			if(!findHash(ph, hashName, &h)){
				debug(DEBUG_ACTION, "Creating hash: %s in symbol table", hashName);
				hashAppend(ph, &h->next, hashName); /* Was not found, add it to the symbol table */
				h = h->next;
				ASSERT_FAIL(h)
				h->context = talloc_new(h);
				MALLOC_FAIL(h->context);
			}
	
		
		}
		else{ /* Empty symbol table */
			debug(DEBUG_ACTION, "Creating first hash: %s in empty symbol table", hashName);
			hashAppend(ph, &ph->steHead, hashName);
			h = ph->steHead;
			h->context = talloc_new(h);
			MALLOC_FAIL(h->context);
		}
		
	
			
		/* Initialize a new key list entry */
		 
		keNew = talloc_zero(h->context, ParseHashKV_t);
		MALLOC_FAIL(keNew)
		keNew->magic = KE_MAGIC;
		keNew->key = talloc_strdup(keNew, key);
		MALLOC_FAIL(keNew->key)
		keNew->value = talloc_strdup(keNew, value);
		MALLOC_FAIL(keNew->value);
		keNew->hash = UtilHash(key);
	
		/* add the key/value to the hash, replacing any existing identical key */
		
		if(!h->head){
			/* First entry */
			debug(DEBUG_ACTION, "First entry in hash %s is: %s", h->name, key);
			h->head = keNew;
		}
		else{
			for(ke = h->head, kePrev = NULL; (ke); ke = ke->next){ /* Traverse key list */
				ASSERT_FAIL(KE_MAGIC == ke->magic);
				/* Compare hashes, and if they match, compare strings */
				if((keNew->hash == ke->hash) && (!strcmp(ke->key, key))){
					/* Key already exists, need to replace it */
					keNew->next = ke->next;
					if(!kePrev){
						debug(DEBUG_ACTION, "Replacing first entry in hash %s: %s", h->name, key);
						h->head = keNew; /* Change first entry */
					}
					else{
						debug(DEBUG_ACTION, "Replacing other than the first entry in hash %s: %s", h->name, key);
						kePrev->next = keNew; /* Change subsequent entry */
					}	
					talloc_free(ke); /* Free the old entry and its strings */
					break;
				}
				else if(!ke->next){
					debug(DEBUG_ACTION, "Appending entry to end of hash %s: %s", h->name, key);
					/* At end of list, need to append it */
					ke->next = keNew;
					break;	
				}
				kePrev = ke;	
			}
		}
		return PASS;
	}	
}

/*
 * Walk the list calling a callback function with each key/value
 *
 * Arguments: 
 *
 * 1. Pointer to the pcode header block.
 * 2. String containing the hash name
 * 3. Callback function (see below)
 * 
 *
 *
 * Return value:
 *
 * None
 *
 ************** Callback Function ***************
 *
 * Arguments:
 *
 * 1. Key found as a String
 * 2. Value found as a String
 *
 * Return value:
 *
 * None
 */
 
void ParserHashWalk(PcodeHeaderPtr_t ph, const String name, void (*parseHashWalkCallback)(const String key, const String value))
{
	ParseHashKVPtr_t ke;
	ParseHashSTEPtr_t h;
	
	ASSERT_FAIL(ph)
	ASSERT_FAIL(name)
	/* NULL function pointer is fatal */
	ASSERT_FAIL(parseHashWalkCallback)

	h = findHash(ph, name, NULL);
	

	
		
	for(ke = h->head; (ke); ke = ke->next){ /* Traverse key list */
		ASSERT_FAIL(KE_MAGIC == ke->magic)
		(*parseHashWalkCallback)(ke->key, ke->value);
	}
	
}


/*
 * Return the value for the push instruction passed in
 *
 * Arguments: 
 *
 * 1. Talloc context to hang the result off of.
 * 2. Pointer to the pcode header block.
 * 3. Pointer to the push instruction to extract the value from.
 * 4. Pointer to string to store value.
 *
 *
 * Return value:
 *
 * Boolean. PASS if variable could be retrieved, else FAIL
 */
 

Bool ParserPcodeGetValue(TALLOC_CTX *ctx, PcodeHeaderPtr_t ph, PcodePtr_t instr, String *pValue)
{
	String value = NULL;
	
	ASSERT_FAIL(ph);
	ASSERT_FAIL(instr)
	ASSERT_FAIL(pValue)
	
	/* Do something based on the operand */
	switch(instr->operand){
		
		case OPRD_STRINGLIT: /* Literals */
		case OPRD_INTLIT:
		case OPRD_FLOATLIT:
			value = instr->data1;
			break;
			
		case OPRD_HASHKV:  /* Assoc array key/value */
			value = ParserHashGetValue(ctx, ph, instr->data1, instr->data2);
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
 *
 * Arguments: 
 *
 * 1. Talloc context to hang the result off of.
 * 2. Pointer to the pcode header block.
 * 3. Pointer to the push instruction contining the lvalue.
 * 4. String containing the value to add to the hash
 *
 *
 * Return value:
 *
 * Boolean. PASS if success, otherwise FAIL
 */

Bool ParserPcodePutValue(TALLOC_CTX *ctx, PcodeHeaderPtr_t ph, PcodePtr_t instr, String value)
{
	ASSERT_FAIL(ph)
	ASSERT_FAIL(instr)
	ASSERT_FAIL(value)
	
	if(instr->operand != OPRD_HASHKV){
		return FAIL;
	}
	

	return ParserHashAddKeyValue(ctx, ph, instr->data1, instr->data2, value);
}

/*
 * This is called by the parser when a pcode structure has to be added to the list. It adds
 * the new instruction to the end of the instruction list.
 *
 *  Arguments: 
 *
 * 1. Pointer to parse control block
 * 2. Opcode type
 * 3. Operand
 * 4. Optional string 1 (Pass NULL if not used)
 * 5. Optional string 2 (Pass NULL if not used)
 *
 *
 * Return value:
 *
 * None
 */

void ParserPcodeEmit(ParseCtrlPtr_t pc, opType_t op, int operand, String data1, String data2)
{
	PcodeHeaderPtr_t ph;
	PcodePtr_t new;
	
	ASSERT_FAIL(pc)
	
	ph = pc->pcodeHeader;
	
	if(ph){
		
		new = talloc_zero(ph, Pcode_t);
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
			MALLOC_FAIL(new->data1);
		}
		if(data2){
			/* Make a copy of the string */
			new->data2 = talloc_strdup(new, data2);
			MALLOC_FAIL(new->data2);
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
 *
 * Arguments: 
 *
 * 1. Pointer to the pcode header block.
 *
 * Return value:
 *
 * None
 */

void ParserPcodeDumpList(PcodeHeaderPtr_t ph)
{

	PcodePtr_t p;
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
 * Sets Jumps for control statements.
 * Called by the Lemon grammar file when a control statement is seen.
 *
 * Arguments: 
 *
 * 1. Pointer to parse control block
 * 2. Token id.
 *
 *
 * Return value:
 *
 * None.
 */
 
void ParserSetJumps(ParseCtrlPtr_t this, int tokenID)
{
	int myCBRC;
	PcodeHeaderPtr_t ph;
	PcodePtr_t tail, p, elseblock = NULL;
	
	ASSERT_FAIL((tokenID == TOK_ELSE) || (tokenID == TOK_IF))
	
	ASSERT_FAIL(this)
	
	ASSERT_FAIL(ph = this->pcodeHeader)
	
	ASSERT_FAIL(tail = ph->tail)
	
	/* Must be currently on a close block instr */
	ASSERT_FAIL((tail->opcode == OP_BLOCK) && (tail->operand == OPRB_END))
	
	p = tail;
	
	ASSERT_FAIL(p) /* Previous instruction must exist */

	myCBRC = p->ctrlStructRefCount;
	
	if(tokenID == TOK_ELSE){
		debug(DEBUG_ACTION,"Set Jumps TOK_ELSE");
		/* Skip back before the current block close instruction */
		ASSERT_FAIL(p = p->prev)
		/* Now, go back in the pcode and find the prior close block instr */
		for(; (p) ; p = p->prev){
			ASSERT_FAIL(p->ctrlStructRefCount >= myCBRC)
			ASSERT_FAIL(p->magic == PC_MAGIC)
			if((p->ctrlStructRefCount == myCBRC) && (p->opcode == OP_BLOCK) && (p->operand == OPRB_END)){
				p->skip = tail; /* Install jump over second block */
				elseblock = p->next; /* Note the beginning of the else block */
				ASSERT_FAIL(elseblock)
				debug(DEBUG_ACTION, "tail seq = %d, elseblock seq = %d", tail->seq, elseblock->seq);
				break;
			}
		}
		ASSERT_FAIL(p)
	
		
	}
	/* Look for test or exists instruction */		
	for(; (p); p = p->prev){
		ASSERT_FAIL(p->ctrlStructRefCount >= myCBRC);
		if((p->ctrlStructRefCount == myCBRC) && ((p->opcode == OP_TEST2)||(p->opcode == OP_EXISTS))){
			if(tokenID == TOK_IF){
				debug(DEBUG_ACTION,"Set Jumps TOK_IF, line = %d, seq = %d, CBRC = %d", p->lineNo, p->seq, p->ctrlStructRefCount);
				ASSERT_FAIL(tail)
				p->skip = tail; /* Note end of block for if test */	
			}
			else{
				ASSERT_FAIL(elseblock)
				p->skip = elseblock; /* Note start of else block for if-else */
			}
			break;	
		}
	}
	ASSERT_FAIL(p);	
}

/*
* Execute a function.
* Called by the Lemon grammar file when a function keyword is seen.
*
* Arguments: 
*
* 1. Pointer to the pcode header block.
* 2. Pointer to the pcode instruction containing the function to execute.
*
*
*
* Return value:
*
* None
*/
 
void ParserExecFunction(PcodeHeaderPtr_t ph, PcodePtr_t pi)
{

	
	ASSERT_FAIL(ph);
	ASSERT_FAIL(pi);
	
	
	debug(DEBUG_ACTION,"Executing function with token id: %d, number of args: %d", pi->operand, ph->pushCount);
	
	
	switch(pi->operand){
		case TOK_XPLCMD:
			sendXPLCommand(ph, pi); 
			break;
			
		case TOK_SPAWN:
			spawn(ph, pi);
			break;
		
		default:
			ASSERT_FAIL(FALSE);
	}	
}




/*
* Execute  p-code generated by parser
*
* Arguments: 
*
* 1. Pointer to the pcode header block.
*
*
*
* Return value:
*
* Boolean. PASS indicates success, otherwise FAIL.
*
*/
 
Bool ParserExecPcode(PcodeHeaderPtr_t ph)
{
	PcodePtr_t pe,p;
	String value,rvalue;
	double leftNum, rightNum;
	Bool testRes = FALSE;
	TALLOC_CTX *ctx;
	int res = PASS;

	ASSERT_FAIL(ph)
	ASSERT_FAIL(ph->head)
	
	ASSERT_FAIL(ctx = talloc_new(ph))
	

	/* Execution loop */
	for(pe = ph->head;(pe) && (!ph->failReason); pe = pe->next){
		ASSERT_FAIL(pe->magic == PC_MAGIC);
		
		/* Print instruction if trace enabled */
		if(ph->tracePcode){
			printOpcode(pe);
		}

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
				ASSERT_FAIL(ph->pushCount == 2)
				p = pe->prev;
				if(ParserPcodeGetValue(ctx, ph, p, &value)){
					undefVar(ph, p->data1, pe->lineNo); 
					break;
				}
				MALLOC_FAIL(value)
				p = pe->prev->prev;
			    if(ParserPcodePutValue(ctx, ph, p, value)){
					undefVar(ph, p->data1, pe->lineNo);
					break;
				}	
				debug(DEBUG_ACTION,"Assign successful on line %d", pe->lineNo);
				break;
				
				
			case OP_TEST2: /* Double Variable test */
				ASSERT_FAIL(ph->pushCount == 2)
				leftNum = rightNum = 0.0;
				p = pe->prev->prev;
				if(ParserPcodeGetValue(ctx, ph, p, &value)){ /* Left */
					undefVar(ph, p->data1, pe->lineNo); 
					break;
				}
				debug(DEBUG_ACTION,"Left string: %s", value);				
				if((pe->operand != OPRT_STREQUALITY) && (FAIL == UtilStod(value, &leftNum))){
					ph->failReason = talloc_asprintf(ph, "Invalid numeric value on line %d", pe->lineNo);
					break;
				}
				p = pe->prev;
				if(ParserPcodeGetValue(ctx, ph, p, &rvalue)){ /* Right */
					undefVar(ph, p->data1, pe->lineNo); 
					break;
				}	
				debug(DEBUG_ACTION,"Right string: %s", rvalue);					
				if((pe->operand != OPRT_STREQUALITY) && (FAIL == UtilStod(rvalue, &rightNum))){
					ph->failReason = talloc_asprintf(ph, "Invalid numeric value on line %d", pe->lineNo);
					break;
				}
				debug(DEBUG_ACTION,"leftNum = %e, rightNum = %e", leftNum, rightNum);
				
				switch(pe->operand){
					case OPRT_NUMEQUALITY:
						testRes = (leftNum == rightNum);
						break;
						
					case OPRT_NUMINEQUALITY:
						testRes = (leftNum != rightNum);
						break;
						
					case OPRT_NUMGTRTHAN:
						testRes = (leftNum > rightNum);
						break;
					
					case OPRT_NUMLESSTHAN:
						testRes = (leftNum < rightNum);
						break;
					
					case OPRT_NUMGTREQTHAN:
						testRes = (leftNum >= rightNum);
						break;
					
					case OPRT_NUMLESSEQTHAN:
						testRes = (leftNum <= rightNum);
						break;
					
					case OPRT_STREQUALITY:
						testRes = (0 == strcmp(rvalue, value));
						break;
					
					default:
						ASSERT_FAIL(0);
				}
				debug(DEBUG_ACTION, "Test result: %d", testRes);
				if(!testRes){
					debug(DEBUG_ACTION,"Test Skip");
					ASSERT_FAIL(pe->skip);
					pe = pe->skip;

				}
				break;
				
			case OP_EXISTS: /* Hash key exists */
				ASSERT_FAIL(ph->pushCount == 1)
				p = pe->prev;
				if(ParserPcodeGetValue(ctx, ph, p, &value) == FAIL){
					debug(DEBUG_ACTION,"Key does not exist");
					ASSERT_FAIL(pe->skip);
					pe = pe->skip; /* Not present */
				}
				else{
					debug(DEBUG_ACTION,"Key exists");
				}
				
				break;
			
				
			case OP_FUNC:
	
				ParserExecFunction(ph, pe);
				break;
				
			
			default:
				debug(DEBUG_UNEXPECTED,"Unrecognized op-code: %d", pe->opcode);
				ASSERT_FAIL(0);
			
		}	
	}
	
	if(ph->failReason){
		res = FAIL;
	}
	talloc_free(ctx);
	return res;	
}


/*
* Lex, parse and execute event code
*
*
* Arguments: 
*
* 1. Pointer to the parser control block.
* 2. Boolean indicating whether 3rd parameter is a path, or a String containing the script to execute.
* 3. String containing the path to a script file, or script text.
*
*
*
* Return value:
*
* Boolean. PASS indicates success, otherwise FAIL.
*
*/

Bool ParserParseHCL(ParseCtrlPtr_t this, Bool fileMode, const String str)
{
	int tokenID;
	int res = FAIL;
	tokenPtr_t pToken;
	void *pParser;
	FILE *file;
	PcodeHeaderPtr_t ph;
	
	ASSERT_FAIL(this);
	
	ph = this->pcodeHeader;
	
	ASSERT_FAIL(ph);
	
	
	/* Initialize parser control variables */
	

	
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
			MALLOC_FAIL(this->failReason)
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
*
* Check syntax of script file passed in.
* Arguments: 
*
* 1. Pointer to the parser control block.
* 2. String containing the path to a script file.
*
*
*
* Return value:
*
* Boolean. PASS indicates success, otherwise FAIL.
*/

String ParserCheckSyntax(TALLOC_CTX *ctx, String file)
{
	ParseCtrlPtr_t parseCtrl;
	PcodeHeaderPtr_t ph;
	String s = NULL;
	
	/* Allocate memory */
	MALLOC_FAIL(parseCtrl = talloc_zero(ctx, ParseCtrl_t))
	MALLOC_FAIL(ph = talloc_zero(ctx, PcodeHeader_t))
	parseCtrl->pcodeHeader = ph;
	
	/* Parse user code */
	
	ParserParseHCL(parseCtrl, TRUE, file);
	
	/* Dump pcode list if debug level >= 4 */
	if(Globals->debugLvl >= 4){
			ParserPcodeDumpList(ph);
	}
		
	if(parseCtrl->failReason){
		MALLOC_FAIL(s = talloc_asprintf(ctx, "Parse error: %s", parseCtrl->failReason))
		talloc_free(parseCtrl);
		return s;
	}
	
	talloc_free(parseCtrl);
	
	if(Globals->debugLvl >= 3 ){
		ph->tracePcode = TRUE;
	}
	
	ph->ignoreAssignErrors = TRUE;
	ParserExecPcode(ph);
	if(ph->failReason){
		MALLOC_FAIL(s = talloc_asprintf(ctx, "Pcode execution error: %s", ph->failReason))
	}
	talloc_free(ph);
	
	return s;
}

