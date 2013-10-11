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

#define WRITE_TEXT(xp, x) if (!appendText(xp, x)) return FALSE;
#define VALID_CHAR(theChar) (((theChar >= 32) && (theChar < 123)) || (theChar = 124) || (theChar = 126))
#define STR_FREE(p) if(p){ talloc_free(p); p = NULL;}


/* name/value list entry */
typedef struct xplNameValueLE_s {
	unsigned magic;
	String itemName;
	String itemValue;
	Bool isBinary; /* RFU */
	int binaryLength; /* RFU */
	struct xplNameValueLE_s *next;
	} xplNameValueLE_t, *xplNameValueLEPtr_t;


/* Describe a received message */
typedef struct {
	unsigned magic;
	XPLMessageType_t messageType;
	int hopCount;

	String sourceVendor;
	String sourceDeviceID;
	String sourceInstanceID;

	Bool isGroupMessage;
	String groupName;

	Bool isBroadcastMessage;
	String targetVendor;
	String targetDeviceID;
	String targetInstanceID;

	String schemaClass;
	String schemaType;
	
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
	time_t lastHeartbeatAt;
	XPLListenerReportMode_t reportMode;
	
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
	unsigned txBuffBytesWritten; /* Holds the number of bytes in txBuff */
	int localConnFD; /* FD for packets from HUB */
	int broadcastFD; /* FD for broadcasts to network */
	int rxReadyFD; /* Ent FC for RX packet ready from receiver */
	int broadcastAddrLen; /* Indicates length of data in broadcastAddr stuct below */
	int localConnPort; /* Ephermeral port for packets sent from local hub */
	void *poller; /* Pointer to the poller object supplied by the user */
	void *rcvr; /* Pointer to the receiver object */
	void *generalPool; /* Pointer to general memory pool for strings and structs */
	String txBuff; /* Holds the transmit string */
	String remoteIP; /* IP of ethernet interface to use */
	String broadcastIP; /* Broadcast address on the interface */
	String internalIP; /* Listen address for hub transmissions */
	xplServicePtr_t servHead; /* Head for linked list of services */
	xplServicePtr_t servTail; /* Tail for linked list of services */
	struct sockaddr_storage broadcastAddr; /* Holds the broadcast address data for sending XPL packets */
} xplObj_t, *xplObjPtr_t;

typedef enum { HBEAT_NORMAL, HBEAT_CONFIG, HBEAT_NORMAL_END, HBEAT_CONFIG_END } Heartbeat_t;

/*
* Forward references
*/

static Bool sendHeartbeat(xplObjPtr_t xp, xplServicePtr_t theService);
static xplMessagePtr_t parseMessage(xplObjPtr_t xp, String theText);
static void releaseMessage(xplMessagePtr_t xm);

/*
 **************************************************************************
 *  Non-categorized Private functions
 **************************************************************************
 */
 
 
/*
 * Create a new name value list entry 
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
 */

static xplNameValueLEPtr_t getNamedValue(xplNameValueLEPtr_t nvListHead, const String name)
{
	xplNameValueLEPtr_t nvp;
	
	/* Traverse the list looking for a match */
	for(nvp = nvListHead; nvp; nvp = nvp->next){
		ASSERT_FAIL(XNV_MAGIC == nvp->magic)
		if(!UtilStrcmpIgnoreCase(name, nvp->itemName)){
			return nvp;
		}
	}
	/* No match found */
	return NULL;
}



/*
 * Process notification of buffer add
 * This is where we parse receive messages.
 */

