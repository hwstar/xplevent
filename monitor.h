#ifndef MONITOR_H
#define MONITOR_H


typedef enum {RS_IDLE = 0, RS_WAIT_LINE, RS_FINISHED, RS_ERROR} rs_state_t;

typedef struct MonRcvInfo_s {
	String name;
	String script;
	String errMsg;
	unsigned scriptLen;
	unsigned scriptBufSize;
	unsigned scriptSizeLimit;
	rs_state_t state;
} MonRcvInfo_t;

typedef MonRcvInfo_t * MonRcvInfoPtr_t;

void MonitorSetup(void);
void MonitorRun(void);
void MonitorSendScript(TALLOC_CTX *ctx, int userSock, String theScript, String id);
Bool MonitorRecvScript(MonRcvInfoPtr_t ri, String line);


#endif
