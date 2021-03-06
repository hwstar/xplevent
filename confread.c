/*
 * confread.c Created 7/07/12
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
#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <talloc.h>

#include "defs.h"
#include "types.h"
#include "util.h"
#include "confread.h"
#include "notify.h"


/* Definitions */


#define MAX_CONFIG_LINE	1024
#define MAX_VALUE 768
#define MAX_KEY 128
#define MAX_SECTION 128

#define CE_MAGIC	0x4F8A1C09
#define SE_MAGIC	0x4FCB128D
#define KE_MAGIC	0x4F091E76



/* Scanner tokens */

enum { TOK_ERR = -1, TOK_NL=0, TOK_SECTION, TOK_KEY, TOK_VALUE, TOK_COMMENT};


/* Internal functions */

static int linescan(String *lp, String tokstring);
static String removespctab(String line);
static char copyuntil(String dest, String *srcp, int max_dest_len,  String stopchrs);



/*
* Copy a string from src pointer to a pointer to dest of a maximum
* specified length looking for one or more stop characters.
* If dest is set to NULL, then throw the characters away, and return on the next stop character.
* Else if dest is non-NULL, do not copy the stop character to the destination
* and always terminate the destination with a NUL character. Return the
* character the copy stopped on. In the case of no stop character
* match, return a nul.
*
* Arguments:
* 
* 1. Destination string or NULL
* 2. Pointer to source string pointer (String poiner will be modified).
* 3. Length limit
* 4. String containg all of the stop characters
*
* Returns 
*
* Stop character detected, or NUL.
*/

static char copyuntil(String dest, String *srcp, int max_dest_len, String stopchrs){

	char *p = "";
	int i;

	/* Note: max_dest_len check below accounts for NUL at eos. */

	for(i = 0; i < max_dest_len - 1; i++){

		/* If nul at current src string pos, stop copy */
		/* and point p to the NUL character in the stop char string */

		if(**srcp == '\0'){
			p = stopchrs + strlen(stopchrs); /* Point to NUL in string */
			break;
		}

		/* Check for one of the stop characters */

		for(p = stopchrs; *p != '\0'; p++){
			if(**srcp == *p)
				break;
		}

		/* If a stop character was matched, *p will be nz */
		/* if *p is zero, then copy the character to the */
		/* destination string if the destination string is non-NULL */

		if(*p)
			break;	
		else{
			if(dest){
				*dest++ = *(*srcp)++;
			}
			else (*srcp)++;		
		}	
	}

	/* NUL terminate the destination string if dest is non-NULL*/
	
	if(dest)
		*dest = 0;
	return *p; /* Return character which stopped copy */
}

/*
* Remove spaces and tabs from the line in place
*
* Arguments:
*
* 1. Input string (will be modified)
*
* Return value:
*
* Modified input String
*/
			
static String removespctab(String line)
{
	int si = 0, di = 0;

	while(line && line[si]){
		if((line[di] == ' ') || (line[di] == '\t')){
			di++;
		}
		else{
			line[si++] = line[di++];
		}
	}
	return line;
}	


/*
* Scan the line for tokens. Return a token code indicating what was
* found. Load tokstring with the token found unless tokstring is set to NULL, 
* in that case, throw the characters away until the next token is detected.
*
* Arguments:
*
* 1. Pointer to String pointer pointing to line (line String pointer will be modified).
* 2. Pointer to a place to store the token string found (Or NULL if not required)
*
* Return value:
* 
* The token detected.
*/

static int linescan(String *lp, String tokstring){

	int retval = TOK_ERR;
	
	switch(**lp){
		case '\n':
			/* New line */
			debug(DEBUG_INCOMPLETE, "TOK_NL");

			retval = TOK_NL;
			break;

		case ';': 
		case '#':
			debug(DEBUG_INCOMPLETE, "TOK_COMMENT");

			/* Comment */

			retval = TOK_COMMENT;
			break;

		case '=':
			/* Value */
			(*lp)++;
			copyuntil(tokstring, lp, MAX_VALUE, "#;\n");
			debug(DEBUG_INCOMPLETE, "TOK_VALUE");
			retval = TOK_VALUE;
			break;


 		case '[':
			/* Section start */

			(*lp)++;
			if(copyuntil(tokstring, lp, MAX_SECTION, "]\n") == ']'){
				(*lp)++;
				debug(DEBUG_INCOMPLETE, "TOK_SECTION");
				retval = TOK_SECTION;
			}
			else{
				debug(DEBUG_UNEXPECTED, "Section not closed off");
				retval = TOK_ERR; /* Section broken */
			}
			break;

		default:
			/* Look for a key */

			if(isalnum(**lp)){
				debug(DEBUG_INCOMPLETE, "lp: %s", *lp);
				copyuntil(tokstring, lp, MAX_KEY, "=\n");
				debug(DEBUG_INCOMPLETE, "stop char: %s", *lp);

				if(**lp == '='){
					debug(DEBUG_INCOMPLETE, "TOK_KEY");
					retval = TOK_KEY;
				}
				else{
					debug(DEBUG_INCOMPLETE, "TOK_ERR, key broken");
					retval = TOK_ERR; // Key broken
				}
			}
			else{
				debug(DEBUG_INCOMPLETE, "TOK_ERR, invalid char %c", **lp);
				retval = TOK_ERR; // Not something valid
			}
			break;
	}				
	return retval;
}					

