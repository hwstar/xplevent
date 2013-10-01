/*
 * xplio.c
 * 
 * Copyright 2013 Steve Rodgers <hwstar@rodgers.sdcoxmail.com>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 * 
 * 
 */


#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <ctype.h>
#include <getopt.h>
#include <limits.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <sqlite3.h>
#include <talloc.h>
#include  "defs.h"
#include "types.h"
#include "notify.h"
#include "socket.h"
#include "parser.h"
#include "util.h"
#include "scheduler.h"
#include "poll.h"
#include "xplevent.h"
#include "xplio.h"

static pthread_t ioThread;
static Bool ioThreadAlive = FALSE;

static void *xplThread(void *userObj)
{
	ioThreadAlive = TRUE; /* Fixme */
	
	while(ioThreadAlive){
		sleep(1);
		debug(DEBUG_ACTION, "Thread tick");
	}
	return NULL;
}



Bool XplioInit(TALLOC_CTX *ctx, String bindAddress)
{
	pthread_attr_t attrs;
	char estr[80];
	int res;
	
	
	if((res = pthread_attr_init(&attrs))){
		debug(DEBUG_UNEXPECTED, "%s: Could not initialize pthread attribute: res = %d", __func__, res);
		return FAIL;
	}
	
	
	if((res = pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_DETACHED))){
		debug(DEBUG_UNEXPECTED, "%s: Could not set detach attribute: res = %d", __func__, res);
		return FAIL;
	}
	
	if((res = pthread_attr_setstacksize(&attrs, 32768))){
		debug(DEBUG_UNEXPECTED, "%s: Could not set stack size attribute: res = %d", __func__, res);
		return FAIL;
	}
	
	
	if(pthread_create(&ioThread, &attrs, xplThread, ctx) < 0){
		debug(DEBUG_UNEXPECTED, "%s: Could not create thread: %s",__func__, strerror_r(errno, estr, 80));
		return FAIL;
	}
	return PASS;
}


