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
#include "types.h"
#include "notify.h"
#include "confread.h"


#define MALLOC_ERROR	malloc_error(__FILE__,__LINE__)

#define SHORT_OPTIONS "c:d:f:hi:l:ns:v"

#define WS_SIZE 256


#define DEF_CONFIG_FILE		"./xplevent.conf"
#define DEF_PID_FILE		"./xplevent.pid"
#define DEF_SQLITE_FILE		"./xplevent.sqlite3"

#define DEF_INTERFACE		"eth1"

#define DEF_INSTANCE_ID		"main"


 
typedef struct cloverrides {
	unsigned pid_file : 1;
	unsigned instance_id : 1;
	unsigned log_path : 1;
	unsigned interface : 1;
	unsigned sqlitefile : 1;
} clOverride_t;


char *progName;
int debugLvl = 0; 
int NumRows = 0;



static Bool noBackground = FALSE;

static clOverride_t clOverride = {0,0,0,0};

static xPL_ServicePtr xpleventService = NULL;
static xPL_MessagePtr xpleventTriggerMessage = NULL;
static xPL_MessagePtr xpleventConfirmMessage = NULL;
static ConfigEntryPtr_t	configEntry = NULL;

static sqlite3 *myDB = NULL;

static char configFile[WS_SIZE] = DEF_CONFIG_FILE;
static char interface[WS_SIZE] = DEF_INTERFACE;
static char logPath[WS_SIZE] = "";
static char instanceID[WS_SIZE] = DEF_INSTANCE_ID;
static char pidFile[WS_SIZE] = DEF_PID_FILE;
static char sqliteFile[WS_SIZE] = DEF_SQLITE_FILE;



/* Commandline options. */

static struct option longOptions[] = {
	{"config-file", 1, 0, 'c'},
	{"debug", 1, 0, 'd'},
	{"pid-file", 0, 0, 'f'},
	{"help", 0, 0, 'h'},
	{"interface", 1, 0, 'i'},
	{"log", 1, 0, 'l'},
	{"no-background", 0, 0, 'n'},	
	{"sqlite-file", 1, 0, 'o'},
	{"instance", 1, 0, 's'},
	{"version", 0, 0, 'v'},
	{0, 0, 0, 0}
};


/*
 * Malloc error handler
 */
 
static void malloc_error(String file, int line)
{
	fatal("Out of memory in file %s, at line %d");
}



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
	xPL_setServiceEnabled(xpleventService, FALSE);
	xPL_releaseService(xpleventService);
	xPL_shutdown();
	if(myDB)
		sqlite3_close(myDB);
	/* Unlink the pid file if we can. */
	(void) unlink(pidFile);
	exit(0);
}




/*
 * Heartbeat logger
 */

// Select Callback for heartbeat logger
static int countRows(void* objptr, int argc, char **argv, char **colnames )
{
	if(objptr)
		(*((int *) objptr))++;
	return 0;
}

