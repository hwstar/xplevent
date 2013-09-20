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
		debug(DEBUG_STATUS, "Scheduler entry %s = %s being considered\n",skedlist->name, skedlist->value);
		strncpy(value,skedlist->value,99);
		value[99] = 0;
		/* point to the substrings for minute, hour, dom, month, and dow */
		for( i = 0, vp = value ; i < 5; i++){
			if(!*vp)
				break;
			while((*vp == ' ') || (*vp == 0x09)) /* get rid of any leading white space */
				vp++;
			strs[i] = vp; /* save pointer to beginning of substring */
			while((*vp != ' ') && (*vp != 0x09) && (*vp != 0)) /* skip over substring */
				vp++;
			if(*vp)
				*vp++ = 0; /* mark end of substring */
		}
		debug(DEBUG_STATUS, "i = %d, min = %s, hour = %s, mday=%s, mon=%s, wday=%s\n",i,
			strs[0], strs[1], strs[2], strs[3], strs[4]); 
 		if(i == 5){
			if((*strs[0] != '*')&&(atoi(strs[0]) != tmnow.tm_min))
				continue;
			if((*strs[1] != '*')&&(atoi(strs[1]) != tmnow.tm_hour))
				continue;
			if((*strs[2] != '*')&&(atoi(strs[2]) != tmnow.tm_mday))
				continue;
			if((*strs[3] != '*')&&(atoi(strs[3]) != tmnow.tm_mon + 1))
				continue;
			if(atoi(strs[4]) == 7)
				strs[4] = "0";
			if((*strs[4] != '*')&&(atoi(strs[4]) != tmnow.tm_wday))
				continue;
			debug(DEBUG_STATUS, "Executing scheduler entry %s = %s\n", skedlist->name, skedlist->value);

		}
		else{
			debug(DEBUG_UNEXPECTED,"Malformed scheduler entry in rpt.conf: %s = %s\n",
				skedlist->name, skedlist->value);
		}
	}

}
