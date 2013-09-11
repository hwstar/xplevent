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
#include "db.h"
#include "monitor.h"
#include "xplevent.h"




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
	Globals->exitRequest = TRUE;
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
	printf("  -c, --config-file PATH  Set the path to the config file\n");
	printf("  -d, --debug LEVEL       Set the debug level, 0 is off, the\n");
	printf("                          compiled-in default is %d and the max\n", Globals->debugLvl);
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
	TALLOC_CTX *m;
	
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
				Globals->noBackground = TRUE;
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
	
	if((Globals->configEntry = ConfReadScan(Globals->masterCTX, configFile, confDefErrorHandler))){
		debug(DEBUG_ACTION,"Using config file: %s", configFile);
		/* Instance ID */
		if((!clOverride.instance_id) && (p = ConfReadValueBySectKey(Globals->configEntry, "general", "instance-id")))
			ConfReadStringCopy(instanceID, p, sizeof(instanceID));
		
		/* Interface */
		if((!clOverride.interface) && (p = ConfReadValueBySectKey(Globals->configEntry, "general", "interface")))
			ConfReadStringCopy(interface, p, sizeof(interface));
			
		/* pid file */
		if((!clOverride.pid_file) && (p = ConfReadValueBySectKey(Globals->configEntry, "general", "pid-file")))
			ConfReadStringCopy(pidFile, p, sizeof(pidFile));	
						
		/* log path */
		if((!clOverride.log_path) && (p = ConfReadValueBySectKey(Globals->configEntry, "general", "log-path")))
			ConfReadStringCopy(logPath, p, sizeof(logPath));
		
		/* db-file */
		if((!clOverride.dbfile) && (p = ConfReadValueBySectKey(Globals->configEntry, "general", "db-file")))
			ConfReadStringCopy(dbFile, p, sizeof(dbFile));
		
	}
	else
		debug(DEBUG_UNEXPECTED, "Config file %s not found or not readable", configFile);
	
	/* Set strings in Global Data*/
	
	Globals->pidFile = pidFile;
	Globals->instanceID = instanceID;
	

	/* Set the broadcast interface */
	
	MonitorPreForkSetup(interface);	
		
	
	if(!(Globals->db = DBOpen(dbFile))){
			fatal("Database file does not exist or is not writeble: %s", dbFile);
	}

	/* Fork into the background. */	
	if(!Globals->noBackground) {
		int retval;
		
		/* Make sure we are not already running (.pid file check). */
		if(pid_read(pidFile) != -1) 
			fatal("%s is already running", Globals->progName);
			
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
	
		if(!Globals->noBackground && (pid_write(pidFile, getpid()) != 0)) {
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
	
	/* Install signal traps for proper shutdown */
 	signal(SIGTERM, shutdownHandler);
 	signal(SIGINT, shutdownHandler);

	
	debug(DEBUG_STATUS,"Initializing Monitor");
	
	MonitorRun();

	exit(1);
}

