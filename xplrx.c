/*
 * xplrx.c
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


#define RXBUFFSIZE 1501
#define RXBUFFPOOLSIZE 1024*128
#define RXQEPOOLSIZE sizeof(rxQEntry_t) * 1024
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
	unsigned numRxEntries;
	unsigned localConnPort;
	int localConnFD;
	int rxReadyFD;
	int rxControlFD;
	unsigned rxControlVal;
	unsigned rxBuffSize;
	void *rxStringPool;
	void *rxQEPool;
	String rxBuff;
	void *rxPoller;
	rxQEntryPtr_t head;
	rxQEntryPtr_t tail;
} rxHead_t, *rxHeadPtr_t;


/*
 * Send RX ready event to FD passed in at initialization
 * 
 * Must be called locked.
 * Arguments:
 *
 * 1. Pointer to receive header
 *
 * Return value
 *
 * Returns PASS if successful, otherwise FAIL
 */
 
static Bool rxSendReady(rxHeadPtr_t xh )
{
	long long incr = 1;
	
	/* If FD is not defined, return successful */
	
	if(xh->rxReadyFD < 0){
		return PASS;
	}
	
	/* Send the event */
	if(write(xh->rxReadyFD, &incr, sizeof(incr)) < 0){
		debug (DEBUG_UNEXPECTED, "%s: Could not write event increment",__func__);
		return FAIL;
	}

	return PASS;
}


/*
 * RX Control Action
 * Called from poller when a control message is ready
 *
 * Arguments:
 *
 * 1. FD with message
 * 2. event ID (not used)
 * 3. A pointer to the receive header object
 *
 * Return value
 *
 * None
 */

static void rxControlAction(int fd, int event, void *objPtr)
{
	rxHeadPtr_t xh = objPtr;
	char buf[8];
	int val;
	
	ASSERT_FAIL(xh);
	
	XH_LOCK
	ASSERT_FAIL(XH_MAGIC == xh->magic);
	val = xh->rxControlVal;
	XH_UNLOCK
	
	if(read(fd, buf, 8) < 0){
		debug(DEBUG_UNEXPECTED,"%s: read error", __func__);
	}
	else{	

		debug(DEBUG_EXPECTED, "%s: Ding! RX control value: %d", __func__, val);
		if(val == XHCM_TERM_REQUEST){
			debug(DEBUG_ACTION, "Received terminate request");
			XH_LOCK
			rxSendReady(xh); /* Send Dying gasp */
			XH_UNLOCK
			pthread_exit(0);
		}
	
			
	}
}

/*
 * Place new queue entry on at the end of the list.
 * Make a copy of the string passed in.
 *
 * Arguments:
 *
 * 1. Pointer to receive header 
 * 2. The raw message string to queue.
 *
 * Return value
 *
 * None
 * 
 * Must be called locked
 */
 
