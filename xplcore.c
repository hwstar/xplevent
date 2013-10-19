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
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
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
#include "xplcore.h"

#define XP_MAGIC 0xC51A6423
#define XM_MAGIC 0x5719034F
#define XS_MAGIC 0xA68C9F24
#define XNV_MAGIC 0x7983E098

#define GENERAL_POOL_SIZE 128*1024
#define MSG_MAX_SIZE 1500
#define DEFAULT_HEARTBEAT_INTERVAL 300
#define CONFIG_HEARTBEAT_INTERVAL 60
#define HUB_DISCOVERY_INTERVAL 3
#define HUB_NO_ECHO_INTERVAL 60
#define DISCOVERY_MAX_TRIES 40

#define WRITE_TEXT(xm, x) if (!appendText(xm, x)) return FALSE;
#define VALID_CHAR(theChar) (((theChar >= 32) && (theChar < 123)) || (theChar = 124) || (theChar = 126))
#define STR_FREE(p) if(p){ talloc_free(p); p = NULL;}


/* name/value list entry */
typedef struct xplNameValueLE_s {
	unsigned magic;
	String itemName;
	String itemValue;
	struct xplNameValueLE_s *next;
	} xplNameValueLE_t, *xplNameValueLEPtr_t;


/* Describe a received message */
typedef struct {
	unsigned magic;
	XPLMessageType_t messageType;
	int hopCount;
	unsigned txBuffBytesWritten; /* Holds the number of bytes in txBuff */

	String sourceVendor;
	String sourceDeviceID;
	String sourceInstanceID;
	String targetVendor;
	String targetDeviceID;
	String targetInstanceID;
	String schemaClass;
	String schemaType;
	String txBuff; /* Holds the transmit string */
	
	XPLMessageClass_t messageClass;
	Bool isUs;
	Bool isBroadcastMessage;

	
	void *xplObj; /* Pointer back to master object */
	void *serviceObj; /* Set to point to service object on TX messages, is set to NULL on receive messages */
	TALLOC_CTX *nvCTX; /* Name/value talloc context. Makes it easy to delete all name value pairs in a message */
	xplNameValueLEPtr_t nvHead;
	xplNameValueLEPtr_t nvTail;
	
} xplMessage_t, *xplMessagePtr_t;


/* Describe a xPL service */
typedef struct xplService_s {
	unsigned magic;
	
	Bool serviceEnabled;	
	Bool configurableService; /* RFU */
	Bool serviceConfigured;   /* RFU */

	unsigned heartbeatInterval;
	unsigned heartbeatTimer;
	unsigned discoveryTries;
	XPLDiscoveryState_t discoveryState;
	time_t lastHeartbeatAt;
	XPLListenerReportMode_t reportMode;
	Bool reportGroupMessages;

	
	String serviceVendor; 
	String serviceDeviceID;
	String serviceInstanceID;
	String serviceVersion;

	
	xplMessagePtr_t heartbeatMessage;

	void *xplObj; /* Pointer back to master object */
	
	XPLListenerFunc_t listener; /* User installed listener function */
	void *userListenerObject; /* User-supplied object for listener callback */
	
	struct xplService_s *prev;
	struct xplService_s *next;
	
} xplService_t, *xplServicePtr_t;

typedef struct xplObj_s {
	unsigned magic;
	int localConnFD; /* FD for packets from HUB */
	int broadcastFD; /* FD for broadcasts to network */
	int rxReadyFD; /* Event FD for RX packet ready from receiver */
	int timerFD; /* Timer FD for timing heart beats */
	int broadcastAddrLen; /* Indicates length of data in broadcastAddr stuct below */
	int localConnPort; /* Ephemeral port for packets sent from local hub */
	int ticks; /* Tick counter */
	void *poller; /* Pointer to the poller object supplied by the user */
	void *rcvr; /* Pointer to the receiver object */
	void *generalPool; /* Pointer to general memory pool for strings and structs */
	String remoteIP; /* IP of ethernet interface to use */
	String broadcastIP; /* Broadcast address on the interface */
	String internalIP; /* Listen address for hub transmissions */
	String uniqPrefix; /* Unique 4 digit prefix based on IP address passed in */
	xplServicePtr_t servHead; /* Head for linked list of services */
	xplServicePtr_t servTail; /* Tail for linked list of services */
	struct sockaddr_storage broadcastAddr; /* Holds the broadcast address data for sending XPL packets */
} xplObj_t, *xplObjPtr_t;

typedef enum { HBEAT_NORMAL, HBEAT_CONFIG, HBEAT_NORMAL_END, HBEAT_CONFIG_END } Heartbeat_t;

/*
* Forward references
*/

static Bool sendHeartbeat(xplServicePtr_t theService);
static xplMessagePtr_t parseMessage(xplObjPtr_t xp, String theText);
static void releaseMessage(xplMessagePtr_t xm);

/*
 * In memory constants
 */
 static const char base36Table[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 
	'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j',
	'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't',
	'u', 'v', 'w', 'x', 'y', 'z' };


/*
 **************************************************************************
 *  Non-categorized Private functions
 **************************************************************************
 */

/*
 * Generate a unique 4 digit base36 prefix based on the buffer passed in
 * Arguments:
 *
 * 1. Prefix string (output). Must be pre-allocated.
 * 2. Buffer to hash (usually a sockaddr_storage struct)
 * 3. Length of buffer to hash
 *
 *
 * Return value:
 *
 * None
 */

static void genUniquePrefix(String prefStr, void *buffer, unsigned buflen)
{
	unsigned char *p = buffer;
	uint32_t i, hash;
	for(hash = i = 0; i < buflen; ++i){
		hash += p[i];
		hash += (hash << 10);
		hash ^= (hash >> 6);
	}
	hash += (hash << 3);
	hash ^= (hash >> 11);
	hash += (hash << 15);
	debug(DEBUG_INCOMPLETE,"%s: 32 bit hash of buffer: %08X", __func__, hash);
	for(i = 0; i < 4; i++){
		prefStr[i] = base36Table[hash % 36];
		hash /= 36;
	}
	prefStr[i] = 0;
	debug(DEBUG_INCOMPLETE, "%s: Unique Prefix based on base36 encoding of hash: %s",__func__, prefStr);
}



/* 
 * Convert a long value into an 8 digit base36 number 
 * and concatenate it onto the passed string.
 * Arguments:
 *
 * 1. the long int. to use
 * 2. A pointer to a pre-allocated buffer of 13 bytes.
 *
 * Return value:
 *
 * None
 *        
 */
 
static void longToBase36(unsigned long theValue, String theBuffer) {
	int charPtr, buffLen;


	/* Fill with zeros */
	strcat(theBuffer, "00000000");
	buffLen = strlen(theBuffer);

	/* Handle the simple case */
	if (theValue == 0){
		return;
	}

	for(charPtr = buffLen - 1; charPtr >= (buffLen - 8); charPtr--) {
		theBuffer[charPtr] = base36Table[theValue % 36];
		if (theValue < 36){
			 break;
		}
		theValue = theValue / 36;
  }
}


/*
 * Create a fairly unique 16 character identifier string
 * 
 * String returned was talloc'd of of xplObj, and must be
 * talloc_free'd when no longer required
 *
 *
 * Arguments:
 *
 * 1. Pointer to allocated xpl object
 *
 * Return value:
 *
 * String with ID
 */
 
const String generateFairlyUniqueID(xplObjPtr_t xp)
{
	char newIdent[32];
	struct timeval rightNow;
	unsigned long timeInMillis;
	String res;
	
	
	/* Start with the unique prefix generated at initialization */
	strcpy(newIdent, xp->uniqPrefix);
	
	/* Now tack on the time of day, radix-36 encoded (which allows   */
	/* packing in a lot more uniqueness for the 8 characters we have */
	gettimeofday(&rightNow, NULL);
	timeInMillis = (rightNow.tv_sec * 1000) + (rightNow.tv_usec / 1000);
	longToBase36(timeInMillis, newIdent);
	if (strlen(newIdent) > 16){
		newIdent[16] = '\0';
	}
	/* Pass a copy off */
	MALLOC_FAIL(res = talloc_strdup(xp, newIdent))	
	debug(DEBUG_ACTION, "Unique identifier generated: %s", res);
	return res;

}
 

/*
 * Create a new name value list entry
 * Arguments:
 *
 * 1. A talloc context for the new entry.
 *
 * Return value:
 *
 * A pointer to the newly allocated name/value entry.
 */

static xplNameValueLEPtr_t newNameValueListEntry(void *nvListContext)
{
	xplNameValueLEPtr_t nvp;
	MALLOC_FAIL(nvp = talloc_zero(nvListContext, xplNameValueLE_t))
	nvp->magic = XNV_MAGIC;
	return nvp;
}
/*
 * Retrieve a value from a name/value list
 * Arguments:
 *
 * 1. Pointer ro the name/value list head
 * 2. Pointer to a string with the name to match.
 *
 * Return value:
 *
 * A pointer to the name/value entry which matches the name or NULL if no match.
 */

static xplNameValueLEPtr_t getNamedValue(xplNameValueLEPtr_t nvListHead, const String name)
{
	xplNameValueLEPtr_t nvp;
	
	/* Traverse the list looking for a match */
	for(nvp = nvListHead; nvp; nvp = nvp->next){
		ASSERT_FAIL(XNV_MAGIC == nvp->magic)
		if(!strcmp(name, nvp->itemName)){
			return nvp;
		}
	}
	/* No match found */
	return NULL;
}



/*
 * Process notification of buffer add
 * This is where we parse receive messages.
 * The poller will call this function when an rx event occurs.
 *
 * Arguments:
 *
 * 1. The eventfd for the rx event.
 * 2. The event ID (not used)
 * 3. A pointer to the master xpl object.
 *
 * Return value:
 *
 * None
 */

