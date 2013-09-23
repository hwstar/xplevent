/*
*    monitor.c
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
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/fcntl.h>
#include <xPL.h>
#include <sqlite3.h>
#include <talloc.h>
#include  "defs.h"
#include "types.h"
#include "notify.h"
#include "socket.h"
#include "parser.h"
#include "db.h"
#include "util.h"
#include "scheduler.h"
#include "monitor.h"
#include "xplevent.h"


typedef struct connectionData_s {
	MonRcvInfoPtr_t rcvInfo;
} connectionData_t;

typedef connectionData_t * connectionDataPtr_t;


/* Client command codes */

enum {CC_EXEC = 0 };

/*  Client command table */

static String clientCommands[]  = {
	"exec",
	"NULL"
};

/* Instance ID */

static String instanceID;



/*
 * Heartbeat logger. Updates the heartbeat table in the database.
 *
 * Arguments:
 *
 * 1. Hertbeat message pointer
 *
 * Return value:
 *
 * None
 *
 */


static void logHeartBeatMessage(xPL_MessagePtr theMessage)
{

	TALLOC_CTX *log;
	const String vendor = xPL_getSourceVendor(theMessage);
	const String device = xPL_getSourceDeviceID(theMessage);
	const String instance_id = xPL_getSourceInstanceID(theMessage);

	char source[64];
	
	/* Setup */
	MALLOC_FAIL(log = talloc_new(Globals))
	
	snprintf(source, 63, "%s-%s.%s", vendor, device, instance_id);
	debug(DEBUG_EXPECTED,"Heartbeat status message received: vendor = %s, device = %s, instance_id = %s",
	vendor, device, instance_id);
	DBUpdateHeartbeatLog(log, Globals->db, source);
	talloc_free(log);
}

/*
 * Callback for parseHCL for debugging. Prints debug message with key and value.
 *
 * Arguments:
 *
 * 1. Key string
 * 2. Value string.
 *
 * Return value:
 *
 * None
 *
 */


static void kvDump(const String key, const String value)
{
	ASSERT_FAIL(key)
	ASSERT_FAIL(value)
	debug(DEBUG_EXPECTED, "Key = %s, Value = %s", key, value);
}

/*
 * Parse HCL and generate pcode
 *
 * Arguments:
 *
 * 1. Pointer to parse control block
 * 2. Script to parse as a string
 *
 * Return value:
 *
 * Boolean. PASS indicates success. FAIL indicaes failure.
 *
 */
 
static Bool parseHCL(ParseCtrlPtr_t parseCtrl, String hcl)
{
	int res;
	
	ASSERT_FAIL(parseCtrl)
	
	res = ParserParseHCL(parseCtrl, FALSE, hcl);
	
	if(res == FAIL){
		debug(DEBUG_UNEXPECTED,"Parse failed: %s", parseCtrl->failReason);
		if(Globals->exitOnErr){
			exit(-1);
		}
	}
	return res;
}


/*
* Execute pcode
*
* Arguments:
*
* 1. Pcode header pointer
*
* Return value:
*
* Boolean. PASS indicates success, FAIL indicates failure.
*
*/
 

static Bool execPcode(PcodeHeaderPtr_t ph)
{
	int res;
	
	ASSERT_FAIL(ph);
	res = ParserExecPcode(ph);
	if(res == FAIL){
		debug(DEBUG_UNEXPECTED,"Code execution failed: %s", ph->failReason);
		if(Globals->exitOnErr){
			exit(-1);
		}
	}
	return res;
}



