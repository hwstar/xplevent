
#define _GNU_SOURCE
#include <stdlib.h>
#include <time.h>

#include "defs.n"
#include "types.h"
#include ""


/* Scheduler */

static void SchedulerDo(struct rpt *myrpt)
{
	int i,res;

	struct tm tmnow;
	struct ast_variable *skedlist;
	char *strs[5],*vp,*val,value[100];

	memcpy(&myrpt->lasttv, &myrpt->curtv, sizeof(struct timeval));
	
	if( (res = gettimeofday(&myrpt->curtv, NULL)) < 0)
		ast_log(LOG_NOTICE, "Scheduler gettime of day returned: %s\n", strerror(res));

	/* Try to get close to a 1 second resolution */
	
	if(myrpt->lasttv.tv_sec == myrpt->curtv.tv_sec)
		return;

	/* Service the sleep timer */
	if(myrpt->p.s[myrpt->p.sysstate_cur].sleepena){ /* If sleep mode enabled */
		if(myrpt->sleeptimer)
			myrpt->sleeptimer--;
		else{
			if(!myrpt->sleep)
				myrpt->sleep = 1; /* ZZZZZZ */
		}
	}
	/* Service activity timer */
	if(myrpt->p.lnkactmacro && myrpt->p.lnkacttime && myrpt->p.lnkactenable && myrpt->linkactivityflag){
		myrpt->linkactivitytimer++;
		/* 30 second warn */
		if ((myrpt->p.lnkacttime - myrpt->linkactivitytimer == 30) && myrpt->p.lnkacttimerwarn){
			if(debug > 4)
				ast_log(LOG_NOTICE, "Warning user of activity timeout\n");
			rpt_telemetry(myrpt,LOCALPLAY, myrpt->p.lnkacttimerwarn);
		}
		if(myrpt->linkactivitytimer >= myrpt->p.lnkacttime){
			/* Execute lnkactmacro */
			if ((MAXMACRO - strlen(myrpt->macrobuf)) < strlen(myrpt->p.lnkactmacro)){
				ast_log(LOG_WARNING, "Link Activity timer could not execute macro %s: Macro buffer full\n",
					myrpt->p.lnkactmacro);
			}
			else{
				if(debug > 4)
					ast_log(LOG_NOTICE, "Executing link activity timer macro %s\n", myrpt->p.lnkactmacro);
				myrpt->macrotimer = MACROTIME;
				strncat(myrpt->macrobuf,myrpt->p.lnkactmacro, MAXMACRO - 1);
			}
			myrpt->linkactivitytimer = 0;
			myrpt->linkactivityflag = 0;
		}
	}
	/* Service repeater inactivity timer */
	if(myrpt->p.rptinacttime && myrpt->rptinactwaskeyedflag){
		if(myrpt->rptinacttimer < myrpt->p.rptinacttime)
			myrpt->rptinacttimer++;
		else{
			myrpt->rptinacttimer = 0;
			myrpt->rptinactwaskeyedflag = 0;
			if ((MAXMACRO - strlen(myrpt->macrobuf)) < strlen(myrpt->p.rptinactmacro)){
				ast_log(LOG_WARNING, "Rpt inactivity timer could not execute macro %s: Macro buffer full\n",
					myrpt->p.rptinactmacro);
			}
			else {
				if(debug > 4)
					ast_log(LOG_NOTICE, "Executing rpt inactivity timer macro %s\n", myrpt->p.rptinactmacro);
				myrpt->macrotimer = MACROTIME;
				strncat(myrpt->macrobuf, myrpt->p.rptinactmacro, MAXMACRO -1);
			}
		}
	}
			
	rpt_localtime(&myrpt->curtv.tv_sec, &tmnow, NULL);

	/* If midnight, then reset all daily statistics */
	
	if((tmnow.tm_hour == 0)&&(tmnow.tm_min == 0)&&(tmnow.tm_sec == 0)){
		myrpt->dailykeyups = 0;
		myrpt->dailytxtime = 0;
		myrpt->dailykerchunks = 0;
		myrpt->dailyexecdcommands = 0;
	}

	if(tmnow.tm_sec != 0)
		return;

	/* Code below only executes once per minute */


	/* Don't schedule if remote */

        if (myrpt->remote)
                return;

	/* Don't schedule if disabled */

        if(myrpt->p.s[myrpt->p.sysstate_cur].schedulerdisable){
		if(debug > 6)
			ast_log(LOG_NOTICE, "Scheduler disabled\n");
		return;
	}

	if(!myrpt->p.skedstanzaname){ /* No stanza means we do nothing */
		if(debug > 6)
			ast_log(LOG_NOTICE,"No stanza for scheduler in rpt.conf\n");
		return;
	}

    /* get pointer to linked list of scheduler entries */
    skedlist = ast_variable_browse(myrpt->cfg, myrpt->p.skedstanzaname);

	if(debug > 6){
		ast_log(LOG_NOTICE, "Time now: %02d:%02d %02d %02d %02d\n",
			tmnow.tm_hour,tmnow.tm_min,tmnow.tm_mday,tmnow.tm_mon + 1, tmnow.tm_wday); 
	}
	/* walk the list */
	for(; skedlist; skedlist = skedlist->next){
		if(debug > 6)
			ast_log(LOG_NOTICE, "Scheduler entry %s = %s being considered\n",skedlist->name, skedlist->value);
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
		if(debug > 6)
			ast_log(LOG_NOTICE, "i = %d, min = %s, hour = %s, mday=%s, mon=%s, wday=%s\n",i,
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
			if(debug)
				ast_log(LOG_NOTICE, "Executing scheduler entry %s = %s\n", skedlist->name, skedlist->value);
			if(atoi(skedlist->name) == 0)
				return; /* Zero is reserved for the startup macro */
			val = (char *) ast_variable_retrieve(myrpt->cfg, myrpt->p.macro, skedlist->name);
			if (!val){
				ast_log(LOG_WARNING,"Scheduler could not find macro %s\n",skedlist->name);
				return; /* Macro not found */
			}
			if ((MAXMACRO - strlen(myrpt->macrobuf)) < strlen(val)){
				ast_log(LOG_WARNING, "Scheduler could not execute macro %s: Macro buffer full\n",
					skedlist->name);
				return; /* Macro buffer full */
			}
			myrpt->macrotimer = MACROTIME;
			strncat(myrpt->macrobuf,val,MAXMACRO - 1);
		}
		else{
			ast_log(LOG_WARNING,"Malformed scheduler entry in rpt.conf: %s = %s\n",
				skedlist->name, skedlist->value);
		}
	}

}
