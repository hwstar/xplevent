
/*
* util.c 
*
* Copyright (C) 2013 Stephen Rodgers
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 3
* of the License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*
*
* Stephen "Steve" Rodgers <hwstar@rodgers.sdcoxmail.com>
*
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <talloc.h>
#include "defs.h"
#include "types.h"
#include "notify.h"
#include "util.h"

/*
 * Split a string into a array of substrings using either space or tab as the split character
 * Return an array of strings, NULL terminated. Individual strings are allocated referenced to the array of strings.
 * Freeing the array of strings is required to avoid memory leaks. Doing so automatically frees the underlying
 * strings as well.
 *
 * Arguments:
 *
 * 1. Talloc Context to hang the result off of.
 * 2. The Input string to split into sub-strings
 *
 * Return value:
 *
 * An array of NUL terminated Strings NULL terminated.
 */
 
String *UtilSplitWhite(TALLOC_CTX *ctx, String input)
{
	String start,p;
	String *strings;
	int len;
	int count = 0;
	int apsize = 8;
	int state = 0;
	Bool done;
	
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(input)
	
	/* Allocate an initial array of strings */
	MALLOC_FAIL(strings = talloc_array(ctx, String, apsize))
	
	
	for(done = FALSE, p = input, strings[0] = NULL; !done ; p++){
		if(count >= apsize - 2){ /* Extend array of pointers if close to top of current allocation */
			apsize <<= 1;
			MALLOC_FAIL(strings = talloc_realloc(ctx, strings, String , apsize))
		}
		
		switch(state){
			case 0: /* Search for first non sep char */
				if(!*p){
					done = TRUE; /* End of string detected */
					break;
				}				
				if((*p == ' ')||(*p == '\t')){
					break; /* Space or tab detected, continue scanning */
				}
				else{
					start = p; /* First non-sep character detected, note its position */
					len = 1;  /* Set initial length */
					state++; /* Start looking for sep characters */
				}
				break;
				
			case 1: /* skip characters till next sep */
				if((*p == ' ') || (*p == '\t') || (!*p)){
					/* Allocate a string of the correct length */
					MALLOC_FAIL(strings[count] = talloc_zero_array(strings, char, len + 1)) 
					/* Copy the string into the newly allocated area */
					strncpy(strings[count++], start, len);
					/* Mark the new end of list */
					strings[count] = NULL;
					/* Go back to looking for non sep characters */
					state--;
					if(!*p){ /* End of string detected */
						done = TRUE;
					}	
				}
				else{
					len++;
				}
		}	
	}
	return strings;	
}

/*
 * Split a string into a array of substrings.
 * Return an array of strings, NULL terminated. Individual strings are allocated referenced to the array of strings.
 * Freeing the array of strings is required to avoid memory leaks. Doing so automatically frees the underlying
 * strings as well.
 *
 * Arguments:
 *
 * 1. Talloc Context to hang the result off of.
 * 2. The Input string to split into sub-strings
 * 3. The separator character.
 *
 * Return value:
 *
 * An array of NUL terminated Strings NULL terminated.
 */
 
String *UtilSplitString(TALLOC_CTX *ctx, String input, char sep)
{
	String start,p;
	String *strings;
	int len;
	int count = 0;
	int apsize = 8;
	int state = 0;
	Bool done;
	
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(input)
	
	/* Allocate an initial array of strings */
	MALLOC_FAIL(strings = talloc_array(ctx, String, apsize))
	
	
	for(done = FALSE, p = input, strings[0] = NULL; !done ; p++){
		if(count >= apsize - 2){ /* Extend array of pointers if close to top of current allocation */
			apsize <<= 1;
			MALLOC_FAIL(strings = talloc_realloc(ctx, strings, String , apsize))
		}
		
		switch(state){
			case 0: /* Search for first non sep char */
				if(!*p){
					done = TRUE; /* End of string detected */
					break;
				}				
				if(*p == sep){
					break; /* Sep char detected, continue scanning */
				}
				else{
					start = p; /* First non-sep character detected, note its position */
					len = 1;  /* Set initial length */
					state++; /* Start looking for sep characters */
				}
				break;
				
			case 1: /* skip characters till next sep */
				if((*p == sep) || (!*p)){
					/* Allocate a string of the correct length */
					MALLOC_FAIL(strings[count] = talloc_zero_array(strings, char, len + 1)) 
					/* Copy the string into the newly allocated area */
					strncpy(strings[count++], start, len);
					/* Mark the new end of list */
					strings[count] = NULL;
					/* Go back to looking for non sep characters */
					state--;
					if(!*p){ /* End of string detected */
						done = TRUE;
					}	
				}
				else{
					len++;
				}
		}	
	}
	return strings;	
}