static void rxReadyAction(int fd, int event, void *objPtr)
{
	xplObjPtr_t xp = objPtr;
	xplServicePtr_t cse = NULL;
	xplNameValueLEPtr_t xnv;
	Bool isApp,isHbeat;
	Bool reportIt;
	int matchCount;

	char buf[8];
	String theString;
	xplMessagePtr_t xm = NULL;
	
	ASSERT_FAIL(xp);
	
	ASSERT_FAIL(XP_MAGIC == xp->magic)
	
	if(read(fd, buf, 8) < 0){
		debug(DEBUG_UNEXPECTED,"%s: read error", __func__);
	}
	else{	
		debug(DEBUG_ACTION, "%s: Ding! RX ready", __func__);
		/* Fetch any strings from the queue */
		while((theString = XplrxDQRawString(xp, xp->rcvr))){
			/* Log or print the message contents if at max debug */
			if(notify_get_debug_level() >= 5){
				debug(DEBUG_EXPECTED, "Processing received message: length = %d", strlen(theString));
				debug(DEBUG_INCOMPLETE,"***Packet received:\n%s\n", theString);
			}
			/* Process the string contents */
			xm = parseMessage(xp, theString);
			if(xm){
				debug(DEBUG_ACTION, "Message parsed OK");

				
				/* Dispatch message to appropriate handler */
				
				/* Traverse the service list */
				for(cse = xp->servHead; cse; cse = cse->next){
					reportIt = FALSE;
					matchCount = 0;
					xm->isUs = FALSE;
					
					
			
					/* All necessary fields must be present */
					ASSERT_FAIL(xm->sourceVendor)
					ASSERT_FAIL(xm->sourceDeviceID)
					ASSERT_FAIL(xm->sourceInstanceID)
					ASSERT_FAIL(xm->schemaType)
					ASSERT_FAIL(xm->schemaClass)
					
					/* Classify the message */
					isApp = (0 == strcmp(xm->schemaType, "app"));
					isHbeat = (0 == strcmp(xm->schemaClass, "hbeat"));
				
					if( isApp && isHbeat){
						xm->messageClass = XPL_MSG_CLASS_HEARTBEAT;
					}
					else if((!strcmp(xm->schemaType, "xpl")) && (!strcmp(xm->schemaClass, "group"))){
						xm->messageClass = XPL_MSG_CLASS_GROUP;
					}
					else if(isApp && (!strcmp(xm->sourceDeviceID, "config"))){
						xm->messageClass = XPL_MSG_CLASS_CONFIG;
					}
					else if(isHbeat && (!strcmp(xm->schemaType, "request"))){
						/* It it is a command to send a heartbeat, do so */
						xnv = getNamedValue(xm->nvHead, "command");
						if(!strcmp(xnv->itemValue, "request")){
							cse->heartbeatTimer %= 7;
							if(cse->heartbeatTimer < 2){
								cse->heartbeatTimer += 2;
							}
						}
					}	
					else{
						xm->messageClass = XPL_MSG_CLASS_NORMAL;
					}
	
					
					
					/* Test for is us */
					
					if((!strcmp(xm->sourceDeviceID, cse->serviceDeviceID))){
						matchCount++;
					}
					if((!strcmp(xm->sourceVendor, cse->serviceVendor))){
						matchCount++;
					}
					if((!strcmp(xm->sourceInstanceID, cse->serviceInstanceID))){
						matchCount++;
					}
					if(matchCount >= 3){
						xm->isUs = TRUE;
						/* If no hub confirmed, see if we have heard a heartbeat echo */
						if(cse->discoveryState != XPL_HUB_CONFIRMED){
							if(XPL_MSG_CLASS_HEARTBEAT == xm->messageClass){
								debug(DEBUG_EXPECTED, "******* Hub confirmed! *******");
								cse->discoveryState = XPL_HUB_CONFIRMED;
								cse->heartbeatTimer = cse->heartbeatInterval;
							}
							
							
						}
						
					}
					/* Decide what to report */
					if(XPL_REPORT_EVERYTHING == cse->reportMode){
						/* Report it all */
						reportIt = TRUE;
					}
					else if (XPL_REPORT_OWN_MESSAGES == cse->reportMode){
						/* Report it if it was us who sent it */
						reportIt = xm->isUs;
					}
					else if (XPL_REPORT_CONFIG_MESSAGES_ONLY == cse->reportMode){
						if(XPL_MSG_CLASS_CONFIG == xm->messageClass){
							goto callit; /* Call user handler */
						}
						goto cleanup; /* Ignore everything else */
					}
					
					if(!reportIt){ /* If nothing is reportable so far */
						/* Try to match a broadcast, group, or targetted message */
						if(xm->isBroadcastMessage){
							if(!xm->isUs){
								reportIt = TRUE;
							}
						}
						else{
							if(XPL_MSG_CLASS_GROUP == xm->messageClass){
								/* Is group message. Report it if enabled */
								reportIt = cse->reportGroupMessages;
							}
							else{
								/* Test to see if it was targetted at this service */
								if(xm->targetDeviceID && xm->targetVendor && xm->targetInstanceID){
									matchCount = 0;
									if((!strcmp(xm->targetDeviceID, cse->serviceDeviceID))){
										matchCount++;
									}
									if((!strcmp(xm->targetVendor, cse->serviceVendor))){
										matchCount++;
									}
									if((!strcmp(xm->targetInstanceID, cse->serviceInstanceID))){
										matchCount++;
									}
									if(matchCount >= 3){
										reportIt = TRUE;
									}
								}
							}
						}
						/* If it needs to be reported, do it here */
						if(reportIt){
callit: /* From config heartbeat test above */
							if(cse->listener){
								/* Call user listener function with the message and the user object */
								(*cse->listener)( xm, cse, cse->userListenerObject, xm->messageClass, 
								xm->isUs, xm->isBroadcastMessage);
							}
						}
					}
				}
cleanup: /* From config heartbeat test above */					
				/* Release the message */
				releaseMessage(xm);
			}
			else{
				debug(DEBUG_UNEXPECTED, "Message parse error");
			}
			/* Release the message string */
			talloc_free(theString);

		}
	}
}

/*
 * Add local interface socket.
 *
 * Called from SocketCreate
 *
 * Arguments:
 *
 * 1. Socket to add
 * 2. Socket address
 * 3. Socket address length
 * 4. Address family
 * 5. Socket type
 * 6. User object
 *
 * Return value
 *
 * FALSE 
 *
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
 *
 * Called from SocketCreate
 *
 * Arguments:
 *
 * 1. Socket to add
 * 2. Socket address
 * 3. Socket address length
 * 4. Address family
 * 5. Socket type
 * 6. User object
 *
 * Return value
 *
 * FALSE 
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
	
	/* Save the broadcast address and length */
	xp->broadcastAddrLen = addrlen;
	memcpy(&xp->broadcastAddr, addr, addrlen);

	debug(DEBUG_ACTION, "Broadcast socket set up on: %s", xp->broadcastIP);
	return FALSE;
}

/*
 * XPL tick function
 * Called from the Poller
 *
 * Arguments:
 *
 * 1. FD to check
 * 2. ID from poller (not used)
 * 3. Pointer to master XPL object
 *
 * Return value
 *
 * None
 */

static void xplTick(int fd, int event, void *objPtr)
{
	xplObjPtr_t xp = objPtr;
	xplServicePtr_t xs;
	char tickBuff[8];
	char eBuff[64];
	
	ASSERT_FAIL(xp)
	ASSERT_FAIL(XP_MAGIC == xp->magic)
	
	xp->ticks++;
	
	/* Test for RX thread running */
	if(9 == xp->ticks % 10){
		if(!XplrxGetAndResetWdogCounter(xp->rcvr)){
			fatal("XPL RX thread is not responding");
		}
	}
		
	if(8 != read(fd, tickBuff, 8)){
		debug(DEBUG_UNEXPECTED, "%s: Could not read timerfd: %s", __func__, strerror_r(errno, eBuff, sizeof(eBuff)));
		return;
	}
	
	debug(DEBUG_INCOMPLETE, "XPL tick");
	/* Traverse the service list */
	for(xs = xp->servHead; xs; xs = xs->next){
		if(FALSE == xs->configurableService){
			/* Is normal service */
			/* Is the heartbeat timer expired ? */
			if(!xs->heartbeatTimer){
				debug(DEBUG_ACTION, "Sending refresh heartbeat");
				if(FALSE == sendHeartbeat(xs)){
					debug(DEBUG_UNEXPECTED, "Refresh heartbeat send failed");
				}		
			}
			else{
				/* Decrement timer and continue */
				xs->heartbeatTimer--;
			}
		}
		else{
			/* is Config Service */
			/* TODO Add config service support */
		}
	}
}

/*
 **************************************************************************
 *  Private Message Functions
 **************************************************************************
 */

/*
 * Create a new name value list entry 
 *
 * Arguments:
 *
 * 1. Pointer to the master XPL object
 * 2. Message type to create
 *
 *
 * Return value
 *
 * A pointer to the talloc'd message object. This must be freed with a call to releaseMessage
 */

static xplMessagePtr_t createReceivedMessage(xplObjPtr_t xp, XPLMessageType_t msgType)
{
	xplMessagePtr_t xm;
	MALLOC_FAIL(xm = talloc_zero(xp->generalPool, xplMessage_t))
	/* Reference the object */
	xm->xplObj = xp;
	/* A received message will have xplServ set to NULL */
	xm->messageType = msgType;
	xm->magic = XM_MAGIC;
	return xm;
}

/*
 * Release (free) a message
 *
 *
 * Arguments:
 *
 * 1. Pointer to message object to release
 *
 * Return value
 *
 * None
 */

static void releaseMessage(xplMessagePtr_t xm)
{
	/* Invalidate message */
	xm->magic = 0;
	/* Free message and any children */
	talloc_free(xm);
}


/* 
 * Send a message string in the tx Buffer in the service object. 
 * or FALSE if there was an error 
 *
 * Arguments:
 *
 * 1. Pointer to message object with the message string.
 *
 * Return value
 *
 * Returns TRUE if the message was broadcasted successfully, otherwise false.
 *
 */  
              
