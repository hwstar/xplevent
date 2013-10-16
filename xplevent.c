/*
*    xplevent.c
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

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <ctype.h>
#include <getopt.h>
#include <limits.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/signalfd.h>
#include <string.h>
#include <talloc.h>
#include  "defs.h"
#include "types.h"
#include "notify.h"
#include "confread.h"
#include "db.h"
#include "parser.h"
#include "monitor.h"
#include "util.h"
#include "poll.h"
#include "socket.h"
#include "xplevent.h"
#include "xplcore.h"


enum {UC_CHECK_SYNTAX = 1, UC_GET_SCRIPT, UC_PUT_SCRIPT, UC_SEND_CMD, UC_GENERATE};


#define SHORT_OPTIONS "b:C:cd:Def:Fg:GHh:i:L:nO:o:P:p:s:S:Vx:"

#define DEF_CONFIG_FILE		"./xplevent.conf,/etc/xplevent/xplevent.conf"
#define DEF_PID_FILE		"/tmp/xplevent.pid"
#define DEF_DB_FILE			"./xplevent.sqlite3"
#define DEF_LOG_FILE		""

#define DEF_INSTANCE_ID		"main"

#define DEF_CMD_SERVICE_NAME "1130"

#define DEF_XPL_SERVICE_NAME "3865"


 
typedef union cloverrides{
	struct {
		unsigned pid_file : 1;
		unsigned instance_id : 1;
		unsigned log_path : 1;
		unsigned interface : 1;
		unsigned dbfile : 1;
		unsigned bindaddress : 1;
		unsigned hostname : 1;
		unsigned cmdserv : 1;
		unsigned xplserv : 1;
	};
	unsigned all;
	
} clOverride_t;


XPLEvGlobalsPtr_t Globals = NULL;
static clOverride_t clOverride;
static Bool forceFlag = FALSE;
static Bool dbDirectFlag = FALSE;
static int utilityCommand = 0;
static String utilityArg = NULL;
static String utilityFile = NULL;
static void *configInfo = NULL;





/* Commandline options. */

static struct option longOptions[] = {
	{"bindaddr", 1, 0, 'b'},
	{"check",0 ,0 ,'c'},
	{"config-file", 0, 0, 'C'},
	{"debug", 1, 0, 'd'},
	{"dbdirect",0, 0, 'D'},
	{"exitonerr",0, 0, 'e'},
	{"file", 0, 0, 'f'},
	{"force", 0, 0, 'F'},
	{"get", 1, 0, 'g'},
	{"generate", 0, 0, 'G'},
	{"help", 0, 0, 'H'},
	{"host", 1, 0, 'h'},
	{"ipaddr", 1, 0, 'i'},
	{"log", 1, 0, 'L'},
	{"no-background", 0, 0, 'n'},	
	{"xplport", 1, 0, 'O'},
	{"db-file", 1, 0, 'o'},
	{"put", 1, 0, 'p'},
	{"pidfile", 1, 0, 'P'},
	{"lstport", 1, 0, 'S'},
	{"instance", 1, 0, 's'},
	{"version", 0, 0, 'V'},
	{"command", 1, 0, 'x'},
	{0, 0, 0, 0}
};




/*
* Show help
*
* Arguments:
*
* None
*
* Return value:
*
* None
*/

