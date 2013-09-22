#define _GNU_SOURCE
#include <stdlib.h>
#include <time.h>
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
  SchedType_t type;
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
  Bool enabled;
  SchedListEntryPtr_t head;
  SchedListEntryPtr_t tail;
  void *listContext;
  struct tm tmNow;
  struct tm tmPrevNow;
} SchedInfo_t;

typedef SchedInfo_t * SchedInfoPtr_t;

/*
 * Do a straight numeric eval of the string expr against the field value
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
 * Do a wildcard eval of the string expr against the field value
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
		
		if(l->type == ST_CRON){
			/* Simplified cron table entry */
			debug(DEBUG_ACTION, "Cron: %s %s %s %s %s", l->cronSubstrs[0], l->cronSubstrs[1],
			l->cronSubstrs[2], l->cronSubstrs[3], l->cronSubstrs[4]);
			/* Evaluate the cron expression */
			for(i = 0, matchCount = 0; i < 5; i++){
				ASSERT_FAIL(l->cronSubstrs[i]);
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
				else{
					switch(i){ /* Straight numeric compare */
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

void *SchedulerInit(TALLOC_CTX *ctx)
{
	SchedInfoPtr_t sch;
	
	ASSERT_FAIL(ctx);
	MALLOC_FAIL(sch = talloc_zero(ctx, SchedInfo_t))
	MALLOC_FAIL(sch->listContext = talloc_new(ctx))
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
 
void SchedulerAdd(void *schedInfo, String entryName, SchedType_t type, String typeParam,  
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
	l->type = type;
	l->magic = SE_MAGIC;
	

	if(type == ST_CRON){
		/* Split cron expression into substrings */
		l->cronSubstrs = UtilSplitWhite(l, typeParam);
	}
	
	
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