static Bool sendRawMessage(xplMessagePtr_t xm)
{
	
	int bytesSent;
	char eStr[64];
	xplObjPtr_t xp = xm->xplObj;
	
	unsigned buffLen = xm->txBuffBytesWritten;

	/* Try to send the message */
	if ((bytesSent = sendto(xp->broadcastFD, xm->txBuff, buffLen, 0, 
		(struct sockaddr *) &xp->broadcastAddr, sizeof(struct sockaddr_storage))) != buffLen) {
		debug(DEBUG_UNEXPECTED, "Unable to broadcast message, %s (%d)", strerror_r(errno, eStr, 64), errno);
		return FALSE;
	}
	debug(DEBUG_INCOMPLETE, "Broadcasted %d bytes (of %d attempted)", bytesSent, buffLen);
	return TRUE;
}

/* 
 * Append text to the tx Buffer and keep track of what we've used 
 *
 * 1. Pointer to message object with the message string.
 * 2. String to append.
 *
 * Return value
 *
 * Returns TRUE if the sting was appended, or FALSE if the message exceeds the maximum buffer size.
 */
 
static Bool appendText(xplMessagePtr_t xm, String theString) 
{
	int stringLen = strlen(theString);

	/* Make sure it fits in the TX buffer */
	if ((xm->txBuffBytesWritten + stringLen) >= MSG_MAX_SIZE) {
		debug(DEBUG_UNEXPECTED, "Message exceeds MSG_MAX_SIZE (%d) -- not sent!", MSG_MAX_SIZE);
		debug(DEBUG_UNEXPECTED, "** Partial message is [%s]", xm->txBuff);
		return FALSE;
	}

	/* Copy the text in */
	memcpy(&xm->txBuff[xm->txBuffBytesWritten], theString, stringLen);
	xm->txBuffBytesWritten += stringLen;
	/* Terminate the string */
	xm->txBuff[xm->txBuffBytesWritten] = '\0';
	return TRUE;
}


/* 
 * Format the message, and place it in the tx Buffer in the service object.
 *
 * 1. Pointer to the message object to format.
 *
 * Return value
 *
 * Returns TRUE if the message was formatted successfully, otherwise FALSE.
 */
 
static Bool formatMessage(xplMessagePtr_t xm)
{
	xplNameValueLEPtr_t le;

	/* Clear the write count */
	xm->txBuffBytesWritten = 0;

	/* Write header */
	switch (xm->messageType) {
		case XPL_MESSAGE_COMMAND:
			WRITE_TEXT(xm, "xpl-cmnd");
			break;
		case XPL_MESSAGE_STATUS:
			WRITE_TEXT(xm, "xpl-stat");
			break;
		case XPL_MESSAGE_TRIGGER:
			WRITE_TEXT(xm, "xpl-trig");
			break;
		default:
			ASSERT_FAIL(0);
	}

	/* Write hop and source info */
	WRITE_TEXT(xm, "\n{\nhop=1\nsource=");
	WRITE_TEXT(xm, xm->sourceVendor);
	WRITE_TEXT(xm, "-");
	WRITE_TEXT(xm, xm->sourceDeviceID);
	WRITE_TEXT(xm, ".");
	WRITE_TEXT(xm, xm->sourceInstanceID);
	WRITE_TEXT(xm, "\n");

	/* Write target */
	if (xm->isBroadcastMessage){
		WRITE_TEXT(xm, "target=*");
	} else{
		WRITE_TEXT(xm, "target=");
		WRITE_TEXT(xm, xm->targetVendor);
		WRITE_TEXT(xm,"-");
		WRITE_TEXT(xm, xm->targetDeviceID);
		WRITE_TEXT(xm, ".");
		WRITE_TEXT(xm, xm->targetInstanceID);
	}
	WRITE_TEXT(xm, "\n}\n");

	/* Write the schema out */
	WRITE_TEXT(xm, xm->schemaClass);
	WRITE_TEXT(xm, ".");
	WRITE_TEXT(xm, xm->schemaType);
	WRITE_TEXT(xm, "\n{\n");

	/* Write Name/Value Pairs out */
	for (le = xm->nvHead; le; le = le->next) {
		WRITE_TEXT(xm, le->itemName);
		WRITE_TEXT(xm, "=");

		/* Write data content out */
		if (le->itemValue != NULL) {
			WRITE_TEXT(xm, le->itemValue);
		}

		/* Terminate line/entry */
		WRITE_TEXT(xm, "\n");
	}

	/* Write message terminator */
	WRITE_TEXT(xm, "}\n");

	/* Terminate and return text */
	xm->txBuff[xm->txBuffBytesWritten] = '\0';
	return TRUE;
}

/* 
 * Send an XPL message.
 *
 * 1. Pointer to service object
 * 2. Pointer to the message object to send.
 *
 * Return value
 *
 * Returns TRUE if the message was formatted and broadcasted successfully, otherwise FALSE.
 *  
 *
 */ 
                                                    
static Bool sendMessage(xplMessagePtr_t xm)
{
	/* Write the message to text */
	if (FALSE == formatMessage(xm)){
		return FALSE;
	}
	
	/* Attempt to broadcast it */
	debug(DEBUG_INCOMPLETE, "*** About to broadcast %d bytes as: \n%s\n", xm->txBuffBytesWritten, xm->txBuff);
	if (!sendRawMessage(xm)){
		return FALSE;
	}
	return TRUE;
}


/* 
 * Create a new transmit message based on a service
 *
 * 1. Pointer to service object with the message string.
 * 2. The message type to create.
 *
 * Return value
 *
 * Returns a message object.
 */
 
static xplMessagePtr_t createSendableMessage(xplServicePtr_t xs, XPLMessageType_t messageType)
{
  xplMessagePtr_t xm;
  
  /* Allocate the message (owned by the service context) */
  MALLOC_FAIL(xm = talloc_zero(xs, xplMessage_t))
  
  /* Allocate a raw message buffer */
  MALLOC_FAIL(xm->txBuff = talloc_zero_array(xm, char, MSG_MAX_SIZE))
	
  
  /* Install references back to service and master objects */
  xm->xplObj = xs->xplObj;
  xm->serviceObj = xs; /* A sendable message will include a reference to a service */
   

  /* Set the version */
  xm->messageType = messageType;
  xm->hopCount = 1;

  xm->sourceVendor = xs->serviceVendor;
  xm->sourceDeviceID = xs->serviceDeviceID;
  xm->sourceInstanceID = xs->serviceInstanceID;
  
  /* Validate the message */
  xm->magic = XM_MAGIC;
  
  return xm;
}

/*
 * Postpend an entry to a Message name-value list
 *
 * 1. Pointer to the address of the name-value list head pointer.
 * 2. Pointer to the address of the name-value list tail pointer.
 * 3. Pointer to the new name-vlaue list entry to add.
 *
 * Return value
 *
 * None
 */
 
static void postpendToNameValueList(xplNameValueLEPtr_t *listHeadPtrPtr, xplNameValueLEPtr_t *listTailPtrPtr, 
	xplNameValueLEPtr_t newEntry)
{
	if(!*listHeadPtrPtr){
		/* Empty list */
		*listHeadPtrPtr = *listTailPtrPtr = newEntry;
	}
	else{
		/* Items already in the list */
		(*listTailPtrPtr)->next = newEntry;
		*listTailPtrPtr = newEntry;
	}	
	
}


/*
 * Add a name value pair to an existing message
 *
 * 1. Pointer to the message object to add the name-value pair to.
 * 2. The name string to add
 * 3. The value string to add.
 *
 * Return value
 *
 * None.
 */
 
static void addMessageNamedValue(xplMessagePtr_t xm, String name, String value )
{
	xplNameValueLEPtr_t newNVLE;
	
	if(!xm->nvCTX){ 
		/* Create a new context from the NV pool for the list to make it simple to delete */
		MALLOC_FAIL(xm->nvCTX = talloc_new(xm));
	}
	/* Create a new list entry */
	MALLOC_FAIL(newNVLE = talloc_zero(xm->nvCTX, xplNameValueLE_t))
	
	/* Add the name */
	MALLOC_FAIL(newNVLE->itemName = talloc_strdup(newNVLE, name))
	
	/* Add the value */
	MALLOC_FAIL(newNVLE->itemValue = talloc_strdup(newNVLE, value))
	
	postpendToNameValueList(&xm->nvHead, &xm->nvTail, newNVLE);
}


/* 
 * Create a message suitable for sending to a specific receiver
 *
 * 1. Pointer to service object to use
 * 2. Message type to create
 * 3. Target vendor
 * 4. Target device ID
 * 5. Target instance ID
 *
 * Return value
 *
 * Returns a pointer to a message object
 */
 
static xplMessagePtr_t createTargetedMessage(xplServicePtr_t xs, XPLMessageType_t messageType, 
	String theVendor, String theDevice, String theInstance) 
{

	xplMessagePtr_t xm = createSendableMessage(xs, messageType);
	MALLOC_FAIL(xm->targetVendor = talloc_strdup(xs, theVendor))
	MALLOC_FAIL(xm->targetDeviceID = talloc_strdup(xm, theDevice))
	MALLOC_FAIL(xm->targetInstanceID = talloc_strdup(xm, theInstance))
	return xm;
}


/* 
 * Create a message suitable for broadcasting to all listeners
 *
 *
 * 1. Pointer to service object to use
 * 2. Message type to create
 *
 * Return value
 *
 * Returns a pointer to a message object
*/

static xplMessagePtr_t createBroadcastMessage(xplServicePtr_t xs, XPLMessageType_t messageType) 
{
	xplMessagePtr_t xm = createSendableMessage(xs, messageType);
	xm->isBroadcastMessage = TRUE;
	return xm;
}

/*
 * Create a heartbeat message
 *
 * 1. Pointer to service object to use
 * 2. Heartbeat Message type to create
 *
 * Return value
 *
 * Returns a pointer to a message object
 */
 