static void showHelp(void)
{
	printf("'%s' is a daemon that responds to xPL trigger messages\n", Globals->progName);
	printf("\n");
	printf("Usage: %s [OPTION]...\n", Globals->progName);
	printf("\n");
	printf("  -b  --bindaddress	ADDR  Set bind address for command socket listener\n");
	printf("  -C, --config-file PATH  Set the path to the config file\n");
	printf("  -c, --check             Utility Function: Check script file syntax\n");
	printf("  -d, --debug LEVEL       Set the debug level, 0 is off, the\n");
	printf("  -D, --dbdirect          Connect to the database directly, (i.e. not over TCP)");
	printf("                          compiled-in default is %d and the max\n", Globals->debugLvl);
	printf("                          level allowed is %d\n", DEBUG_MAX);
	printf("  -e --exitonerr          Exit on parse or execution error\n");
	printf("  -f, --file PATH         Set file path for utility functions\n");
	printf("  -F, --force             Force option\n");
	printf("  -g, --get scriptname    Utility function: Get script name from database and write to file\n");
	printf("  -G, --generate          Utility function: Generate an empty database file\n");
	printf("  -H, --help              Shows this\n");
	printf("  -h, --host HOST         Set host name for utility client mode\n");
	printf("  -i, --ipaddr ADDR       Set the broadcast interface IP address\n");
	printf("  -L, --log  PATH         Path name to debug log file when daemonized\n");
	printf("  -n, --no-background     Do not fork into the background (useful for debugging)\n");
	printf("  -O, --xplport NAME/PORT Use port number or service name specified for xPL connections");
	printf("  -o, --db-file           Database file\n");
	printf("  -P, --pidfile PATH      Set new pid file path, default is: %s\n", Globals->pidFile);
	printf("  -p, --put scriptname    Utility function: Put file in script name\n");
	printf("  -s, --instance ID       Set instance id. Default is %s\n", Globals->instanceID);
	printf("  -S, --lstport NAME/PORT Set service name or port number for command listener\n");
	printf("  -V, --version           Display program version\n");
	printf("  -x, --command COMMAND   Execute command on daemon from client\n");
	printf("\n");
 	printf("Report bugs to <%s>\n\n", EMAIL);
	return;

}

/*
* Error handler callback for confreadScan()
*
* Arguments:
*
* 1 Error type. (See confread.h)
* 2. Line number where the error occurred (valid for syntax error only)
* 3. Info string containg output from strerror(). (valid for I/O error only)
*
* Return value:
*
* None
*
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
 * Atexit callback to shut down xplevent
 *
 * Arguments:
 *
 * None
 *
 * Return value
 *
 * None
 */ 

static void xpleventShutdown(void)
{
	DBClose(Globals->db);
	
	if(TRUE == Globals->weWroteThePIDFile){
		(void) unlink(Globals->pidFile);
	}
	
	/* If running in the foreground and the debug level is 4, print talloc report on exit */
	if(Globals->noBackground && (Globals->debugLvl == 4)){
		talloc_report(Globals, stdout);
	}
	
	if(Globals){ /* Free the master context */
		talloc_free(Globals);
	}
}

/*
* Print -f switch reqired error and exit
*
* Arguments:
*
* None
*
* Return value:
*
* None
*/


static void noFileSwitch(void)
{
	fatal("-f switch is required for this utility function");
}

/*
 * Send a command line to the daemon and display the results
 *
 * Arguments:
 *
 * 1. Command line to send
 *
 * Return value:
 *
 * None
 */

static void utilitySendCmd(String utilityArg)
{
	int daemonSock;
	unsigned length;

	String line;
	/* Try and connect to daemon */
	if(( daemonSock = SocketConnectIP(Globals->cmdHostName, Globals->cmdService, AF_UNSPEC, SOCK_STREAM)) < 0){
		fatal("Could not connect to daemon at address: %s", Globals->cmdHostName);
	}	
	SocketPrintf(Globals, daemonSock, "cl:%s\n", utilityArg);
	line = SocketReadLine(Globals, daemonSock, &length);	
	if(line && length){
		printf("Result = %s\n", line);
	}
	else{
		printf("Error getting result\n");
	}
	talloc_free(line);
	close(daemonSock); /* Done */
}


/*
 * Get a script from the daemon
 *
 * Arguments:
 *
 * 1. Script Name as a string
 * 2. Filename to store the script in.
 *
 * Return value:
 *
 * None
 */


