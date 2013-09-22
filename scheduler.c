#define _GNU_SOURCE
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <math.h>
#include <string.h>
#include <talloc.h>

#include "defs.h"
#include "types.h"
#include "notify.h"
#include "util.h"
#include "sunriset.h"
#include "scheduler.h"

#define SI_MAGIC 0x194A6F2C
#define SE_MAGIC 0x8AC5E461


typedef struct SchedListEntry_s{
  unsigned magic;
  Bool hasRun;
  String typeParam;
  String entryName;
  String execParam;
  String *cronSubstrs;
  void (*exec)(TALLOC_CTX *ctx, const String entryName, const String execParam);
  struct SchedListEntry_s *next;
  struct SchedListEntry_s *prev;
} SchedListEntry_t;

typedef SchedListEntry_t * SchedListEntryPtr_t;

typedef struct SchedInfo_s{
  unsigned magic;
  int dawnHour;
  int dawnMinute;
  int duskHour;
  int duskMinute;
  int sunriseHour;
  int sunriseMinute;
  int sunsetHour;
  int sunsetMinute;
  Bool enabled;
  Bool startupRan;
  Bool ddValid;
  Bool ssValid;
  SchedListEntryPtr_t head;
  SchedListEntryPtr_t tail;
  void *listContext;
  double lat;
  double lon;
  double utcOffset;
  struct tm tmNow;
  struct tm tmPrevNow;
} SchedInfo_t;

typedef SchedInfo_t * SchedInfoPtr_t;


/*
 * Calculate sunrise/sunset times
 */

static void calcDawnDusk(SchedInfoPtr_t sch)
{
	int res;
	time_t utc, local, now; 
	struct tm utctm, localtm;
	double utcOffsetSeconds;
	double civ_start_utc, civ_start, civ_end_utc, civ_end;
	double sunrise_utc, sunrise, sunset_utc, sunset;
	
	
	/* Recalculate the offset to GMT */
	time(&now);
	gmtime_r(&now, &utctm);
	localtime_r(&now, &localtm);
	local = mktime(&localtm);
	utc = mktime(&utctm);
	
	/* Calculate utc offset */
	utcOffsetSeconds = difftime(local, utc);
	
	/* Adjust for daylight savings time */
	if(localtm.tm_isdst){
		utcOffsetSeconds += 3600;
	}
	/* Convert to hours */
	sch->utcOffset = utcOffsetSeconds / 3600; 
	
	debug(DEBUG_ACTION, "*** Calculate dawn/sunrise and sunset/dusk times ***");
	debug(DEBUG_ACTION, "UTC offset in hours: %2.3e", sch->utcOffset);
	debug(DEBUG_ACTION, "Latitude used: %2.5e", sch->lat);
	debug(DEBUG_ACTION, "Longitude used: %3.5e", sch->lon);
	/* Calculate civil day length */
	res  = civil_twilight( 1900.0 + localtm.tm_year, localtm.tm_mon, localtm.tm_mday,
		sch->lon, sch->lat, &civ_start_utc, &civ_end_utc );
	if(!res){
	/*	debug(DEBUG_ACTION, "UTC dawn start: %2.5e", civ_start_utc); */
	/*	debug(DEBUG_ACTION, "UTC_dusk start: %3.5e", civ_end_utc); */
		sch->ddValid = TRUE;
		civ_start = civ_start_utc + sch->utcOffset;
		if(civ_start < 0.0){
			civ_start += 24.0;
		}
		civ_end = civ_end_utc + sch->utcOffset;
		if(civ_end < 0.0){
			civ_end += 24.0;
		}	
		sch->duskHour = (int) civ_end;
		sch->duskMinute = (int)(( civ_end - (double) sch->duskHour) * 60.0);
		sch->dawnHour = (int) civ_start;
		sch->dawnMinute = (int)(( civ_start - (double) sch->dawnHour) * 60.0);
		
		debug(DEBUG_ACTION, "Dawn start: %02d:%02d", sch->dawnHour, sch->dawnMinute);
		debug(DEBUG_ACTION, "Twilight end: %02d:%02d", sch->duskHour, sch->duskMinute);	
	}
	else{
		debug(DEBUG_ACTION, "Dusk and dawn times are not valid");
		sch->ddValid = FALSE;
	}	
	res  = sun_rise_set( 1900.0 + localtm.tm_year, localtm.tm_mon, localtm.tm_mday,
		sch->lon, sch->lat, &sunrise_utc, &sunset_utc );
	if(!res){
/*		debug(DEBUG_ACTION, "UTC Sunrise: %2.5e", sunrise_utc); */
/*		debug(DEBUG_ACTION, "UTC Sunset: %3.5e", sunset_utc); */
		sch->ssValid = TRUE;
		sunrise = sunrise_utc + sch->utcOffset;
		if(sunrise < 0.0){
			sunrise += 24.0;
		}
		sunset = sunset_utc + sch->utcOffset;
		if(sunset < 0.0){
			sunset += 24.0;
		}	
		sch->sunsetHour = (int) sunset;
		sch->sunsetMinute = (int)(( sunset - (double) sch->sunsetHour) * 60.0);
		sch->sunriseHour = (int) sunrise;
		sch->sunriseMinute = (int)(( sunrise - (double) sch->sunriseHour) * 60.0);
		
		debug(DEBUG_ACTION, "Sunrise: %02d:%02d", sch->sunriseHour, sch->sunriseMinute);
		debug(DEBUG_ACTION, "Sunset: %02d:%02d", sch->sunsetHour, sch->sunsetMinute);	
	}
	else{
		debug(DEBUG_ACTION, "Sunset and Sunrise times are not valid");
		sch->ssValid = FALSE;
	}	
	debug(DEBUG_ACTION, "*** End calculate dawn/sunrise and sunset/dusk times ***");
}