static xplMessagePtr_t createHeartbeatMessage(xplServicePtr_t xs, Heartbeat_t heartbeatType) 
{
	xplMessagePtr_t theHeartbeat;
	xplObjPtr_t xp = xs->xplObj;
	String portStr;
	String interval = "";

	/* Create the Heartbeat message */
	theHeartbeat = createBroadcastMessage(xs, XPL_MESSAGE_STATUS);
    
	/* Configure the heartbeat */
	switch (heartbeatType) {
		case HBEAT_NORMAL:
			theHeartbeat->schemaClass = "hbeat";
			theHeartbeat->schemaType = "app";
			MALLOC_FAIL(interval = talloc_asprintf(theHeartbeat, "%d", xs->heartbeatInterval / 60))
			break;

		case HBEAT_NORMAL_END:
			theHeartbeat->schemaClass = "hbeat";
			theHeartbeat->schemaType = "end";
			MALLOC_FAIL(interval = talloc_asprintf(theHeartbeat, "%d", xs->heartbeatInterval / 60))
			break;

		case HBEAT_CONFIG:
			theHeartbeat->schemaClass = "config";
			theHeartbeat->schemaType = "app";
			MALLOC_FAIL(interval = talloc_asprintf(theHeartbeat, "%d", CONFIG_HEARTBEAT_INTERVAL / 60))
			break;

		case HBEAT_CONFIG_END:
			theHeartbeat->schemaClass = "config";
			theHeartbeat->schemaType = "app";
			MALLOC_FAIL(interval = talloc_asprintf(theHeartbeat, "%d", CONFIG_HEARTBEAT_INTERVAL / 60))
			break;

		default: 
			ASSERT_FAIL(0)
	}
	addMessageNamedValue(theHeartbeat, "interval", interval);
	/* Free interval string as it was copied when it was inserted into the name/value list */
	talloc_free(interval);
	
	/* Add standard heartbeat data */
	MALLOC_FAIL(portStr = talloc_asprintf(theHeartbeat, "%d", xp->localConnPort))
	addMessageNamedValue(theHeartbeat, "port", portStr);
	addMessageNamedValue(theHeartbeat, "remote-ip", xp->remoteIP);
	if (xs->serviceVersion) {
		addMessageNamedValue(theHeartbeat, "version", xs->serviceVersion);
	}    
  return theHeartbeat;
}

 
/*
 * Send a standard XPL Heartbeat immediately 
 *
 * 1. Pointer to service object to use
 *
 * Return value
 *
 * Returns TRUE if the heartbeat was sent successfully, otherwise FALSE
 */
 
static Bool sendHeartbeat(xplServicePtr_t xs)
{
	xplMessagePtr_t theHeartbeat;
	unsigned hbi;

	/* Create the Heartbeat message, if needed */
	if (!xs->heartbeatMessage){
		/* Configure the heartbeat */
		if (xs->configurableService && !xs->serviceConfigured){
			theHeartbeat = createHeartbeatMessage(xs, HBEAT_CONFIG);
		} 
		else {
			theHeartbeat = createHeartbeatMessage(xs, HBEAT_NORMAL);
		}

		/* Install a new heartbeat message */
		xs->heartbeatMessage = theHeartbeat;
		debug(DEBUG_ACTION, "%s: Just allocated a new Heartbeat message for the service", __func__);
	} 
	else {
		theHeartbeat = xs->heartbeatMessage;
	}
    
	/* Send the message */
	if (!sendMessage(theHeartbeat)){
		return FALSE;
	}

	/* Update last heartbeat time */
	xs->lastHeartbeatAt = time(NULL);
	
	/* Reset the heartbeat timer */
	switch(xs->discoveryState){
		case XPL_HUB_UNCONFIRMED: /* Poll at short interval */
			xs->discoveryTries++;
			if(xs->discoveryTries >= DISCOVERY_MAX_TRIES){
				/* Retries exahusted to find a hub */
				debug(DEBUG_UNEXPECTED, "No hub found, dropping polling rate back to %d seconds", HUB_NO_ECHO_INTERVAL);
				xs->discoveryState = XPL_HUB_NO_ECHO;
				hbi = HUB_NO_ECHO_INTERVAL;
			}
			else{
				debug(DEBUG_ACTION, "Attempting to discover hub. Try = %d", xs->discoveryTries);
				hbi = HUB_DISCOVERY_INTERVAL;
			}
			break;
			
		case XPL_HUB_NO_ECHO: /* Poll at backoff interval */
			debug(DEBUG_UNEXPECTED, "******* Still no hub found... ********");
			hbi = HUB_NO_ECHO_INTERVAL;
			break;
			
		case XPL_HUB_CONFIRMED: /* Poll at normal interval */
			hbi = xs->heartbeatInterval;
			break;
			
		default: 
			ASSERT_FAIL(0) 
	}		
			
	xs->heartbeatTimer = hbi;
	 
	debug(DEBUG_ACTION, "Sent Heatbeat message. Interval = %u ", hbi);
	return TRUE;
}


/* 
 * Send an Goodbye XPL Heartbeat immediately
 *
 * 1. Pointer to service object to use
 *
 * Return value
 *
 * Returns TRUE if the goodbye heartbeat was sent successfully, otherwise FALSE
 *
 */
 
static Bool sendGoodbyeHeartbeat(xplServicePtr_t xs)
{

	xplMessagePtr_t theHeartbeat;
	
  
	/* Create a shutdown message */
	if (xs->configurableService && !xs->serviceConfigured){
		theHeartbeat = createHeartbeatMessage(xs, HBEAT_CONFIG_END);
	}
	else{
		theHeartbeat = createHeartbeatMessage(xs, HBEAT_NORMAL_END);
	}
    
	/* Send the message */
	if (!sendMessage(theHeartbeat)){
		return FALSE;
	}

	/* Release message */
	talloc_free(theHeartbeat);

	debug(DEBUG_ACTION, "Sent Goodbye Heatbeat");
	return TRUE;
}

/* 
 * Parse data until end of block as a block.  If the block is valid, then the number of bytes 
 * parsed is returned.  If there is an error, a negated number of bytes read thus far is      
 * returned (ABS of this number points to the failing character)                              
 * If we run out of bytes before we start a new block, it's likely end of stream garbage and  
 * we return 0 (which means parsing this message is done)
 *
 * Arguments:
 *
 * 1. name/value list tallock context to use to talloc the name-value list
 * 2. Pointer to the address of a name value list head pointer.
 * 3. Pointer to the address of a name value list tail pointer.
 * 4. String containing the received message text.
 * 5. String to store the block header in.
 * 6. Size of the block header string.
 *
 *
 * Return value
 *
 * See above
 */

static int parseBlock(void *nvListContext, xplNameValueLEPtr_t *nvListHead, xplNameValueLEPtr_t *nvListTail,
String theText, String blockHeader, int blockHeaderLength)
{
	int curState = 0, curIndex, theLength = strlen(theText), charCount = 0;
	char theChar;
	char nb[32];
	char vb[32];
	Bool blockStarted = FALSE;
	xplNameValueLEPtr_t theNameValue;

	


	/* Parse character by character */
	for (curIndex = 0; curIndex < theLength; curIndex++){
		theChar = theText[curIndex];
    
		switch(curState) {
			case 0: /* Parse block header part of message */
				/* Handle an LF transition */
				if ((theChar == '\n') && blockStarted){
					blockHeader[charCount] = '\0';
					charCount = 0;
					curState = 1;
					continue;
				}

				/* Handle leading junk chars */
				if (!blockStarted && (theChar <= 32)){
					continue;
				}

				/* Handle known good characters */
				if (VALID_CHAR(theChar)) {
					/* Handle normal letters */
					blockStarted = TRUE;
					/* Bounds check buffer */
					if(charCount < blockHeaderLength - 1){
						blockHeader[charCount++] = theChar;
						continue;
					}
					else{
						debug(DEBUG_UNEXPECTED,"Block header buffer overflow");
						return -curIndex;
					}
				}

				/* Handle error */
				debug(DEBUG_UNEXPECTED, "Got invalid character parsing block header - %c at position %d", theChar, curIndex);
				return -curIndex;
    
			case 1:
				/* Advance */
				if (theChar == '{') {
					curState = 2;
					continue;
				}

				/* Bad */
				debug(DEBUG_UNEXPECTED, "Got invalid character parsing start of block - %c at position %d (wanted a {)", theChar, curIndex);
				return -curIndex;
      

			case 2:
				/* Advance */
				if (theChar == '\n') {
					curState = 3;
					charCount = 0;
					continue;
				}

				/* Bad */
				debug(DEBUG_UNEXPECTED, "Got invalid character parsing start of block -  %c at position %d (wanted a LF)", theChar, curIndex);
				return -curIndex;

			case 3:
				/* Handle end of name */
				if (theChar == '='){
					if(charCount < 32){
						nb[charCount++] = '\0';
					}
					else{
						debug(DEBUG_UNEXPECTED,"Name buffer overflow");
						return -curIndex;
					}
					charCount = 0;
					curState = 4;
					continue;
				}


				/* Handle end of block */
				if (theChar == '}'){
					curState = 5;
					continue;
				}

				/* Handle normal chars */
				if (VALID_CHAR(theChar)){
					/* Buffer Name */
					if(charCount < 31){
						nb[charCount++] = theChar;
						continue;
					}
					else{
						debug(DEBUG_UNEXPECTED,"Name buffer overflow");
						return -curIndex;
					}
					continue;
				}

				/* Bad characters! */
				debug(DEBUG_UNEXPECTED, "Got invalid character parsing block name/value name -  %c at position %d", theChar, curIndex);
				return -curIndex;

			case 4:
				/* Handle end of line */
				if (theChar == '\n'){
					
					/* Terminate the value string */
					vb[charCount]  = '\0';

					/* Create a name/value list entry and append it to the end of the list */
					
					theNameValue = newNameValueListEntry(nvListContext);
		
					MALLOC_FAIL(theNameValue->itemValue = talloc_strdup(theNameValue, vb))
					MALLOC_FAIL(theNameValue->itemName = talloc_strdup(theNameValue, nb))
					

					/* Postpend name value pair to name value list */
					postpendToNameValueList(nvListHead, nvListTail, theNameValue);

					/* Reset things */
					curState = 3;
					charCount = 0;
					continue;
				}

				/* Handle normal characters */
				if (VALID_CHAR(theChar) || (theChar == 32)){
					/* Buffer char */
					if(charCount < 31){
						vb[charCount++] = theChar;
					}
					else{
						debug(DEBUG_UNEXPECTED,"Value buffer overflow");
						return -curIndex;
					}
					continue;
				}

				/* Bad character! */
				debug(DEBUG_EXPECTED, "Got invalid character parsing name/value value -  %c at position %d", theChar, curIndex);
				return -curIndex;

			case 5:
				/* Should be an EOL - we are done if so */
				if (theChar == '\n'){
					/* We are done */
					return curIndex + 1;
				}

				/* Bad data */
				debug(DEBUG_UNEXPECTED, "Got invalid character parsing end of name/value -  %c at position %d (wanted a LF)",
				theChar, curIndex);
				return -curIndex;
			
			default:
				ASSERT_FAIL(0);
		} /* End switch */
		break;
	} /* End for */
	
	/* If we didn't start a block, then it's just end of the stream */
	if (!blockStarted){
		return 0;
	}
	
	/* If we got here, we ran out of characters - this is an error too */
	debug(DEBUG_UNEXPECTED, "Ran out of characters parsing block");
	return -theLength;
}

