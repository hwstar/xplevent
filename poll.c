/*
*    poll.c
* 
*    Copyright (C) 2013  Stephen A. Rodgers
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*
*    Interface to epoll
*   
* 
* 
* Steve Rodgers <hwstar@rodgers.sdcoxmail.com>
*
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <limits.h>
#include <errno.h>
#include <time.h>
#include <sys/epoll.h>
#include <talloc.h>
#include "defs.h"
#include "types.h"
#include "notify.h"
#include "poll.h"

#define PH_MAGIC 0x561A0F2C
#define PE_MAGIC 0x6812AC45
#define TE_MAGIC 0x4A2E7F35

/* Poll list entry */
typedef struct PollEntry_s {
	unsigned magic;
	int fd;
	void *userObject;
	void (*action)(int fd, int event, void *userObject);
	struct PollEntry_s *prev;
	struct PollEntry_s *next;
} PollEntry_t;


typedef PollEntry_t  * PollEntryPtr_t;


/* Poll header */
typedef struct PollHeader_s {
	unsigned magic;
	int fd;
	unsigned maxevents;
	struct epoll_event *eventlist;
	PollEntryPtr_t peHead;
	PollEntryPtr_t peTail;

} PollHeader_t;

typedef PollHeader_t * PollHeaderPtr_t;

/*
 * Remove a poll item from the list
 *
 * Arguments:
 *
 * 1. Pointer to poll object 
 * 2. Pointer to entry to remove
 *
 * Return value
 *
 * None
 */

static void removePollItem(PollHeaderPtr_t ph, PollEntryPtr_t e)
{			
	if(!e->prev){
		/* At the beginning of the list, and/or the only entry */
		if(!e->next){
			/* Only entry */
			ph->peHead = ph->peTail = NULL;
		}
		else{
			/* Entries follow */
			ph->peHead->next->prev = NULL;
			ph->peHead = ph->peHead->next;
		}	
	}
	else if(!e->next){
		/* At the end of the list */
		ph->peTail->prev->next = NULL;
		ph->peTail = ph->peTail->prev;
	}
	else{
		/* In the middle of the list */
		e->prev->next = e->next;
		e->next->prev = e->prev;

	}
}


/*
 * Create/Modify/Delete a poll event
 * Arguments:
 *
 * 1. Pointer to poll object 
 * 2. File descriptor to register/modify/unregister
 * 3. Watch type (See poll.h)
 * 4. Function to call when event occurs.
 * 5. A user object to pass to the function when the event occurs.
 *
 * Return value
 *
 * PASS if success, otherwise FAIL
 */
 
static Bool pollFDOp(void *pHead, int regFD, int op, uint32_t watchType, 
void (*action)(int fd, int event, void *userObject), void *userObject)
{
	PollEntryPtr_t e;
	PollHeaderPtr_t ph = pHead;
	struct epoll_event event = (struct epoll_event) {0};
	
	/* Translate our watch type flags to EPOLL flags */	
	if(watchType & POLL_WT_IN){
		event.events |= EPOLLIN;
	}
	if(watchType & POLL_WT_OUT){
		event.events |= EPOLLOUT;
	}
	if(watchType & POLL_WT_RD_HUP){
		event.events |= EPOLLRDHUP;
	}
	if(watchType & POLL_WT_PRI){
		event.events |= EPOLLPRI;
	}
	if(watchType & POLL_WT_ERR){
		event.events |= EPOLLERR;
	}
	if(watchType & POLL_WT_HUP){
		event.events |= EPOLLHUP;
	}
	if(watchType & POLL_WT_HUP){
		event.events |= EPOLLHUP;
	}
	if(watchType & POLL_WT_ET){
		event.events |= EPOLLET;
	}
	if(watchType & POLL_WT_OS){
		event.events |= EPOLLONESHOT;
	}	
	
	/* Allocate a list entry */
	MALLOC_FAIL(e = talloc_zero(pHead, PollEntry_t))
	e->magic = PE_MAGIC;
	e->userObject = userObject;
	e->fd = regFD;
	e->action = action;
	
	/* Store the entry object in the event structure */
	event.data.ptr = e;
	
	/* Tell the kernel about the new poll entry */
	if(epoll_ctl(ph->fd, op, regFD, &event)){
		 debug(DEBUG_UNEXPECTED, "%s: epoll_ctl() error: %s", __func__, strerror(errno));
		 talloc_free(e); /* Free the entry */
		 return FAIL;
	}
	
	/* Append entry to list */
	if(!ph->peHead){
		/* Append to empty list */
		ph->peHead = ph->peTail = e;
	}
	else{
		/* Append to existing list */
		e->prev = ph->peTail;
		e->prev->next = e;
		ph->peTail = e;
		
		
	}
	
	return PASS;
}


