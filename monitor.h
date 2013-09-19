#ifndef MONITOR_H
#define MONITOR_H
void MonitorPreForkSetup(String interface, String instance_id);
void MonitorRun(void);
void MonitorSendScript(TALLOC_CTX *ctx, int userSock, String theScript, String id);
Bool MonitorRecvScript(MonRcvInfoPtr_t ri, String line);


#endif