/* 
**************************
* Global functions       *
**************************
*/


/*
* Retrieve a section structure by name. If it doesn't exist, return NULL
*
* Arguments:
*
* 1. Pointer to configuration information.
* 2. Section name
*
* Return value:
*
* Pointer to section entry or NULL if it does not exist.
*/


SectionEntryPtr_t ConfReadFindSection(ConfigEntryPtr_t ce, const String section)
{
	unsigned sh;
	SectionEntryPtr_t se;

	ASSERT_FAIL(ce)
	ASSERT_FAIL(ce->magic == CE_MAGIC)
	ASSERT_FAIL(section)

	if(!ce->head)
		return NULL;

	/* Hash the section string passed in */
	sh = UtilHash(section);
	for(se = ce->head; (se); se = se->next){ /* Traverse section list */
		/* Compare hashes, and if they match, compare strings */
		if((sh == se->hash) && (!strcmp(se->section, section))){
			return se;
		}
	}
	return NULL; /* No match found */
}

/*
* Return the section name, or NULL if it does not exist
*
* Arguments:
*
* 1. Pointer to section entry.
*
* Return value:
*
* Pointer to section name or NULL if it does not exist.
*
*/

const String ConfReadGetSection(SectionEntryPtr_t se)
{
	ASSERT_FAIL(se)
	ASSERT_FAIL(se->magic == SE_MAGIC)
	return se->section;
}


/*
* Return a pointer to the first section entry if it exists. If it does not exist, return NULL
*
*
* Arguments:
*
* 1. Pointer to configuration information.
*
* Return value:
*
* Pointer to section entry or NULL if it does not exist.
*
*/

SectionEntryPtr_t ConfReadGetFirstSection(ConfigEntryPtr_t ce)
{
	ASSERT_FAIL(ce)
	ASSERT_FAIL(ce->magic == CE_MAGIC)
	return ce->head;
}

/*
* Return a pointer to the next section entry if it exists. If it does not exist, return NULL
*
* Arguments:
*
* 1. Pointer to current section entry
*
* Return value:
*
* Pointer to the next section entry or NULL if it does not exist.
*
*/

SectionEntryPtr_t ConfReadGetNextSection(SectionEntryPtr_t se)
{
	ASSERT_FAIL(se)
	ASSERT_FAIL(se->magic == SE_MAGIC)
	return se->next;
}

/*
* Return the line number for the section entry
*
*
* Arguments:
*
* 1. Pointer to the section entry.
*
* Return value:
*
* Line number
*/

unsigned ConfReadSectionLineNum(SectionEntryPtr_t se)
{
	ASSERT_FAIL(se)
	ASSERT_FAIL(se->magic == SE_MAGIC)
	return se->linenum;
}



/*
* Return a pointer to the matching key in a section if it exists
*
* Arguments:
*
* 1. Pointer to section entry.
* 2. String with key name
*
* Return value:
*
* Pointer to the key entry or NULL if it does not exist.
*/

KeyEntryPtr_t ConfReadFindKey(SectionEntryPtr_t se, const String key)
{
	unsigned kh;
	KeyEntryPtr_t ke;
	
	ASSERT_FAIL(se)
	ASSERT_FAIL(se->magic == SE_MAGIC)

	if(!se->key_head)
		return NULL;

	/* Hash the section string passed in */
	kh = UtilHash(key);
	for(ke = se->key_head; (ke); ke = ke->next){ /* Traverse key list */
		ASSERT_FAIL(ke->magic == KE_MAGIC)
		/* Compare hashes, and if they match, compare strings */
		if((kh == ke->hash) && (!strcmp(ke->key, key)))
			return ke;
	}
	return NULL; /* No match found */
}