// Logger function
static void logHeartBeatMessage(xPL_MessagePtr theMessage)
{
	int errs = 0;
	int numRows = 0;
	const String vendor = xPL_getSourceVendor(theMessage);
	const String device = xPL_getSourceDeviceID(theMessage);
	const String instance_id = xPL_getSourceInstanceID(theMessage);
	char buffer[128];
	char source[64];
	String errorMessage;
	
	// Setup
	snprintf(source, 63, "%s-%s.%s", vendor, device, instance_id);
	debug(DEBUG_EXPECTED,"Heartbeat status message received: vendor = %s, device = %s, instance_id = %s",
	vendor, device, instance_id);
	
	// Transaction start
	sqlite3_exec(myDB, "BEGIN TRANSACTION", NULL, NULL, &errorMessage);
	if(errorMessage){
		errs++;
		debug(DEBUG_UNEXPECTED,"Sqlite error: %s", errorMessage);
		sqlite3_free(errorMessage);
	}	

	// See if source is already in the table

	snprintf(buffer, 127, "SELECT * FROM hbeatlog WHERE source='%s'", source);
	if(!errs){
		sqlite3_exec(myDB, buffer, countRows, &numRows, &errorMessage);
		if(errorMessage){
			debug(DEBUG_UNEXPECTED,"Sqlite select error on hbeatlog: %s", errorMessage);
			sqlite3_free(errorMessage);
		}
		
		if(numRows){
			// Delete any rows if they exist
			snprintf(buffer, 127,"DELETE FROM hbeatlog WHERE source='%s'", source);
			sqlite3_exec(myDB, buffer, countRows, NULL, &errorMessage);
			if(errorMessage){
				debug(DEBUG_UNEXPECTED,"Sqlite delete error on hbeatlog: %s", errorMessage);
				sqlite3_free(errorMessage);
				errs++;
			}
		}
	}
	if(!errs){
		// Insert new record
		snprintf(buffer, 127, "INSERT INTO hbeatlog VALUES ('%s',DATETIME())", source);
		sqlite3_exec(myDB, buffer, countRows, NULL, &errorMessage);
		if(errorMessage){
			debug(DEBUG_UNEXPECTED,"Sqlite insert error on hbeatlog: %s", errorMessage);
			sqlite3_free(errorMessage);
			errs++;
		}
	}			
	if(!errs){
		// Transaction commit	
		sqlite3_exec(myDB, "COMMIT TRANSACTION", NULL, NULL, &errorMessage);
		if(errorMessage){
			debug(DEBUG_UNEXPECTED,"Sqlite commit error on hbeatlog: %s", errorMessage);
			sqlite3_free(errorMessage);
		}	
	}
	else{
		sqlite3_exec(myDB, "ROLLBACK TRANSACTION", NULL, NULL, &errorMessage);
		if(errorMessage){
			debug(DEBUG_UNEXPECTED,"Sqlite rollback error hbeatlog: %s", errorMessage);
			sqlite3_free(errorMessage);
		}
	}		
}




/*
 * Our xPL listener
 */


static void xPLListener(xPL_MessagePtr theMessage, xPL_ObjectPtr userValue)
{
	if(xPL_isBroadcastMessage(theMessage)){ /* If broadcast message */
		int mtype = xPL_getMessageType(theMessage);
		const String class = xPL_getSchemaClass(theMessage);
		const String type = xPL_getSchemaType(theMessage);
		if((mtype == xPL_MESSAGE_STATUS) && !strcmp(class, "hbeat") && !strcmp(type, "app")){
			// Log heartbeat messages
			logHeartBeatMessage(theMessage);
		}
	}
}



/*
* Our tick handler. 
* 
*/

static void tickHandler(int userVal, xPL_ObjectPtr obj)
{

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
	printf("                          level allowed is %d\n", DEBUG_MAX);
	printf("  -f, --pid-file PATH     Set new pid file path, default is: %s\n", pidFile);
	printf("  -h, --help              Shows this\n");
	printf("  -i, --interface NAME    Set the broadcast interface (e.g. eth0)\n");
	printf("  -l, --log  PATH         Path name to debug log file when daemonized\n");
	printf("  -n, --no-background     Do not fork into the background (useful for debugging)\n");
	printf("  -o, --sqlite-file       Sqlite database file");
	printf("  -s, --instance ID       Set instance id. Default is %s", instanceID);
	printf("  -v, --version           Display program version\n");
	printf("\n");
 	printf("Report bugs to <%s>\n\n", EMAIL);
	return;

}

/*
* Default error handler for confreadScan()
*/

