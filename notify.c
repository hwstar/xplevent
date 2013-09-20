/*
*    xplevent - an xPL event handler
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
#include "defs.h"
#include "types.h"
#include "notify.h"
#include "xplevent.h"

#define LOGOUT (output == NULL ? stderr : output)

static FILE *output = NULL;

static Bool timeen = 0;


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
* Redirect logging and error output. If a logging file is open,
* close it before opening the new file.
*
* Arguments:
* 
* String with path to log file
*
* Return value:
*
* None
*/

void notify_logpath(const String path)
{
  FILE *f;

  if(output != NULL){
    fclose(output);
  } 
  if((f = fopen(path,"we")) == NULL){
    fatal_with_reason(errno, "Can't open log file for writing");
  }
  output = f;
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

    fprintf(LOGOUT, "%s: ", Globals->progName);
    vfprintf(LOGOUT, message, ap);
    fprintf(LOGOUT, ": %s\n",strerror(theErrno));

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
	fprintf(LOGOUT,"%s: ", Globals->progName);
	vfprintf(LOGOUT,message,ap);
	fprintf(LOGOUT,"\n");
	
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
	fprintf(LOGOUT,"%s: ",Globals->progName);
	vfprintf(LOGOUT,message,ap);
	fprintf(LOGOUT,"\n");
	
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
	fprintf(LOGOUT,"%s: warning: ", Globals->progName);
	vfprintf(LOGOUT,message,ap);
	fprintf(LOGOUT,"\n");
	
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
	if(Globals->debugLvl >= level) {
		if(timeen){
			t = time(NULL);
			strncpy(timenow,ctime(&t), 31);
			timenow[31] = 0;
			l = strlen(timenow);
			if(l)
			timenow[l-1] = '\0';
      
			/* Print the error message. */
			fprintf(LOGOUT,"%s [ %s ] (debug): ", Globals->progName, timenow);
		}
		vfprintf(LOGOUT, message, ap);
		fprintf(LOGOUT,"\n");
		fflush(LOGOUT);
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

	if(Globals->debugLvl >= level) {
		fprintf(LOGOUT,"%s: (debug): ",Globals->progName);
		vfprintf(LOGOUT,message,ap);
		for(i = 0 ; i < buflen ; i++){
			fprintf(LOGOUT,"%02X ",((int) ((char *)buf)[i]) & 0xFF);
		}
		fprintf(LOGOUT,"\n");
	}
}

