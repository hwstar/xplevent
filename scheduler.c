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
	struct SchListEntryPtr_t schedlist;

	
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(sch)


	
	/* Get seconds */
	time(&now);

	
	/* 1 minute resolution */
	
	if(sch->now == sch->prevNow)
		return;
		
	/* Code below only executes once per second */
	sch->prevNow = sch->now;

	
	/* walk the list */
	for(schedlist = sch->head; schedlist; schedlist = schedlist->next){

	}

}