/*
* Parse and execute script
*
* Arguments:
*
* 1. Talloc context to hang result off of.
* 2. Script to parse and execute.
*
* Return value:
*
* Boolean. PASS indicates success, FAIL indicates failure.
*
*/
 
 
static int parseAndExecScript(TALLOC_CTX *ctx, String hcl)
{
	ParseCtrlPtr_t parseCtrl;
	PcodeHeaderPtr_t ph;

	Bool res = PASS;
	
	debug(DEBUG_ACTION, "***Parsing***\n %s", hcl);

	parseCtrl = talloc_zero(ctx, ParseCtrl_t);
	MALLOC_FAIL(parseCtrl);
	
	ph = talloc_zero(ctx, PcodeHeader_t);
	MALLOC_FAIL(ph);
	
	/* Add XPL service pointer and database handle */
	ph->xplServicePtr = Globals->xplEventService;
	ph->DB = Globals->db;
	
	
	/* Save pointer to pcode header in parse control block */
	
	parseCtrl->pcodeHeader = ph;
	 
	/* Parse the script */
	
	res = parseHCL(parseCtrl, hcl);

	/* Free the parser data structures */
	
	talloc_free(parseCtrl);
	

	debug(DEBUG_ACTION, "***Parsing complete***");
	
	/* Execute user code */
	
	if(res == PASS){
		execPcode(ph);	
	}
	
	talloc_free(ph);
	
	return res;
}



/*
* Parse and execute based on contents of trigger message.
*
* Arguments:
*
* 1. Pcode header pointer
* 2. Trigger message pointer
* 3. Script to execute.
*
* Return value:
*
* Boolean. PASS indicates success, FAIL indicates failure.
*
*/
 

 
static int parseAndExecTrig(PcodeHeaderPtr_t ph, xPL_MessagePtr triggerMessage, String hcl)
{
	ParseCtrlPtr_t parseCtrl;
	xPL_NameValueListPtr msgBody;
	String classType,sourceAddress; 
	int i;
	Bool res = PASS;
	
	debug(DEBUG_ACTION, "***Parsing***\n %s", hcl);

	parseCtrl = talloc_zero(Globals, ParseCtrl_t);
	MALLOC_FAIL(parseCtrl);
	
	/* Save pointer to pcode header in parse control block */
	
	parseCtrl->pcodeHeader = ph;
	 
	
	/* Initialize and fill %xplnvin */
		
	msgBody = xPL_getMessageBody(triggerMessage);
	
	for(i = 0; msgBody && i < msgBody->namedValueCount; i++){
		if(!msgBody->namedValues[i]->isBinary){
			ParserHashAddKeyValue(ph, ph, "xplnvin", msgBody->namedValues[i]->itemName, msgBody->namedValues[i]->itemValue);
		}
	}

	debug(DEBUG_ACTION, "xplnvin:");
	
	ParserHashWalk(ph, "xplnvin", kvDump);
	
	/* Initialize and fill %xplin */
	
	classType = talloc_asprintf(ph, "%s.%s", 
	xPL_getSchemaClass(triggerMessage),
	xPL_getSchemaType(triggerMessage));
	MALLOC_FAIL(classType);
	ParserHashAddKeyValue(ph, ph, "xplin", "classtype", classType);
	
	sourceAddress= talloc_asprintf(ph,"%s-%s.%s",
	xPL_getSourceVendor(triggerMessage),
	xPL_getSourceInstanceID(triggerMessage),
	xPL_getSourceDeviceID(triggerMessage));
	MALLOC_FAIL(sourceAddress);
	ParserHashAddKeyValue(ph, ph, "xplin", "sourceaddress", sourceAddress);
	
	/* Parse the script */
	
	res = parseHCL(parseCtrl, hcl);
	
	/* Free the parser data structures */
	
	talloc_free(parseCtrl);
	

	debug(DEBUG_ACTION, "***Parsing complete***");
	
	/* Execute user code */
	
	if(res == PASS){
		execPcode(ph);	
	}
	return res;
}

/*
* Execute a trigger script, and return the pcode header using reference provided for further processing
*
* Arguments:
*
* 1. Trigger message pointer.
* 2. Script to execute
* 3. Reference to pcode header pointer
*
* Return value:
*
* Boolean. PASS indicates success, FAIL indicates failure.
*
*/
 

