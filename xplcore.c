/*
* 	 xplcore.c
*    
*    Copyright (C) 2013  Stephen A. Rodgers
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
*    
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
#include <net/if.h>
#include <ifaddrs.h>

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

#define XP_MAGIC 0xC51A6423

typedef struct xplObj_s {
	unsigned magic;
	int localConnFD;
	int broadcastFD;
	int rxReadyFD;
	int localConnPort;
	String broadcastIP;
	void *poller;
	void *rcvr;
} xplObj_t, *xplObjPtr_t;
	

/*
 * Process notification of buffer add
 */

static void rxReadyAction(int fd, int event, void *objPtr)
{
	xplObjPtr_t xp = objPtr;
	char buf[8];
	String theString;
	
	ASSERT_FAIL(xp);
	
	ASSERT_FAIL(XP_MAGIC == xp->magic)
	
	if(read(fd, buf, 8) < 0){
		debug(DEBUG_UNEXPECTED,"%s: read error", __func__);
	}
	else{	

		debug(DEBUG_EXPECTED, "%s: Ding! RX ready", __func__);
		/* Fetch any strings from the queue */
		while((theString = XplrxDQRawString(xp, xp->rcvr))){
			if(theString){
				debug(DEBUG_ACTION, "Processing received message");
				/* Process the string contents */
		
				talloc_free(theString);
			}
			else{
				break;
			}
		}
	}
}

/*
 * Add local interface socket 
 */

static int addLocalSock(int sock, void *addr, int addrlen, int family, int socktype, void *userObj)
{
	xplObjPtr_t xp = userObj;
	struct sockaddr_in sockInfo = {0};
	socklen_t sockInfoSize = sizeof(struct sockaddr_in);
	char eStr[64];
	String astr;
	int flag = 1;
	
	ASSERT_FAIL(xp);
	ASSERT_FAIL(XP_MAGIC == xp->magic)
	ASSERT_FAIL(xp->localConnFD == -1)
	ASSERT_FAIL(astr = SocketPrintableAddress(xp, addr))
	
	/* Mark as a broadcast socket */
	if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &flag, sizeof(flag)) < 0){
		fatal("%s: Unable to set SO_BROADCAST on socket %s (%d)", __func__, strerror_r(errno, eStr, 64), errno);
	}

	/* Attempt to bind */
	if ((bind(sock, addr, addrlen)) < 0){
		fatal("%s: Unable to bind listener socket to port %d, %s (%d)", __func__, 3865, strerror_r(errno, eStr, 64), errno);
	}

	/* Retreive the assigned ephemeral socket port # */

	if (getsockname(sock, (struct sockaddr *) &sockInfo, (socklen_t *) &sockInfoSize)){
		fatal("Unable to fetch socket info for bound listener, %s (%d)", strerror(errno), errno);
	}
	
	/* Extract assigned ephemeral port */
	xp->localConnPort = ntohs(sockInfo.sin_port);
	

	/* Note the socket and port number */
	xp->localConnFD = sock;

	debug(DEBUG_ACTION,"%s: Local interface Address: %s", __func__, astr);
	debug(DEBUG_ACTION,"%s: Ephemeral port: %d", __func__, xp->localConnPort);
	talloc_free(astr);
 
	return FALSE;
	
}

/*
 * Add the broadcast socket
 */

static int addBroadcastSock(int sock, void *addr, int addrlen, int family, int socktype, void *userObj)
{  
	int flag = 1;
	xplObjPtr_t xp = userObj;
	char eStr[64];
		
	ASSERT_FAIL(xp);
	ASSERT_FAIL(XP_MAGIC == xp->magic)
	ASSERT_FAIL(xp->broadcastFD == -1)
	
	/* Mark as a broadcasting socket */
	if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &flag, sizeof(flag)) < 0) {
		close(sock);
		fatal("%s: Unable to set SO_BROADCAST on socket %s (%d)", __func__, strerror_r(errno, eStr, 64), errno);
	}
	xp->broadcastFD = sock;
	debug(DEBUG_ACTION, "Broadcast socket set up on: %s", xp->broadcastIP);
	return FALSE;
}

/*
 * XPL tick function
 */

static void xplTick(int id, void *objPtr)
{
	xplObjPtr_t xp = objPtr;

	
	ASSERT_FAIL(xp)
	ASSERT_FAIL(XP_MAGIC == xp->magic)
	
	/* debug(DEBUG_ACTION, "XPL tick"); */

}

/*
 * Destroy an XPL object
 */
void XplDestroy(void *objPtr)
{
	xplObjPtr_t xp = objPtr;
	
	ASSERT_FAIL(xp)
	ASSERT_FAIL(XP_MAGIC == xp->magic)
	
	/* Destroy the receiver object */
	if(xp->rcvr){
		XplRXDestroy(xp->rcvr);
	}
	/* Close the local connection */
	if(xp->localConnFD != -1){
		close(xp->localConnFD);
	}
	/* Close the broadcast connection */
	if(xp->broadcastFD != -1){
		close(xp->broadcastFD);
	}
	
	/* Close the ready event FD */
	if(xp->rxReadyFD != -1){
		close(xp->rxReadyFD);
	}
	
	/* Invalidate and free the object */
	xp->magic = 0;
	talloc_free(xp);
		
}


/*
 * Initialize XPL object
 */
 

void *XplInit(TALLOC_CTX *ctx, void *Poller, String BroadcastIP)
{
	xplObjPtr_t xp;
	
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(Poller)
	ASSERT_FAIL(BroadcastIP);
	
	MALLOC_FAIL(xp = talloc_zero(ctx, xplObj_t))
	MALLOC_FAIL(xp->broadcastIP = talloc_strdup(xp, BroadcastIP))
	xp->localConnFD = xp->rxReadyFD = xp->broadcastFD = -1;
	xp->poller = Poller;
	xp->magic = XP_MAGIC;
	
	/* Get socket for local interface (Note: IPV4 only for now) */
	if((FAIL == SocketCreate("127.0.0.1", "0", AF_INET, SOCK_DGRAM, xp, addLocalSock)) || (xp->localConnFD < 0)){
		fatal("%s: Could not create socket for local interface", __func__);
	}
	
	/* Get socket for broadcast interface (Note: IPV4 only for now) */
	if((FAIL == SocketCreate(xp->broadcastIP, "3865", AF_INET, SOCK_DGRAM, xp, addBroadcastSock)) || (xp->broadcastFD < 0)){
		fatal("%s: Could not create socket for broadcast interface", __func__);
	}
	
	
	/* Create an event FD for ready */
	if((xp->rxReadyFD = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)) < 0){
		debug(DEBUG_UNEXPECTED, "%s: Could not create an event FD", __func__);
		XplDestroy(xp);
		return NULL;
	}

	/* Add the RX ready FD to the poller */
	if(FAIL == PollRegEvent(xp->poller, xp->rxReadyFD, POLL_WT_IN, rxReadyAction, xp)){
		debug(DEBUG_UNEXPECTED, "%s: Could not register RX Ready eventfd", __func__);
		XplDestroy(xp);
		return NULL;
	}
	
	/* Add the xplTick to the polling list */
	if(FAIL == PollRegTimeout(xp->poller, xplTick, xp)){
		fatal("%s: Could not register timeout", __func__);
	}
	
	/* Initialize receiver thread */
	if(NULL == (xp->rcvr = XplRXInit(xp, xp->localConnFD, xp->localConnPort, xp->rxReadyFD))){
		debug(DEBUG_UNEXPECTED, "%s: Could not initialize xpl recever thread", __func__);
		XplDestroy(xp);
		return NULL;
	}
	
	return xp;
}


