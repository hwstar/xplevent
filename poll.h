#ifndef POLL_H
#define POLL_H

/*
 * Poll watch types
 */

#define POLL_WT_IN		1
#define POLL_WT_OUT		2
#define POLL_WT_RD_HUP	4
#define POLL_WT_PRI		8
#define POLL_WT_ERR		0x10
#define POLL_WT_HUP		0x20
#define POLL_WT_ET		0x40
#define POLL_WT_OS		0x80


/*
 * Public functions
 */
 
void *PollInit(TALLOC_CTX *ctx, unsigned maxEvents);
Bool PollDestroy(void *pHead);

Bool PollRegEvent(void *pHead, int regFD, uint32_t watchType, 
void (*action)(int fd, int event, void *userObject), void *userObject);
Bool PollUnRegEvent(void *pHead, int regFD);
int PollRegTimeout(void *pHead, void (*action)(int id, void *userObject), void *userObject);
Bool PollUnRegTimeout(void *pHead, int id);
Bool PollWait(void *pHead, int timeoutMs, const sigset_t *sigmask);

#endif
