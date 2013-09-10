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
/* Define these if not defined */

#ifndef VERSION
	#define VERSION "X.X.X"
#endif

//#define DEBUG

#ifndef EMAIL
	#define EMAIL "hwstar@rodgers.sdcoxmail.com"
#endif

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
#include "confread.h"
#include "parser.h"
#include "db.h"



#define SHORT_OPTIONS "c:d:ef:hi:l:ns:v"

#define WS_SIZE 256


#define DEF_CONFIG_FILE		"./xplevent.conf"
#define DEF_PID_FILE		"./xplevent.pid"
#define DEF_DB_FILE		"./xplevent.sqlite3"

#define DEF_INTERFACE		"eth1"

#define DEF_INSTANCE_ID		"main"


 
typedef struct cloverrides {
	unsigned pid_file : 1;
	unsigned instance_id : 1;
	unsigned log_path : 1;
	unsigned interface : 1;
	unsigned dbfile : 1;
} clOverride_t;




char *progName;
int debugLvl = 0; 
int NumRows = 0;
Bool exitOnErr = FALSE;
Bool exitRequest = FALSE;



static Bool noBackground = FALSE;

static clOverride_t clOverride = {0,0,0,0};

static TALLOC_CTX *masterCTX = NULL;

static xPL_ServicePtr xpleventService = NULL;
static xPL_MessagePtr xpleventTriggerMessage = NULL;
static ConfigEntryPtr_t	configEntry = NULL;

static void *myDB = NULL;

static char configFile[WS_SIZE] = DEF_CONFIG_FILE;
static char interface[WS_SIZE] = DEF_INTERFACE;
static char logPath[WS_SIZE] = "";
static char instanceID[WS_SIZE] = DEF_INSTANCE_ID;
static char pidFile[WS_SIZE] = DEF_PID_FILE;
static char dbFile[WS_SIZE] = DEF_DB_FILE;



/* Commandline options. */

static struct option longOptions[] = {
	{"config-file", 1, 0, 'c'},
	{"debug", 1, 0, 'd'},
	{"exitonerr",0, 0, 'e'},
	{"pid-file", 0, 0, 'f'},
	{"help", 0, 0, 'h'},
	{"interface", 1, 0, 'i'},
	{"log", 1, 0, 'l'},
	{"no-background", 0, 0, 'n'},	
	{"db-file", 1, 0, 'o'},
	{"instance", 1, 0, 's'},
	{"version", 0, 0, 'v'},
	{0, 0, 0, 0}
};




/* 
 * Get the pid from a pidfile.  Returns the pid or -1 if it couldn't get the
 * pid (either not there, stale, or not accesible).
 */
static pid_t pid_read(char *filename) {
	FILE *file;
	pid_t pid;
	
	/* Get the pid from the file. */
	file=fopen(filename, "r");
	if(!file) {
		return(-1);
	}
	if(fscanf(file, "%d", &pid) != 1) {
		fclose(file);
		return(-1);
	}
	if(fclose(file) != 0) {
		return(-1);
	}
	
	/* Check that a process is running on this pid. */
	if(kill(pid, 0) != 0) {
		
		/* It might just be bad permissions, check to be sure. */
		if(errno == ESRCH) {
			return(-1);
		}
	}
	
	/* Return this pid. */
	return(pid);
}


/* 
 * Write the pid into a pid file.  Returns zero if it worked, non-zero
 * otherwise.
 */
static int pid_write(char *filename, pid_t pid) {
	FILE *file;
	
	/* Create the file. */
	file=fopen(filename, "w");
	if(!file) {
		return -1;
	}
	
	/* Write the pid into the file. */
	(void) fprintf(file, "%d\n", pid);
	if(ferror(file) != 0) {
		(void) fclose(file);
		return -1;
	}
	
	/* Close the file. */
	if(fclose(file) != 0) {
		return -1;
	}
	
	/* We finished ok. */
	return 0;
}


/*
* When the user hits ^C, logically shutdown
* (including telling the network the service is ending)
*/

