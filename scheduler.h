#ifndef SCHEDULER_H
#define SCHEDULER_H

typedef enum {ST_CRON, ST_EPHEM } SchedType_t;

void SchedulerDo(void *schedInfo);
void *SchedulerInit(TALLOC_CTX *ctx, double lat, double lon);
void SchedulerStart(void *schedInfo);
void SchedulerStop(void *schedInfo);
void SchedularRemoveAllEntries(void *schedInfo);
void SchedulerAdd(void *schedInfo, String entryName, SchedType_t type, String typeParam,  
	void (*exec)(TALLOC_CTX *ctx, const String entryName, const String execParam),  String execParam);
void SchedulerDefaultHandler(TALLOC_CTX *ctx, const String entryName, const String execParam);






#endif