static void getScript(String utilityArg, String utilityFile)
{
	Bool done;
	unsigned length;
	int daemonSocket;
	String line, script;
	String id = "xplevent:getScript";
	MonRcvInfoPtr_t ri;
	
	if(utilityFile){
		if(dbDirectFlag){
			/* Access the local database file */
			if(!(script = DBFetchScript(Globals, Globals->db, utilityArg))){
				fatal("%s: Problem retreiving script: %s from database", id, utilityArg);
			}
			
			if(FAIL == UtilFileWriteString(utilityFile, script)){ /* Save the script */
				talloc_free(script); /* Free the script */
				fatal_with_reason(errno, "%s: Could not write file: %s", id, utilityFile);
			}
			talloc_free(script); /* Free the script */
		
		}
		else{

			/* Receive script from daemon */
			if(( daemonSocket = SocketConnectIP(Globals->cmdHostName, Globals->cmdService, AF_UNSPEC, SOCK_STREAM)) < 0){
				fatal("%s:Could not connect to daemon at address: %s",id, Globals->cmdHostName);
			}
			/* Allocate the receive info structure */
			MALLOC_FAIL(ri = talloc_zero(Globals, MonRcvInfo_t))
			/* Initialize the structure */
			ri->scriptBufSize = 2048;
			ri->scriptSizeLimit = 65536;
			MALLOC_FAIL(ri->script = talloc_array(ri, char, ri->scriptBufSize))
			ri->script[0] = 0; /* Set to zero length */
		

			/* Send the send script comment */
			SocketPrintf(Globals, daemonSocket, "ss:%s\n", utilityArg);
			/* Loop while getting the contents of the script from the server */
			for(done = FALSE;!done;){
				/* Read one line at a time, then process it */
				ASSERT_FAIL(line = SocketReadLine(Globals, daemonSocket, &length))
				if(!length){
					/* empty line from file */
					break;
				}
				done = MonitorRecvScript(ri, line);
				/* Free the line */
				talloc_free(line);
			}
	
			/* Close the socket */
			if(close(daemonSocket) < 0){
				fatal("%s: Close error on socket: %s", id, strerror(errno));
			}
			/* Check for errors */
			if(ri->state != RS_ERROR){	
				if(UtilFileWriteString(utilityFile, ri->script) == FAIL){ /* Error free, save the script */
					fatal_with_reason(errno, "%s: Could not write file: %s", id, utilityFile);
				}
				note("Script received successfully");
			}
			else{
				talloc_free(ri); /* Error detected */
				fatal("Script receive error"); /* FIXME */
			}	
			talloc_free(ri); /* Free the receive info structure */
		}
	}
	else{
		/* File not specified */
		noFileSwitch();
	}
}


/*
 * Send a script to the daemon
 *
 * Arguments:
 *
 * 1. Script name as a string
 * 2. Script file to read as a string.
 *
 * Return value:
 *
 * None
 */


static void putScript(String utilityArg, String utilityFile)
{
	int daemonSock = -1;
	unsigned length;
	String id = "xplevent:putScript";
	String ack = NULL;
	
	if(utilityFile){
		String script, s;
		
		/* Check to see if we can write where the file is to be saved */
		if(access(utilityFile, R_OK | F_OK)){
			fatal("%s: Can't open %s for reading", id, utilityFile);
		}
		/* Check syntax locally before sending to server */
		s = ParserCheckSyntax(Globals, utilityFile);
		if(s){
			fatal("%s:%s: script not added to database",id, s);
		}
		
		/* Slurp the file */
		if(!(script = UtilFileReadString(Globals, utilityFile))){
			fatal_with_reason(errno, "%s: Could not read file: %s", id, utilityFile);
		}

		if(dbDirectFlag){
			/* Access the local database */
			if(FAIL == DBIRScript(Globals, Globals->db, utilityArg, script)){
				fatal("%s: Could not insert or replace script %s", id, utilityArg);
			}
		}
		else{
			/* Connect to a daemon */
			/* Send script to daemon */
			if(( daemonSock = SocketConnectIP(Globals->cmdHostName, Globals->cmdService, AF_UNSPEC, SOCK_STREAM)) < 0){
				fatal("%s: Could not connect to daemon at address: %s",id, Globals->cmdHostName);
			}

			/* Send the command */
			debug(DEBUG_ACTION,"Sending recieve script request");
			SocketPrintf(Globals, daemonSock, "rs:%s\n", utilityArg);	
		
			/* Send the script */
			MonitorSendScript(Globals, daemonSock, script, utilityArg);
			
			/* Get the response */
			debug(DEBUG_ACTION,"Waiting for response...");
			if((ack = SocketReadLine(Globals, daemonSock, &length)) == NULL){
				fatal("%s: Failed to get acknowlgement of receipt of script");
			}
			debug(DEBUG_ACTION,"Got response");
		}
		
		/* Free the script */
		talloc_free(script);
		
	
		if(!dbDirectFlag){
			/* Close the socket */
			if(close(daemonSock) < 0){
				fatal("%s: Close error on socket: %s", id, strerror(errno));
			}
		
			/* Check the response */
			if(!strncmp("er:", ack, 3)){
				error("Error sending script to server: %s", ack + 3);
			}
			else{
				note("Script uploaded successfully");
			}
			
			/* Free the response */
		
			talloc_free(ack);
		}
	
	}
	else{
		/* No file name specified */
		noFileSwitch();
	}
}