static void rxQueueRawString(rxHeadPtr_t xh, String rawStr)
{
	rxQEntryPtr_t rq;
	
	/* Make a queue entry */
	MALLOC_FAIL(rq = talloc_zero(xh->rxQEPool, rxQEntry_t))
	/* Make a copy of the raw string and store it in the queue entry */
	MALLOC_FAIL(rq->rawStr = talloc_strdup(xh->rxStringPool, rawStr))
	rq->magic = RQ_MAGIC;
	
	if(!xh->tail){
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
 * RX Incoming Action
 *
 * Called from poller
 *
 * Arguments:
 *
 * 1. FD with message to read 
 * 2. Event (not used)
 * 3. Pointer to the receive header
 *
 * Return value
 *
 * None
 */

static void rxIncomingAction(int fd, int event, void *objPtr)
{
	int bytesRead;
	rxHeadPtr_t xh = objPtr;
	char eStr[64];
	
	
	ASSERT_FAIL(xh);
	
	XH_LOCK
	ASSERT_FAIL(XH_MAGIC == xh->magic);
	XH_UNLOCK
	
	/* Get the packet */
	if ((bytesRead = recvfrom(xh->localConnFD, xh->rxBuff, xh->rxBuffSize - 1, 0, NULL, NULL)) < 0){
		debug(DEBUG_UNEXPECTED,"%s: recvfrom error: %s", __func__, strerror_r(errno, eStr, 64));
		return;
	}
	
	XH_LOCK
	
	/* Make it a string */
	xh->rxBuff[bytesRead] = 0;
	
	/* Place it in the queue */
	rxQueueRawString(xh, xh->rxBuff);

	/* Send notification of buffer add */
	rxSendReady(xh);

	XH_UNLOCK
	
	
}

/*
 * Remove a queue entry from the queue, and return the string.
 * 
 * 
 * Must be called locked
 * Returned String must be talloc_freed while locked.
 *
 * Arguments:
 *
 * 1. Pointer to receive header
 *
 * Return value:
 *
 * String with message text
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
	rq = xh->head;
	
	/* Is it real? */
	ASSERT_FAIL(RQ_MAGIC == rq->magic)
	
	
	/* Remove the first entry from the queue */
	if(rq->next){
		/* Remove one entry from the beginning of the queue */
		rq->next->prev = NULL;
		xh->head = rq->next;
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
 * RX thread tick handler
 *
 * Arguments:
 *
 * 1. Tick id (not used)
 * 2. Pointer to the receive header
 *
 * Return value
 *
 * None
 */
 
static void rxTick(int id, void *objPtr)
{
	rxHeadPtr_t xh = objPtr;

	ASSERT_FAIL(xh)
	ASSERT_FAIL(XH_MAGIC == xh->magic)
	
	debug(DEBUG_INCOMPLETE,"RX thread tick");

}

/*
 * Rx Thread main line code
 *
 * This function calls the poll object and waits for either a message, or a conrol command
 * from the main thread.
 *
 * Arguments:
 *
 * 1. Pointer to receive header
 *
 * Return value
 *
 * None
 */
 
static void *rxThread(void *objPtr)
{
	rxHeadPtr_t xh = objPtr;
	void *poller;

		

	debug(DEBUG_ACTION, "xpl RX thread started");
	
	XH_LOCK
	ASSERT_FAIL(XH_MAGIC == xh->magic)
	

	
	/* Copy the polling resource pointer */
	poller = xh->rxPoller;
	
	XH_UNLOCK
	

	PollWait(poller, 1000, NULL);
	debug(DEBUG_UNEXPECTED, "Poll returned serious error: %d", errno);
	fatal("%s: Poll error in RX thread", __func__);
	
	return NULL;
}


/*
 * Send an control message to the RX thread 
 *
 * Arguments:
 *
 * 1. Pointer to receive header
 * 2. Control message code to send
 *
 * Return value
 *
 * PASS if message sent successfully, otherwise FAIL
 */
 
Bool XplrxSendControlMsg(void *objPtr, int val)
{
	rxHeadPtr_t xh = objPtr;
	int evFD;
	long long incr = 1;
	ASSERT_FAIL(xh)
	ASSERT_FAIL(XH_MAGIC == xh->magic)
	XH_LOCK
	xh->rxControlVal = val; /* Get the value */
	evFD = xh->rxControlFD; /* Copy the fd */
	XH_UNLOCK
	
	/* Send the event */
	if(write(evFD, &incr, sizeof(incr)) < 0){
		debug (DEBUG_UNEXPECTED, "%s: Could not write event increment",__func__);
		return FAIL;
	}

	return PASS;
}


/*
 * Destroy the receiver. 
 * 
 * Called from the main thread.
 * Kills the receiver thread, 
 * destroys the receiver poll object, closes the control FD.
 * and frees all memory used.
 *
 * Arguments:
 *
 * 1. Pointer to receive header 
 *
 * Return value
 *
 * None
 */
 
void XplRXDestroy(void *objPtr)
{
	int termCount = 0;
	rxHeadPtr_t xh = objPtr;
	char eBuf[8];
	
	ASSERT_FAIL(xh)
	ASSERT_FAIL(XH_MAGIC == xh->magic)
	
	debug(DEBUG_ACTION, "%s: Sending request to terminate rx thread...", __func__);
	/* Send terminate request */
	if(FAIL == XplrxSendControlMsg(xh, XHCM_TERM_REQUEST)){
		fatal("%s: Terminate event transmission failed, exiting unclean",__func__);
	}
	/* Wait for RX thread to signal it terminated */
	XH_LOCK
	while(termCount < 10){ /* Wait for RX thread dying gasp */
		XH_UNLOCK
		if(8 == read(xh->rxReadyFD, eBuf, 8)){
			XH_LOCK
			break;
		}
		usleep(100000);
		termCount++;
		XH_LOCK
	}
	
	XH_UNLOCK
	
	if(termCount < 10){
		debug(DEBUG_ACTION,"%s: RX thread terminated", __func__);
	}
	else{
		fatal("%s: Problem terminating RX thread", __func__);
	}
	

	/* Unregister the local connection */
	PollUnRegEvent(xh->rxPoller, xh->localConnFD);
	/* Unregister the control FD */
	PollUnRegEvent(xh->rxPoller, xh->rxControlFD);
	
	/* Close the control FD */
	if(xh->rxControlFD > 0){
		close(xh->rxControlFD);
	}
	
	/* Destroy the poller */
	PollDestroy(xh->rxPoller);
	/* Invalidate then free the object */
	xh->magic = 0;
	talloc_free(xh);
}



/*
 * Receiver Initialization function
 *
 * Allocates memory pools and buffers.
 * Creates an eventfd for control messages.
 * Creates a receiver poll object.
 * Creates the receiver thread.
 *
 * Arguments:
 *
 * 1. FD for local hub connection
 * 2. Ephemeral port for hub connection
 * 3. FD to use to send RX ready events.
 *
 *
 * Return value
 *
 * Pointer to Receive header
 */

void *XplRXInit(int localConnFD, int localConnPort, int rxReadyFD)
{
	pthread_attr_t attrs;
	int res;
	rxHeadPtr_t xh;
	
	/* Allocate a Header */
	MALLOC_FAIL(xh = talloc_zero(NULL, rxHead_t))
	

	/* Initialize the guarding mutex */
	ASSERT_FAIL( 0 == pthread_mutex_init(&xh->lock, NULL))
	
			
	/* Allocate the receive string pool */
	MALLOC_FAIL(xh->rxStringPool = talloc_pool(xh, RXBUFFPOOLSIZE));
	
	/* Allocate the queue entry pool */
	MALLOC_FAIL(xh->rxQEPool = talloc_pool(xh, RXQEPOOLSIZE));

	/* Allocate the receive buffer */
	xh->rxBuffSize = RXBUFFSIZE;
	MALLOC_FAIL(xh->rxBuff = talloc_array(xh, char, xh->rxBuffSize))
	
	
	/* Note the local connection FD and the port */
	xh->localConnFD = localConnFD;
	xh->localConnPort = localConnPort;
	
	/* Note the ready file descriptor */
	xh->rxReadyFD = rxReadyFD;
	
	/* Set the magic number */
	xh->magic = XH_MAGIC;
	
	/* Get an event FD for control */
	if((xh->rxControlFD = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)) < 0){
		debug(DEBUG_UNEXPECTED, "Could not create an event FD");
		talloc_free(xh);
		return NULL;
	}
	
	/* Create a polling resource */
	if(!(xh->rxPoller = PollInit(xh, 4))){
		debug(DEBUG_UNEXPECTED, "%s: Could not create polling resource", __func__);
	}

	/* Add the control FD to the polling list */
	if(FAIL == PollRegEvent(xh->rxPoller, xh->rxControlFD, POLL_WT_IN, rxControlAction, xh)){
		debug(DEBUG_UNEXPECTED, "%s: Could not register RX Control eventfd", __func__);
	}
	
	/* Add the local connection FD to the polling list */
	if(FAIL == PollRegEvent(xh->rxPoller, xh->localConnFD, POLL_WT_IN, rxIncomingAction, xh)){
		debug(DEBUG_UNEXPECTED, "%s: Could not register local connection FD", __func__);
	}
	
	/* Add the rxTick to the polling list */
	if(FAIL == PollRegTimeout(xh->rxPoller, rxTick, xh)){
		fatal("%s: Could not register timeout", __func__);
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
 * Remove a string from the receive queue and return a copy of it.
 *
 * Used by the main thread to get a message from the queue.
 *
 * Arguments:
 *
 * 1. Talloc context to hang the message string off of. 
 * 2. Pointer to the receive header.
 *
 * Return value
 *
 * Message string or NULL if there is no message in the queue.
 */
 
String XplrxDQRawString(TALLOC_CTX *ctx, void *objPtr)
{
	String res,pStr;
	rxHeadPtr_t xh = objPtr;
	
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
	/* Move the string into the supplied context, freeing the original back to the string pool */
	MALLOC_FAIL(res = UtilMoveString(ctx, pStr, 0))
	
	/* Unlock the mutex */
	XH_UNLOCK
	/* Return the string */
	return res;
}

	