/* 
 * Parse the header name/value pairs for this message.
 *
 * Arguments:
 *
 * 1. Pointer to service object with the message string.
 * 2. Head of the name value list to check.
 *
 * Return value
 *
 * If they are all found and valid, then  
 * we return TRUE.  Otherwise, FALSE.
 */

static Bool parseMessageHeader(xplMessagePtr_t xm, xplNameValueLEPtr_t nameValueList)
{
	int hopCount;
	xplObjPtr_t xp = xm->xplObj;
	xplNameValueLEPtr_t theNameValue;
	String theVendor, theDeviceID, theInstanceID;

	/* Parse the hop count */
	if(!(theNameValue = getNamedValue(nameValueList, "hop"))){
		debug(DEBUG_UNEXPECTED, "Message missing HOP count");
		return FALSE;
	}
	if(FAIL == UtilStoi(theNameValue->itemValue, &hopCount) || (hopCount < 1)){
		debug(DEBUG_UNEXPECTED, "Message HOP Count invalid");
		return FALSE;
	}
	xm->hopCount = hopCount;

	/* Parse the source */
	if(!(theNameValue = getNamedValue(nameValueList, "source"))){
		debug(DEBUG_UNEXPECTED, "Message missing SOURCE");
		return FALSE;
	}
	/* Make a copy of the source tag */
	MALLOC_FAIL(theVendor = talloc_strdup(xp->generalPool, theNameValue->itemValue))
	
	if(!(theDeviceID = strchr(theVendor, '-'))){
		debug(DEBUG_UNEXPECTED, "SOURCE Missing Device ID - %s", theVendor);
		talloc_free(theVendor);
		return FALSE;
	}
	
	*theDeviceID++ = '\0';
	
	if(!(theInstanceID = strchr(theDeviceID, '.'))){
		debug(DEBUG_UNEXPECTED, "SOURCE Missing Instance ID - %s.%s", theVendor, theDeviceID);
		talloc_free(theVendor);
		return FALSE;
	}

	*theInstanceID++ = '\0';

	/* Install source into message */
	MALLOC_FAIL(xm->sourceVendor = talloc_strdup(xm, theVendor))
	MALLOC_FAIL(xm->sourceDeviceID = talloc_strdup(xm, theDeviceID))
	MALLOC_FAIL(xm->sourceInstanceID = talloc_strdup(xm, theInstanceID))

	/* Release mangled copy of source tag */
	talloc_free(theVendor);

	/* Parse the target (if anything) */
	if ((theNameValue = getNamedValue(nameValueList, "target")) == NULL) {
		debug(DEBUG_UNEXPECTED, "Message missing TARGET");
		return FALSE;
	}

	/* Parse the target */
  
	/* Check for a wildcard */
	if(!strcmp(theNameValue->itemValue, "*")){
		xm->isBroadcastMessage = TRUE;
	} 
	else{
		/* Not wildcard. Parse target tag */
		MALLOC_FAIL(theVendor = talloc_strdup(xp->generalPool, theNameValue->itemValue))
		if(!(theDeviceID = strchr(theVendor, '-'))){
			debug(DEBUG_UNEXPECTED, "TARGET Missing Device ID - %s", theVendor);
			talloc_free(theVendor);
			return FALSE;
		}

		*theDeviceID++ = '\0';
		if(!(theInstanceID = strchr(theDeviceID, '.'))){
			debug(DEBUG_UNEXPECTED, "TARGET Missing Instance ID - %s.%s", theVendor, theDeviceID);
			talloc_free(theVendor);
			return FALSE;
		}

		*theInstanceID++ = '\0';
		
		/* Install target into message */
		MALLOC_FAIL(xm->targetVendor = talloc_strdup(xm, theVendor))
		MALLOC_FAIL(xm->targetDeviceID = talloc_strdup(xm, theDeviceID))
		MALLOC_FAIL(xm->targetInstanceID = talloc_strdup(xm, theInstanceID))


		/* Release mangled string */
		talloc_free(theVendor);
 
	}

	/* Header parsed OK */
	return TRUE;
}

/* 
 * Convert a text message into a xPL message.
 *
 * Arguments:
 *
 * 1. Pointer to master XPL object
 * 2. String with the message text.
 *
 * Return value:
 *
 * Return the message
 * or NULL if there is a parse error
 */
 
static xplMessagePtr_t parseMessage(xplObjPtr_t xp, String theText) {
	int parsedThisTime;
	String blockDelimPtr,classType;
	xplNameValueLEPtr_t listHead = NULL, listTail = NULL;
	void *listCTX;
	xplMessagePtr_t xm;
	char blockHeader[64];
	
  
	/* Allocate a message */
	xm = createReceivedMessage(xp, XPL_MESSAGE_ANY);
	
	/* Allocate the header list context so we can easily free it later */
	MALLOC_FAIL(listCTX = talloc_new(xp->generalPool))

	/* Parse the header */
	if ((parsedThisTime = parseBlock(listCTX, &listHead, &listTail, theText, blockHeader, 64)) <= 0) {
		debug(DEBUG_UNEXPECTED, "Error parsing message header");
		releaseMessage(xm);
		return NULL;
	}


	/* Parse the header */
	if (!strcmp(blockHeader, "xpl-cmnd")){
		xm->messageType = XPL_MESSAGE_COMMAND;
	} 
	else if(!strcmp(blockHeader, "xpl-stat")){
		xm->messageType = XPL_MESSAGE_STATUS;
	} 
	else if(!strcmp(blockHeader, "xpl-trig")){
		xm->messageType = XPL_MESSAGE_TRIGGER;
	}
	else{
		debug(DEBUG_UNEXPECTED, "Unknown message header of %s - bad message", blockHeader);
		releaseMessage(xm);
		return NULL;
	}
	
	/* Must have a header name value list. */
	if(!listHead){
		debug(DEBUG_UNEXPECTED, "No name value list for header");
		releaseMessage(xm);
		return NULL;
	}
	

	/* Parse the message header name/values into the message */
	if (!parseMessageHeader(xm, listHead)){
		debug(DEBUG_UNEXPECTED, "Unable to parse message header");
		releaseMessage(xm);
		return NULL;
	}
	
	/* Free the header name value list, and invalidate the pointers */
	
	talloc_free(listCTX);
	listCTX = listHead = listTail = NULL;


	/* Parse the next block */
	if ((parsedThisTime = parseBlock(xm, &xm->nvHead, &xm->nvTail,
	theText + parsedThisTime, blockHeader, 64)) < 0){
		debug(DEBUG_UNEXPECTED, "Error parsing message block");
		releaseMessage(xm);
		return NULL;
	}
    
    MALLOC_FAIL(classType = talloc_strdup(xp->generalPool, blockHeader))
	
	/* Parse the block header */
	if ((blockDelimPtr = strchr(classType, '.')) == NULL) {
		debug(DEBUG_UNEXPECTED, "Malformed message block header - %s", blockHeader);
		releaseMessage(xm); 
		talloc_free(classType); 
		return NULL;
	}
	*blockDelimPtr++ = '\0';

	/* Record the message schema class/type */
	MALLOC_FAIL(xm->schemaClass = talloc_strdup(xm, classType))
	MALLOC_FAIL(xm->schemaType = talloc_strdup(xm, blockDelimPtr))	
	
	talloc_free(classType);
	
	/* Return the message */
	return xm;
}


/*
 **************************************************************************
 *  Private Service Functions
 **************************************************************************
 */
 
/* 
 * Change the current heartbeat interval
 *
 * Arguments:
 *
 * 1. Pointer to service object.
 * 2. The new heartbeat interval in seconds
 *
 * Return value
 *
 * None
 *
 */
 
static void setHeartbeatInterval(xplServicePtr_t xs, int newInterval)
{
	/* Skip out of range values */
	if ((newInterval < 0) || (newInterval > 172800)){
		return;
	}
	xs->heartbeatInterval = newInterval;
}

/*
 * Create an XPL service object
 *
 * Arguments:
 *
 * 1. Pointer to master XPL object
 * 2. String with source vendor name
 * 3. String wih source the device ID 
 * 4. String with the source instance ID.
 * 5. Optional version string
 *
 * Return value
 *
 */
 