static void shutdownHandler(int onSignal)
{
	exitRequest = TRUE;
}



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
	ASSERT_FAIL(log = talloc_new(masterCTX))
	
	snprintf(source, 63, "%s-%s.%s", vendor, device, instance_id);
	debug(DEBUG_EXPECTED,"Heartbeat status message received: vendor = %s, device = %s, instance_id = %s",
	vendor, device, instance_id);
	DBUpdateHeartbeatLog(log, myDB, source);
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

	parseCtrl = talloc_zero(masterCTX, ParseCtrl_t);
	ASSERT_FAIL(parseCtrl);
	
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
	ASSERT_FAIL(classType);
	ParserHashAddKeyValue(ph, ph, "xplin", "classtype", classType);
	
	sourceAddress= talloc_asprintf(ph,"%s-%s.%s",
	xPL_getSourceVendor(triggerMessage),
	xPL_getSourceInstanceID(triggerMessage),
	xPL_getSourceDeviceID(triggerMessage));
	ASSERT_FAIL(sourceAddress);
	ParserHashAddKeyValue(ph, ph, "xplin", "sourceaddress", sourceAddress);
	
	/* Parse user code */
	
	res = ParserParseHCL(parseCtrl, FALSE, hcl);
	
	if(parseCtrl->failReason){
		debug(DEBUG_UNEXPECTED,"Parse failed: %s", parseCtrl->failReason);
		if(exitOnErr){
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
			if(exitOnErr){
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
	
	*ph = talloc_zero(masterCTX, pcodeHeader_t);
	ASSERT_FAIL(*ph);
	
	/* Set the pointer to the service */
	(*ph)->xplServicePtr = xpleventService;

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
	
	ASSERT_FAIL(ctx = talloc_new(masterCTX))
	

	/* Get any preprocessing script */
	pScript = DBFetchScript(ctx, myDB, "preprocess");
	
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
		*sourceDevice = talloc_strdup(masterCTX, source);
		ASSERT_FAIL(*sourceDevice);
	}
	
	debug(DEBUG_EXPECTED,"Trigger message received from: %s", source);

	/*
	 * Check to see if this is a trigger message we need to act on
	 */
	 
 
	/* Fetch the script by source tag and sub-address */
	
	script = DBFetchScriptByTag(ctx, myDB, source);

		
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
	
	logctx = talloc_new(masterCTX);
	ASSERT_FAIL(logctx);

	/* Allocate space for nvpairs */
	nvpairs = talloc_zero(logctx, char);
	ASSERT_FAIL(nvpairs)
	
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
	ASSERT_FAIL(schema);
	
	/*
	 * Update trigger log
	 */
	 
	 
	/* Build name/value pair list */
	if(msgBody){
		for(i = 0; i < msgBody-> namedValueCount; i++){
			if(!msgBody->namedValues[i]->isBinary){
				if(i){
					nvpairs = talloc_asprintf_append(nvpairs, ",");
					ASSERT_FAIL(nvpairs)
				}
				nvpairs = talloc_asprintf_append(nvpairs, "%s=%s",
				msgBody->namedValues[i]->itemName, msgBody->namedValues[i]->itemValue);
				ASSERT_FAIL(nvpairs)
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
	
	DBUpdateTrigLog(logctx, myDB, sourceDevice, schema, nvpairs);
	
	
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
	if(noBackground && (ticks >= 30)){
		ticks = 0;
		talloc_report(masterCTX, stdout);
	}
	
	
	/* Terminate if requested to do so */
	if(exitRequest){
		xPL_setServiceEnabled(xpleventService, FALSE);
		xPL_releaseService(xpleventService);
		xPL_shutdown();
		DBClose(myDB);
	
		/* Unlink the pid file if we can. */
		(void) unlink(pidFile);
		if(masterCTX){
			talloc_free(masterCTX);
		}
		exit(0);
	}
}

/*
* Show help
*/

void showHelp(void)
{
	printf("'%s' is a daemon that XXXXXXXXXX\n", progName);
	printf("via XXXXXXXXXXX\n");
	printf("\n");
	printf("Usage: %s [OPTION]...\n", progName);
	printf("\n");
	printf("  -c, --config-file PATH  Set the path to the config file\n");
	printf("  -d, --debug LEVEL       Set the debug level, 0 is off, the\n");
	printf("                          compiled-in default is %d and the max\n", debugLvl);
	printf("  -e --exitonerr          Exit on parse or execution error\n");
	printf("                          level allowed is %d\n", DEBUG_MAX);
	printf("  -f, --pid-file PATH     Set new pid file path, default is: %s\n", pidFile);
	printf("  -h, --help              Shows this\n");
	printf("  -i, --interface NAME    Set the broadcast interface (e.g. eth0)\n");
	printf("  -l, --log  PATH         Path name to debug log file when daemonized\n");
	printf("  -n, --no-background     Do not fork into the background (useful for debugging)\n");
	printf("  -o, --db-file           Database file");
	printf("  -s, --instance ID       Set instance id. Default is %s", instanceID);
	printf("  -v, --version           Display program version\n");
	printf("\n");
 	printf("Report bugs to <%s>\n\n", EMAIL);
	return;

}

/*
* Default error handler for confreadScan()
*/

static void confDefErrorHandler( int etype, int linenum, const String info)
{
	switch(etype){

		case CRE_SYNTAX:
			error("Syntax error in config file on line: %d", linenum);
			break;

		case CRE_IO:
			error("I/O error in confead.c: %s", info);
			break;

		case CRE_FOPEN:
			break;

		default:
			error("Unknown error code: %d", etype);
			break;

	}


}

/*
* main
*/


int main(int argc, char *argv[])
{
	int longindex;
	int optchar;
	String p;
	
	masterCTX = talloc_new(NULL);
	ASSERT_FAIL(masterCTX);


	/* Set the program name */
	progName=argv[0];

	/* Parse the arguments. */
	while((optchar=getopt_long(argc, argv, SHORT_OPTIONS, longOptions, &longindex)) != EOF) {
		
		/* Handle each argument. */
		switch(optchar) {
			
				/* Was it a long option? */
			case 0:
				
				/* Hrmm, something we don't know about? */
				fatal("Unhandled long getopt option '%s'", longOptions[longindex].name);
			
				/* If it was an error, exit right here. */
			case '?':
				exit(1);
		
				/* Was it a config file switch? */
			case 'c':
				ConfReadStringCopy(configFile, optarg, WS_SIZE - 1);
				debug(DEBUG_ACTION,"New config file path is: %s", configFile);
				break;
				
				/* Was it a debug level set? */
			case 'd':

				/* Save the value. */
				debugLvl=atoi(optarg);
				if(debugLvl < 0 || debugLvl > DEBUG_MAX) {
					fatal("Invalid debug level");
				}

				break;
				
			case 'e':
				exitOnErr = TRUE;
				break;

			/* Was it a pid file switch? */
			case 'f':
				ConfReadStringCopy(pidFile, optarg, WS_SIZE - 1);
				clOverride.pid_file = 1;
				debug(DEBUG_ACTION,"New pid file path is: %s", pidFile);
				break;
			
				/* Was it a help request? */
			case 'h':
				showHelp();
				exit(0);

				/* Specify interface to broadcast on */
			case 'i': 
				ConfReadStringCopy(interface, optarg, WS_SIZE -1);
				clOverride.interface = 1;
				break;

			case 'l':
				/* Override log path*/
				ConfReadStringCopy(logPath, optarg, WS_SIZE - 1);
				clOverride.log_path = 1;
				debug(DEBUG_ACTION,"New log path is: %s",
				logPath);

				break;

				/* Was it a no-backgrounding request? */
			case 'n':
				/* Mark that we shouldn't background. */
				noBackground = TRUE;
				break;

			
			case 'o': /* Database file */
				ConfReadStringCopy(dbFile, optarg, WS_SIZE);
				clOverride.dbfile = 1;
				debug(DEBUG_ACTION,"New db file is: %s", dbFile);
				break;			
			
			
			case 's': /* Instance ID */
				ConfReadStringCopy(instanceID, optarg, WS_SIZE);
				clOverride.instance_id = 1;
				debug(DEBUG_ACTION,"New instance ID is: %s", instanceID);
				break;


				/* Was it a version request? */
			case 'v':
				printf("Version: %s\n", VERSION);
				exit(0);
	
			
				/* It was something weird.. */
			default:
				fatal("Unhandled getopt return value %d", optchar);
		}
	}

	
	/* If there were any extra arguments, we should complain. */

	if(optind < argc) {
		fatal("Extra argument on commandline, '%s'", argv[optind]);
	}

	/* Attempt to read a config file */
	
	if((configEntry = ConfReadScan(masterCTX, configFile, confDefErrorHandler))){
		debug(DEBUG_ACTION,"Using config file: %s", configFile);
		/* Instance ID */
		if((!clOverride.instance_id) && (p = ConfReadValueBySectKey(configEntry, "general", "instance-id")))
			ConfReadStringCopy(instanceID, p, sizeof(instanceID));
		
		/* Interface */
		if((!clOverride.interface) && (p = ConfReadValueBySectKey(configEntry, "general", "interface")))
			ConfReadStringCopy(interface, p, sizeof(interface));
			
		/* pid file */
		if((!clOverride.pid_file) && (p = ConfReadValueBySectKey(configEntry, "general", "pid-file")))
			ConfReadStringCopy(pidFile, p, sizeof(pidFile));	
						
		/* log path */
		if((!clOverride.log_path) && (p = ConfReadValueBySectKey(configEntry, "general", "log-path")))
			ConfReadStringCopy(logPath, p, sizeof(logPath));
		
		/* db-file */
		if((!clOverride.dbfile) && (p = ConfReadValueBySectKey(configEntry, "general", "db-file")))
			ConfReadStringCopy(dbFile, p, sizeof(dbFile));
		
	}
	else
		debug(DEBUG_UNEXPECTED, "Config file %s not found or not readable", configFile);
		
	/* Set the xPL interface */
	xPL_setBroadcastInterface(interface);
		
	/* Turn on library debugging for level 5 */
	if(debugLvl >= 5)
		xPL_setDebugging(TRUE);
		
	
	if(!(myDB = DBOpen(dbFile))){
			fatal("Database file does not exist or is not writeble: %s", dbFile);
	}
		
	/* Fork into the background. */	
	if(!noBackground) {
		int retval;
		
	    /* Make sure we are not already running (.pid file check). */
		if(pid_read(pidFile) != -1) 
			fatal("%s is already running", progName);
			
		debug(DEBUG_STATUS, "Forking into background");

    	/* 
		* If debugging is enabled, redirect the debug output to a log file if
    	* the path to the logfile is defined
		*/

		if((debugLvl) && (logPath[0]))                          
			notify_logpath(logPath);
			
	
		/* Fork and exit the parent */

		if((retval = fork())){
      			if(retval > 0)
				exit(0);  /* Exit parent */
			else
				fatal_with_reason(errno, "parent fork");
    		}
	


		/*
		* The child creates a new session leader
		* This divorces us from the controlling TTY
		*/

		if(setsid() == -1)
			fatal_with_reason(errno, "creating session leader with setsid");


		/*
		* Fork and exit the session leader, this prohibits
		* reattachment of a controlling TTY.
		*/

		if((retval = fork())){
			if(retval > 0)
        			exit(0); /* exit session leader */
			else
				fatal_with_reason(errno, "session leader fork");
		}

		/* 
		* Change to the root of all file systems to
		* prevent mount/unmount problems.
		*/

		if(chdir("/"))
			fatal_with_reason(errno, "chdir to /");

		/* set the desired umask bits */

		umask(022);
		
		/* Close STDIN, STDOUT, and STDERR */

		close(0);
		close(1);
		close(2);
 
	}
	debug(DEBUG_STATUS,"Initializing xPL library");
	


	/* Start xPL up */
	if (!xPL_initialize(xPL_getParsedConnectionType())) {
		fatal("Unable to start xPL lib");
	}
	
	/* Create a service and set our application version */
	xpleventService = xPL_createService("hwstar", "xplevent", instanceID);
  	xPL_setServiceVersion(xpleventService, VERSION);

	/*
	* Create trigger message object
	*/

	xpleventTriggerMessage = xPL_createBroadcastMessage(xpleventService, xPL_MESSAGE_TRIGGER);
	xPL_setSchema(xpleventTriggerMessage, "x10", "basic");


  	/* Install signal traps for proper shutdown */
 	signal(SIGTERM, shutdownHandler);
 	signal(SIGINT, shutdownHandler);


	/* Add 1 second tick service */
	xPL_addTimeoutHandler(tickHandler, 1, NULL);

  	/* And a listener for all xPL messages */
  	xPL_addMessageListener(xPLListener, NULL);


 	/* Enable the service */
  	xPL_setServiceEnabled(xpleventService, TRUE);

	if(!noBackground && (pid_write(pidFile, getpid()) != 0)) {
		debug(DEBUG_UNEXPECTED, "Could not write pid file '%s'.", pidFile);
	}


 	/** Main Loop **/

	for (;;) {
		/* Let XPL run forever */
		xPL_processMessages(-1);
  	}

	exit(1);
}

