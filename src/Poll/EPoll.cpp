#include "macros.h"

#ifdef HAVE_EPOLL
#include <sys/epoll.h>
#include "EventLoop.h"
#include "EventPoll.h"
#include "Log.h"

NS_USING;

EventEPoll::EventEPoll(EventLoop *loop):
	IEventPoll(loop)
{
	epfd = epoll_create(1024);
	events = (struct epoll_event*)zmalloc(sizeof(struct epoll_event) * loop->nSize);
}

EventEPoll::~EventEPoll()
{
	close(epfd);
	zfree(events);
}

int EventEPoll::add(int fd, int mask)
{
	struct epoll_event ee;
	int op = pLoop->pEvents[fd].mask == EV_IO_NONE ?
			EPOLL_CTL_ADD : EPOLL_CTL_MOD;

	ee.events = 0;
	mask|= pLoop->pEvents[fd].mask; /* Merge old events */
	if (mask & EV_IO_READ) ee.events |= EPOLLIN;
	if (mask & EV_IO_WRITE) ee.events |= EPOLLOUT;
	ee.data.u64 = 0; /* avoid valgrind warning */
	ee.data.fd = fd;
	if (epoll_ctl(epfd, op, fd, &ee) == -1) return -1;
	return 0;
}

int EventEPoll::del(int fd, int delmask)
{
	struct epoll_event ee;
	int mask = pLoop->pEvents[fd].mask & (~delmask);

	ee.events = 0;
	if (mask & EV_IO_READ) ee.events |= EPOLLIN;
	if (mask & EV_IO_WRITE) ee.events |= EPOLLOUT;
	ee.data.u64 = 0; /* avoid valgrind warning */
	ee.data.fd = fd;
	if (mask != EV_IO_NONE) {
		return epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ee);
	} else {
		/* Note, Kernel < 2.6.9 requires a non null event pointer even for
		 * EPOLL_CTL_DEL. */
		return epoll_ctl(epfd, EPOLL_CTL_DEL, fd, &ee);
	}
}

int EventEPoll::poll(struct timeval *tvp)
{
	if(epfd == -1){
		return 0;
	}
	
	int nevents = 0;
	int retval = epoll_wait(epfd, events, pLoop->nSize,
			tvp ? (tvp->tv_sec*1000 + tvp->tv_usec/1000) : -1);
	if (retval > 0) {
		int j;
		nevents = retval;
		for (j = 0; j < nevents; j++) {
			int mask = 0;
			struct epoll_event *e = events + j;

			if (e->events & EPOLLIN) mask |= EV_IO_READ;
			if (e->events & EPOLLOUT) mask |= EV_IO_WRITE;
			if (e->events & EPOLLERR) mask |= EV_IO_WRITE;
			if (e->events & EPOLLHUP) mask |= EV_IO_WRITE;
			pLoop->pFireds[j].fd = e->data.fd;
			pLoop->pFireds[j].mask = mask;
		}
	}

	return nevents;
}

void EventEPoll::resize()
{
	events = (struct epoll_event*)zrealloc(events, sizeof(struct epoll_event) * pLoop->nSize);
}

#endif