/*
 * Do a straight numeric eval of the string expr against the field value. Supports commas but not ranges.
 */

static void cronNumEval(TALLOC_CTX *ctx, int fieldValue, String expr, int fieldIndex, int *matchCountPtr)
{
	String *values;
	int i;
		
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(expr)
	ASSERT_FAIL(matchCountPtr)
	
	values = UtilSplitString(ctx, expr, ',');
	for(i = 0; values[i]; i++){
		if(atoi(values[i]) == fieldValue){
			(*matchCountPtr)++;
			break;
		}
	}
	talloc_free(values);
	
}

/*
 * Do a wildcard eval of the string expr against the field value. Also supports / interval operator.
 */
 
static void cronWildCardEval(TALLOC_CTX *ctx, int fieldValue, String expr, int fieldIndex, int *matchCountPtr)
{
	int val;
	
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(expr)
	ASSERT_FAIL(matchCountPtr)
	
	if(expr[1] == '/'){
		val = atoi(expr + 2);
		if(!val){
			return;
		}
		if(!(fieldValue % val)){
			(*matchCountPtr)++;
		}
	}
}

/*
 * Evaluate cron '@' style command
 */

static void cronAtCommand(SchedInfoPtr_t sch, SchedListEntryPtr_t l)
{
	int i;
	Bool exec = FALSE;
	
	static const String atCommands[] = {
		"startup",
		"sunrise",
		"sunset",
		"dawn",
		"dusk",
		NULL
	};
	
	ASSERT_FAIL(sch)
	ASSERT_FAIL(l);
	
	/* Try to match command */
	
	for(i = 0; atCommands[i]; i++){
		if(!strcmp(l->typeParam + 1, atCommands[i])){
			break;
		}
	}
	if(atCommands[i]){
		switch(i){
			case 0: /* startup */
				if(!sch->startupRan){
					exec = sch->startupRan = TRUE;
				}			
				break;
			
			case 1: /* sunrise */
				if((sch->ssValid) && (sch->tmNow.tm_hour == sch->sunriseHour) && (sch->tmNow.tm_min == sch->sunriseMinute)){
					exec = TRUE;
				}
				break;
			
			case 2: /* sunset */
				if((sch->ssValid) && (sch->tmNow.tm_hour == sch->sunsetHour) && (sch->tmNow.tm_min == sch->sunsetMinute)){
					exec = TRUE;
				}
				break;
			
			case 3: /* dawn */
				if((sch->ddValid) && (sch->tmNow.tm_hour == sch->dawnHour) && (sch->tmNow.tm_min == sch->dawnMinute)){
					exec = TRUE;
				}
				break;
			
			case 4: /* dusk */
				if((sch->ddValid) && (sch->tmNow.tm_hour == sch->duskHour) && (sch->tmNow.tm_min == sch->duskMinute)){
					exec = TRUE;
				}
				break;
			
			default:
				ASSERT_FAIL(0)
		}
		if(exec){
			/* Execute the provided function */
			(*l->exec)(sch, l->entryName, l->execParam);
		}
	}
	else{
		debug(DEBUG_UNEXPECTED, "Unrecognized '@' command: %s", l->cronSubstrs[0]);
	}	
}


