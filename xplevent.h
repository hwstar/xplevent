#ifndef XPLEVENT_H
#define XPLEVENT_H

typedef struct XPLEvGlobals_s {
	Bool exitOnErr;
	Bool exitRequest;
	Bool noBackground;
	int debugLvl;
	String progName;
	String pidFile;
	String instanceID;
	void *masterCTX;
	void *xplEventService;
	void *configEntry;
	void *db;	
} XPLEvGlobals_t;

typedef XPLEvGlobals_t * XPLEvGlobalsPtr_t;
	
extern XPLEvGlobalsPtr_t Globals;


void XPLEventExit(int returnCode);

#endif