static Bool trigExec(xPL_MessagePtr triggerMessage, const String script, PcodeHeaderPtrPtr_t ph)
{
	Bool res;

	ASSERT_FAIL(triggerMessage)
	ASSERT_FAIL(script)
	ASSERT_FAIL(ph);
	
	
	/* Initialize pcode header */
	
	*ph = talloc_zero(Globals, PcodeHeader_t);
	MALLOC_FAIL(*ph);
	
	/* Set the pointer to the service */
	(*ph)->xplServicePtr = Globals->xplEventService;
	
	/* Set the pointer to the database */
	(*ph)->DB = Globals->db;

	res = parseAndExecTrig(*ph, triggerMessage, script);
		
	return res;
	
}



/*
* We received a message we need to act on. Parse and exec a script based on the trigger message
* 
*
* Arguments:
*
* 1. Pointer to trigger message
* 2. Script name to execute.
*
* Return value:
*
* Boolean. PASS indicates success, FAIL indicates failure.
*
*/
 


static Bool actOnXPLTrig(xPL_MessagePtr triggerMessage, const String trigaction)
{
	Bool res;
	PcodeHeaderPtr_t ph = NULL;

	
	
	ASSERT_FAIL(triggerMessage)
	ASSERT_FAIL(trigaction)
	
	
	res = trigExec(triggerMessage, trigaction, &ph);
	
	talloc_free(ph);
	
	return res;
	
}


/*
* Trigger message check. Run the preprocess script if it exists, then look up the trigger event
* in the database and see if there is a script associated with it. If there is, then execute the
* script and return. Return a string through the source device reference of the source address
* and/or device which sent the trigger message.
*
* Arguments:
*
* 1. Trigger Message pointer
* 2. Reference to a String to store the Source device name
*
* Return value:
*
* None
*
*/
 



static void checkTriggerMessage(xPL_MessagePtr theMessage, String *sourceDevice)
{
	
	PcodeHeaderPtr_t ph;
	String vendor;
	String device;
	String instance_id;
	String schema_class;
	String schema_type; 
	String pScript;
	String subAddress;
	String script = NULL;
	TALLOC_CTX *ctx;
	char source[96];
	char schema[64];
	int errs = 0;

	ASSERT_FAIL(theMessage);
	
	vendor = xPL_getSourceVendor(theMessage);
	device = xPL_getSourceDeviceID(theMessage);
	instance_id = xPL_getSourceInstanceID(theMessage);
	schema_class = xPL_getSchemaClass(theMessage);
	schema_type = xPL_getSchemaType(theMessage); 

	/* Test for valid schema */
	if(!schema_class || !schema_type){
		debug(DEBUG_UNEXPECTED, "logTriggerMessage: Bad or missing schema");
		return;
	}
	
	MALLOC_FAIL(ctx = talloc_new(Globals))
	
	/* Make combined schema string */
	snprintf(schema, 63, "%s.%s", schema_class, schema_type);
	debug(DEBUG_ACTION, "Schema: %s", schema);
	
	/* Get any preprocessing script */
	pScript = DBFetchScript(ctx, Globals->db, "preprocess");
	
	if(!pScript){
		debug(DEBUG_EXPECTED,"Preprocess script not found, using canned subaddress handling");
	

		/* Test for sensor.basic */
		if(!strcmp(schema, "sensor.basic"))
			subAddress = xPL_getMessageNamedValue(theMessage, "device");
	
		/* Test for hvac.zone or security.gateway */
		if((!strcmp(schema, "hvac.zone")) || (!strcmp(schema, "security.gateway")))
			subAddress = xPL_getMessageNamedValue(theMessage, "zone");
	
		/* Make source name with sub-address if available */
		snprintf(source, 63, "%s-%s.%s", vendor, device, instance_id);
		if(subAddress){
			snprintf(source + strlen(source), 31, ":%s", subAddress);
		}	
	}
	else{
		debug(DEBUG_EXPECTED,"Preprocess script found");
		errs = trigExec(theMessage, pScript, &ph);
		if(!errs){
			
		}
		/* See if subaddress is set in the result hash */
		snprintf(source, 63, "%s-%s.%s", vendor, device, instance_id);
		subAddress = ParserHashGetValue(ph, ph, "result", "subaddress");
	
		if(subAddress){
			snprintf(source + strlen(source), 31, ":%s", subAddress);
		}	
		
		/* Free script and pcode */
		talloc_free(pScript);
		talloc_free(ph);
		
	}
	
	if(sourceDevice){ /* Store a copy of the source device tag if so requested */
		*sourceDevice = talloc_strdup(Globals, source);
		ASSERT_FAIL(*sourceDevice);
	}
	
	debug(DEBUG_EXPECTED,"Trigger message received from: %s", source);

	/*
	 * Check to see if this is a trigger message we need to act on
	 */
	 
 
	/* Fetch the script by source tag and sub-address */
	
	script = DBFetchScriptByTag(ctx, Globals->db, source);

		
	/* Execute the script if it exists */ 
	if(script){
		errs = actOnXPLTrig(theMessage, script);
	}
	talloc_free(ctx);
			
}


