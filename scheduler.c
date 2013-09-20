#define _GNU_SOURCE
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "defs.h"
#include "types.h"
#include "notice.h"
#include "utils.h"
#include "scheduler.h"
#include "xplevent.h"


/* Scheduler */

static void SchedulerDo(TALLOC_CTX *ctx, SchedInfoPtr_t sch)
{
	int i,res;

	struct tm tmnow;
	struct SchListEntryPtr_t skedlist;
	char *strs[5],*vp,*val,value[100];
	
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(sch)

	memcpy(&sch->lasttv, &sch->curtv, sizeof(struct timeval));
	
	/* Get seconds and microseconds since epoch */
	if( (res = gettimeofday(&sch->curtv, NULL)) < 0){
		debug(DEBUG_UNEXPECTED, "Scheduler gettime of day returned: %s\n", strerror(res));
		
	}
	
	/* 1 minute resolution */
	
	if(sch->lasttv.tv_min == sch->curtv.tv_min)
		return;

	/* Code below only executes once per minute */

	/* Convert time to struct */
	localtime_r(&sch->curtv.tv_sec, &tmnow);

	/* If midnight, then reset all daily statistics */
	
	if((tmnow.tm_hour == 0)&&(tmnow.tm_min == 0){
	}



	debug(DEBUG_ACTION, "Time now: %02d:%02d %02d %02d %02d\n",
		tmnow.tm_hour,tmnow.tm_min,tmnow.tm_mday,tmnow.tm_mon + 1, tmnow.tm_wday); 
	
	/* walk the list */
	for(skedlist = sch->list; skedlist; skedlist = skedlist->next){

	}

}
