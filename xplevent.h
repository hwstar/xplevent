#ifndef XPLEVENT_H
#define XPLEVENT_H

typedef struct XPLEvGlobals_s {
	Bool exitOnErr;
	Bool noBackground;
	int debugLvl;
	String progName;
	String cmdBindAddress;
	String cmdHostName;
	String cmdService;
	String pidFile;
	String dbFile;
	String interface;
	String instanceID;
	void *xplEventService;
	void *db;	
} XPLEvGlobals_t;

typedef XPLEvGlobals_t * XPLEvGlobalsPtr_t;
	
extern XPLEvGlobalsPtr_t Globals;

Bool XpleventCheckExit(void);

#endif


