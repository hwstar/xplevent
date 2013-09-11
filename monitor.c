/*
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

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <ctype.h>
#include <getopt.h>
#include <limits.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <xPL.h>
#include <sqlite3.h>
#include <talloc.h>
#include  "defs.h"
#include "types.h"
#include "notify.h"
#include "parser.h"
#include "db.h"
#include "xplevent.h"



/*
 * Heartbeat logger
 */


static void logHeartBeatMessage(xPL_MessagePtr theMessage)
{

	TALLOC_CTX *log;
	const String vendor = xPL_getSourceVendor(theMessage);
	const String device = xPL_getSourceDeviceID(theMessage);
	const String instance_id = xPL_getSourceInstanceID(theMessage);

	char source[64];
	
	/* Setup */
	MALLOC_FAIL(log = talloc_new(Globals->masterCTX))
	
	snprintf(source, 63, "%s-%s.%s", vendor, device, instance_id);
	debug(DEBUG_EXPECTED,"Heartbeat status message received: vendor = %s, device = %s, instance_id = %s",
	vendor, device, instance_id);
	DBUpdateHeartbeatLog(log, Globals->db, source);
	talloc_free(log);
}

/*
 * Callback for parseHCL for debugging
 */


static void kvDump(const String key, const String value)
{
	ASSERT_FAIL(key)
	ASSERT_FAIL(value)
	debug(DEBUG_EXPECTED, "Key = %s, Value = %s", key, value);
}

/*
 * Parse and execute based on contents of trigger message
 */
 
static int parseAndExec(pcodeHeaderPtr_t ph, xPL_MessagePtr triggerMessage, String hcl)
{
	ParseCtrlPtr_t parseCtrl;
	xPL_NameValueListPtr msgBody;
	String classType,sourceAddress; 
	int i;
	Bool res = PASS;
	
	debug(DEBUG_ACTION, "***Parsing***\n %s", hcl);

	parseCtrl = talloc_zero(Globals->masterCTX, ParseCtrl_t);
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
	
	/* Parse user code */
	
	res = ParserParseHCL(parseCtrl, FALSE, hcl);
	
	if(parseCtrl->failReason){
		debug(DEBUG_UNEXPECTED,"Parse failed: %s", parseCtrl->failReason);
		if(Globals->exitOnErr){
			exit(-1);
		}
	}
	
	talloc_free(parseCtrl);
	

	debug(DEBUG_ACTION, "***Parsing complete***");
	
	/* Execute user code */
	
	if(res == PASS){
		res = ParserExecPcode(ph);
		if(res == FAIL){
			debug(DEBUG_UNEXPECTED,"Code execution failed: %s", ph->failReason);
			if(Globals->exitOnErr){
				exit(-1);
			}
		}
		
	}
	return res;
}

/*
 * Execute a trigger script, and return the pcode header for further processing
 */

static Bool trigExec(xPL_MessagePtr triggerMessage, const String script, pcodeHeaderPtrPtr_t ph)
{
	Bool res;

	ASSERT_FAIL(triggerMessage)
	ASSERT_FAIL(script)
	ASSERT_FAIL(ph);
	
	
	/* Initialize pcode header */
	
	*ph = talloc_zero(Globals->masterCTX, pcodeHeader_t);
	MALLOC_FAIL(*ph);
	
	/* Set the pointer to the service */
	(*ph)->xplServicePtr = Globals->xplEventService;
	
	/* Set the pointer to the database */
	(*ph)->DB = Globals->db;

	res = parseAndExec(*ph, triggerMessage, script);
		
	return res;
	
}



/*
 * We received a message we need to act on.
 * 
 * Parse the string passed in
 */

static Bool actOnXPLTrig(xPL_MessagePtr triggerMessage, const String trigaction)
{
	Bool res;
	pcodeHeaderPtr_t ph = NULL;

	
	
	ASSERT_FAIL(triggerMessage)
	ASSERT_FAIL(trigaction)
	
	
	res = trigExec(triggerMessage, trigaction, &ph);
	
	talloc_free(ph);
	
	return res;
	
}