/*
 * Walk the schedule list
 *
 * This function actually does the work of determining
 * what gets executed.
 */
 

static void schedulerWalk(SchedInfoPtr_t sch)
{
	SchedListEntryPtr_t l;
	int i, matchCount;

	
	/* walk the list */
	for(l = sch->head; l; l = l->next){
		ASSERT_FAIL(l->magic == SE_MAGIC)

		/* Simplified/Customized cron implementation*/
		debug(DEBUG_ACTION, "Cron: %s",l->typeParam);
		
		if(l->typeParam[0] == '@'){ 
			/* Special single cron expression */
			cronAtCommand(sch, l);
		}
		else{
			/* Evaluate cron expression consisting of 5 parts*/
			for(i = 0, matchCount = 0; i < 5; i++){
				if(!l->cronSubstrs[i]){
					debug(DEBUG_UNEXPECTED, "Not all 5 fields are populated in entry: %s. Skipping this entry", l->entryName); 
					break;
				}		
				/* Check for wildcard */
				if(l->cronSubstrs[i][0] == '*'){
					if(!l->cronSubstrs[i][1]){
						matchCount++; /* Straight wildcard */
						continue;
					}
					else if(l->cronSubstrs[i][1] == '/'){
						switch(i){ /* Wildcard with interval */
							case 0:
								cronWildCardEval(sch, sch->tmNow.tm_min, l->cronSubstrs[i], i, &matchCount);
								break;
								
							case 1:
								cronWildCardEval(sch, sch->tmNow.tm_hour, l->cronSubstrs[i], i, &matchCount);
								break;
								
							case 2:
								cronWildCardEval(sch, sch->tmNow.tm_mday, l->cronSubstrs[i], i, &matchCount);
								break;
								
							case 3:
								cronWildCardEval(sch, sch->tmNow.tm_mon, l->cronSubstrs[i], i, &matchCount);
								break;
								
							case 4:
								cronWildCardEval(sch, sch->tmNow.tm_wday, l->cronSubstrs[i], i, &matchCount);
								break;
								
							default:
								ASSERT_FAIL(0)
						}
					}					
				}
				else if(isdigit(l->cronSubstrs[i][0])){
					switch(i){ /* Straight numeric compare with comma support, but not ranges */
						case 0:
							cronNumEval(sch, sch->tmNow.tm_min, l->cronSubstrs[i], i, &matchCount);
							break;
							
						case 1:
							cronNumEval(sch, sch->tmNow.tm_hour, l->cronSubstrs[i], i, &matchCount);
							break;
							
						case 2:
							cronNumEval(sch, sch->tmNow.tm_mday, l->cronSubstrs[i], i, &matchCount);
							break;
							
						case 3:
							cronNumEval(sch, sch->tmNow.tm_mon, l->cronSubstrs[i], i, &matchCount);
							break;
							
						case 4:
							cronNumEval(sch, sch->tmNow.tm_wday, l->cronSubstrs[i], i, &matchCount);
							break;
							
							
						default:
							ASSERT_FAIL(0);
					}
				}
				else{
					debug(DEBUG_UNEXPECTED, "Unrecognized expression: %s. Skipping entry: %s", l->cronSubstrs[i], l->entryName);
					break;
				}		
			}
			if(matchCount == 5){
				/* Execute the provided function */
				(*l->exec)(sch, l->entryName, l->execParam);
			}
		}
	}
}

/* 
 * Scheduler 
 */

