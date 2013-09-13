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


/*
* Read a file into a string
*/

String UtilFileReadString(TALLOC_CTX *ctx, String filename)
{
	FILE *file;
	String id = "UtilFileReadString";
	long size;
	int arraylen;
  
	String str = NULL;
  
	ASSERT_FAIL(filename)
  
	file = fopen(filename, "r");
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
*/

Bool UtilFileWriteString(String filename, String str)
{
	int len;
	FILE *file;
	String id = "UtilFileWriteString";
  
	ASSERT_FAIL(filename)
	ASSERT_FAIL(str)
  
	file = fopen(filename, "w");
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
* Hash a string
*/

uint32_t UtilHash(const String key)
{
	int len;
	register uint32_t hash, i;

	ASSERT_FAIL(key)

	len = strlen(key);

	if(!key){
		return 0;
	}

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
* Move string from one context to another
*/

String UtilMoveString(TALLOC_CTX *newCtx, String oldStr, int offset)
{
	String newStr;

	ASSERT_FAIL(newCtx)
	ASSERT_FAIL(oldStr)

	ASSERT_FAIL(strlen(oldStr) > offset)

	if((newStr = talloc_strdup(newCtx, oldStr + offset))){
		talloc_free(oldStr);
	}

	return newStr;
}


/*
* Safer string copy
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
* Get the pid from a pidfile. Returns the pid or -1 if it couldn't get the
* pid (either not there, stale, or not accesible).
*/

pid_t UtilPIDRead(String filename) {
	FILE *file;
	pid_t pid;

	/* Get the pid from the file. */
	file=fopen(filename, "r");
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
* Write the pid into a pid file. Returns zero if it worked, non-zero
* otherwise.
*/

int UtilPIDWrite(String filename, pid_t pid) {
	FILE *file;

	/* Create the file. */
	file=fopen(filename, "w");
	if(!file) {
		return -1;
	}

	/* Write the pid into the file. */
	(void) fprintf(file, "%d\n", pid);
	if(ferror(file) != 0) {
		(void) fclose(file);
		return -1;
	}

	/* Close the file. */
	if(fclose(file) != 0) {
		return -1;
	}

	/* We finished ok. */
	return 0;
}