/*
 * Trigger message check
 */


static void checkTriggerMessage(xPL_MessagePtr theMessage, String *sourceDevice)
{
	
	pcodeHeaderPtr_t ph;
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
	
	MALLOC_FAIL(ctx = talloc_new(Globals->masterCTX))
	

	/* Get any preprocessing script */
	pScript = DBFetchScript(ctx, Globals->db, "preprocess");
	
	if(!pScript){
		debug(DEBUG_EXPECTED,"Preprocess script not found, using canned subaddress handling");
		// Make combined schema string;
		snprintf(schema, 63, "%s.%s", schema_class, schema_type);
		debug(DEBUG_ACTION, "Schema: %s", schema);
	
		// Test for sensor.basic
		if(!strcmp(schema, "sensor.basic"))
			subAddress = xPL_getMessageNamedValue(theMessage, "device");
	
		// Test for hvac.zone or security.gateway
		if((!strcmp(schema, "hvac.zone")) || (!strcmp(schema, "security.gateway")))
			subAddress = xPL_getMessageNamedValue(theMessage, "zone");
	
		// Make source name with sub-address if available
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
		*sourceDevice = talloc_strdup(Globals->masterCTX, source);
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
	
	logctx = talloc_new(Globals->masterCTX);
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
 * Our xPL listener
 */


static void xPLListener(xPL_MessagePtr theMessage, xPL_ObjectPtr userValue)
{
	if(xPL_isBroadcastMessage(theMessage)){ /* If broadcast message */
		int mtype = xPL_getMessageType(theMessage);
		const char *class = xPL_getSchemaClass(theMessage);
		const char *type = xPL_getSchemaType(theMessage);
		if((mtype == xPL_MESSAGE_STATUS) && !strcmp(class, "hbeat") && !strcmp(type, "app")){
			// Log heartbeat messages
			logHeartBeatMessage(theMessage);
		}
		else if(mtype == xPL_MESSAGE_TRIGGER){
			String sourceDevice;
			// Process trigger message
			checkTriggerMessage(theMessage, &sourceDevice);
			logTriggerMessage(theMessage, sourceDevice);
			talloc_free(sourceDevice);
		}
	}
}



/*
* Our tick handler. 
* 
*/

static void tickHandler(int userVal, xPL_ObjectPtr obj)
{
	static int ticks = 0;
	
	/* Report memory usage every 30 seconds if enabled and not in daemon mode */
	ticks++;
	if(Globals->noBackground && (ticks >= 30)){
		ticks = 0;
		talloc_report(Globals->masterCTX, stdout);
	}
	
	
	/* Terminate if requested to do so */
	if(Globals->exitRequest){
		xPL_setServiceEnabled(Globals->xplEventService, FALSE);
		xPL_releaseService(Globals->xplEventService);
		xPL_shutdown();
		DBClose(Globals->db);
	
		/* Unlink the pid file if we can. */
		(void) unlink(Globals->pidFile);
		if(Globals->masterCTX){
			TALLOC_CTX *m = Globals->masterCTX;
			talloc_free(m);
		}
		exit(0);
	}
}

void MonitorPreForkSetup(String interface)
{
	/* Set the xPL interface */
	xPL_setBroadcastInterface(interface);
	
}

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
	Globals->xplEventService = xPL_createService("hwstar", "xplevent", Globals->instanceID);
  	xPL_setServiceVersion(Globals->xplEventService, VERSION);


	/* Add 1 second tick service */
	xPL_addTimeoutHandler(tickHandler, 1, NULL);

  	/* And a listener for all xPL messages */
  	xPL_addMessageListener(xPLListener, NULL);


 	/* Enable the service */
  	xPL_setServiceEnabled(Globals->xplEventService, TRUE);



 	/** Main Loop **/

	for (;;) {
		/* Let XPL run forever */
		xPL_processMessages(-1);
  	}
}

