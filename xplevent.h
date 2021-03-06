#ifndef XPLEVENT_H
#define XPLEVENT_H

/* Variables which need to be accessed across modules */

typedef struct XPLEvGlobals_s {
	Bool exitOnErr;
	Bool noBackground;
	Bool weWroteThePIDFile;
	int debugLvl;
	int timerFD;
	String progName;
	String cmdBindAddress;
	String cmdHostName;
	String cmdService;
	String configFile;
	String pidFile;
	String dbFile;
	String logFile;
	String ipAddr;
	String xplService;
	String instanceID;
	void *poller;
	void *xplObj;
	void *xplEventService;
	void *db;	
	void *sch;
	void *controlACL;
	double lat;
	double lon;
} XPLEvGlobals_t;

typedef XPLEvGlobals_t * XPLEvGlobalsPtr_t;
	
extern XPLEvGlobalsPtr_t Globals;

#endif


