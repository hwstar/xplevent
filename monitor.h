#ifndef MONITOR_H
#define MONITOR_H


typedef enum {RS_IDLE = 0, RS_WAIT_LINE, RS_FINISHED, RS_ERROR} rs_state_t;

typedef struct MonRcvInfo_s {
	String script;
	unsigned scriptLen;
	unsigned scriptBufSize;
	unsigned scriptSizeLimit;
	int userSock;
	rs_state_t state;
} MonRcvInfo_t;


void MonitorPreForkSetup(String interface, String instance_id);
void MonitorRun(void);
void MonitorSendScript(TALLOC_CTX *ctx, int userSock, String theScript, String id);
Bool MonitorRecvScript(MonRcvInfoPtr_t ri, String line);


#endif
