#include "macros.h"

#ifdef HAVE_KQUEUE
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include "EventLoop.h"
#include "EventPoll.h"
#include "Log.h"

NS_USING;

EventKqueue::EventKqueue(EventLoop *loop):
	IEventPoll(loop)
{
	kqfd = kqueue();
	events = (struct kevent*)zmalloc(sizeof(struct kevent) * loop->nSize);
}

EventKqueue::~EventKqueue()
{
	close(kqfd);
	zfree(events);
}

int EventKqueue::add(int fd, int mask)
{
	aeApiState *state = eventLoop->apidata;
	struct kevent ke;

	if (mask & EV_IO_READ) {
		EV_SET(&ke, fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
		if (kevent(kqfd, &ke, 1, NULL, 0, NULL) == -1) return -1;
	}
	
	if (mask & EV_IO_WRITE) {
		EV_SET(&ke, fd, EVFILT_WRITE, EV_ADD, 0, 0, NULL);
		if (kevent(kqfd, &ke, 1, NULL, 0, NULL) == -1) return -1;
	}
	
	return 0;
}

int EventKqueue::del(int fd, int delmask)
{
	struct kevent ke;

	if (delmask & EV_IO_READ) {
		EV_SET(&ke, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
		kevent(kqfd, &ke, 1, NULL, 0, NULL);
	}
	if (delmask & EV_IO_WRITE) {
		EV_SET(&ke, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
		kevent(kqfd, &ke, 1, NULL, 0, NULL);
	}
	
	return 0;
}

int EventKqueue::poll(struct timeval *tvp)
{
	if(kqfd == -1){
		return 0;
	}
	
	int retval = 0, nevents = 0;
	if (tvp != NULL) {
		struct timespec timeout;
		timeout.tv_sec = tvp->tv_sec;
		timeout.tv_nsec = tvp->tv_usec * 1000;
		retval = kevent(kqfd, NULL, 0, events, pLoop->nSize, &timeout);
	} else {
		retval = kevent(kqfd, NULL, 0, events, pLoop->nSize, NULL);
	}
	
	if (retval > 0) {
		int j;
		nevents = retval;
		for (j = 0; j < nevents; j++) {
			int mask = 0;
			struct kevent *e = events + j;
			
			if (e->filter == EVFILT_READ) mask |= EV_IO_READ;
			if (e->filter == EVFILT_WRITE) mask |= EV_IO_WRITE;
			pLoop->pFireds[j].fd = e->ident; 
			pLoop->pFireds[j].mask = mask;
		}
	}

	return nevents;
}

void EventKqueue::resize()
{
	events = (struct kevent*)zrealloc(events, sizeof(struct kevent) * pLoop->nSize);
}

#endif