/*
* Trigger message logging
*
* Arguments:
*
* 1. Trigger message pointer
* 2. Source address and device which generated the trigger message
*
* Return value:
*
* None
*
*/
 


static void logTriggerMessage(xPL_MessagePtr theMessage, String sourceDevice)
{
	ASSERT_FAIL(theMessage);
	
	int i;
	xPL_NameValueListPtr msgBody;
	String schema;
	String nvpairs;
	String schema_class, schema_type;
	TALLOC_CTX *logctx;

	ASSERT_FAIL(theMessage)
	ASSERT_FAIL(sourceDevice);

	/* Allocate a dedicated context off of master */
	
	logctx = talloc_new(Globals);
	MALLOC_FAIL(logctx);

	/* Allocate space for nvpairs */
	nvpairs = talloc_zero(logctx, char);
	MALLOC_FAIL(nvpairs)
	
	/* Grab xPL strings */
	ASSERT_FAIL(theMessage);
	msgBody = xPL_getMessageBody(theMessage);
	ASSERT_FAIL(msgBody);
	schema_class = xPL_getSchemaClass(theMessage);
	ASSERT_FAIL(schema_class);
	schema_type = xPL_getSchemaType(theMessage); 
	ASSERT_FAIL(schema_type);
	
	/* Make combined schema/class */
	schema = talloc_asprintf(logctx, "%s.%s", schema_class, schema_type);
	MALLOC_FAIL(schema);
	
	/*
	 * Update trigger log
	 */
	 
	 
	/* Build name/value pair list */
	if(msgBody){
		for(i = 0; i < msgBody-> namedValueCount; i++){
			if(!msgBody->namedValues[i]->isBinary){
				if(i){
					nvpairs = talloc_asprintf_append(nvpairs, ",");
					MALLOC_FAIL(nvpairs)
				}
				nvpairs = talloc_asprintf_append(nvpairs, "%s=%s",
				msgBody->namedValues[i]->itemName, msgBody->namedValues[i]->itemValue);
				MALLOC_FAIL(nvpairs)
			}
			else{
				debug(DEBUG_UNEXPECTED, "Skipping binary message");
			}
		}
		debug(DEBUG_ACTION, "Name value pairs: %s", nvpairs);
	}
	else{
		debug(DEBUG_UNEXPECTED, "Missing message body");
		nvpairs[0] = 0;
	}
	
	DBUpdateTrigLog(logctx, Globals->db, sourceDevice, schema, nvpairs);
	
	
	talloc_free(logctx);
}

/*
* Our xPL listener. This is called by xPLLIB when a message is received.
*
* Arguments:
*
* 1. Message pointer
* 2. Object pointer (not used here)
*
* Return value:
*
* None
*
*/
  



