#ifndef XPLEVENT_H
#define XPLEVENT_H

typedef struct XPLEvGlobals_s {
	Bool exitOnErr;
	Bool noBackground;
	Bool schInitTried;
	int debugLvl;
	String progName;
	String cmdBindAddress;
	String cmdHostName;
	String cmdService;
	String configFile;
	String pidFile;
	String dbFile;
	String logFile;
	String interface;
	String instanceID;
	void *xplEventService;
	void *db;	
	void *sch;
	double lat;
	double lon;
} XPLEvGlobals_t;

typedef XPLEvGlobals_t * XPLEvGlobalsPtr_t;
	
extern XPLEvGlobalsPtr_t Globals;

Bool XpleventCheckExit(void);

#endif


