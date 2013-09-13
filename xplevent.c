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
#include <sys/wait.h>
#include <xPL.h>
#include <sqlite3.h>
#include <talloc.h>
#include  "defs.h"
#include "types.h"
#include "notify.h"
#include "confread.h"
#include "db.h"
#include "parser.h"
#include "monitor.h"
#include "util.h"
#include "xplevent.h"


enum {UC_CHECK_SYNTAX = 1, UC_GET_SCRIPT, UC_PUT_SCRIPT};


#define SHORT_OPTIONS "C:c:d:ef:g:Hi:L:no:p:s:V"

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





XPLEvGlobalsPtr_t Globals = NULL;

static clOverride_t clOverride = {0,0,0,0,0};

static char configFile[WS_SIZE] = DEF_CONFIG_FILE;
static char interface[WS_SIZE] = DEF_INTERFACE;
static char logPath[WS_SIZE] = "";
static char instanceID[WS_SIZE] = DEF_INSTANCE_ID;
static char pidFile[WS_SIZE] = DEF_PID_FILE;
static char dbFile[WS_SIZE] = DEF_DB_FILE;



/* Commandline options. */

static struct option longOptions[] = {
	{"check",1 ,0 ,'c'},
	{"config-file", 1, 0, 'C'},
	{"debug", 1, 0, 'd'},
	{"exitonerr",0, 0, 'e'},
	{"pid-file", 0, 0, 'f'},
	{"get", 1, 0, 'g'},
	{"help", 0, 0, 'H'},
	{"interface", 1, 0, 'i'},
	{"log", 1, 0, 'L'},
	{"no-background", 0, 0, 'n'},	
	{"db-file", 1, 0, 'o'},
	{"put", 1, 0, 'p'},
	{"instance", 1, 0, 's'},
	{"version", 0, 0, 'V'},
	{0, 0, 0, 0}
};





/*
* When the user hits ^C, logically shutdown
* (including telling the network the service is ending)
*/

static void shutdownHandler(int onSignal)
{
	Globals->exitRequest = TRUE;
}

/*
* Reap zombie child processes
*/

static void reaper(int onSignal)
{
	int status;
	while (waitpid(-1, &status, WNOHANG) > 0);
}

/*
* Show help
*/