static void confDefErrorHandler( int etype, int linenum, String info)
{
	switch(etype){

		case CRE_MALLOC:
			error("Memory allocation error in confread.c, line %d", linenum);
			break;

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
	int rc;
	int optchar;
	String p;
	
		

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
				confreadStringCopy(configFile, optarg, WS_SIZE - 1);
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

			/* Was it a pid file switch? */
			case 'f':
				confreadStringCopy(pidFile, optarg, WS_SIZE - 1);
				clOverride.pid_file = 1;
				debug(DEBUG_ACTION,"New pid file path is: %s", pidFile);
				break;
			
				/* Was it a help request? */
			case 'h':
				showHelp();
				exit(0);

				/* Specify interface to broadcast on */
			case 'i': 
				confreadStringCopy(interface, optarg, WS_SIZE -1);
				clOverride.interface = 1;
				break;

			case 'l':
				/* Override log path*/
				confreadStringCopy(logPath, optarg, WS_SIZE - 1);
				clOverride.log_path = 1;
				debug(DEBUG_ACTION,"New log path is: %s",
				logPath);

				break;

				/* Was it a no-backgrounding request? */
			case 'n':
				/* Mark that we shouldn't background. */
				noBackground = TRUE;
				break;

			
			case 'o': /* Instance ID */
				confreadStringCopy(sqliteFile, optarg, WS_SIZE);
				clOverride.sqlitefile = 1;
				debug(DEBUG_ACTION,"New sqlite file is: %s", sqliteFile);
				break;			
			
			
			case 's': /* Instance ID */
				confreadStringCopy(instanceID, optarg, WS_SIZE);
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
	
	if((configEntry = confreadScan(configFile, confDefErrorHandler))){
		debug(DEBUG_ACTION,"Using config file: %s", configFile);
		/* Instance ID */
		if((!clOverride.instance_id) && (p = confreadValueBySectKey(configEntry, "general", "instance-id")))
			confreadStringCopy(instanceID, p, sizeof(instanceID));
		
		/* Interface */
		if((!clOverride.interface) && (p = confreadValueBySectKey(configEntry, "general", "interface")))
			confreadStringCopy(interface, p, sizeof(interface));
			
		/* pid file */
		if((!clOverride.pid_file) && (p = confreadValueBySectKey(configEntry, "general", "pid-file")))
			confreadStringCopy(pidFile, p, sizeof(pidFile));	
						
		/* log path */
		if((!clOverride.log_path) && (p = confreadValueBySectKey(configEntry, "general", "log-path")))
			confreadStringCopy(logPath, p, sizeof(logPath));
		
		/* sqlite-file */
		if((!clOverride.sqlitefile) && (p = confreadValueBySectKey(configEntry, "general", "sqlite-file")))
			confreadStringCopy(sqliteFile, p, sizeof(sqliteFile));
		
	}
	else
		debug(DEBUG_UNEXPECTED, "Config file %s not found or not readable", configFile);

	/* Turn on library debugging for level 5 */
	if(debugLvl >= 5)
		xPL_setDebugging(TRUE);
		
		
	/* Open Database File */
	if(!access(sqliteFile, R_OK | W_OK)){
		if((rc = sqlite3_open_v2(sqliteFile, &myDB, SQLITE_OPEN_READWRITE, NULL)))
			fatal("Sqlite error on open: %s", sqlite3_errmsg(myDB));
	}
	else
		fatal("Database file does not exist or is not writeble: %s", sqliteFile);
		
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
	
	/* Set the xPL interface */
	xPL_setBroadcastInterface(interface);

	/* Start xPL up */
	if (!xPL_initialize(xPL_getParsedConnectionType())) {
		fatal("Unable to start xPL lib");
	}
	
	/* Create a service and set our application version */
	xpleventService = xPL_createService("hwstar", "xplevent", instanceID);
  	xPL_setServiceVersion(xpleventService, VERSION);

	/*
	* Create trigger message objecta
	*/

	xpleventConfirmMessage = xPL_createBroadcastMessage(xpleventService, xPL_MESSAGE_TRIGGER);
	xPL_setSchema(xpleventConfirmMessage, "x10", "confirm");

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