/*
* Return a key from a key struct
*
* Arguments:
*
* 1. Pointer to key entry
*
* Return value:
*
* String with key entry
*/

const String ConfReadGetKey(KeyEntryPtr_t ke)
{
	ASSERT_FAIL(ke)
	ASSERT_FAIL(ke->magic == KE_MAGIC)
	return ke->key;
}


/*
* Return a line number from a key entry
*
* Arguments:
*
* 1. Pointer to key entry
*
* Return value:
*
* Line number
*/

unsigned ConfReadKeyLineNum(KeyEntryPtr_t ke)
{
	ASSERT_FAIL(ke)
	ASSERT_FAIL(ke->magic == KE_MAGIC)
	return ke->linenum;
}


/*
* Return first key structure in a given section if it exists
*
* Arguments:
*
* 1. Pointer to section entry
*
* Return value:
*
* Pointer to key entry, or NULL if it does not exist.
*/

KeyEntryPtr_t ConfReadGetFirstKey(SectionEntryPtr_t se)
{
	ASSERT_FAIL(se)
	ASSERT_FAIL(se->magic == SE_MAGIC)
	return se->key_head;
}

/*
* Return the next key structure pointed to by the current key structure if it exists
*
* Arguments:
*
* 1. Pointer to key entry
*
* Return value:
*
* Pointer to next key entry or NULL if it does not exist.
*/

KeyEntryPtr_t ConfReadGetNextKey(KeyEntryPtr_t ke)
{
	ASSERT_FAIL(ke)
	ASSERT_FAIL(ke->magic == KE_MAGIC)
	return ke->next;
}


/*
* Return a value associated with a key struct
*
* Arguments:
*
* 1. Pointer to key entry.
*
* Return value:
*
* String containing the value, or NULL if it does not exist.
*/

const String ConfReadGetValue(KeyEntryPtr_t ke)
{
	ASSERT_FAIL(ke)
	ASSERT_FAIL(ke->magic == KE_MAGIC)
	return ke->value;

}


/*
* Return value string by section entry and key name
*
* Arguments:
*
* 1. Pointer to section entry.
* 2. String with key name
*
* Return value:
*
* Value in a String or NULL if it does not exist.
*/

const String ConfReadValueBySectEntKey(SectionEntryPtr_t se, const String key)
{
	KeyEntryPtr_t ke;
	
	ke = ConfReadFindKey(se, key);
	if(ke){
		return ConfReadGetValue(ke);
	}
	return NULL;
}

/*
* Return key entry by section and key
*
*
* Arguments:
*
* 1. Pointer to configuration information.
* 2. Section name
* 3. Key name
*
* Return value:
*
* Value in a String or  NULL if it does not exist.
*/


KeyEntryPtr_t ConfReadKeyEntryBySectKey(ConfigEntryPtr_t ce, const String section, const String key)
{
	SectionEntryPtr_t se;

 	se = ConfReadFindSection(ce, section);
 	if(se){
		return ConfReadFindKey(se, key);
 	}
 	return NULL;
}

/*
* Return first Key in section
*
* Arguments:
*
* 1. Pointer to configuration information.
* 2. Section name
*
* Return value:
*
* Pointer to key entry or NULL if it does not exist.
*/

KeyEntryPtr_t ConfReadGetFirstKeyBySection(ConfigEntryPtr_t ce, const String section)
{
	SectionEntryPtr_t se = ConfReadFindSection(ce, section);
	if(se){
		return ConfReadGetFirstKey(se);
	}
	return NULL;
}

/*
* Return a count of the number of entries in a section
*
* Arguments:
*
* 1. Pointer to configuration information.
* 2. Section name
*
* Return value:
*
* Count of key/value pairs in this section.
*/

unsigned ConfReadGetNumEntriesInSect(ConfigEntryPtr_t ce, const String section)
{
	SectionEntryPtr_t se = ConfReadFindSection(ce, section);
	if(se)
		return se->entry_count;
	else
		return 0;
}


/*
* Find a value by section and key
*
* Arguments:
*
* 1. Pointer to configuration information.
* 2. Section name
* 3. Key
*
* Return value:
*
* Value as a string or NULL if it does not exist.
*/

const String ConfReadValueBySectKey(ConfigEntryPtr_t ce, const String section, const String key)
{
	KeyEntryPtr_t ke = ConfReadKeyEntryBySectKey(ce, section, key);
	if(ke){
		return ConfReadGetValue(ke);
	}
	return NULL;

}

