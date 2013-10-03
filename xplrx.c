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
#define RXQEPOOLSIZE sizeof(rxQentry_t) * 1024
#define RXTHREADSTACKSIZE 32768
#define XH_MAGIC 0x7A1F0CE2
#define RQ_MAGIC 0x39CF41A2

typedef struct rxQEntry_s {
	unsigned magic;
	String rawStr;
	struct rxQEntry_s *prev;
	struct rxQEntry_s *next;
} rxQEntry_t, *rxQEntryPtr_t;

typedef struct rxHead_s {
	unsigned magic;
	pthread_t rxThread;
	pthread_mutex_t lock;
	int numRxEntries;
	int sockFD;
	int rxEventFD;
	int rxEventVal;
	void *rxStringPool;
	void *rQEPool;
	void *rxPoller;
	rxQEntryPtr_t head;
	rxQEntryPtr_t tail;
} rxHead_t, *rxHeadPtr_t;

/* 
 * Clean up
 */
 
static void rxCleanup(rxHeadPtr_t xh)
{
	XH_LOCK
	PollDestroy(xh->rxPoller);
	close(xh->rxEventFD);
	talloc_free(xh->rxStringPool);
	talloc_free(xh->rQEPool);
	XH_UNLOCK
}
/*
 * RX Event Action
 */

static void rxEventAction(int fd, int event, void *userObject)
{
	rxHeadPtr_t xh = userObject;
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
 * Place new queue entry on at the head of the list.
 * Make a copy of the string passed in.
 * 
 * Must be called locked
 */
 
static void rxQueueRawString(rxHeadPtr_t xh, String rawStr)
{
	rxQEntryPtr_t rq;
	
	/* Make a queue entry */
	MALLOC_FAIL(rq = talloc_zero(xh->rQEPool, rxQEntry_t))
	/* Make a copy of the raw string and store it in the queue entry */
	MALLOC_FAIL(rq->rawStr = talloc_strdup(xh->rxStringPool, rawStr))
	rq->magic = RQ_MAGIC;
	
	if(!xh->head){
		/* First entry */
		xh->head = xh->tail = rq;
	}
	else{
		/* Insert on end */
		rq->prev = xh->tail;
		xh->tail->next = rq;
		xh->tail = rq;
	}
	/* Increment the entry count */
	xh->numRxEntries++;
}
/*
 * Remove a queue entry from the queue, and return the string.
 * 
 * 
 * Must be called locked
 * Returned String must be talloc_freed while locked.
 */
 
static String rxDQRawString(rxHeadPtr_t xh)
{
	rxQEntryPtr_t rq;
	String res;
	
	/* Return if nothing in the queue */
	if(!xh->head){
		return NULL;
	}
	
	/* Get the entry to remove */
	rq = xh->tail;
	
	/* Is it real? */
	ASSERT_FAIL(RQ_MAGIC == rq->magic)
	
	
	/* Remove the last entry from the queue */
	if(rq->prev){
		/* Remove one entry off the end of the queue */
		rq->prev->next = NULL;
		xh->tail = rq->prev;
	}
	else{
		/* Queue is now empty */
		xh->head = xh->tail = NULL;
	}
	/* Note the string pointer */
	res = rq->rawStr;
	/* Clear the magic */
	rq->magic = 0;
	/* Free the queue entry */
	talloc_free(rq);
	/* Decerement the entry count */
	xh->numRxEntries--;
	/* Return the string */
	return res;	
}
	

/*
 * Rx Thread
 */
 
static void *rxThread(void *userObj)
{
	rxHeadPtr_t xh = userObj;
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
	
	/* Allocate the receive queue entry pool */
	
	/* Allocate the receive string pool */
	MALLOC_FAIL(xh->rxStringPool = talloc_pool(xh, RXBUFFPOOLSIZE));
	
	/* Copy the polling resource pointer */
	poller = xh->rxPoller;
	
	XH_UNLOCK
	
	

	debug(DEBUG_ACTION, "xpl RX thread started");
	

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
	rxHeadPtr_t xh;
	
	/* Allocate a Header */
	MALLOC_FAIL(xh = talloc_zero(ctx, rxHead_t))
	

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
	rxHeadPtr_t xh = xplrxheader;
	int evFD;
	long long incr = 1;
	XH_LOCK
	xh->rxEventVal = val; /* Get the value */
	evFD = xh->rxEventFD; /* Copy the fd */
	XH_UNLOCK
	
	/* Send the event */
	if(write(evFD, &incr, sizeof(incr)) < 0){
		debug (DEBUG_UNEXPECTED, "%s: Could not write event increment",__func__);
		return FAIL;
	}

	return PASS;
}

/*
 * Remove a string from the receive queue and return a copy of it
 */
 
String XplrxDQRawString(TALLOC_CTX *ctx, void *xplrxheader)
{
	String res,pStr;
	rxHeadPtr_t xh = xplrxheader;
	
	/* Lock the mutex */
	XH_LOCK
	
	/* Sanity checks */
	ASSERT_FAIL(xh);
	ASSERT_FAIL(XH_MAGIC == xh->magic);
	
	/* See if there's something in the queue */
	
	if(!(pStr = rxDQRawString(xh))){
		/*Nothing is in the queue */
		/*Unlock the mutex and return */
		XH_UNLOCK
		return NULL;
	}
	/* Copy the string into the supplied context */
	MALLOC_FAIL(res = talloc_strdup(ctx, pStr));
	
	/* Free the pool copy of the string */	
	talloc_free(pStr);
	
	/* Unlock the mutex */
	XH_UNLOCK
	/* Return the string */
	return res;
}

	