/*
 * Run through the list of events returned
 *
 * Arguments:
 *
 * 1. Pointer to poll object
 * 2. Number of events to check
 *
 * Return value
 *
 * None
 */
 
static void doEventList(PollHeaderPtr_t ph, int numEvents)
{
	PollEntryPtr_t e = NULL;
	int i;
	unsigned events, watchType = 0;
	for(i = 0; i < numEvents; i++){
		e = ph->eventlist[i].data.ptr; /* Recover the list entry */
		events = ph->eventlist[i].events;
		ASSERT_FAIL(PE_MAGIC == e->magic)
		ASSERT_FAIL(e->action);
		/* convert flags to our format */
		if(events & EPOLLIN){
			watchType |= POLL_WT_IN;
		}
		if(events & EPOLLOUT){
			watchType |= POLL_WT_OUT;
		}
		if(events & EPOLLRDHUP){
			watchType |= POLL_WT_RD_HUP;
		}	
		if(events & EPOLLPRI){
			watchType |= POLL_WT_PRI;
		}	
		if(events & EPOLLERR){
			watchType |= POLL_WT_ERR;
		}
		if(events & EPOLLHUP){
			watchType |= POLL_WT_HUP;
		}
		if(events & EPOLLET){
			watchType |= POLL_WT_ET;
		}
		if(events & EPOLLONESHOT){
			watchType |= POLL_WT_OS;
		}
		
		/* Call the specified action callback */
		
		(*e->action)(e->fd, watchType, e->userObject);
		
		/* A one-shot item needs to be removed */
		if(events & EPOLLONESHOT){
			/* Remove item from list */
			removePollItem(ph, e);
			e->magic = 0;
			talloc_free(e);
		}
		
	}	
}




/*
 * Create a poll object
 *
 * Arguments:
 *
 * 1. Talloc context to use to allocate memory. 
 * 2. Maximum number of events to process per wakeup. (Affects amount of memory allocated)
 *
 *
 * Return value
 *
 * The new poll object if successful, otherwise NULL.
 */	

void *PollInit(TALLOC_CTX *ctx, unsigned maxEvents)
{
	PollHeaderPtr_t ph;
	int fd;
	
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(maxEvents > 0)
	
	/* Try and get an epoll FD */
	if ((fd = epoll_create1(EPOLL_CLOEXEC)) < 0){
		/* Error encountered */
		debug(DEBUG_UNEXPECTED, "%s: epoll_create() error: %s", __func__, strerror(errno));
		return NULL;
	}
	/* Allocate the object structure */
	MALLOC_FAIL(ph = talloc_zero(ctx, PollHeader_t))
	
	/* Fill it in */
	ph->magic = PH_MAGIC;
	ph->fd = fd;
	ph->maxevents = maxEvents;
	
	/* Pre-allocate the event list */
	MALLOC_FAIL(ph->eventlist = talloc_zero_array(ph, struct epoll_event, maxEvents)) 

	/* Return the object */
	return ph;
	
}

/*
 * Destroy a poll object 
 *
 * Arguments:
 *
 * 1. Pointer to poll object 
 *
 * Return value
 *
 * PASS if successful, oherwise FAIL
 */

