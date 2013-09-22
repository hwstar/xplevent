#ifndef SCHEDULER_H
#define SCHEDULER_H

void SchedulerDo(void *schedInfo);
void *SchedulerInit(TALLOC_CTX *ctx, double lat, double lon);
void SchedulerStart(void *schedInfo);
void SchedulerStop(void *schedInfo);
void SchedularRemoveAllEntries(void *schedInfo);
void SchedulerAdd(void *schedInfo, String entryName, String typeParam,  
	void (*exec)(TALLOC_CTX *ctx, const String entryName, const String execParam),  String execParam);
void SchedulerDefaultHandler(TALLOC_CTX *ctx, const String entryName, const String execParam);






#endif