static xplServicePtr_t createService(xplObjPtr_t xp, String theVendor, String theDeviceID, String theInstanceID, String theVersion) 
{
	xplServicePtr_t xs;
	
	/* Allocate space for the service object */
	MALLOC_FAIL(xs = talloc_zero(xp->generalPool, xplService_t))
	
	/* Install reference to master object */
	xs->xplObj = xp;

	/* Install info */
	MALLOC_FAIL(xs->serviceVendor = talloc_strdup(xs, theVendor))
	MALLOC_FAIL(xs->serviceDeviceID = talloc_strdup(xs, theDeviceID))
	MALLOC_FAIL(xs->serviceInstanceID = talloc_strdup(xs, theInstanceID))
	if(theVersion){
		MALLOC_FAIL(xs->serviceVersion = talloc_strdup(xs, theVersion))
	}
	xs->configurableService = FALSE; /* Not a config service */
	setHeartbeatInterval(xs, DEFAULT_HEARTBEAT_INTERVAL);
	
	/* Validate the object */
	xs->magic = XS_MAGIC;
	return xs;
}

/* 
 * Set service state
 *
 * This enables or disables a previously created service. If the service is enabled, heartbeat messages
 * will start being sent at the preset interval. If the service is disabled, then a goodbye heartbeat
 * message will be sent.
 
 * Arguments:
 *
 * 1. Pointer to service object to enable or disable
 * 2. New enable state. TRUE = enable, FALSE = disable
 *
 * Return value:
 *
 * None
 *
 */
 
static void setServiceState(xplServicePtr_t xs, Bool newState)
{
	
	/* Skip if there's no change to the enable state */
	if (xs->serviceEnabled == newState){
		return;
	}

	/* Set the new service state */
	xs->serviceEnabled = newState;

	/* Handle enabling a disabled service */
	if (newState){
		/* If there is an existing heartbeat message, release it, so it will get rebuilt */
		if (xs->heartbeatMessage){
			talloc_free(xs->heartbeatMessage);
			xs->heartbeatMessage = NULL;
		}
		/* Start sending discovery heartbeats */
		xs->discoveryState = XPL_HUB_UNCONFIRMED;
		xs->discoveryTries = 0;
		sendHeartbeat(xs);
	} else {
		/* Send goodbye heartbeat */
		sendGoodbyeHeartbeat(xs);
	}
}


/*
 **************************************************************************
 *  Public interface functions
 **************************************************************************
 */


/*
 * Destroy an XPL object.
 *
 * Disables all active services, kills the RX thread, removes FD's from the polling list,
 * closes all open FD's, frees the master object. This should be called at program exit 
 *
 * Arguments:
 *
 * 1. Pointer to master object to destroy
 *
 * Return value:
 *
 * None
 *
 *
 */
void XplDestroy(void *xplObj)
{
	xplObjPtr_t xp = xplObj;
	xplServicePtr_t xs;
	
	ASSERT_FAIL(xp)
	ASSERT_FAIL(XP_MAGIC == xp->magic)
	
	/* Traverse the service list and disable any services which are enabled */
	for(xs = xp->servHead; xs; xs = xs->next){
		if(xs->serviceEnabled){
			setServiceState(xs, FALSE);
		}	
	}
	
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
	
	PollUnRegEvent(Globals->poller, xp->rxReadyFD);
	/* Close the ready event FD */
	if(xp->rxReadyFD != -1){
		close(xp->rxReadyFD);
	}

	PollUnRegEvent(Globals->poller, xp->timerFD);
	/* Close the timer FD */
	if(xp->timerFD != -1){
		close(xp->timerFD);
	}
	
	/* Invalidate and free the object */
	xp->magic = 0;
	talloc_free(xp);
		
}


/*
 * Initialize XPL master object
 *
 * This function sets up the XPL subsystem and returns a master object to be used for all
 * future references. It determines what IP address to broadcast on given an IP address 
 * of an interface, creates the appopriate sockets for listening for and sending XPL messages.
 * A dedicated receive (RX) thread is created and used to receive queue incoming messages
 *
 * Arguments:
 *
 * 1. The talloc context to use for allocating memory pools in the main thread.
 * 2. A poll object. (See poll.c for details).
 * 3. A string containing an IP address of the interface to use to broadcast messages.
 * 4. A string containing the service name or port number to use. Usually set to "3865".
 *
 * Return value
 *
 * The XPL master object or NULL if there was an error.
 *
 */
 

void *XplInit(TALLOC_CTX *ctx, void *Poller, String IPAddr, String servicePort)
{
	xplObjPtr_t xp = NULL;
	char interfaceAddr[INET6_ADDRSTRLEN];
	char broadcastAddr[INET6_ADDRSTRLEN];
	struct ifaddrs *interfaceList = NULL, *curIFEntry = NULL;
	struct itimerspec its;
	void *addrPtr;
	int res;
	int addrFamily;
	Bool found = FALSE;
	char uniqPrefix[5];
	
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(Poller)
	ASSERT_FAIL(IPAddr)

	/* Figure out the IP addresses associated with a particular interface */
	if(!(res = getifaddrs(&interfaceList))){ /* Fetch list */
		/* Traverse the list looking for a match on the interface and address family */
		for(curIFEntry = interfaceList; curIFEntry; curIFEntry = curIFEntry->ifa_next){
			addrFamily = curIFEntry->ifa_addr->sa_family;
			/* Match only IPV4 or IPV6, and skip local interface */	
			if(((addrFamily != AF_INET) && (addrFamily != AF_INET6)) || (!strcmp("lo", curIFEntry->ifa_name))){
				continue;
			}
			/* Convert address to presentation */
			addrPtr = SocketFixAddrPointer(curIFEntry->ifa_addr);
			inet_ntop(addrFamily, addrPtr, interfaceAddr, sizeof(interfaceAddr));
		
			if(AF_INET == addrFamily){
				if(!curIFEntry->ifa_ifu.ifu_broadaddr){
					continue; /* IPV4 must have a broadcast address */
				}
			}
			else{ 
				continue; /* IPV6 Not supported yet. Need futher input on how to multicast xPL*/
			}
			if(strcmp(IPAddr, interfaceAddr)){ /* Address must match that passed in */
				continue;
			}	
			/* We have a useable address */
			found = TRUE;
			/* Generate 4 digit prefix for unique instance ID's */
			genUniquePrefix(uniqPrefix, addrPtr, (addrFamily == AF_INET6) ? 16 : 4);
			debug(DEBUG_ACTION, "Interface Name: %s", curIFEntry->ifa_name);
			debug(DEBUG_ACTION, "Interface Address: %s", interfaceAddr);
			addrPtr = SocketFixAddrPointer(curIFEntry->ifa_ifu.ifu_broadaddr);
			inet_ntop(curIFEntry->ifa_addr->sa_family, addrPtr, broadcastAddr, sizeof(broadcastAddr));
			debug(DEBUG_ACTION, "Broadcast/Multicast Address: %s", broadcastAddr);
			break;
		}
	}
	else{
		debug(DEBUG_UNEXPECTED, "Can't get a list of interfaces for %s", IPAddr);
		return NULL;
	}
	if(!found){
		debug(DEBUG_UNEXPECTED, "Can't get information for IP address %s", IPAddr);
		return NULL;
	}
	
	
	/* Allocate the object */
	MALLOC_FAIL(xp = talloc_zero(ctx, xplObj_t))

	/* Invalidate the FD's */
	xp->localConnFD = xp->rxReadyFD = xp->broadcastFD = -1;
	
	/* Save the internal IP address */
	MALLOC_FAIL(xp->internalIP = talloc_strdup(xp, (AF_INET6 == addrFamily) ? "::" : "0.0.0.0"))
	/* Save the broadcast IP address */
	MALLOC_FAIL(xp->broadcastIP = talloc_strdup(xp, broadcastAddr )) 
	/* Save the remote IP address */
	MALLOC_FAIL(xp->remoteIP = talloc_strdup(xp, interfaceAddr ))
	/* Save the unique prefix */
	MALLOC_FAIL(xp->uniqPrefix = talloc_strdup(xp, uniqPrefix))
	
	/* Release the getifaddrs info */
	freeifaddrs(interfaceList);   
	
	/* Save the poller object passed in */
	xp->poller = Poller;
	
	
	/* Allocate a working string pool */
	MALLOC_FAIL(xp->generalPool = talloc_pool(xp, GENERAL_POOL_SIZE))

	
	/* Validate the object */
	xp->magic = XP_MAGIC;
	
	/* Get socket for local interface */
	if((FAIL == SocketCreate(xp->internalIP, "0", addrFamily, SOCK_DGRAM, xp, addLocalSock)) || (xp->localConnFD < 0)){
		fatal("%s: Could not create socket for local interface", __func__);
	}
	
	/* Get socket for broadcast interface */
	if((FAIL == SocketCreate(xp->broadcastIP, servicePort, addrFamily, SOCK_DGRAM, xp, addBroadcastSock)) || (xp->broadcastFD < 0)){
		fatal("%s: Could not create socket for broadcast interface", __func__);
	}
	
	
	/* Create an event FD for ready */
	if((xp->rxReadyFD = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)) < 0){
		debug(DEBUG_UNEXPECTED, "%s: Could not create an event FD", __func__);
		XplDestroy(xp);
		return NULL;
	}

	
	/* Create a timerfd to keep track of heartbeats */
	if((xp->timerFD = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK)) < 0){
		debug(DEBUG_UNEXPECTED, "%s: Could not create an timer FD", __func__);
		XplDestroy(xp);
		return NULL;
	}
	
	/* Set the timerfd time interval */
	its.it_value.tv_sec = its.it_interval.tv_sec = 1;
	its.it_value.tv_nsec = its.it_interval.tv_nsec = 0;
	if(timerfd_settime(xp->timerFD, 0, &its, NULL) < 0){
		debug(DEBUG_UNEXPECTED, "%s: Could not set timer FD interval", __func__);
		XplDestroy(xp);
		return NULL;
	}		


	/* Add the RX ready FD to the poller */
	if(FAIL == PollRegEvent(xp->poller, xp->rxReadyFD, POLL_WT_IN, rxReadyAction, xp)){
		debug(DEBUG_UNEXPECTED, "%s: Could not register RX Ready eventfd", __func__);
		XplDestroy(xp);
		return NULL;
	}
	
	/* Add the timerFD and xplTick to the polling list */
	if(FAIL == PollRegEvent(xp->poller, xp->timerFD, POLL_WT_IN, xplTick, xp)){
		fatal("%s: Could not register timerFD", __func__);
	}
	
	/* Initialize receiver thread */
	if(NULL == (xp->rcvr = XplRXInit(xp->localConnFD, xp->localConnPort, xp->rxReadyFD))){
		debug(DEBUG_UNEXPECTED, "%s: Could not initialize xpl recever thread", __func__);
		XplDestroy(xp);
		return NULL;
	}
	
	return xp;
}
/*
 * Create a new service object
 * Service will be created in the disabled state.
 * 
 * 
 * 
 * Arguments:
 * 1. Pointer to master XPL object
 * 2. String with vendor name (required).
 * 3. String with device ID (required).
 * 4. String with instance ID or NULL for a randomly generated instance ID
 * 5. String with the service version or NULL
 * 
 * Return value:
 * 
 * Address of new service object.
 * Must be destroyed with XplDestroyService when no longer needed
 */
 