Bool PollDestroy(void *pHead)
{
	PollHeaderPtr_t ph = pHead;
	ASSERT_FAIL(ph)
	ASSERT_FAIL(PH_MAGIC == ph->magic)
	if(close(ph->fd) < 0){
		debug(DEBUG_UNEXPECTED, "%s: close() error: %s", __func__, strerror(errno));
		return FAIL;
	}
	ph->magic = 0;
	talloc_free(ph);
	return PASS;
}

/*
 * Register an event
 *
 * Arguments:
 *
 * 1. Pointer to poll object 
 * 2. The file descriptor to register
 * 3. The watch types to monitor (See poll.h)
 * 4. A pointer to a function to call when the file descriptor meets the criteria of the watch types
 * 5. A pointer to a user object.
 *
 * Return value
 *
 * PASS if successful, otherwise FAIL
 */

Bool PollRegEvent(void *pHead, int regFD, uint32_t watchType, void (*action)(int fd, int event, void *userObject), void *userObject)
{
	Bool res = PASS;
	PollHeaderPtr_t ph = pHead;

	ASSERT_FAIL(ph)
	ASSERT_FAIL(PH_MAGIC == ph->magic)
	ASSERT_FAIL(action)
	ASSERT_FAIL(regFD >= 0)

	res = pollFDOp(pHead, regFD, EPOLL_CTL_ADD, watchType, action, userObject);
	
	return res;
}

/*
 * Unregister an event
 *
 * Arguments:
 *
 * 1. Pointer to poll object 
 * 2. The file descriptor to unregister
 *
 *
 * Return value
 *
 * PASS if successful, otherwise FAIL
 */

Bool PollUnRegEvent(void *pHead, int regFD)
{
	PollHeaderPtr_t ph = pHead;
	PollEntryPtr_t e;

	ASSERT_FAIL(ph)
	ASSERT_FAIL(PH_MAGIC == ph->magic)
	ASSERT_FAIL(regFD >= 0)
	ASSERT_FAIL(ph->peHead)

	/*
	 * Find the fd in the list.
	 */

	 
	 for(e = ph->peHead; e; e = e->next){
		 ASSERT_FAIL(PE_MAGIC == e->magic)
		 if(e->fd == regFD){
			break;
		}
	 }
	 
	/* Was it found? */
	if(!e){
		debug(DEBUG_UNEXPECTED,"%s: file descriptor %d not in list", __func__, regFD); 
		return FAIL;
	}
	/* Remove it */
	removePollItem(ph, e);
	
	/* Tell the kernel to remove the poll entry */
	if(epoll_ctl(ph->fd, EPOLL_CTL_DEL , e->fd, pHead)){
		 debug(DEBUG_UNEXPECTED, "%s: epoll_ctl() error: %s", __func__, strerror(errno));
		 e->magic = 0;
		 talloc_free(e); /* Free the entry anyway*/
		 return FAIL;
	}
	
	/* Free the poll entry */
	
	talloc_free(e);
	
	return PASS;
}




/* 
 * Wait for a poll event to occur
 *
 * Arguments:
 *
 * 1. Pointer to poll object 
 * 2. A time out in milliseconds for the timeout handlers, or -1 to disable the use of timeout handlers.
 * 3. A signal mask set or NULL if not used.
 *
 * Return value
 *
 * Normally, this function should not return unless the underlying epoll_pwait fails.
 * If that happens, then the function will return FAIL.
 */
 

Bool PollWait(void *pHead, const sigset_t *sigmask )
{
	PollHeaderPtr_t ph = pHead;
	int eventCount;
	
	ASSERT_FAIL(ph)
	ASSERT_FAIL(PH_MAGIC == ph->magic)
	
	for(;;){ /* Loop until a serious error occurs */
		if((eventCount = epoll_pwait(ph->fd, ph->eventlist, ph->maxevents, -1, sigmask)) < 0){
			if(EINTR == errno){
				continue;
				/* Timeout */
			}
			else{
				/* Something more serious */
				debug(DEBUG_UNEXPECTED, "%s: epoll_pwait() failed: %s",__func__, strerror(errno));
				break;
			}
		}
		else{
			if(eventCount){
				/* got an event or series of events */
				doEventList(ph, eventCount);
			}
			
		}
		
	} 
	return FAIL;
}