static void xPLListener(xPL_MessagePtr theMessage, xPL_ObjectPtr userValue)
{
	if(xPL_isBroadcastMessage(theMessage)){ /* If broadcast message */
		int mtype = xPL_getMessageType(theMessage);
		const char *class = xPL_getSchemaClass(theMessage);
		const char *type = xPL_getSchemaType(theMessage);
		if((mtype == xPL_MESSAGE_STATUS) && !strcmp(class, "hbeat") && !strcmp(type, "app")){
			/* Log heartbeat messages */
			logHeartBeatMessage(theMessage);
		}
		else if(mtype == xPL_MESSAGE_TRIGGER){
			String sourceDevice;
			/* Process trigger message */
			checkTriggerMessage(theMessage, &sourceDevice);
			logTriggerMessage(theMessage, sourceDevice);
			talloc_free(sourceDevice);
		}
	}
}

/*
* At exit callback used to shut down the xPL library.
*
* Arguments:
*
* None
*
* Return value:
*
* None
*
*/
 

static void xplShutdown(void)
{
		xPL_setServiceEnabled(Globals->xplEventService, FALSE);
		xPL_releaseService(Globals->xplEventService);
		xPL_shutdown();
}

/*
 * Scheduler handler for executing scripts
 *
 * Arguments:
 *
 * 1. Talloc context to hang transitory data off of.
 * 2. Scheduler entry name as a string.
 * 3. Name of the script to execute as a string.
 *
 * Return value:
 *
 * None
 */

void schedulerExec(TALLOC_CTX *ctx, const String entryName, const String scriptName)
{
	String script;
	
	debug(DEBUG_EXPECTED, "Scheduler exec called: entryName = %s, scriptName = %s", entryName, scriptName);
	script = DBFetchScript(Globals, Globals->db, scriptName);
	if(!script){
		debug(DEBUG_UNEXPECTED, "Script not in database");
	}
	else{
		if(parseAndExecScript(Globals, script) == FAIL){
			debug(DEBUG_UNEXPECTED, "Scheduler script failed");
		}
		talloc_free(script);
	}
	
}



/*
 * Callback from sqlite3 exec to add a scheduler entry to the scheduler
 *
 * Arguments:
 *
 * 1. Talloc context to hang transitory data off of.
 * 2. Number of fields
 * 3. Field values as an array of strings.
 * 4. Field names as an array of strings.
 *
 * Return value:
 *
 * Integer. Always zero to ensure that sqlite exec does not abort.
 */

static int addScheduleEntry(void *data, int argc, String *argv, String *colnames)
{
	ASSERT_FAIL(argc == 4)
	SchedulerAdd(data, argv[1], argv[2], schedulerExec, argv[3]);
	return 0;
}


/*
* Our tick handler. Called by xPL Library once per second.
* We do exit checking, and logging memory usage here.
*
* Arguments:
*
* 1. User value (not used)
* 2. Object pointer (not used)
*
* Return value:
*
* None
*
*/
 

static void tickHandler(int userVal, xPL_ObjectPtr obj)
{

	
	if(!Globals->schInitTried){
		/* Attempt to start the scheduler */
		Globals->schInitTried = TRUE;
		/* Initialize scheduler */
		Globals->sch = SchedulerInit(Globals, Globals->lat, Globals->lon);
		if(DBReadRecords(Globals->sch, Globals->db, Globals->sch, "schedule", 32, addScheduleEntry)){
			debug(DEBUG_UNEXPECTED, "Can't read scheduler table in database. Disabling scheduler");
			talloc_free(Globals->sch);
			Globals->sch = NULL;
		}
		else{
			SchedulerStart(Globals->sch);
		}
	}
	if(Globals->sch){ /* If scheduler initialized */
		/* Run each tick through the scheduler */
		SchedulerDo(Globals->sch);
	}
	
	/* Terminate if requested to do so */
	if(XpleventCheckExit()){
		exit(0);
	}
}