/*
* Find value by section and key, convert to unsigned int, return in res.
*
* Arguments:
*
* 1. Pointer to configuration information.
* 2. Section name
* 3. Key
* 4. Pointer to an unsigned int to store the converted value.

*
* Return value:
*
* PASS indicates success, FAIL indicates failure.
*/

Bool ConfReadValueBySectKeyAsUnsigned(ConfigEntryPtr_t ce, const String section, const String key, unsigned *res)
{
	String num;
	ASSERT_FAIL(ce)
	ASSERT_FAIL(section)
	ASSERT_FAIL(key)
	ASSERT_FAIL(res)
	
	num = ConfReadValueBySectKey(ce, section, key);
	
	
	if((!num) || (FAIL == UtilStou(num, res))){
		*res = 0;
		return FAIL;
	}
	return PASS;

}


/*
* Default error handler for confreadScan()
*
* Arguments:
*
* 1. Error code
* 2. Line number (only valid with syntax errors)
* 3. Info string (only valid with I/O errors)
*
* Return value:
*
* None
*/

void ConfReadDefErrorHandler( int etype, int linenum, const String info)
{
	switch(etype){

		case CRE_SYNTAX:
			error("Syntax error in config file on line: %d", linenum);
			break;

		case CRE_IO:
			error("I/O error in confead.c: %s", info);
			break;

		case CRE_FOPEN:
			error("Could not open config file at: %s", info);
			break;

		default:
			error("Unknown error code: %d", etype);
			break;

	}


}



/*
* Free all data structures associated with the config file
*
* Arguments:
*
* 1. Pointer to configuration information.
*
* Return value:
*
* None
*/

void ConfReadFree(ConfigEntryPtr_t ce)
{
	if(ce){
		talloc_free(ce);
	}
}


/*
* Dump all printable fields in all data structures associated with the config file
*
* Arguments:
*
* 1. Pointer to configuration information.
*
* Return value:
*
* None
*
*/

void ConfReadDebugDump(ConfigEntryPtr_t ce)
{
	SectionEntryPtr_t se;
	KeyEntryPtr_t kv;
	
	ASSERT_FAIL(ce)
	ASSERT_FAIL(ce->magic == CE_MAGIC)


	se = ce->head; /* Start at beginning of section list and work forward */
	
	while((se) && (se->magic == SE_MAGIC)){
		if(se->section)
			printf("**** Section: %s Hash: %08X Line Number: %d ****\n", se->section, se->hash, se->linenum);
		else
			printf("!!!! NULL Section string on line number %d !!!!\n", se->linenum);

		kv = se->key_head; /* Start at beginning of kv list and work forward */
		while((kv) && (kv->magic == KE_MAGIC)){
			if(kv->key)
				printf("Key: %s Hash: %08X Line Number %d ", kv->key, kv->hash, kv->linenum);
			else
				printf("!! NULL Key on line number %d", kv->linenum);
			if(kv->value)
				printf(" Value: %s\n",kv->value);
			else
				printf(" !! NULL Value\n");
			kv = kv->next;		
		}
		se = se->next;
	}
}


/*
* Scan a config file and load it into our data structures
* Pass in the path to the config file, and optionally an error handling function.
* If the default error handling function is going to be used, then pass in a NULL for the
* second argument.
*
* Arguments:
*
* 1. Talloc context to hang the configuration data structures off of.
* 2. A string with the path name to the configuration file.
* 3. An error callback or NULL if default error handler is to be used (see below)
*
* Return value:
*
* Pointer to configuration information or NULL if there was an error.
*
********** Callback Function ***********
*
* Arguments:
*
* 1. Error code
* 2. Line number (only valid with syntax errors)
* 3. Info string (only valid with I/O errors)
*
* Return value:
*
* None
*
*/