void SchedulerDo(void *schedInfo)
{
	SchedInfoPtr_t sch = schedInfo;
	

	time_t now;
		
	if((!sch) || (!sch->enabled))
		return;
		
	ASSERT_FAIL(sch->magic == SI_MAGIC);

	/* Get seconds now */
	time(&now);
	
	/* convert to localtime tm struct */
	localtime_r(&now, &sch->tmNow);
	

	/* Check for a change of the minute of local time */
	if(sch->tmNow.tm_min == sch->tmPrevNow.tm_min){
		return;
	}
	
	/* Code below only executes once per minute */
	
	debug(DEBUG_EXPECTED, "************* Minute = %d *************", sch->tmNow.tm_min);
	
	/* Midnight housekeeping */
	if((!sch->tmNow.tm_hour) && (!sch->tmNow.tm_min)){
		calcDawnDusk(sch);
	}
	/* Walk the list and execute any functions which are triggered */
	
	schedulerWalk(sch);
	
	/* Copy the new tm struct over the old one */
	memcpy(&sch->tmPrevNow, &sch->tmNow, sizeof(struct tm));
	return;
}

/*
 * Initialize scheduler master data structure.
 * 
 * Scheduler data structures can be freed by calling talloc_free on the returned generic pointer.
 */

void *SchedulerInit(TALLOC_CTX *ctx, double lat, double lon)
{
	SchedInfoPtr_t sch;
	
	ASSERT_FAIL(ctx);
	MALLOC_FAIL(sch = talloc_zero(ctx, SchedInfo_t))
	MALLOC_FAIL(sch->listContext = talloc_new(ctx))
	sch->lat = lat;
	sch->lon = lon;
	sch->magic = SI_MAGIC;
	return sch;
}

/*
 * Start the scheduler
 */
 
void SchedulerStart(void *schedInfo)
{

	SchedInfoPtr_t sch = schedInfo;
	
	ASSERT_FAIL(sch);
	ASSERT_FAIL(sch->magic == SI_MAGIC)
	/* Calculate dawn and dusk times */
	calcDawnDusk(sch);
	sch->enabled = TRUE;
	
}

/*
 * Stop the scheduler
 */
 
void SchedulerStop(void *schedInfo)
{
	SchedInfoPtr_t sch = schedInfo;
	ASSERT_FAIL(sch);
	ASSERT_FAIL(sch->magic == SI_MAGIC)
	sch->enabled = FALSE;
}

/*
 * Remove all active schedular entries
 */
 
void SchedularRemoveAllEntries(void *schedInfo)
{
	SchedInfoPtr_t sch = schedInfo;
	ASSERT_FAIL(sch);
	ASSERT_FAIL(sch->magic == SI_MAGIC)
	sch->head = NULL;
	sch->tail = NULL;
	talloc_free(sch->listContext); /* Buh-Bye */
	ASSERT_FAIL(sch->listContext = talloc_new(schedInfo))
}

/*
 * Add a scheduler list entry.
 */
 
void SchedulerAdd(void *schedInfo, String entryName, String typeParam,  
	void (*exec)(TALLOC_CTX *ctx, const String entryName, const String execParam),  String execParam)
{
	SchedInfoPtr_t sch = schedInfo;
	SchedListEntryPtr_t l;
	
	ASSERT_FAIL(sch);
	ASSERT_FAIL(sch->magic == SI_MAGIC)
	ASSERT_FAIL(entryName)
	ASSERT_FAIL(typeParam)
	ASSERT_FAIL(exec)
	ASSERT_FAIL(execParam)
	
	/* Allocate new list entry structure */
	ASSERT_FAIL(l = talloc_zero(sch->listContext, SchedListEntry_t))
	ASSERT_FAIL(l->entryName = talloc_strdup(l, entryName))
	ASSERT_FAIL(l->typeParam = talloc_strdup(l, typeParam))
	ASSERT_FAIL(l->execParam = talloc_strdup(l, execParam))
	l->exec = exec;
	l->magic = SE_MAGIC;
	
	l->cronSubstrs = UtilSplitWhite(l, typeParam);
	
	/* Insert on end of list */

	if(!sch->head){
		sch->head = l;
	}
	else{
		sch->tail->next = l;
		l->prev = sch->tail;
	}
	sch->tail = l;
	
}

/*
 * Scheduler default handler (for testing)
 */

void SchedulerDefaultHandler(TALLOC_CTX *ctx, const String entryName, const String execParam)
{
	debug(DEBUG_EXPECTED, "Scheduler default handler called. entryName = %s, execParam = %s", entryName, execParam);
}