static void rxReadyAction(int fd, int event, void *objPtr)
{
	xplObjPtr_t xp = objPtr;
	xplServicePtr_t cse;
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
			if(theString){
				if(notify_get_debug_level() >= 5){
					debug(DEBUG_EXPECTED, "Processing received message: length = %d", strlen(theString));
					debug(DEBUG_EXPECTED,"Packet received:\n%s", theString);
				}
				/* Process the string contents */
				xm = parseMessage(xp, theString);
				if(xm){
					debug(DEBUG_ACTION, "Message parsed OK");
					/* Dispatch message to appropriate handler */
					
					/* Traverse the service list */
					for(cse = xp->servHead; cse; cse = cse->next){
						Bool report = FALSE;
						if(XPL_REPORT_EVERYTHING == cse->reportMode){
							/* Report it all */
							report = TRUE;
						}
						else if (XPL_REPORT_OWN_MESSAGES == cse->reportMode){
							/* Try to match source tag to our service */
							int matchCount = 0;
							if((!UtilStrcmpIgnoreCase(xm->sourceDeviceID, cse->serviceDeviceID))){
								matchCount++;
							}
							if((!UtilStrcmpIgnoreCase(xm->sourceVendor, cse->serviceVendor))){
								matchCount++;
							}
							if((!UtilStrcmpIgnoreCase(xm->sourceInstanceID, cse->serviceInstanceID))){
								matchCount++;
							}
							if(matchCount >= 3){
								report = TRUE;
							}
						}
						if(!report){ /* If nothing still matches, then try to match a broadcast or targetted message */
							if(xm->isBroadcastMessage){
								report = TRUE;
							}
							else{
								int matchCount = 0;
								if((!UtilStrcmpIgnoreCase(xm->targetDeviceID, cse->serviceDeviceID))){
									matchCount++;
								}
								if((!UtilStrcmpIgnoreCase(xm->targetVendor, cse->serviceVendor))){
									matchCount++;
								}
								if((!UtilStrcmpIgnoreCase(xm->targetInstanceID, cse->serviceInstanceID))){
									matchCount++;
								}
								if(matchCount >= 3){
									report = TRUE;
								}
							}
						if(report && cse->listener){
							/* Call user listener function with the message and the user object */
							(*cse->listener)( xm, cse, cse->userListenerObject);
						}
							
							
						}
					}
					
					/* Destroy the message */
					releaseMessage(xm);
				}
				else{
					debug(DEBUG_UNEXPECTED, "Message parse error");
				}
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
	
	/* Save the broadcast address and length */
	xp->broadcastAddrLen = addrlen;
	memcpy(&xp->broadcastAddr, addr, addrlen);

	debug(DEBUG_ACTION, "Broadcast socket set up on: %s", xp->broadcastIP);
	return FALSE;
}

/*
 * XPL tick function
 */

static void xplTick(int id, void *objPtr)
{
	xplObjPtr_t xp = objPtr;
	xplServicePtr_t xs;

	
	ASSERT_FAIL(xp)
	ASSERT_FAIL(XP_MAGIC == xp->magic)
	
	debug(DEBUG_INCOMPLETE, "XPL tick");
	/* Traverse the service list */
	for(xs = xp->servHead; xs; xs = xs->next){
		/* Is the heartbeat timer expired ? */
		if(!xs->heartbeatTimer){
			debug(DEBUG_ACTION, "Sending refresh heartbeat");
			if(FALSE == sendHeartbeat(xp, xs)){
				debug(DEBUG_UNEXPECTED, "Refresh heartbeat send failed");
			}		
		}
		else{
			/* Decrement timer and continue */
			xs->heartbeatTimer--;
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
 */

static void releaseMessage(xplMessagePtr_t xm)
{
	/* Invalidate message */
	xm->magic = 0;
	/* Free message and any children */
	talloc_free(xm);
}


/* 
 * Send the passed string and return TRUE if it appears it was transmitted successfully
 * or FALSE if there was an error 
 */  
              
static Bool sendRawMessage(xplObjPtr_t xp)
{
	
	int bytesSent;
	char eStr[64];
	unsigned buffLen = xp->txBuffBytesWritten;

	/* Try to send the message */
	if ((bytesSent = sendto(xp->broadcastFD, xp->txBuff, buffLen, 0, 
		(struct sockaddr *) &xp->broadcastAddr, sizeof(struct sockaddr_storage))) != buffLen) {
		debug(DEBUG_UNEXPECTED, "Unable to broadcast message, %s (%d)", strerror_r(errno, eStr, 64), errno);
		return FALSE;
	}
	debug(DEBUG_INCOMPLETE, "Broadcasted %d bytes (of %d attempted)", bytesSent, buffLen);
	return TRUE;
}

/* 
 * Append text and keep track of what we've used 
 */
 
static Bool appendText(xplObjPtr_t xp, String theString) 
{
	int stringLen = strlen(theString);

	/* Make sure it fits in the TX buffer */
	if ((xp->txBuffBytesWritten + stringLen) >= MSG_MAX_SIZE) {
		debug(DEBUG_UNEXPECTED, "Message exceeds MSG_MAX_SIZE (%d) -- not sent!", MSG_MAX_SIZE);
		debug(DEBUG_UNEXPECTED, "** Partial message is [%s]", xp->txBuff);
		return FALSE;
	}

	/* Copy the text in */
	memcpy(&xp->txBuff[xp->txBuffBytesWritten], theString, stringLen);
	xp->txBuffBytesWritten += stringLen;
	/* Terminate the string */
	xp->txBuff[xp->txBuffBytesWritten] = '\0';
	return TRUE;
}


/* 
 * Write out the message 
 */
 
static Bool formatMessage(xplObjPtr_t xp, xplMessagePtr_t theMessage)
{
	xplNameValueLEPtr_t le;

	/* Clear the write count */
	xp->txBuffBytesWritten = 0;

	/* Write header */
	switch (theMessage->messageType) {
		case XPL_MESSAGE_COMMAND:
			WRITE_TEXT(xp, "xpl-cmnd");
			break;
		case XPL_MESSAGE_STATUS:
			WRITE_TEXT(xp, "xpl-stat");
			break;
		case XPL_MESSAGE_TRIGGER:
			WRITE_TEXT(xp, "xpl-trig");
			break;
		default:
			ASSERT_FAIL(0);
	}

	/* Write hop and source info */
	WRITE_TEXT(xp, "\n{\nhop=1\nsource=");
	WRITE_TEXT(xp, theMessage->sourceVendor);
	WRITE_TEXT(xp, "-");
	WRITE_TEXT(xp, theMessage->sourceDeviceID);
	WRITE_TEXT(xp, ".");
	WRITE_TEXT(xp, theMessage->sourceInstanceID);
	WRITE_TEXT(xp, "\n");

	/* Write target */
	if (theMessage->isBroadcastMessage){
		WRITE_TEXT(xp, "target=*");
	} else{
		if (theMessage->isGroupMessage) {
			WRITE_TEXT(xp, "target=XPL-GROUP.");
			WRITE_TEXT(xp, theMessage->groupName);
		} else{
			WRITE_TEXT(xp, "target=");
			WRITE_TEXT(xp, theMessage->targetVendor);
			WRITE_TEXT(xp,"-");
			WRITE_TEXT(xp, theMessage->targetDeviceID);
			WRITE_TEXT(xp, ".");
			WRITE_TEXT(xp, theMessage->targetInstanceID);
		}
	}
	WRITE_TEXT(xp, "\n}\n");

	/* Write the schema out */
	WRITE_TEXT(xp, theMessage->schemaClass);
	WRITE_TEXT(xp, ".");
	WRITE_TEXT(xp, theMessage->schemaType);
	WRITE_TEXT(xp, "\n{\n");

	/* Write Name/Value Pairs out */
	for (le = theMessage->nvHead; le; le = le->next) {
		WRITE_TEXT(xp, le->itemName);
		WRITE_TEXT(xp, "=");

		/* Write data content out */
		if (le->itemValue != NULL) {
			if (le->isBinary){ 
				ASSERT_FAIL(0) /* Not supported */
				/*
				writeBinaryValue(le->itemValue, le->binaryLength);
				*/
			}
			else{
				WRITE_TEXT(xp, le->itemValue);
			}
		}

		/* Terminate line/entry */
		WRITE_TEXT(xp, "\n");
	}

	/* Write message terminator */
	WRITE_TEXT(xp, "}\n");

	/* Terminate and return text */
	xp->txBuff[xp->txBuffBytesWritten] = '\0';
	return TRUE;
}

/* 
 * Send an xpl message. 
 *  
 * If the message is valid and is successfully sent,
 * TRUE is returned.  
 */ 
                                                    
static Bool sendMessage(xplObjPtr_t xp, xplMessagePtr_t theMessage)
{
	/* Write the message to text */
	if (FALSE == formatMessage(xp, theMessage)){
		return FALSE;
	}
	
	/* Attempt to broadcast it */
	debug(DEBUG_INCOMPLETE, "About to broadcast %d bytes as [%s]", xp->txBuffBytesWritten, xp->txBuff);
	if (!sendRawMessage(xp)){
		return FALSE;
	}
	return TRUE;
}


/* 
 * Create a new message based on a service
 */
 
static xplMessagePtr_t createSendableMessage(xplServicePtr_t theService, XPLMessageType_t messageType) {
  xplMessagePtr_t theMessage;
  
  /* Allocate the message (owned by the service context) */
  MALLOC_FAIL(theMessage = talloc_zero(theService, xplMessage_t))
  
  /* Install references back to service and master objects */
  theMessage->xplObj = theService->xplObj;
  theMessage->serviceObj = theService; /* A sendable message will include a reference to a service */
   

  /* Set the version */
  theMessage->messageType = messageType;
  theMessage->hopCount = 1;

  theMessage->sourceVendor = theService->serviceVendor;
  theMessage->sourceDeviceID = theService->serviceDeviceID;
  theMessage->sourceInstanceID = theService->serviceInstanceID;
  
  /* Validate the message */
  theMessage->magic = XM_MAGIC;
  
  return theMessage;
}

/*
 * Postpend an entry to a name-value list
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
 */
 
static void addMessageNamedValue(xplMessagePtr_t theMessage, String name, String value )
{
	xplNameValueLEPtr_t newNVLE;
	
	if(!theMessage->nvCTX){ 
		/* Create a new context from the NV pool for the list to make it simple to delete */
		MALLOC_FAIL(theMessage->nvCTX = talloc_new(theMessage));
	}
	/* Create a new list entry */
	MALLOC_FAIL(newNVLE = talloc_zero(theMessage->nvCTX, xplNameValueLE_t))
	
	/* Add the name */
	MALLOC_FAIL(newNVLE->itemName = talloc_strdup(newNVLE, name))
	
	/* Add the value */
	MALLOC_FAIL(newNVLE->itemValue = talloc_strdup(newNVLE, value))
	
	postpendToNameValueList(&theMessage->nvHead, &theMessage->nvTail, newNVLE);
}


/* 
 * Create a message suitable for sending to a specific receiver
 */
 
static xplMessagePtr_t createTargetedMessage(xplServicePtr_t theService, XPLMessageType_t messageType, 
	String theVendor, String theDevice, String theInstance) 
{

	xplMessagePtr_t theMessage = createSendableMessage(theService, messageType);
	MALLOC_FAIL(theMessage->targetVendor = talloc_strdup(theMessage, theVendor))
	MALLOC_FAIL(theMessage->targetDeviceID = talloc_strdup(theMessage, theDevice))
	MALLOC_FAIL(theMessage->targetInstanceID = talloc_strdup(theMessage, theInstance))
	return theMessage;
}

#if(0)
/* Create a message suitable for sending to a group */
static xplMessagePtr_t createGroupTargetedMessage(xplServicePtr_t theService, XPLMessageType_t messageType, String theGroup) 
{
	xplMessagePtr_t theMessage = createSendableMessage(theService, messageType);
	MALLOC_FAIL(theMessage->groupName = talloc_strdup(theMessage, theGroup))
	return theMessage;
}
#endif

/* Create a message suitable for broadcasting to all listeners */
static xplMessagePtr_t createBroadcastMessage(xplServicePtr_t theService, XPLMessageType_t messageType) 
{
	xplMessagePtr_t theMessage = createSendableMessage(theService, messageType);
	theMessage->isBroadcastMessage = TRUE;
	return theMessage;
}

/*
 * Create a heartbeat message
 */
 
static xplMessagePtr_t createHeartbeatMessage(xplObjPtr_t xp, xplServicePtr_t theService, Heartbeat_t heartbeatType) 
{
	xplMessagePtr_t theHeartbeat;
	String portStr;
	String interval = "";

	/* Create the Heartbeat message */
	theHeartbeat = createBroadcastMessage(theService, XPL_MESSAGE_STATUS);
    
	/* Configure the heartbeat */
	switch (heartbeatType) {
		case HBEAT_NORMAL:
			theHeartbeat->schemaClass = "hbeat";
			theHeartbeat->schemaType = "app";
			MALLOC_FAIL(interval = talloc_asprintf(theHeartbeat, "%d", theService->heartbeatInterval / 60))
			break;

		case HBEAT_NORMAL_END:
			theHeartbeat->schemaClass = "hbeat";
			theHeartbeat->schemaType = "end";
			MALLOC_FAIL(interval = talloc_asprintf(theHeartbeat, "%d", theService->heartbeatInterval / 60))
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
	
	/* Add standard heartbeat data */
	MALLOC_FAIL(portStr = talloc_asprintf(theHeartbeat, "%d", xp->localConnPort))
	addMessageNamedValue(theHeartbeat, "port", portStr);
	addMessageNamedValue(theHeartbeat, "remote-ip", xp->remoteIP);
	if (theService->serviceVersion) {
		addMessageNamedValue(theHeartbeat, "version", theService->serviceVersion);
	}    
  return theHeartbeat;
}

 
/*
 * Send a standard XPL Heartbeat immediately 
 */
 
static Bool sendHeartbeat(xplObjPtr_t xp, xplServicePtr_t theService)
{
	xplMessagePtr_t theHeartbeat;

	/* Create the Heartbeat message, if needed */
	if (!theService->heartbeatMessage){
		/* Configure the heartbeat */
		if (theService->configurableService && !theService->serviceConfigured){
			theHeartbeat = createHeartbeatMessage(xp, theService, HBEAT_CONFIG);
		} 
		else {
			theHeartbeat = createHeartbeatMessage(xp, theService, HBEAT_NORMAL);
		}

		/* Install a new heartbeat message */
		theService->heartbeatMessage = theHeartbeat;
		debug(DEBUG_ACTION, "%s: Just allocated a new Heartbeat message for the service", __func__);
	} 
	else {
		theHeartbeat = theService->heartbeatMessage;
	}
    
	/* Send the message */
	if (!sendMessage(xp, theHeartbeat)){
		return FALSE;
	}

	/* Update last heartbeat time */
	theService->lastHeartbeatAt = time(NULL);
	
	/* Reset the heartbeat timer */
	theService->heartbeatTimer = theService->heartbeatInterval;
	 
	debug(DEBUG_ACTION, "Sent Heatbeat message");
	return TRUE;
}


/* 
 * Send an Goodbye XPL Heartbeat immediately
 */
 
static Bool sendGoodbyeHeartbeat(xplObjPtr_t xp, xplServicePtr_t theService)
{
	xplMessagePtr_t theHeartbeat;
  
	/* Create a shutdown message */
	if (theService->configurableService && !theService->serviceConfigured){
		theHeartbeat = createHeartbeatMessage(xp, theService, HBEAT_CONFIG_END);
	}
	else{
		theHeartbeat = createHeartbeatMessage(xp, theService, HBEAT_NORMAL_END);
	}
    
	/* Send the message */
	if (!sendMessage(xp, theHeartbeat)){
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
 */

static int parseBlock(void *nvListContext, xplNameValueLEPtr_t *nvListHead, xplNameValueLEPtr_t *nvListTail,
String theText, String blockHeader, int blockHeaderLength, Bool forceUpperCase)
{
	int curState = 0, curIndex, theLength = strlen(theText), charCount = 0;
	char theChar;
	char nb[32];
	char vb[32];
	Bool isBinaryValue = FALSE, blockStarted = FALSE;
	xplNameValueLEPtr_t theNameValue;

	


	/* Parse character by character */
	for (curIndex = 0; curIndex < theLength; curIndex++){
		theChar = theText[curIndex];

		/* Convert identifiers to upper case */
		
		if (forceUpperCase && (theChar >= 97) && (theChar <= 122)){
			theChar -= 32;
		}
    
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
					isBinaryValue = FALSE;
					charCount = 0;
					curState = 4;
					continue;
				}

				/* Handle end of binary name */
				if (theChar == '!'){
					nb[0] = '\0'; /* Binary not supported yet */
					isBinaryValue = TRUE;
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
					theNameValue->isBinary = isBinaryValue;
					if (!isBinaryValue){
						MALLOC_FAIL(theNameValue->itemValue = talloc_strdup(theNameValue, vb))
						MALLOC_FAIL(theNameValue->itemName = talloc_strdup(theNameValue, nb))
					}
					else{
							debug(DEBUG_UNEXPECTED, "Unsupported binary name/value pair");
							return -curIndex;
						}

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
 * If they are all found and valid, then 
 * 
 * we return TRUE.  Otherwise, FALSE.
 */

static Bool parseMessageHeader(xplObjPtr_t xp, xplMessagePtr_t theMessage, xplNameValueLEPtr_t nameValueList)
{
	int hopCount;
	xplNameValueLEPtr_t theNameValue;
	String theVendor, theDeviceID, theInstanceID;

	/* Parse the hop count */
	if(!(theNameValue = getNamedValue(nameValueList, "HOP"))){
		debug(DEBUG_UNEXPECTED, "Message missing HOP count");
		return FALSE;
	}
	if(FAIL == UtilStoi(theNameValue->itemValue, &hopCount) || (hopCount < 1)){
		debug(DEBUG_UNEXPECTED, "Message HOP Count invalid");
		return FALSE;
	}
	theMessage->hopCount = hopCount;

	/* Parse the source */
	if(!(theNameValue = getNamedValue(nameValueList, "SOURCE"))){
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
	MALLOC_FAIL(theMessage->sourceVendor = talloc_strdup(theMessage, theVendor))
	MALLOC_FAIL(theMessage->sourceDeviceID = talloc_strdup(theMessage, theDeviceID))
	MALLOC_FAIL(theMessage->sourceInstanceID = talloc_strdup(theMessage, theInstanceID))

	/* Release mangled copy of source tag */
	talloc_free(theVendor);

	/* Parse the target (if anything) */
	if ((theNameValue = getNamedValue(nameValueList, "TARGET")) == NULL) {
		debug(DEBUG_UNEXPECTED, "Message missing TARGET");
		return FALSE;
	}

	/* Parse the target */
  
	/* Check for a wildcard */
	if(!strcmp(theNameValue->itemValue, "*")){
		theMessage->isBroadcastMessage = TRUE;
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
		MALLOC_FAIL(theMessage->targetVendor = talloc_strdup(theMessage, theVendor))
		MALLOC_FAIL(theMessage->targetDeviceID = talloc_strdup(theMessage, theDeviceID))
		MALLOC_FAIL(theMessage->targetInstanceID = talloc_strdup(theMessage, theInstanceID))


		/* Release mangled string */
		talloc_free(theVendor);
 
	}

	/* Header parsed OK */
	return TRUE;
}

/* 
 * Convert a text message into a xPL message.  Return the message
 * or NULL if there is a parse error
 */
 
static xplMessagePtr_t parseMessage(xplObjPtr_t xp, String theText) {
	int parsedThisTime;
	String blockDelimPtr,classType;
	xplNameValueLEPtr_t listHead = NULL, listTail = NULL;
	void *listCTX;
	xplMessagePtr_t theMessage;
	char blockHeader[64];
	
  
	/* Allocate a message */
	theMessage = createReceivedMessage(xp, XPL_MESSAGE_ANY);
	
	/* Allocate the header list context so we can easily free it later */
	MALLOC_FAIL(listCTX = talloc_new(xp->generalPool))

	/* Parse the header */
	if ((parsedThisTime = parseBlock(listCTX, &listHead, &listTail, theText, blockHeader, 64, FALSE)) <= 0) {
		debug(DEBUG_UNEXPECTED, "Error parsing message header");
		releaseMessage(theMessage);
		return NULL;
	}


	/* Parse the header */
	if (!UtilStrcmpIgnoreCase(blockHeader, "XPL-CMND")){
		theMessage->messageType = XPL_MESSAGE_COMMAND;
	} 
	else if(!UtilStrcmpIgnoreCase(blockHeader, "XPL-STAT")){
		theMessage->messageType = XPL_MESSAGE_STATUS;
	} 
	else if(!UtilStrcmpIgnoreCase(blockHeader, "XPL-TRIG")){
		theMessage->messageType = XPL_MESSAGE_TRIGGER;
	}
	else{
		debug(DEBUG_UNEXPECTED, "Unknown message header of %s - bad message", blockHeader);
		releaseMessage(theMessage);
		return NULL;
	}
	
	/* Must have a header name value list. */
	if(!listHead){
		debug(DEBUG_UNEXPECTED, "No name value list for header");
		releaseMessage(theMessage);
		return NULL;
	}
	

	/* Parse the message header name/values into the message */
	if (!parseMessageHeader(xp, theMessage, listHead)){
		debug(DEBUG_UNEXPECTED, "Unable to parse message header");
		releaseMessage(theMessage);
		return NULL;
	}
	
	/* Free the header name value list, and invalidate the pointers */
	
	talloc_free(listCTX);
	listCTX = listHead = listTail = NULL;


	/* Parse the next block */
	if ((parsedThisTime = parseBlock(theMessage, &theMessage->nvHead, &theMessage->nvTail,
	theText + parsedThisTime, blockHeader, 64, FALSE)) < 0){
		debug(DEBUG_UNEXPECTED, "Error parsing message block");
		releaseMessage(theMessage);
		return NULL;
	}
    
    MALLOC_FAIL(classType = talloc_strdup(xp->generalPool, blockHeader))
	
	/* Parse the block header */
	if ((blockDelimPtr = strchr(classType, '.')) == NULL) {
		debug(DEBUG_UNEXPECTED, "Malformed message block header - %s", blockHeader);
		releaseMessage(theMessage); 
		talloc_free(classType); 
		return NULL;
	}
	*blockDelimPtr++ = '\0';

	/* Record the message schema class/type */
	MALLOC_FAIL(theMessage->schemaClass = talloc_strdup(theMessage, classType))
	MALLOC_FAIL(theMessage->schemaType = talloc_strdup(theMessage, blockDelimPtr))	
	
	talloc_free(classType);
	
	/* Return the message */
	return theMessage;
}


/*
 **************************************************************************
 *  Private Service Functions
 **************************************************************************
 */
 
/* 
 * Change the current heartbeat interval 
 */
 
static void setHeartbeatInterval(xplServicePtr_t theService, int newInterval)
{
	/* Skip out of range values */
	if ((newInterval < 0) || (newInterval > 172800)){
		return;
	}
	theService->heartbeatInterval = newInterval;
}
/*
 * Create an XPL service 
 */
 
static xplServicePtr_t createService(xplObjPtr_t xp, String theVendor, String theDeviceID, String theInstanceID) 
{
	xplServicePtr_t theService;
	
	/* Allocate space for the service object */
	MALLOC_FAIL(theService = talloc_zero(xp->generalPool, xplService_t))
	
	/* Install reference to master object */
	theService->xplObj = xp;

	/* Install info */
	MALLOC_FAIL(theService->serviceVendor = talloc_strdup(theService, theVendor))
	MALLOC_FAIL(theService->serviceDeviceID = talloc_strdup(theService, theDeviceID))
	MALLOC_FAIL(theService->serviceInstanceID = talloc_strdup(theService, theInstanceID))
	
	setHeartbeatInterval(theService, DEFAULT_HEARTBEAT_INTERVAL);
	
	/* Validate the object */
	theService->magic = XS_MAGIC;
	return theService;
}

/* 
 * Set service state
 */
 
static void setServiceState(xplObjPtr_t xp, xplServicePtr_t theService, Bool newState)
{
	
	/* Skip if there's no change to the enable state */
	if (theService->serviceEnabled == newState){
		return;
	}

	/* Set the new service state */
	theService->serviceEnabled = newState;

	/* Handle enabling a disabled service */
	if (newState){
		/* If there is an existing heartbeat message, release it, so it will get rebuilt */
		if (theService->heartbeatMessage){
			talloc_free(theService->heartbeatMessage);
			theService->heartbeatMessage = NULL;
		}
		/* Start sending heartbeats */
		sendHeartbeat(xp, theService);
	} else {
		/* Send goodbye heartbeat */
		sendGoodbyeHeartbeat(xp, theService);
	}
}

/* 
 * Send a message out from this service.  
 * If the message has not had it's
 * source set or the source does not match the sending service, it is
 * updated and the message sent
 */
#if(0) 
static Bool sendServiceMessage(xplObjPtr_t xp, xplServicePtr_t theService, xplMessagePtr_t theMessage)
{
	if ((theService == NULL) || (theMessage == NULL)){
	  return FALSE;
	}

	/* Ensure the message comes from this service */
	theMessage->sourceVendor = theService->serviceVendor;
	theMessage->sourceDeviceID = theService->serviceDeviceID;
	theMessage->sourceInstanceID = theService->serviceInstanceID;

	return sendMessage(xp, theMessage);
}
#endif


/*
 **************************************************************************
 *  Public interface functions
 **************************************************************************
 */


/*
 * Destroy an XPL object
 */
void XplDestroy(void *xplObj)
{
	xplObjPtr_t xp = xplObj;
	
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
 

void *XplInit(TALLOC_CTX *ctx, void *Poller, String IPAddr, unsigned port)
{
	xplObjPtr_t xp;
	char portStr[6];
	char interfaceAddr[INET6_ADDRSTRLEN];
	char broadcastAddr[INET6_ADDRSTRLEN];
	struct ifaddrs *interfaceList = NULL, *curIFEntry = NULL;
	void *addrPtr;
	int res;
	int addrFamily;
	Bool found = FALSE;
	
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
			inet_ntop(curIFEntry->ifa_addr->sa_family, addrPtr, interfaceAddr, sizeof(interfaceAddr));
		
			if(addrFamily == AF_INET){
				if(!curIFEntry->ifa_ifu.ifu_broadaddr){
					continue; /* IPV4 must have a broadcast address */
				}
			}
			else{ 
				continue; /* IPV6 Not supported yet */
			}
			if(strcmp(IPAddr, interfaceAddr)){ /* Address must match that passed in */
				continue;
			}	
			/* We have a useable address */
			found = TRUE;	
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
	
	/* Convert port number to string */
	snprintf(portStr, 6, "%u", port);
	
	/* Allocate the object */
	MALLOC_FAIL(xp = talloc_zero(ctx, xplObj_t))

	/* Invalidate the FD's */
	xp->localConnFD = xp->rxReadyFD = xp->broadcastFD = -1;
	
	/* Save the internal IP address */
	MALLOC_FAIL(xp->internalIP = talloc_strdup(xp, (addrFamily == AF_INET6) ? "::" : "0.0.0.0"))
	/* Save the broadcast IP address */
	MALLOC_FAIL(xp->broadcastIP = talloc_strdup(xp, broadcastAddr )) 
	/* Save the remote IP address */
	MALLOC_FAIL(xp->remoteIP = talloc_strdup(xp, interfaceAddr ))
	
	/* Release the getifaddrs info */
	freeifaddrs(interfaceList);   
	
	/* Save the poller object passed in */
	xp->poller = Poller;
	
	
	/* Allocate the raw message buffer */
	MALLOC_FAIL(xp->txBuff = talloc_zero_array(xp, char, MSG_MAX_SIZE))
	
	/* Allocate a working string pool */
	MALLOC_FAIL(xp->generalPool = talloc_pool(xp, GENERAL_POOL_SIZE))

	
	/* Validate the object */
	xp->magic = XP_MAGIC;
	
	/* Get socket for local interface */
	if((FAIL == SocketCreate(xp->internalIP, "0", addrFamily, SOCK_DGRAM, xp, addLocalSock)) || (xp->localConnFD < 0)){
		fatal("%s: Could not create socket for local interface", __func__);
	}
	
	/* Get socket for broadcast interface */
	if((FAIL == SocketCreate(xp->broadcastIP, portStr, addrFamily, SOCK_DGRAM, xp, addBroadcastSock)) || (xp->broadcastFD < 0)){
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
/*
 * Create a new service object
 * Service will be created in the disabled state.
 */
 
void *XplNewService(void *xplObj, String theVendor, String theDeviceID, String theInstanceID)
{	
	xplServicePtr_t xs;
	xplObjPtr_t xp = xplObj;
	ASSERT_FAIL(xp)
	ASSERT_FAIL(XP_MAGIC == xp->magic)
	ASSERT_FAIL(theVendor)
	ASSERT_FAIL(theDeviceID)
	ASSERT_FAIL(theInstanceID)
	
	xs = createService(xp, theVendor, theDeviceID, theInstanceID);
	
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
 * The service should be disabled before this is called. 
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
 */
 
void XplEnableService(void *servToEnable)
{
	xplServicePtr_t xs = servToEnable;
	xplObjPtr_t xp;
	
	ASSERT_FAIL(xs)
	ASSERT_FAIL(XS_MAGIC == xs->magic)
	ASSERT_FAIL(xp = xs->xplObj)
	ASSERT_FAIL(XP_MAGIC == xp->magic)

	setServiceState(xp, xs, TRUE);
}

/*
 * Disable a previously created service object.
 * Sends a goodbye heartbeat message, and prevents
 * message monitor callbacks from being called.
 */
 
void XplDisableService(void *servToDisable)
{
	xplObjPtr_t xp;
	xplServicePtr_t xs = servToDisable;
	
	ASSERT_FAIL(xs)
	ASSERT_FAIL(XS_MAGIC == xs->magic)
	ASSERT_FAIL(xp = xs->xplObj)
	ASSERT_FAIL(XP_MAGIC == xp->magic)
	
	setServiceState(xp, xs, FALSE);
}

/*
 * Create a new message block
 */
 
void *XplInitMessage(void *XPLServ, XPLMessageType_t messageType, 
String theVendor, String theDeviceID, String theInstanceID)
{
	xplServicePtr_t xs = XPLServ;
	ASSERT_FAIL(xs)
	ASSERT_FAIL(XS_MAGIC == xs->magic)
	if(!theVendor){
		/* Is to be a broadcast message */
		return createBroadcastMessage(xs, messageType); 
	}
	else{
		/* Is to be a targetted message */
		ASSERT_FAIL(theDeviceID)
		ASSERT_FAIL(theInstanceID)
		return createTargetedMessage(xs, messageType, theVendor, theDeviceID, theInstanceID);
	}
	 
		
	
	
}

/*
 * Destroy an existing message block
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
 */
 

void XplSetMessageClassType(void *xplMessage, const String theClass, const String theType)
{
	
	
	xplMessagePtr_t xm = xplMessage;
	ASSERT_FAIL(xm);
	ASSERT_FAIL(XM_MAGIC == xm->magic)
	ASSERT_FAIL(xm->serviceObj)

	if(theClass){
		STR_FREE(xm->schemaClass);
		MALLOC_FAIL(xm->schemaClass = talloc_strdup(xplMessage, theClass))
	}	
	if(theType){
		STR_FREE(xm->schemaType);
		MALLOC_FAIL(xm->schemaType = talloc_strdup(xplMessage, theType))
	}
}

/* 
 * Delete Message Name/Value list
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
 */
 
Bool XplSendMessage(void *XPLMessage)
{
	xplObjPtr_t xp;
	xplMessagePtr_t xm = XPLMessage;
	ASSERT_FAIL(xm) /* Object must exist */
	ASSERT_FAIL(XM_MAGIC == xm->magic) /* Object must be valid */
	ASSERT_FAIL(xm->serviceObj) /* Message must be sendable */
	xp = xm->xplObj; /* Re-create pointer to master object */
	ASSERT_FAIL(xp) /* Master object must exist */
	ASSERT_FAIL(XP_MAGIC == xp->magic) /* Master object must be valid */
	ASSERT_FAIL(xm->schemaClass)
	ASSERT_FAIL(xm->schemaType)
	return sendMessage(xp, xm);	
}

/*
 * Add a message listener function to the service
 */
 
void XplAddMessageListener(void *XPLService, XPLListenerReportMode_t reportMode, void *userObj, XPLListenerFunc_t listener)
{
	xplServicePtr_t xs = XPLService;
	ASSERT_FAIL(xs); /* Object must exist */
	ASSERT_FAIL(XS_MAGIC == xs->magic) /* Object must be valid */
	ASSERT_FAIL(listener) /* Must have supplied a listening function */
	xs->reportMode = reportMode;
	xs->userListenerObject = userObj;
	xs->listener = listener;
}

/*
 * Remove the message listener from the service
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
 * Return TRUE if the message is a broadcast message
 */

Bool XplMessageIsBroadcast(void *XPLMessage)
{
	xplMessagePtr_t xm = XPLMessage;
	ASSERT_FAIL(xm) /* Object must exist */
	ASSERT_FAIL(XM_MAGIC == xm->magic) /* Object must be valid */
	
	return xm->isBroadcastMessage; /* Return the flag */
}

/*
 * Return TRUE if the message is a received message
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
 * Return a string containing comma-separated list of name-value pairs
 * String must be talloc_freed, when it is no longer required.
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

 