/*
* Interpret a client command
*
* Arguments:
*
* 1. Socket providing a connection to the client.
* 2. Command line from the client.
*
* Return value:
*
* None
*
*/
 
 
static Bool interpretClientCommand(connectionDataPtr_t cdp, int userSock, String cl)
{
	String *argv = UtilSplitString(cdp, cl, ' ');
	int i;
	Bool res = PASS;
	String script;
	
	ASSERT_FAIL(cdp)
	ASSERT_FAIL(cl)
	
	for(i = 0; clientCommands[i]; i++){
		if(!strcmp(clientCommands[i], argv[0])){
			break;
		}
	}
	if(clientCommands[i]){
		switch(i){
			case CC_EXEC:
				if(argv[1]){
					debug(DEBUG_EXPECTED, "Exec: %s", argv[1]);
					if(!(script = DBFetchScript(cdp, Globals->db, argv[1]))){
							res = FAIL;
					}
					else{
						res = parseAndExecScript(cdp, script);	
					}
				}
				else{
					res = FAIL;
				}	
				break;
				
			default:
				ASSERT_FAIL(0);
		}
		
	}
	else{
		debug(DEBUG_UNEXPECTED, "Wacky client command: %s", cl);
		res = FAIL;
	}

	SocketPrintf(argv, userSock,"%s\n", (res == PASS) ? "ok:" : "er:Command not recognized");

	talloc_free(argv);
	return res;
}


/*
* Client command data is ready. Callback from xPL library
*
* Arguments:
*
* 1. Socket connection to client.
* 2. Read events (not used)
* 3. User value (set to connection data pointer).
*
* Return value:
*
* None
*
*/
 

static void clientCommandListener(int userSock, int revents, int uservalue)
{
	unsigned length = 0;
	String line, theScript;
	connectionDataPtr_t cdp = (connectionDataPtr_t) uservalue;
	MonRcvInfoPtr_t ri;
	String res = "ok:";
	
	ASSERT_FAIL(cdp)
	
	ri = cdp->rcvInfo; /* Shorthand reference */
	
	
	if((line = SocketReadLine(cdp, userSock, &length)) == NULL){
		debug(DEBUG_UNEXPECTED, "Could not read socket");
	}
	else{
		if(length){
			if(ri){ /* If in the midst of receiving a script */
				if(MonitorRecvScript(ri, line)){
					/* Process script */
					debug(DEBUG_EXPECTED,"Script %s received, result = %d", ri->name, ri->state);
					
					if(ri->state != RS_ERROR){
						if(DBIRScript(ri, Globals->db, 
						ri->name, ri->script)){
							debug(DEBUG_UNEXPECTED, "Error while saving script");
							res = "er:Could not save script";
						}
					}
					else{
						res = "er:Script receive error";
					}
					/* Sand status back to client */
					debug(DEBUG_ACTION, "Sending response: %s", res);
					SocketPrintf(ri, userSock, "%s\n", res);
					debug(DEBUG_ACTION, "Response sent");
					/* Done with the received script, free the data structure and the underlying buffer */
					talloc_free(ri);
					ri = cdp->rcvInfo = NULL;
				}
				
			}
			else{		
				debug(DEBUG_ACTION, "Line read from socket: %s", line); 
				if(!strncmp("cl:", line, 3)){ /* Command line */
					interpretClientCommand(cdp, userSock, line + 3);
				}
				if(!strncmp("ss:", line, 3)){ /* Send script */
					theScript = DBFetchScript(cdp, Globals->db, line + 3);
					if(theScript){
						MonitorSendScript(cdp, userSock, theScript, line + 3);
					}
					else{
						SocketPrintf(cdp, userSock, "er:Script not in database\n");
					}
				}
				if(!strncmp("rs:", line, 3)){ /* Receive script */
					debug(DEBUG_ACTION, "Receive script command");
					MALLOC_FAIL(ri = talloc_zero(cdp, MonRcvInfo_t))
					ri->scriptBufSize = 2048; /* Starting buffer size */
					ri->scriptSizeLimit = 65536; /* Maximum buffer size */
					MALLOC_FAIL(ri->script = talloc_array(ri, char, ri->scriptBufSize)) /* Allocate the initial buffer */
					ri->script[0] = 0; /* Set to zero length */
					cdp->rcvInfo = ri; /* Activate the receiver */
				}
			}
		}
	}
	
	if(line){
		talloc_free(line);
	}
	if(!length){ 
		/* EOF or ERROR */
		/* Remove the socket from the polling list and close it */
		debug(DEBUG_ACTION, "Removing socket from poll list, close the socket, and free the persistent connection data");
		xPL_removeIODevice(userSock); /* Remove listener from the polling list */
		close(userSock); /* Close the socket */
		talloc_free(cdp); /* Free persistent connection data */
	}
}