/*
* Read a file into a string
*
* Arguments:
*
* 1. Talloc context to hang the result off of.
* 2. Path to the file name.
*
* Return Value:
*
* File contents as a NUL terminated string or NULL if an error occured. 
* String must be freed when no longer required.
*/

String UtilFileReadString(TALLOC_CTX *ctx, const String filename)
{
	FILE *file;
	String id = "UtilFileReadString";
	long size;
	int arraylen;
  
	String str = NULL;
  
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(filename)
  
	file = fopen(filename, "re");
	if(!file) {
		debug(DEBUG_UNEXPECTED, "%s: Can't open file: %s for reading: %s",id, filename, strerror(errno));
		return NULL;
	}
  
	if(fseek(file, 0L, SEEK_END) < 0){
		debug(DEBUG_UNEXPECTED, "%s: Can't seek to end of file: %s: %s",id, filename, strerror(errno));
		return NULL;
	}
	
	size = ftell(file);
	if(fseek(file, 0L, SEEK_SET) < 0){
		debug(DEBUG_UNEXPECTED, "%s: Can't seek to end of file: %s: %s",id, filename, strerror(errno));
		return NULL;
	}

	arraylen = ((int) size) + 1; 
 
	str = talloc_array(ctx, char, arraylen);
  
	MALLOC_FAIL(str);
  
	if(size != fread(str, sizeof(char), (int) size, file)){
		debug(DEBUG_UNEXPECTED, "%s: Read error on file: %s: %s",id, filename, strerror(errno));
		talloc_free(str);
		return NULL;
	}
  
	str[arraylen - 1] = 0;
  
	if(fclose(file)){
		debug(DEBUG_UNEXPECTED, "%s: Close error on file: %s: %s",id, filename, strerror(errno));
		return NULL;
	}

	return str;
  
}

/*
* Write a string to a file
*
* Arguments:
*
* 1. Path to location to write the file
* 2. String data to write into the file (NUL terminated)
*
* Return Value:
*
* Boolean. PASS indicates success, FAIL indicates there was some type of error.
*/

Bool UtilFileWriteString(const String filename, const String str)
{
	int len;
	FILE *file;
	String id = "UtilFileWriteString";
  
	ASSERT_FAIL(filename)
	ASSERT_FAIL(str)
  
	file = fopen(filename, "we");
	if(!file) {
		debug(DEBUG_UNEXPECTED, "%s: Can't open file: %s for writing: %s",id, filename, strerror(errno));
		return FAIL;
	}
  
	len = strlen(str);
  
	if(len != fwrite(str, sizeof(char), len, file)){
		debug(DEBUG_UNEXPECTED, "%s: Write error on file: %s: %s",id, filename, strerror(errno));
		return FAIL;
	}
  
	if(fclose(file)){
	debug(DEBUG_UNEXPECTED, "%s: Close error on file: %s: %s",id, filename, strerror(errno));
	return FAIL;
	}

	return PASS;
}


/*
* Hash a string.
* 
* Arguments:
* 
* 1. Nul terminated string to hash.
*
* Return Value:
*
* 32 bit unsigned integer containing the hash.
*/