void *XplNewService(void *xplObj, String theVendor, String theDeviceID, String theInstanceID, String theVersion)
{	
	xplServicePtr_t xs;
	xplObjPtr_t xp = xplObj;
	String id;
	ASSERT_FAIL(xp)
	ASSERT_FAIL(XP_MAGIC == xp->magic)
	ASSERT_FAIL(theVendor)
	ASSERT_FAIL(theDeviceID)
	
	/* If user passed a NULL for the instance ID, they want it autogenerated */
	if(!theInstanceID){
		id = generateFairlyUniqueID(xplObj);
	}
	else{
		/* User supplied the ID */
		id = theInstanceID;
	}
	/* Create the service */
	xs = createService(xp, theVendor, theDeviceID, id, theVersion);
	
	/* If ID was autogenerated, release the string */
	if(!theInstanceID){
		talloc_free(id);
	}
	
	/* Install new service object in service list */
	if(!xp->servHead){
		/* Empty list case */
		xp->servHead = xp->servTail = xs;
	}
	else {
		/* Add to end of list */
		xp->servTail->next = xs;
		xs->prev = xp->servTail;
		xp->servTail = xs;
	}
	
	return xs;	
}

/*
 * Destroy a service.
 * The service should be disabled before this is called, but it will disable the service before destruction.
 *
 * Arguments:
 *
 * 1. Pointer to service object to destroy
 *
 * Return value
 *
 * TRUE if the service was found in the list and destroyed. FALSE otherwise.
 *
 */
 
Bool XplDestroyService(void *servToDestroy)
{	
	xplObjPtr_t xp;
	xplServicePtr_t xst, xs = servToDestroy;
	ASSERT_FAIL(xs)
	ASSERT_FAIL(XS_MAGIC == xs->magic)
	ASSERT_FAIL(xp = xs->xplObj)
	ASSERT_FAIL(XP_MAGIC == xp->magic)
	/* Traverse the list looking for our service */
	for(xst = xp->servHead; xst; xst = xst->next){
		if(xst == xs){
			/* Found it */
			if(xs->serviceEnabled){ /* Disable service if enabled */
				setServiceState(xs, FALSE);
			}
			break;
		}
	}
	if(!xst){
		return FALSE; /* Not found */
	}
	/* Found the service. Remove it from the service list */
	
	if(!xst->prev){
		/* At the beginning of the list, and/or the only entry */
		if(!xst->next){
			/* Only entry */
			xp->servHead = xp->servTail = NULL;
		}
		else{
			/* Entries follow */
			xp->servHead->next->prev = NULL;
			xp->servHead = xp->servHead->next;
		}	
	}
	else if(!xst->next){
		/* At the end of the list */
		xp->servTail->prev->next = NULL;
		xp->servTail = xp->servTail->prev;
	}
	else{
		/* In the middle of the list */
		xst->prev->next = xst->next;
		xst->next->prev = xst->prev;

	}
	/* Service object is now removed from the list. Invalidate it. */
	xst->magic = 0;
	
	/* Free all of its memory and any children it allocated */
	talloc_free(xst);
	
	return TRUE;
	
}
/*
 * Enable a previously created service object.
 * Enabling a service starts the heartbeat message generation, and 
 * also will allow message monitor callbacks to be called.
 *
 * Arguments:
 *
 * 1. Pointer to service object to enable.
 *
 * Return value
 *
 * None
 *
 */
 
void XplEnableService(void *servToEnable)
{
	xplServicePtr_t xs = servToEnable;
	xplObjPtr_t xp;
	
	ASSERT_FAIL(xs)
	ASSERT_FAIL(XS_MAGIC == xs->magic)
	ASSERT_FAIL(xp = xs->xplObj)
	ASSERT_FAIL(XP_MAGIC == xp->magic)
	
	setServiceState(xs, TRUE);
}

/*
 * Disable a previously created service object.
 * Sends a goodbye heartbeat message, and prevents
 * message monitor callbacks from being called.
 *
 * Arguments:
 *
 * 1. Pointer to service object to disable
 *
 * Return value
 *
 * None
 *
 */
 
void XplDisableService(void *servToDisable)
{
	xplObjPtr_t xp;
	xplServicePtr_t xs = servToDisable;
	
	ASSERT_FAIL(xs)
	ASSERT_FAIL(XS_MAGIC == xs->magic)
	ASSERT_FAIL(xp = xs->xplObj)
	ASSERT_FAIL(XP_MAGIC == xp->magic)
	
	setServiceState(xs, FALSE);
}

/*
 * Get Hub discovery state for the supplied service object
 *
 * Arguments:
 *
 * 1. Pointer to service object to check.
 *
 * Return value
 *
 * Discovery state (See xplcore.h)
 *
 */

XPLDiscoveryState_t XplGetHubDiscoveryState(void  *servToCheck)
{
	
	xplServicePtr_t xs = servToCheck;
	
	ASSERT_FAIL(xs)
	ASSERT_FAIL(XS_MAGIC == xs->magic)

	return xs->discoveryState;
	
}

 


/*
 * Create a new message object for a targetted message.
 *
 * Arguments:
 *
 * 1. Pointer to service object to link with the message object.
 * 2. Message type (see xplcore.h)
 * 3. The target vendor
 * 4. The target device ID
 * 5. The target instance ID
 *
 * Return value
 *
 * Pointer to the new message object.
 *
 */
 
void *XplInitTargettedMessage(void *XPLServ, XPLMessageType_t messageType, 
String theVendor, String theDeviceID, String theInstanceID)
{
	xplServicePtr_t xs = XPLServ;
	ASSERT_FAIL(xs)
	ASSERT_FAIL(XS_MAGIC == xs->magic)
	ASSERT_FAIL(theVendor)
	ASSERT_FAIL(theDeviceID)
	ASSERT_FAIL(theInstanceID)
	return createTargetedMessage(xs, messageType, theVendor, theDeviceID, theInstanceID);
	 
		
	
	
}

/*
 * Create a broadcast message
 *
 * Arguments:
 *
 * 1. Pointer to service object to link with the message object.
 * 2. Message type (see xplcore.h)
 *
 * Return value
 *
 * Pointer to the new message object.
 
 */
void *XplInitBroadcastMessage(void *XPLServ, XPLMessageType_t messageType)
{
	xplServicePtr_t xs = XPLServ;
	ASSERT_FAIL(xs)
	ASSERT_FAIL(XS_MAGIC == xs->magic)
	return createBroadcastMessage(xs, messageType); 
}


/*
 * Send a group message
 *
 * Arguments:
 *
 * 1. Pointer to service object to link with the message object.
 * 2. Message type (see xplcore.h)
 * 3. String with the message group name.
 *
 * Return value
 *
 * Pointer to the new message object.

 */

void *XplInitGroupMessage(void *XPLServ, XPLMessageType_t messageType, String controlGroup)
{
	xplServicePtr_t xs = XPLServ;
	ASSERT_FAIL(xs)
	ASSERT_FAIL(XS_MAGIC == xs->magic);
	ASSERT_FAIL(controlGroup);
	
	return XplInitTargettedMessage(xs, messageType, "xpl", "group", controlGroup);
}



/*
 * Destroy an existing message object
 *
 * Arguments:
 *
 * 1. Pointer to message object to destroy
 *
 * Return value:
 *
 * None
 */

void XplDestroyMessage(void *XPLMessage)
{
	xplMessagePtr_t xm = XPLMessage;
	ASSERT_FAIL(xm)
	ASSERT_FAIL(XM_MAGIC == xm->magic)
	ASSERT_FAIL(xm->serviceObj)
	/* Invalidate message */
	xm->magic = 0;
	talloc_free(xm);	
}

/*
 * Add Name-Value pair to message
 *
 * Arguments:
 *
 * 1. Pointer to message object 
 * 2. The name as a string
 * 3. The value as a string
 *
 * Return value
 *
 * None
 */
 
void XplAddNameValue(void *XPLMessage, String theName, String theValue)
{
	xplMessagePtr_t xm = XPLMessage;
	ASSERT_FAIL(xm)
	ASSERT_FAIL(XM_MAGIC == xm->magic)
	ASSERT_FAIL(xm->serviceObj)
	addMessageNamedValue(xm, theName, theValue);
}

/*
 * Set the message schema class and type
 *
 * Arguments:
 *
 * 1. Pointer to message object 
 * 2. The class as a string
 * 3. The type as a string
 *
 * Return value
 *
 * None
 */
 

void XplSetMessageClassType(void *xplMessage, const String theClass, const String theType)
{
	
	
	xplMessagePtr_t xm = xplMessage;
	ASSERT_FAIL(xm);
	ASSERT_FAIL(XM_MAGIC == xm->magic)
	ASSERT_FAIL(xm->serviceObj)

	if(theClass){
		UtilReplaceString(&xm->schemaClass, xm, theClass);
	}	
	if(theType){
		UtilReplaceString(&xm->schemaType, xm, theType);
	}
}