/*
* Read data ready on one of the command sockets. Callback from xPL library.
*
* Arguments:
*
* 1. Socket connection to client.
* 2. Read events (not used)
* 3. User value (not used)
*
* Return value:
*
* None
*
*/
 
static void commandSocketListener(int fd, int revents, int uservalue)
{
	connectionDataPtr_t cdp;
	int userSock;
	
	debug(DEBUG_ACTION, "Accepting socket connection");
	/* Accept the user connection. */
	userSock = accept4(fd, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
	if(userSock == -1) {
		debug(DEBUG_UNEXPECTED, "Could not accept socket");
		return;
	}

	/* Allocate a data structure for connection persistent data for use by the listener */
	ASSERT_FAIL(cdp = talloc_zero(Globals, connectionData_t))
	
	/* Add the accepted socket to the polling list */
	xPL_addIODevice(clientCommandListener, (int) cdp, userSock, TRUE, FALSE, FALSE);
}

/*
* Call back to add command sockets. Called from SocketCreateListenList.
* Calls the xPL library to add the listening socket to the poll list.
*
* Arguments:
*
* 1. Listening socket to add
* 2. Address family
* 3. Socket type
*
* Return value:
*
* 0 indicates success. Nonzero indicates failure.
*
*/
 
static int addIPSocket(int sock, void *addr, int family, int socktype)
{
	void *p;
	char addrstr[INET6_ADDRSTRLEN];

	if(Globals->debugLvl > 1){
		p = SocketFixAddrPointer(addr);
		inet_ntop(family, p, addrstr, sizeof(addrstr));
		debug(DEBUG_EXPECTED, "Monitor Socket listen ip address: %s", addrstr);
	}
	return xPL_addIODevice(commandSocketListener, 0, sock, TRUE, FALSE, FALSE);

}


/*
* Send a script
*
* Arguments:
* 
* 1. Talloc context to hang termprary data off of.
* 2. Socket providing a connection to the client.
* 3. Script as a string
*
* Return value:
*
* None
*
*/

void MonitorSendScript(TALLOC_CTX *ctx, int userSock, String theScript, String id)
{
	String *lines;
	int i;
	
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(theScript)
	
	SocketPrintf(ctx, userSock, "sb:%s\n", id);
	lines = UtilSplitString(ctx, theScript, '\n');
	for(i = 0; lines[i]; i++){
		SocketPrintf(ctx, userSock, "sl:%s\n", lines[i]); 
	}
	SocketPrintf(ctx, userSock, "se:%s\n", id);
	talloc_free(lines);
}
/*
* Receive a script
*
* Arguments:
* 
* 1. Pointer to a receive state info structure
* 2. Line received
* 
*
* Return value:
*
* TRUE if script has been received, otherwise FALSE
*
*/
Bool MonitorRecvScript(MonRcvInfoPtr_t ri, String line) 
{
	char *p;
	int len;
	ASSERT_FAIL(ri)
	ASSERT_FAIL(line)
	
	len = strlen(line);
	if((len < 3) || (len > 258)){ /* Check to see that the line length is within limits */
		debug(DEBUG_UNEXPECTED,"Line length too long or too short");
		MALLOC_FAIL(ri->errMsg = talloc_asprintf(ri, "Invalid line lingth: %d", len))
		ri->state = RS_ERROR;
		return TRUE;
	}
	
	if(!strncmp("er:", line, 3)){ /* Will get this if other side terminates */
		debug(DEBUG_UNEXPECTED,"Receive terminated: ");
		MALLOC_FAIL(ri->errMsg = talloc_asprintf(ri, "Receive terminated"))
		ri->state = RS_ERROR;
		return TRUE;
	}

	switch(ri->state){
		case RS_IDLE:
			/* Wait for start of script */
			if(!strncmp("sb:", line, 3)){
				debug(DEBUG_EXPECTED,"Received script start. Name = %s", line + 3);
				MALLOC_FAIL(ri->name = talloc_strdup(ri, line + 3))
				ri->state = RS_WAIT_LINE;
				return FALSE;
			}
			break;
		
		case RS_WAIT_LINE:
			if(!strncmp("se:", line, 3)){
				/* Script end detected */
				debug(DEBUG_EXPECTED,"Script received");
				ri->state = RS_FINISHED;
				return TRUE;
			}
			else if (!strncmp("sl:", line, 3)){
				/* Script line detected */
				p = line + 3;
				len -= 3;
				/* Check to see we don't hit the buffer size limit */
				if(ri->scriptLen + len >= ri->scriptSizeLimit){
					debug(DEBUG_UNEXPECTED, "Script size exceeds limit");
					MALLOC_FAIL(ri->errMsg = talloc_asprintf(ri, "Script size exceeds limit of %d bytes", 
					ri->scriptSizeLimit))
					ri->state = RS_ERROR; /* Upload size exceeded */
					return TRUE;
				}
				/* Check to see if we need to increase the buffer size */
				if(ri->scriptLen + len + 2 >= ri->scriptBufSize){
					ri->scriptBufSize <<= 1; /* Increase buffer size */
					debug(DEBUG_ACTION,"Increasing buffer size to: %u", ri->scriptBufSize);
					MALLOC_FAIL(ri->script = talloc_realloc(ri, ri->script, 
					char, ri->scriptBufSize))
				}
				/* Insert line into buffer */
				ri->scriptLen += snprintf(ri->script + ri->scriptLen, len + 2, "%s\n", p);
			}
			break;
			
		case RS_FINISHED:
		case RS_ERROR:
			return TRUE; 
			
		default:
			ASSERT_FAIL(0);
	}
	
	return FALSE;
}



/*
*
* Pre setup of monitor code.
*
* Arguments:
*
* 1. Interface to listen on as a string. 
* 2. Instance ID to use.
*
* Return value:
*
* None
*
*/

void MonitorPreForkSetup(String interface, String instance_id)
{
	/* Set the xPL interface */
	xPL_setBroadcastInterface(interface);
	instanceID = instance_id;
	
}


/*
* Run monitor. This function sets up the xPL service, registers the necessaty handlers,
* enables the xPL service, and registers the cleanup code. It then calls the xPL library
* message processing function. This function never returns.
*
* Arguments:
*
* None
*
* Return value:
*
* None
*/

void MonitorRun(void)
{
	
	
	/* Start xPL up */
	if (!xPL_initialize(xPL_getParsedConnectionType())) {
		fatal("Unable to start xPL lib");
	}
	
	/* Turn on library debugging for level 5 */
	if(Globals->debugLvl >= 5)
		xPL_setDebugging(TRUE);
		
	/* Create a service and set our application version */
	Globals->xplEventService = xPL_createService("hwstar", "xplevent", instanceID);
  	xPL_setServiceVersion(Globals->xplEventService, VERSION);


	/* Add 1 second tick service */
	xPL_addTimeoutHandler(tickHandler, 1, NULL);

  	/* And a listener for all xPL messages */
  	xPL_addMessageListener(xPLListener, NULL);
  	

	/* Add a listener for the command socket */
	if(SocketCreateListenList(Globals->cmdBindAddress, Globals->cmdService, AF_UNSPEC, SOCK_STREAM, addIPSocket ) == FAIL){
		fatal("Can't create listening socket(s)");
	}	


 	/* Enable the service */
  	xPL_setServiceEnabled(Globals->xplEventService, TRUE);

	atexit(xplShutdown);
	

	
 	/** Main Loop **/

	for (;;) {
		/* Let XPL run forever */
		xPL_processMessages(-1);
  	}
}