uint32_t UtilHash(const String key)
{
	int len;
	register uint32_t hash, i;

	ASSERT_FAIL(key)

	len = strlen(key);


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
* Move a string or the right hand portion of a string from one context to another, freeing the old context.
*
* Arguments:
*
* 1. The new talloc context to hang the result off of.
* 2. The old string to move and free.
* 3. The offset to be applied to the left hand side of the old string.
*
* Return value:
*
* A pointer to the new string, or NULL if the memory allocation failed. 
*/

String UtilMoveString(TALLOC_CTX *newCtx, String oldStr, int offset)
{
	String newStr;

	ASSERT_FAIL(newCtx)
	ASSERT_FAIL(oldStr)

	ASSERT_FAIL(strlen(oldStr) > offset) /* Offset must not be larger than the old string */

	if((newStr = talloc_strdup(newCtx, oldStr + offset))){
		talloc_free(oldStr);
	}

	return newStr;
}


/*
* Copy N characters of one string to another, and gaurantee that the destination
* string is always NUL-terminated.
*
* Arguments:
*
* 1. Destination String
* 2. Source String
* 3. Characters to copy
*
* Return value:
*
* The destination string.
*
*/

String UtilStringCopy(String dest, const String src, int charsToCopy)
{
	ASSERT_FAIL(dest)
	ASSERT_FAIL(src)
 

	strncpy(dest, src, charsToCopy);
	dest[charsToCopy - 1] = 0;
	return dest;
}


/*
* Get the pid from a pidfile. 
*
* Arguments:
*
* 1. The full path to the .pid file 
* 
* Return value:
*
* Returns the pid or -1 if it couldn't get the
* pid (either not there, stale, or not accesible).
*
*
*/

pid_t UtilPIDRead(const String filename) {
	FILE *file;
	pid_t pid;
	
	ASSERT_FAIL(filename)
	/* Get the pid from the file. */
	file=fopen(filename, "re");
	if(!file) {
		return -1;
	}
	if(fscanf(file, "%d", &pid) != 1) {
		fclose(file);
		return -1;
	}
	if(fclose(file) != 0) {
		return -1;
	}
	/* Check that a process is running on this pid. */
	if(kill(pid, 0) != 0) {
		/* It might just be bad permissions, check to be sure. */
		if(errno == ESRCH) {
			return -1 ;
		}
	}
	/* Return this pid. */
	return(pid);
}

/*
* Write the pid into a pid file. 
*
* Arguments:
*
* 1. Full path to the file name to create.
* 2. The process id to store in the file.
* 
* Return value
*
* Returns zero if it worked, non-zero
* otherwise.
*/

Bool UtilPIDWrite(const String filename, pid_t pid) {
	FILE *file;
	
	ASSERT_FAIL(filename)

	/* Create the file. */
	file=fopen(filename, "we");
	if(!file) {
		return FAIL;
	}
	/* Write the pid into the file. */
	(void) fprintf(file, "%d\n", pid);
	if(ferror(file) != 0) {
		(void) fclose(file);
		return FAIL;
	}
	/* Close the file. */
	if(fclose(file) != 0) {
		return FAIL;
	}
	/* We finished ok. */
	return PASS;
}

/*
* Fork and execute a command by invoking the shell and passing the command to it. 
*
* Arguments:
*
* 1. The command to execute.
* 2. A pointer to a pid_t where the process id of the child process will be stored, or NULL if don't care.
*
* Return value:
*
* Boolean: PASS indicates success, FAIL indicates failure.
*/

Bool UtilSpawn(const String command, pid_t *pid)
{
	String id = "UtilForkAndExec";
	pid_t retval;
	
	ASSERT_FAIL(command)
	
	if((retval = fork())){
		if(retval > 0){
			if(pid){
				*pid = retval;
			}
			/*  Parent */
			return PASS;
		}
		else{
			/* Error */
			debug(DEBUG_UNEXPECTED, "%s: Fork failure: %s", id, strerror(errno));
			return FAIL;
		}
	}
	else{ /* Child */
		execlp("sh","sh","-c", command, (String) NULL);
		exit(1);
	}
	return FAIL;
}