ConfigEntryPtr_t ConfReadScan(void *ctx, const String thePath, void (*error_callback)(int type, int linenum, const String info )){
	FILE *conf_file;
	char *p;
	ConfigEntryPtr_t ce = NULL;
	SectionEntryPtr_t se = NULL;
	KeyEntryPtr_t kv = NULL;
	int linenum;
	
	
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(thePath)

	/* User our built in handler if no error handler is specified */

	if(!error_callback)
		error_callback = ConfReadDefErrorHandler;

	/* Allocate a config entry */

	ce = talloc_zero(ctx, ConfigEntry_t);
	MALLOC_FAIL(ce)
	
	
	/* Initialize config entry */
	ce->magic = CE_MAGIC;

	
	/* Allocate a line buffer */

	ce->line = talloc_zero_array(ce, char, MAX_CONFIG_LINE);
	MALLOC_FAIL(ce->line)


	/* Allocate a work string */

	ce->work_string = talloc_zero_array(ce, char, MAX_CONFIG_LINE);
	MALLOC_FAIL(ce->work_string)


	/* Open the config file */

	if((conf_file = fopen(thePath, "re")) == NULL){
		(*error_callback)(CRE_FOPEN, __LINE__, thePath);
		return NULL;
	}
	
	for(linenum = 1; ; linenum++){
		/* Get a line */
		if(fgets(ce->line, MAX_CONFIG_LINE, conf_file) == NULL)
			break;

		/* Remove spaces and tabs */
		removespctab(ce->line);

		
		p = ce->line;
		
		/* Parse tree root */

		switch(linescan(&p, ce->work_string)){

			/* It was a newline or a comment, get another line */

			case TOK_NL:
			case TOK_COMMENT:
				break;

			/* It was a section ID. Get it */
			
			case TOK_SECTION:

				se = talloc_zero(ce, SectionEntry_t);
				MALLOC_FAIL(se)
				
				/* Initialize section entry */
				se->magic = SE_MAGIC;

				/* Copy the section name into the new entry */
				se->section = talloc_strdup(se, ce->work_string);
				MALLOC_FAIL(se->section);
			
				/* Hash the section */
				se->hash = UtilHash(se->section);

				/* Record the line number */
				se->linenum = linenum;
	
				/* Scan rest of line looking for a comment or a new line */

				switch(linescan(&p, NULL)){
					case TOK_NL:
					case TOK_COMMENT:
						break;

					default:
						debug(DEBUG_UNEXPECTED,"only newline or comment token is valid after a section token");
						(*error_callback)(CRE_SYNTAX, linenum, NULL);
						return NULL;
				}

				/* Insert into section list */
				if(!ce->head){
					ce->head = se; /* First entry */
				}
				else{
					ce->tail->next = se; /* Subsequent entry */
					se->prev = ce->tail;
				}
				ce->tail = se;
				break; 


			case TOK_KEY:

				kv = NULL;
				if(se){	/* There has to be a section defined */
					kv = talloc_zero(se, KeyEntry_t);
					MALLOC_FAIL(kv)
					
		
					/* Initialize section entry */
					kv->magic = KE_MAGIC;

					/* Save the key */
					kv->key = talloc_strdup(kv, ce->work_string);
					MALLOC_FAIL(kv->key);
				

					/* Hash the key */
					kv->hash = UtilHash(kv->key);

					/* Record the line number */
					kv->linenum = linenum;
				}

				/* Next token had better be a value */

				switch(linescan(&p, ce->work_string)){
					case TOK_VALUE:
						if(kv && se){
							/* Save value */
							kv->value = talloc_strdup(kv, ce->work_string);
							ASSERT_FAIL(kv->value)
						
							/* Count the new entry */
							se->entry_count++;
							/* Insert new key/value into list in current section */
							if(!se->key_head){
								se->key_head = kv; /* First entry */
							}
							else{
								se->key_tail->next = kv; /* Subsequent entry */
								kv->prev = se->key_tail;
							}
							se->key_tail = kv;
								
						}
						break;
					default:
						debug(DEBUG_UNEXPECTED, "should have received a value token");
						(*error_callback)(CRE_SYNTAX, linenum, NULL);
						return NULL;
				}

				/* Next token had better be a */
				/* newline or comment */

				switch(linescan(&p, NULL)){
					case TOK_NL:
					case TOK_COMMENT:
						break;
					default:
						debug(DEBUG_UNEXPECTED, "invalid token found while parsing a key/value");
						(*error_callback)(CRE_SYNTAX, linenum, NULL);
						return NULL;

				}
				break;
			case TOK_ERR:
				debug(DEBUG_UNEXPECTED, "TOK_ERR returned from linescan()");
				(*error_callback)(CRE_SYNTAX, linenum, NULL);
				return NULL;


			default:
				debug(DEBUG_UNEXPECTED,"unexpected token returned");
				(*error_callback)(CRE_SYNTAX, linenum, NULL);
				return NULL;

		}
	
	}			 

	if(ferror(conf_file)){
		(*error_callback)(CRE_IO, __LINE__, strerror(errno));
		return NULL;		
	}

	else
		fclose(conf_file);
	return ce;
}