/* 
 * Delete Message Name/Value list
 *
 * Arguments:
 *
 * 1. Pointer to message object 
 *
 * Return value
 *
 * None
 */
 
void XplClearNameValues(void *XPLMessage)
{
	xplMessagePtr_t xm = XPLMessage;
	ASSERT_FAIL(xm);
	ASSERT_FAIL(XM_MAGIC == xm->magic)
	ASSERT_FAIL(xm->serviceObj)
	
	/* Destroy name value list and initialize it as empty */
	
	talloc_free(xm->nvCTX);
	xm->nvCTX = xm->nvHead = xm->nvTail = NULL;
	
	/* See how easy that was? */
	
	return;
}

/*
 * Send the message
 *
 * Arguments:
 *
 * 1. Pointer to message object 
 *
 * Return value
 *
 * TRUE if the message was sent successfully, otherwise FALSE
 */
 
Bool XplSendMessage(void *XPLMessage)
{
	xplMessagePtr_t xm = XPLMessage;
	ASSERT_FAIL(xm) /* Object must exist */
	ASSERT_FAIL(XM_MAGIC == xm->magic) /* Object must be valid */
	ASSERT_FAIL(xm->serviceObj) /* Message must be sendable */
	ASSERT_FAIL(xm->schemaClass)
	ASSERT_FAIL(xm->schemaType)
	return sendMessage(xm);	
}


/*
 * Add a message listener function to the service
 *
 * Arguments:
 *
 * 1. Pointer to service object 
 * 2. The listener mode (see xplcore.h)
 * 3. A flag to indicate whether to report group messages.
 * 4. A pointer to a user-defined object.
 * 5. A pointer to a listening function (see xplcore.h)
 *
 * Return value
 *
 * None
 */
 
void XplAddMessageListener(void *XPLService, XPLListenerReportMode_t reportMode, Bool reportGroupMessages,
	void *userObj, XPLListenerFunc_t listener)
{
	xplServicePtr_t xs = XPLService;
	ASSERT_FAIL(xs); /* Object must exist */
	ASSERT_FAIL(XS_MAGIC == xs->magic) /* Object must be valid */
	ASSERT_FAIL(listener) /* Must have supplied a listening function */
	xs->reportMode = reportMode;
	xs->reportGroupMessages = reportGroupMessages;
	xs->userListenerObject = userObj;
	xs->listener = listener;
}

/*
 * Remove the message listener from the service
 *
 * Arguments:
 *
 * 1. Pointer to service object 
 *
 *
 * Return value:
 *
 * None
 *
 */

void XplRemoveMessageListener(void *XPLService)
{
	xplServicePtr_t xs = XPLService;
	ASSERT_FAIL(xs);
	ASSERT_FAIL(XS_MAGIC == xs->magic)
	xs->listener = NULL;
}

/*
 * Set user supplied pointer to string pointers with the  3 elements of the source tag:
 * 
 * If a NULL is passed in for one of the string pointers, that element will be ignored.
 * 
 * The returned strings should be talloc_freed when they are no longer required.
 *
 * Arguments:
 *
 * 1. Pointer to message object 
 * 2. A talloc context to use for allocating the component strings.
 * 3. An address for the vendor string.
 * 4. An address for the device ID string.
 * 5. An address for the instance ID string.
 *
 * Return value
 *
 * None
 *
 */

void XplGetMessageSourceTagComponents(void *XPLMessage, TALLOC_CTX *stringCTX,
	String *theVendor, String *theDeviceID, String *theInstanceID)
{
	xplMessagePtr_t xm = XPLMessage;
	ASSERT_FAIL(xm) /* Object must exist */
	ASSERT_FAIL(XM_MAGIC == xm->magic) /* Object must be valid */
	
	if(theVendor){
		MALLOC_FAIL(*theVendor = talloc_strdup(stringCTX, xm->sourceVendor))
	}
	
	if(theDeviceID){
		MALLOC_FAIL(*theDeviceID = talloc_strdup(stringCTX, xm->sourceDeviceID))
	}
	
	if(theInstanceID){
		MALLOC_FAIL(*theInstanceID = talloc_strdup(stringCTX, xm->sourceInstanceID))
	}
	
}

/*
 * Get the message type
 *
 * Arguments:
 *
 * 1. Pointer to message object 
 *
 * Return value
 *
 * The message type (see xplcore.h)
 */
 
XPLMessageType_t XplGetMessageType(void *XPLMessage)
{
	xplMessagePtr_t xm = XPLMessage;
	ASSERT_FAIL(xm) /* Object must exist */
	ASSERT_FAIL(XM_MAGIC == xm->magic) /* Object must be valid */
	return xm->messageType;

}

/*
 * Get class and type (message schema)
 *
 * If a NULL is passed in for one of the string pointers, that element will be ignored.
 * 
 * The returned strings should be talloc_freed when they are no longer required.
 *
 * Arguments:
 *
 * 1. Pointer to message object 
 * 2. A talloc context to use for allocating the component strings.
 * 3. An address for the class string.
 * 4. An address for the type string.
 *
 * Return value
 *
 * None
 
 */

void XplGetMessageSchema(void *XPLMessage, TALLOC_CTX *stringCTX, String *theClass,  String *theType)
{
	xplMessagePtr_t xm = XPLMessage;
	ASSERT_FAIL(xm) /* Object must exist */
	ASSERT_FAIL(XM_MAGIC == xm->magic) /* Object must be valid */
	ASSERT_FAIL(stringCTX)
	
	if(theClass){
		MALLOC_FAIL(*theClass = talloc_strdup(stringCTX, xm->schemaClass))
	}
	
	if(theType){
		MALLOC_FAIL(*theType = talloc_strdup(stringCTX, xm->schemaType))
	}
	
}

/*
 * Return TRUE if the message is a received message
 *
 * Arguments:
 *
 * 1. Pointer to message object 
 *
 * Return value
 *
 * TRUE if he message is a received message, otherwise FALSE which indicates it is a transmit message 
 */

Bool XplMessageIsReceive(void *XPLMessage)
{
	xplMessagePtr_t xm = XPLMessage;
	ASSERT_FAIL(xm) /* Object must exist */
	ASSERT_FAIL(XM_MAGIC == xm->magic) /* Object must be valid */
	
	return (xm->serviceObj == NULL); /* Return the flag */
}


/*
 * Return a value for a given name
 * 
 * If the value doesn't exist, return NULL
 * 
 * An talloc'd string is returned, so it will need to talloc_freed when it is no longer required.
 *
 * Arguments:
 *
 * 1. Pointer to message object 
 * 2. A talloc context to use for allocating the component strings.
 * 3. The name to look up.
 *
 * Return value
 *
 * String with the associated value.
 */
 
String XplGetMessageValueByName(void *XPLMessage, TALLOC_CTX *stringCTX, String theName)
{
	xplNameValueLEPtr_t xnv;
	xplMessagePtr_t xm = XPLMessage;
	String theValue;
	ASSERT_FAIL(xm) /* Object must exist */
	ASSERT_FAIL(XM_MAGIC == xm->magic) /* Object must be valid */
	ASSERT_FAIL(stringCTX)
	ASSERT_FAIL(theName)
	
	if((xnv = getNamedValue(xm->nvHead, theName))){
		/* Value exists, make a duplicate and return it */
		MALLOC_FAIL(theValue = talloc_strdup(stringCTX, xnv->itemValue))
		return theValue;
	}
	/* Value does not exist */
	return NULL;
}

/* 
 * Return a string containing comma-separated list of all name-value pairs associated with the message
 * String must be talloc_freed, when it is no longer required.
 *
 * Arguments:
 *
 * 1. A talloc context to use for allocating the component strings.
 * 1. A pointer to the message object.
 *
 * Return value
 *
 * String containing the list.
 */
 
String XplGetMessageNameValuesAsString(TALLOC_CTX *stringCTX, void *XPLMessage)
{
	xplMessagePtr_t xm = XPLMessage;
	xplNameValueLEPtr_t xnv;
	String res;
	int i;
	
	ASSERT_FAIL(xm) /* Object must exist */
	ASSERT_FAIL(XM_MAGIC == xm->magic) /* Object must be valid */
	
	MALLOC_FAIL(res = talloc_array(stringCTX, char, 64))
	res[0] = 0;
	/* Traverse the list */
	for(xnv = xm->nvHead, i = 0; xnv; xnv = xnv->next, i++){
		ASSERT_FAIL(XNV_MAGIC == xnv->magic)
		if(i){
			MALLOC_FAIL(res = talloc_asprintf_append(res, ",%s=%s", xnv->itemName, xnv->itemValue))
		}
		else{
			MALLOC_FAIL(res = talloc_asprintf_append(res, "%s=%s", xnv->itemName, xnv->itemValue))
		}
	}
	return res;		
}

/*
 * Iterate through the name value list of a message, calling a user supplied callback function for each list entry
 *
 * Arguments:
 *
 * 1. Pointer to a message object 
 * 2. Pointer to a user defined object.
 * 3. Callback function (see xplcore.h)
 *
 * Return value
 *
 * None
 */
 
void XplMessageIterateNameValues(void *XPLMessage, void *userObj, XPLIterateNVCallback_t callback )
{
	xplMessagePtr_t xm = XPLMessage;
	xplNameValueLEPtr_t xnv;
	
	ASSERT_FAIL(xm) /* Object must exist */
	ASSERT_FAIL(XM_MAGIC == xm->magic) /* Object must be valid */
	ASSERT_FAIL(callback)
	
	/* Traverse the list and call the user supplied callback function for each entry */
	for(xnv = xm->nvHead; xnv; xnv = xnv->next){
		ASSERT_FAIL(XNV_MAGIC == xnv->magic)
		(*callback)(userObj, xnv->itemName, xnv->itemValue);
	}	
}

