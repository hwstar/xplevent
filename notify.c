/*
*    notify.c
*    Copyright (C) 2012,2013  Stephen A. Rodgers
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*
*    Notification helpers
*   
* 
* 
* Steve Rodgers <hwstar@rodgers.sdcoxmail.com>
*
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include "defs.h"
#include "types.h"
#include "notify.h"

#define NFY_LOCK pthread_mutex_lock(&lock);
#define NFY_UNLOCK pthread_mutex_unlock(&lock);

#define LOGOUT (output == NULL ? stderr : output)

static FILE *output = NULL;

static Bool timeen = 0;

static String progName = "";

static int debugLvl = 1;

static pthread_mutex_t lock;


/*
* Enable/disable time reporting
*
* Arguments:
* 
* Emable flag
*
* Return value:
*
* None
*
*/
 
void notify_timen(Bool ena)
{
	timeen = ena;
}


/*
* Set up the notify module
*
* Arguments:
* 
* 1. String with program name (e.g. argv[0])
*
* Return value:
*
* None
*/

void notify_init(const String pgmName)
{
	/* Initialize the guarding mutex */
	pthread_mutex_init(&lock, NULL);
	
	if(pgmName){
		progName = pgmName;
	}
}

/*
* If a logging file is open,
* close it before opening the new file.
*
* Arguments:
* 
* 1. String with path to log file
*
* Return value:
*
* None
*/

void notify_logpath(const String path)
{
	FILE *f;
	if(path){ 
		if(output != NULL){
			fclose(output);
		} 
		if((f = fopen(path, "we")) == NULL){
			fatal_with_reason(errno, "Can't open log file for writing");
		}
		output = f;
	}
}


/*
* Set the debug level.
* 
*
* Arguments:
* 
* 1. debugging level as integer.
*
* Return value:
*
* None
*/

void notify_set_debug_level(int dbglvl)
{
	debugLvl = dbglvl;
}

/*
* Set the debug level.
* 
*
* Arguments:
* 
* None
*
* Return value:
*
* Debug level as integer
*/

int notify_get_debug_level(void)
{
	return debugLvl;
}


/* 
* Fatal error handler with strerror(errno) reason
*
* Arguments:
* 
* 1. errno
* 2. Format string
* 3. Variadic arguments
*
* Return value:
*
* None
*/


void fatal_with_reason(int theErrno, const String message, ...)
{
    va_list ap;
    
    va_start(ap, message);
    
	NFY_LOCK
    fprintf(LOGOUT, "%s: ", progName);
    vfprintf(LOGOUT, message, ap);
    fprintf(LOGOUT, ": %s\n",strerror(theErrno));
    NFY_UNLOCK

    va_end(ap);
    exit(1);
}


/*
* Fatal error handler. 
* Arguments:
* 
* 1. Format string
* 2. Variadic arguments
*
* Return value:
*
* None
*/

void fatal(const String message, ...) {
	va_list ap;
	va_start(ap, message);
	
	/* Print error message. */
	NFY_LOCK
	fprintf(LOGOUT,"%s: Fatal: ", progName);
	vfprintf(LOGOUT,message,ap);
	fprintf(LOGOUT,"\n");
	NFY_UNLOCK
	
	/* Exit with an error code. */
	va_end(ap);
	exit(1);
}

/* 
* Assertion failure handler 
*
* Arguments:
* 
* 1. File name
* 2. line number
*
* Return value:
*
* None
*/

void assertion_failure(const String file, int line)
{
	fatal("Assertion failure detected in file: %s, line: %d", file, line);
}

/* 
* Memory allocation failure handler
*
* Arguments:
* 
* 1. File name
* 2. line number
*
* Return value:
*
* None
*/

void malloc_failure(const String file, int line)
{
	fatal("Memory allocation failure detected in file: %s, line: %d", file, line);
}


/* 
* Normal error handler.
*
* Arguments:
* 
* 1. Format string
* 2. Variadic arguments
*
* Return value:
*
* None
*/
void error(const String message, ...) {
	va_list ap;
	va_start(ap, message);
	
	/* Print error message. */
	NFY_LOCK
	fprintf(LOGOUT,"%s: Error: ",progName);
	vfprintf(LOGOUT,message,ap);
	fprintf(LOGOUT,"\n");
	NFY_UNLOCK
	
	va_end(ap);
	return;
}


/* 
* Warning handler
* Arguments:
* 
* 1. Format string
* 2. Variadic arguments
*
* Return value:
*
* None
*/

void warn(const String message, ...) {
	va_list ap;
	va_start(ap, message);
	
	/* Print warning message. */
	NFY_LOCK
	fprintf(LOGOUT,"%s: Warning: ", progName);
	vfprintf(LOGOUT,message,ap);
	fprintf(LOGOUT,"\n");
	NFY_UNLOCK
	
	va_end(ap);
	return;
}

/* 
* Note handler
* Arguments:
* 
* 1. Format string
* 2. Variadic arguments
*
* Return value:
*
* None
*/

void note(const String message, ...) {
	va_list ap;
	va_start(ap, message);
	
	/* Print warning message. */
	NFY_LOCK
	fprintf(LOGOUT,"%s: Note: ", progName);
	vfprintf(LOGOUT,message,ap);
	fprintf(LOGOUT,"\n");
	NFY_UNLOCK
	
	va_end(ap);
	return;
}



/* 
* Debugging message handler
*
* Arguments:
* 
* 1. Debug level
* 2. Format string
* 3. Variadic arguments
*
* Return value:
*
* None
*/
void debug(int level, const String message, ...) {
	va_list ap;
	va_start(ap, message);
	time_t t;

	char timenow[32];
	int l;
 	
	/* We only do this code if we are at or above the debug level. */
	if(debugLvl >= level) {
		NFY_LOCK
		if(timeen){
			t = time(NULL);
			strncpy(timenow,ctime(&t), 31);
			timenow[31] = 0;
			l = strlen(timenow);
			if(l)
			timenow[l-1] = '\0';
      
			/* Print the error message. */
			fprintf(LOGOUT,"%s [ %s ] (debug): ", progName, timenow);
		}
		vfprintf(LOGOUT, message, ap);
		fprintf(LOGOUT,"\n");
		fflush(LOGOUT);
		NFY_UNLOCK
	}
	va_end(ap);
}

/* 
* Print a debug string with a buffer of bytes to print
*
* Arguments:
* 
* 1. Debug level
* 2. Buffer to print as hex values
* 3. Format string
* 4. Variadic arguments
*
* Return value:
*
* None
*/

void debug_hexdump(int level, const void *buf, int buflen, const String message, ...){
	int i;
	va_list ap;
	va_start(ap, message);

	if(debugLvl >= level) {
		NFY_LOCK
		fprintf(LOGOUT,"%s: (debug): ",progName);
		vfprintf(LOGOUT,message,ap);
		for(i = 0 ; i < buflen ; i++){
			fprintf(LOGOUT,"%02X ",((int) ((char *)buf)[i]) & 0xFF);
		}
		fprintf(LOGOUT,"\n");
		NFY_UNLOCK
	}
}