void showHelp(void)
{
	printf("'%s' is a daemon that XXXXXXXXXX\n", Globals->progName);
	printf("via XXXXXXXXXXX\n");
	printf("\n");
	printf("Usage: %s [OPTION]...\n", Globals->progName);
	printf("\n");
	printf("  -C, --config-file PATH  Set the path to the config file\n");
	printf("  -c, --check PATH        Check script file syntax\n");
	printf("  -d, --debug LEVEL       Set the debug level, 0 is off, the\n");
	printf("                          compiled-in default is %d and the max\n", Globals->debugLvl);
	printf("  -e --exitonerr          Exit on parse or execution error\n");
	printf("                          level allowed is %d\n", DEBUG_MAX);
	printf("  -f, --pid-file PATH     Set new pid file path, default is: %s\n", pidFile);
	printf("  -g, --get script file   Get script name from database and write to file");
	printf("  -h, --help              Shows this\n");
	printf("  -i, --interface NAME    Set the broadcast interface (e.g. eth0)\n");
	printf("  -L, --log  PATH         Path name to debug log file when daemonized\n");
	printf("  -n, --no-background     Do not fork into the background (useful for debugging)\n");
	printf("  -o, --db-file           Database file");
	printf("  -p, --put file script   Put file in script name");
	printf("  -s, --instance ID       Set instance id. Default is %s", instanceID);
	printf("  -V, --version           Display program version\n");
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
 * Shutdown and exit
 */ 

static void shutdown(void)
{
	DBClose(Globals->db);
	if(!Globals->noBackground){
		/* Unlink the pid file if we can. */
		(void) unlink(pidFile);
	}
	if(Globals->masterCTX){
		TALLOC_CTX *m = Globals->masterCTX;
		talloc_free(m);
	}
}


/*
 * Do utility command and exit
 */
void doUtilityCommand(int utilityCommand, String utilityArg, String utilityExtra)
{
	int res = 0;
	String s;
	
	
	debug(DEBUG_ACTION, "Util cmd: %d, arg: %s, extra: %s", utilityCommand,
	(utilityArg)? utilityArg : "(nil)",
	(utilityExtra)? utilityExtra : "(nil)");
	
	switch(utilityCommand){
		case UC_CHECK_SYNTAX:
			s = ParserCheckSyntax(Globals->masterCTX, utilityArg);
			if(s){
				fatal("%s", s);
			}
			break;
	
		default:
			ASSERT_FAIL(0);
	}
	exit(res);
}


/*
* main
*/


int main(int argc, char *argv[])
{
	int longindex;
	int optchar;
	String p;
	TALLOC_CTX *m;
	int utilityCommand = 0;
	String utilityArg = NULL;
	String utilityExtra = NULL;

	
	if(!(m = talloc_new(NULL))){
		fprintf(stderr, "Memory allocation failed in file %s on line %d\n", __FILE__, __LINE__);
		exit(1);
	}
	
	if(!(Globals = talloc_zero(m, XPLEvGlobals_t))){
		fprintf(stderr, "Memory allocation failed in file %s on line %d\n", __FILE__, __LINE__);
		exit(1);
	}
	Globals->masterCTX = m;
	
	Globals->progName = argv[0];
	
	atexit(shutdown);
	

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
			case 'C':
				UtilStringCopy(configFile, optarg, WS_SIZE - 1);
				debug(DEBUG_ACTION,"New config file path is: %s", configFile);
				break;
				
			case 'c': /* Check syntax of a script file */
				utilityCommand = UC_CHECK_SYNTAX;
				MALLOC_FAIL(utilityArg = talloc_strdup(Globals->masterCTX, optarg))
				break;
				
				/* Was it a debug level set? */
			case 'd':

				/* Save the value. */
				Globals->debugLvl=atoi(optarg);
				if(Globals->debugLvl < 0 || Globals->debugLvl > DEBUG_MAX) {
					fatal("Invalid debug level");
				}

				break;
				
			case 'e':
				Globals->exitOnErr = TRUE;
				break;

			/* Was it a pid file switch? */
			case 'f':
				UtilStringCopy(pidFile, optarg, WS_SIZE - 1);
				clOverride.pid_file = 1;
				debug(DEBUG_ACTION,"New pid file path is: %s", pidFile);
				break;
				
			case 'g': /* Get script */
				utilityCommand = UC_GET_SCRIPT;
				MALLOC_FAIL(utilityArg = talloc_strdup(Globals->masterCTX, optarg))
				break;
			
			
				/* Was it a help request? */
			case 'h':
				showHelp();
				exit(0);

				/* Specify interface to broadcast on */
			case 'i': 
				UtilStringCopy(interface, optarg, WS_SIZE -1);
				clOverride.interface = 1;
				break;

			case 'L':
				/* Override log path*/
				UtilStringCopy(logPath, optarg, WS_SIZE - 1);
				clOverride.log_path = 1;
				debug(DEBUG_ACTION,"New log path is: %s",
				logPath);

				break;

				/* Was it a no-backgrounding request? */
			case 'n':
				/* Mark that we shouldn't background. */
				Globals->noBackground = TRUE;
				break;

			
			case 'o': /* Database file */
				UtilStringCopy(dbFile, optarg, WS_SIZE);
				clOverride.dbfile = 1;
				debug(DEBUG_ACTION,"New db file is: %s", dbFile);
				break;		
				
				
			case 'p': /* Put Script */
				utilityCommand = UC_PUT_SCRIPT;
				MALLOC_FAIL(utilityArg = talloc_strdup(Globals->masterCTX, optarg))
				break;
			
			
			case 's': /* Instance ID */
				UtilStringCopy(instanceID, optarg, WS_SIZE);
				clOverride.instance_id = 1;
				debug(DEBUG_ACTION,"New instance ID is: %s", instanceID);
				break;


				/* Was it a version request? */
			case 'V':
				printf("Version: %s\n", VERSION);
				exit(0);
	
			
				/* It was something weird.. */
			default:
				fatal("Unhandled getopt return value %d", optchar);
		}
	}

	
	/* If there were any extra arguments, we may complain. */

	if(optind < argc) {
		if((utilityCommand == UC_PUT_SCRIPT)||(utilityCommand == UC_GET_SCRIPT)){
			if(argv[optind]){
				MALLOC_FAIL(utilityExtra = talloc_strdup(Globals->masterCTX, argv[optind]));
			}
			else{
				fatal("Missing second parameter for utility command");
			}
		}
		else{
			fatal("Extra argument on command line: %s", argv[optind]);
		}
	}

	/* Attempt to read a config file */
	
	if((Globals->configEntry = ConfReadScan(Globals->masterCTX, configFile, confDefErrorHandler))){
		debug(DEBUG_ACTION,"Using config file: %s", configFile);
		/* Instance ID */
		if((!clOverride.instance_id) && (p = ConfReadValueBySectKey(Globals->configEntry, "general", "instance-id")))
			UtilStringCopy(instanceID, p, sizeof(instanceID));
		
		/* Interface */
		if((!clOverride.interface) && (p = ConfReadValueBySectKey(Globals->configEntry, "general", "interface")))
			UtilStringCopy(interface, p, sizeof(interface));
			
		/* pid file */
		if((!clOverride.pid_file) && (p = ConfReadValueBySectKey(Globals->configEntry, "general", "pid-file")))
			UtilStringCopy(pidFile, p, sizeof(pidFile));	
						
		/* log path */
		if((!clOverride.log_path) && (p = ConfReadValueBySectKey(Globals->configEntry, "general", "log-path")))
			UtilStringCopy(logPath, p, sizeof(logPath));
		
		/* db-file */
		if((!clOverride.dbfile) && (p = ConfReadValueBySectKey(Globals->configEntry, "general", "db-file")))
			UtilStringCopy(dbFile, p, sizeof(dbFile));
		
	}
	else{
		warn("Config file %s not found or not readable", configFile);
	}
	
	/* Install signal traps for proper shutdown */
 	signal(SIGTERM, shutdownHandler);
 	signal(SIGINT, shutdownHandler);
 	signal(SIGCHLD, reaper);

		
	/* Open the database */
	if(!(Globals->db = DBOpen(dbFile))){
			fatal("Database file does not exist or is not writeble: %s", dbFile);
	}
	

	/* Check for utility commands */
	if(utilityCommand){
		if(Globals->noBackground){
			fatal("-n switch not valid with utility command");
		}
		if(Globals->exitOnErr){
			fatal("-e switch not valid with utility command");
		}		
		doUtilityCommand(utilityCommand, utilityArg, utilityExtra);
	}
	
	

	/* Make sure we are not already running (.pid file check). */
	if(UtilPIDRead(pidFile) != -1){
		fatal("%s is already running", Globals->progName);
	}
	
	/* Set the broadcast interface */
	
	MonitorPreForkSetup(interface, instanceID);	


	/* Fork into the background. */	
	if(!Globals->noBackground) {
		int retval;
		
		debug(DEBUG_STATUS, "Forking into background");

    	/* 
		* If debugging is enabled, redirect the debug output to a log file if
    	* the path to the logfile is defined
		*/

		if((Globals->debugLvl) && (logPath[0]))                          
			notify_logpath(logPath);
			
	
		/* Fork and exit the parent */

		if((retval = fork())){
      			if(retval > 0)
				exit(0);  /* Exit parent */
			else
				fatal_with_reason(errno, "parent fork");
    		}
	
		if(!Globals->noBackground && (UtilPIDWrite(pidFile, getpid()) != 0)) {
			debug(DEBUG_UNEXPECTED, "Could not write pid file '%s'.", pidFile);
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


	
	debug(DEBUG_STATUS,"Initializing Monitor");
	
	MonitorRun();

	exit(1);
}