/* 
 * Generate an empty DB file
 *
 * Arguments:
 *
 * 1. The path to the file to generate as a string.
 *
 * Return value:
 *
 * None
 */
 
static void generateDBFile(String theFile)
{
	if(!theFile){
		fatal("Path to database file must be specified");
	}
	DBGenFile(Globals, theFile, forceFlag);
}


/*
 * Do utility command and exit
 *
 * Arguments:
 * 
 * 1. Utility command code
 * 2. Utility command argument (NULL if none)
 * 3. Utility filename set with -f switch (NULL if none)
 *
 * Return Value:
 *
 * None
 */
static void doUtilityCommand(int utilityCommand, String utilityArg, String utilityFile)
{
	int res = 0;
	String s;
	
	
	debug(DEBUG_ACTION, "Util cmd: %d, arg: %s, extra: %s", utilityCommand,
	(utilityArg)? utilityArg : "(nil)",
	(utilityFile)? utilityFile : "(nil)");
	
	switch(utilityCommand){
		case UC_CHECK_SYNTAX: /* Check script syntax */
			if(utilityFile){
				if(access(utilityFile, R_OK | F_OK)){
					fatal("Can't open %s for reading", utilityFile);
				}
				s = ParserCheckSyntax(Globals, utilityFile);
				if(s){
					fatal("%s", s);
				}
			}
			else{
				noFileSwitch();
			}
			break;
			
		case UC_GET_SCRIPT: /* Fetch a script from the database */
			getScript(utilityArg, utilityFile);
			break;		
			
			
		case UC_PUT_SCRIPT: /* Check syntax, and if good, place a script in the database */
			putScript(utilityArg, utilityFile);
			break;	
			
		case UC_SEND_CMD: /* Send a command to the server */
			utilitySendCmd(utilityArg);
			break;
			
		case UC_GENERATE: /* Generate an empty database file */
			generateDBFile(utilityFile);
			break;

		default:
			ASSERT_FAIL(0)
	}
	exit(res);
}

/*
* Set up a utility command. If a command has already been specified, exit with an error message.
*
* Arguments:
*
* 1. Utility command code
* 2. Optional argument (If not used, pass in a NULL)
*
* Return value:
*
* None
* 
*/

static void prepareUtilityCommand(int command, String optarg)
{
	if(!utilityCommand){
		utilityCommand = command;
		if(optarg){
			MALLOC_FAIL(utilityArg = talloc_strdup(Globals, optarg))
		}
	}
	else{
		fatal("Only one of -c -p -s -x -G may be specified on the command line. These switches are mutually exclusive");
	}
}

/*
 * Handle signals
 */
 
static void signalHandler(int fd, int event, void *userObject)
{
	struct signalfd_siginfo fdsi;
	ssize_t s;
	
	debug(DEBUG_EXPECTED, "\nSignal handler called");
	
	for(;;){
		if((s = read(fd, &fdsi, sizeof(fdsi))) < 0){
			if(EAGAIN == errno){
				break; /* Nothing left to process */
			}
			else{
				fatal_with_reason(errno, "%s: error reading signal info", __func__);
			}	
		}
		
		switch(fdsi.ssi_signo){
			case SIGINT:
			case SIGTERM:
			case SIGQUIT:
				exit(0);
				
			case SIGHUP:
				if(Globals->noBackground){
					exit(0);
				}
				else{
					if(Globals->logFile[0]){
						debug(DEBUG_EXPECTED, "Closing %s", Globals->logFile);
						notify_logpath(Globals->logFile);
						debug(DEBUG_EXPECTED, "Re-opening %s", Globals->logFile);
					}
				}
				break;
		
			default:
				debug(DEBUG_UNEXPECTED,"Unhandled signal: %d", fdsi.ssi_signo);
		}
		
	
	}
}


