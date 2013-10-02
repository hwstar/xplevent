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
#include <sys/eventfd.h>
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
#include "xplrx.h"

#define XH_LOCK pthread_mutex_lock(&xh->lock);
#define XH_UNLOCK pthread_mutex_unlock(&xh->lock);



#define RXBUFFPOOLSIZE 1024*128
#define RXTHREADSTACKSIZE 32768
#define XH_MAGIC 0x7A1F0CE2

typedef struct XplRXHead_s {
	unsigned magic;
	pthread_t rxThread;
	pthread_mutex_t lock;
	int sockFD;
	int rxEventFD;
	int rxEventVal;
	void *rxBufferPool;
	void *rxPoller;
	
} XplRXHead_t, *XplRXHeadPtr_t;

/* 
 * Clean up
 */
 
static void rxCleanup(XplRXHeadPtr_t xh)
{
	XH_LOCK
	PollDestroy(xh->rxPoller);
	close(xh->rxEventFD);
	talloc_free(xh->rxBufferPool);
	XH_UNLOCK
}
/*
 * RX Event Action
 */

static void rxEventAction(int fd, int event, void *userObject)
{
	XplRXHeadPtr_t xh = userObject;
	char buf[8];
	int val;
	
	ASSERT_FAIL(xh);
	
	
	
	XH_LOCK
	ASSERT_FAIL(XH_MAGIC == xh->magic);
	val = xh->rxEventVal;
	XH_UNLOCK
	
	if(read(fd, buf, 8) < 0){
		debug(DEBUG_UNEXPECTED,"%s: read error", __func__);
	}
	else{	

		debug(DEBUG_EXPECTED, "Ding! event value: %d", val);
	}
}

/*
 * Rx Thread
 */
 
static void *rxThread(void *userObj)
{
	XplRXHeadPtr_t xh = userObj;
	void *poller;
	
	
	XH_LOCK
	
	ASSERT_FAIL(XH_MAGIC == xh->magic)
	

	/* Create a polling resource */
	if(!(xh->rxPoller = PollInit(xh, 4))){
		debug(DEBUG_UNEXPECTED, "Could not create polling resource");
		talloc_free(xh);
		return NULL;
	}

	/* Add the rxEvent FD to the polling list */
	if(FAIL == PollRegEvent(xh->rxPoller, xh->rxEventFD, POLL_WT_IN, rxEventAction, xh)){
		debug(DEBUG_UNEXPECTED, "Could not register RX eventfd");
	}
	
	/* Allocate the receive buffer pool */
	MALLOC_FAIL(xh->rxBufferPool = talloc_pool(xh, RXBUFFPOOLSIZE));
	
	/* Copy the polling resource pointer */
	poller = xh->rxPoller;
	
	XH_UNLOCK
	
	

	PollWait(poller, -1, NULL);
	debug(DEBUG_UNEXPECTED, "Poll returned serious error: %d", errno);
	rxCleanup(xh);
	exit(1);
	
	return NULL;
}




/*
 * Initialization function
 */

void *XplRXInit(TALLOC_CTX *ctx)
{
	pthread_attr_t attrs;
	int res;
	XplRXHeadPtr_t xh;
	
	/* Allocate a Header */
	MALLOC_FAIL(xh = talloc_zero(ctx, XplRXHead_t))
	

	/* Initialize the guarding mutex */
	ASSERT_FAIL( 0 == pthread_mutex_init(&xh->lock, NULL))
	
	/* Set the magic number */
	xh->magic = XH_MAGIC;
	
	/* Get an event FD */
	if((xh->rxEventFD = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)) < 0){
		debug(DEBUG_UNEXPECTED, "Could not create an event FD");
		talloc_free(xh);
		return NULL;
	}
	
	/* Initialize attr type */
	if((res = pthread_attr_init(&attrs))){
		debug(DEBUG_UNEXPECTED, "%s: Could not initialize pthread attribute: res = %d", __func__, res);
		talloc_free(xh);
		return NULL;
	}
	
	/* Set the detached state attr */
	if((res = pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_DETACHED))){
		debug(DEBUG_UNEXPECTED, "%s: Could not set detach attribute: res = %d", __func__, res);
		talloc_free(xh);
		return NULL;
	}
	
	/* Set the stack size attr */
	if((res = pthread_attr_setstacksize(&attrs, RXTHREADSTACKSIZE))){
		debug(DEBUG_UNEXPECTED, "%s: Could not set stack size attribute: res = %d", __func__, res);
		talloc_free(xh);
		return NULL;
	}
	
	/* Create the thread */
	if(pthread_create(&xh->rxThread, &attrs, rxThread, xh) < 0){
		debug(DEBUG_UNEXPECTED, "%s: Could not create thread", __func__);
		talloc_free(xh);
		return NULL;
	}
	
	/* Return the object */
	
	return xh;
}

/*
 * Send an event to the RX thread 
 */
 
Bool XplrxSendEvent(void *xplrxheader, int val)
{
	XplRXHeadPtr_t xh = xplrxheader;
	int evFD;
	long long incr = 1;
	XH_LOCK
	xh->rxEventVal = val; /* Get the value */
	evFD = xh->rxEventFD; /* Copy the fd */
	XH_UNLOCK
	
	if(write(evFD, &incr, sizeof(incr)) < 0){
		debug (DEBUG_UNEXPECTED, "%s: Could not write event increment",__func__);
		return FAIL;
	}

	return PASS;
}