/*
* Program entry point
*
* Arguments 
*
* 1. Count of command line arguments
* 2. Array of command line arguments as Strings
*
* Return value:
*
* Program error state.
*/


int main(int argc, char *argv[])
{
	int longindex;
	int optchar;
	int i;
	int sfd;
	pid_t pid;
	String p;
	String maxPath = NULL, fullPath = NULL;
	String configFiles;
	String *configPaths;
	struct sigaction sa_chld;
	sigset_t mask;

	notify_init(argv[0]);

	configFiles = DEF_CONFIG_FILE;
	
	/* Set up Globals */
	if(!(Globals = talloc_zero(NULL, XPLEvGlobals_t))){
		fatal("Memory allocation failed in file %s on line %d\n", __FILE__, __LINE__);
	}
	
	/* Initialize defaults in Globals */
	
	Globals->progName = argv[0];
	Globals->cmdService = DEF_CMD_SERVICE_NAME;
	Globals->cmdHostName = "::1";
	Globals->pidFile = DEF_PID_FILE;
	Globals->dbFile = DEF_DB_FILE;
	MALLOC_FAIL(Globals->logFile = talloc_strdup(Globals, DEF_LOG_FILE))
	Globals->instanceID = DEF_INSTANCE_ID;
	Globals->xplService = DEF_XPL_SERVICE_NAME;
	Globals->lat = 33.0;
	Globals->lon = -117.0;
	
	/* Add the shutdown hook */
	
	atexit(xpleventShutdown);

	/* Parse the arguments. */
	while((optchar=getopt_long(argc, argv, SHORT_OPTIONS, longOptions, &longindex)) != EOF) {
		
		/* Handle each argument. */
		switch(optchar) {
			
			case 0:
				/* Hrmm, something we don't know about? */
				fatal("Unhandled long getopt option '%s'", longOptions[longindex].name);
			
			case '?':
				exit(1);
				
			case 'b': /* Bind address */
				clOverride.bindaddress = 1;
				MALLOC_FAIL(Globals->cmdBindAddress = talloc_strdup(Globals, optarg));
				break;
		
				/* Was it a config file switch? */
			case 'C':
				MALLOC_FAIL(configFiles = talloc_strdup(Globals, optarg));
				debug(DEBUG_ACTION,"New config file list is: %s", configFiles);
				break;
				
			case 'c': /* Check syntax of a script file */
				prepareUtilityCommand(UC_CHECK_SYNTAX, NULL);
				break;
				
				/* Was it a debug level set? */
			case 'd':

				/* Save the value. */
				Globals->debugLvl=atoi(optarg);
				if((FAIL == UtilStoi(optarg, &Globals->debugLvl)) ||
					(Globals->debugLvl < 0) || (Globals->debugLvl > DEBUG_MAX)) {
					fatal("Invalid debug level");
				}
	
				break;
				
			case 'D': /* Database direct flag */
				dbDirectFlag = TRUE;
				break;
				
				
			case 'e':
				Globals->exitOnErr = TRUE;
				break;

			/* Was it a utility file switch? */
			case 'f':
				utilityFile = talloc_strndup(Globals, optarg, PATH_MAX);
				debug(DEBUG_ACTION,"New utility file path is: %s", utilityFile);
				break;
				
			case 'F': /* Force flag */
				forceFlag = TRUE;
				break;
				
			case 'g': /* Get script */
				prepareUtilityCommand(UC_GET_SCRIPT, optarg);
				break;
			
			case 'G': /* Generate database file */
				prepareUtilityCommand(UC_GENERATE, NULL);
				break;
			
				/* Was it a help request? */
			case 'H':
				showHelp();
				exit(0);
				
				/* Was it a host name request */
			case 'h':
				clOverride.hostname = 1;
				MALLOC_FAIL(Globals->cmdHostName = talloc_strdup(Globals, optarg));
				break;

				/* Specify interface to broadcast on */
			case 'i': 
				clOverride.interface = 1;
				MALLOC_FAIL(Globals->ipAddr = talloc_strdup(Globals, optarg));
				break;

			case 'L':
				/* Override log path*/
				clOverride.log_path = 1;
				talloc_free(Globals->logFile);
				MALLOC_FAIL(Globals->logFile = talloc_strdup(Globals, optarg));
				break;
				

				/* Was it a no-backgrounding request? */
			case 'n':
				/* Mark that we shouldn't background. */
				Globals->noBackground = TRUE;
				break;

			case 'O': /* xPL Service port name or number */
				clOverride.xplserv = 1;
				MALLOC_FAIL(Globals->xplService = talloc_strdup(Globals, optarg));
				break;

			
			case 'o': /* Database file */
				clOverride.dbfile = 1;
				MALLOC_FAIL(Globals->dbFile = talloc_strdup(Globals, optarg));
				debug(DEBUG_ACTION,"New db file is: %s", Globals->dbFile);
				break;		
				
				
			case 'p': /* Put Script */
				prepareUtilityCommand(UC_PUT_SCRIPT, optarg);
				break;
			
			case 'P': /* PID file */
				clOverride.pid_file = 1;
				MALLOC_FAIL(Globals->pidFile = talloc_strdup(Globals, optarg));
				debug(DEBUG_ACTION,"New pid file path is: %s", Globals->pidFile);
				break;
				
			case 'S': /* Listening Service port name or number */
				clOverride.cmdserv = 1;
				MALLOC_FAIL(Globals->cmdService = talloc_strdup(Globals, optarg));
				break;
			
			case 's': /* Instance ID */
				clOverride.instance_id = 1;
				MALLOC_FAIL(Globals->instanceID = talloc_strdup(Globals, optarg));
				debug(DEBUG_ACTION,"New instance ID is: %s", Globals->instanceID);
				break;


				/* Was it a version request? */
			case 'V':
				printf("Version: %s\n", VERSION);
				exit(0);
	
			case 'x': /* Execute a command */
				prepareUtilityCommand(UC_SEND_CMD, optarg);
				break;
						
				/* It was something weird.. */
			default:
				fatal("Unhandled getopt return value %d", optchar);
		}
	}

	
	/* If there were any extra arguments, we may complain. */

	if(optind < argc) {
		fatal("Extra argument on command line: %s", argv[optind]);
	}

	/* Attempt to read in a config file from a comma separated list */
	MALLOC_FAIL(maxPath = talloc_array(Globals, char, PATH_MAX))
	for(i = 0, configInfo = NULL, configPaths = UtilSplitString(Globals, configFiles, ','); configPaths[i]; i++){
		/* Determine the real path name */
		if(!realpath(configPaths[i], maxPath)){
			debug(DEBUG_UNEXPECTED, "Can't resolve full path to: %s", configPaths[i]);
			continue;
		}
		/* Make a copy with a more reasonable size */
		MALLOC_FAIL(fullPath = talloc_strdup(Globals, maxPath))
		/* Try to open the config file */
		debug(DEBUG_ACTION,"Trying config file path: %s", fullPath);	
		if((configInfo = ConfReadScan(Globals, fullPath, confDefErrorHandler))){
	
			debug(DEBUG_ACTION,"Using config file: %s", fullPath);
			break;
		}
		free(fullPath); /* Wasn't this path */
	}
	/* Free working strings */
	talloc_free(maxPath);
	talloc_free(configPaths);
			
	if(configInfo){
		String allow, deny;
		
		/* Note the full path to the config file if it is needed in the future */
		
		Globals->configFile = fullPath;
		
		/* Instance ID */
		if((!clOverride.instance_id) && (p = ConfReadValueBySectKey(configInfo, "general", "instance-id"))){
			MALLOC_FAIL(Globals->instanceID = talloc_strdup(Globals, p));
		}
		
		/* xPL IP Address */
		if((!clOverride.interface) && (p = ConfReadValueBySectKey(configInfo, "general", "ip-addr"))){
			MALLOC_FAIL(Globals->ipAddr = talloc_strdup(Globals, p));
		}
		
		/* Xpl Service name or port */
		if((!clOverride.xplserv) && (p = ConfReadValueBySectKey(configInfo, "general", "service"))){
			MALLOC_FAIL(Globals->xplService = talloc_strdup(Globals, p));
		}
			
		/* Control Bind Address */
		if((!clOverride.bindaddress) && (p = ConfReadValueBySectKey(configInfo, "control", "bind-addr"))){
			MALLOC_FAIL(Globals->cmdBindAddress = talloc_strdup(Globals, p));
		}
		
		/* Control Host name */
		if((!clOverride.hostname) && (p = ConfReadValueBySectKey(configInfo, "control", "host"))){
			MALLOC_FAIL(Globals->cmdHostName = talloc_strdup(Globals, p));
		}
		
		/* Control Service name or port */
		if((!clOverride.cmdserv) && (p = ConfReadValueBySectKey(configInfo, "control", "service"))){
			MALLOC_FAIL(Globals->cmdService = talloc_strdup(Globals, p));
		}
			
		/* pid file */
		if((!clOverride.pid_file) && (p = ConfReadValueBySectKey(configInfo, "general", "pid-file"))){
			MALLOC_FAIL(Globals->pidFile = talloc_strdup(Globals, p));
		}
						
		/* log path */
		if((!clOverride.log_path) && (p = ConfReadValueBySectKey(configInfo, "general", "log-path"))){
			talloc_free(Globals->logFile);
			MALLOC_FAIL(Globals->logFile = talloc_strdup(Globals, p))

		}
		
		/* db-file */
		if((!clOverride.dbfile) && (p = ConfReadValueBySectKey(configInfo, "general", "db-file"))){
			MALLOC_FAIL(Globals->dbFile = talloc_strdup(Globals, p));
		}
		
		/* latitude */
		if((!clOverride.dbfile) && (p = ConfReadValueBySectKey(configInfo, "general", "lat"))){
			UtilStod(p, &Globals->lat);
		}
		/* longitude */
		if((!clOverride.dbfile) && (p = ConfReadValueBySectKey(configInfo, "general", "lon"))){
			UtilStod(p, &Globals->lon);
		}
		
		/* Control ACL */
		
		allow = ConfReadValueBySectKey(configInfo, "control", "allow");
		deny = ConfReadValueBySectKey(configInfo, "control", "deny");
		
		if(FAIL == SocketGenACL(Globals, &Globals->controlACL, allow, deny)){
			fatal("Error parsing control allow/deny IP addresses");
		}
	}
	else{
		talloc_free(fullPath);
		debug(DEBUG_UNEXPECTED, "No config file found");
	}
	
	
	/* Free the config info */
	ConfReadFree(configInfo);
	configInfo = NULL;
	
	/* Set the debug level */

	notify_set_debug_level(Globals->debugLvl);

	/* See if database needs to be opened */
 	if(utilityCommand != UC_GENERATE){ /* If not generating a new database file AND ... */
 		if((!utilityCommand) ||  /* If server mode OR... */
 		((dbDirectFlag) && ((utilityCommand == UC_GET_SCRIPT) || /* Direct script get OR ...*/
 		((dbDirectFlag) && (utilityCommand == UC_PUT_SCRIPT))))){ /* Direct script put */
			/* Open the database */
			if(!(Globals->db = DBOpen(Globals->dbFile))){
				fatal("Database file does not exist or is not writaeble: %s", Globals->dbFile);
			}
 		}
	}
	

	/*
	* Convert path to lof file into an absolute path if it isn't already
	*/
	if(Globals->logFile[0]){
		MALLOC_FAIL(maxPath = talloc_array(Globals, char, PATH_MAX))
		if(!realpath(Globals->logFile, maxPath)){
			fatal_with_reason(errno, "%s: Can't resolve log file path", __func__);
		}
		talloc_free(Globals->logFile); /* Free existing path */
		MALLOC_FAIL(Globals->logFile = talloc_strdup(Globals, maxPath))
		talloc_free(maxPath); /* Free max path holding string */
		debug(DEBUG_ACTION, "Absolute path to log file: %s", Globals->logFile);
	}	


	
	
	/* Check for utility commands */
	if(utilityCommand){
		if((clOverride.instance_id) ||
		(clOverride.interface) ||
		(clOverride.cmdserv) ||
		(clOverride.xplserv) ||
		(clOverride.log_path) ||
		(clOverride.pid_file) || 
		(Globals->noBackground) || 
		(Globals->exitOnErr)){
			fatal("Switches: -n -e -i -L -O -P -S or -s are not valid with utility command");
		}
		doUtilityCommand(utilityCommand, utilityArg, utilityFile);
	}
	
	/* Let kernel reap dead children */
	
	sigemptyset(&sa_chld.sa_mask);
	sa_chld.sa_flags = SA_NOCLDWAIT | SA_NOCLDSTOP;
	sigaction(SIGCHLD, &sa_chld, NULL);

	
	/* Mask off the signals we will handle with signalfd */

    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGQUIT);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGHUP);
 
    if(sigprocmask(SIG_BLOCK, &mask, NULL) < 0){
		fatal_with_reason(errno, "%s: Error masking signals", __func__);
	}

	/* Make sure we are not already running (.pid file check). */
	if((pid = UtilPIDRead(Globals->pidFile)) != -1){
		fatal("%s is already running as pid: %d", Globals->progName, pid);
	}
	
	/* Fork into the background. */	
	if(Globals->noBackground) {
		if((UtilPIDWrite(Globals->pidFile, getpid()) != 0)) {
			debug(DEBUG_UNEXPECTED, "Could not write pid file '%s'.", Globals->pidFile);
		}
		Globals->weWroteThePIDFile = TRUE;
	}
	else{
		int retval;
		

		debug(DEBUG_STATUS, "Forking into background");

    	/* 
		* If debugging is enabled, redirect the debug output to a log file if
    	* the path to the logfile is defined
		*/

		if((Globals->logFile[0])){
			note("Logging to file: %s, debug level: %d", Globals->logFile, Globals->debugLvl);                          
			notify_logpath(Globals->logFile);
		}	
	
		/* Fork and exit the parent */

		if((retval = fork())){
      			if(retval > 0)
				exit(0);  /* Exit parent */
			else
				fatal_with_reason(errno, "parent fork");
    		}
    		
    	/* We forked successfully */
    	
    	/* Write the pid file */	
		if((UtilPIDWrite(Globals->pidFile, getpid()) != 0)) {
			debug(DEBUG_UNEXPECTED, "Could not write pid file '%s'.", Globals->pidFile);
		}
		/* Indicate we wrote the pid file */
		Globals->weWroteThePIDFile = TRUE;
		
	
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
		
		/* Set up the monitor using the forked and detached process */


 
	}
	
	/*
	* Must have IP address
	*/
	
	if(!Globals->ipAddr){
		fatal("No IP address specified");
	}
	
	/*
	* Create poller object
	*/
	
	if(!(Globals->poller = PollInit(Globals, 4))){
		fatal("Could not create poller object");
	}
	
		
	/* Create the signalfd and add it to the poller object */
	if(-1 == (sfd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC))){
		fatal_with_reason(errno, "%s: signalfd:", __func__);
	}
	if(FAIL == PollRegEvent(Globals->poller, sfd, POLL_WT_IN, signalHandler, NULL)){
		fatal("Cannot register signal handler");
	}
	
	
	/* Initialize monitor */
	
	debug(DEBUG_STATUS,"Initializing Monitor");
	
	MonitorSetup();
	
	debug(DEBUG_STATUS,"Running Monitor");
	
	
	if(FAIL == PollWait(Globals->poller, 1000, NULL)){
		fatal("Poll error detected");
	}

	exit(1);
}

