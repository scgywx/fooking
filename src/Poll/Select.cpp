#include "macros.h"

#ifdef HAVE_SELECT
#include <string.h>
#include "EventLoop.h"
#include "EventPoll.h"
#include "Log.h"

NS_USING;

EventSelect::EventSelect(EventLoop *loop):
	IEventPoll(loop)
{
	FD_ZERO(&rfds);
	FD_ZERO(&wfds);
}

EventSelect::~EventSelect()
{
}

int EventSelect::add(int fd, int mask)
{
	if (mask & EV_IO_READ) FD_SET(fd, &rfds);
	if (mask & EV_IO_WRITE) FD_SET(fd, &wfds);
	
	return 0;
}

int EventSelect::del(int fd, int delmask)
{
	if (delmask & EV_IO_READ) FD_CLR(fd, &rfds);
	if (delmask & EV_IO_WRITE) FD_CLR(fd, &wfds);
	return 0;
}

int EventSelect::poll(struct timeval *tvp)
{
	memcpy(&_rfds, &rfds, sizeof(fd_set));
	memcpy(&_wfds, &wfds, sizeof(fd_set));
	
	int nevents = 0;
	int retval = select(pLoop->nMaxfd + 1, &_rfds, &_wfds, NULL, tvp);
	if (retval > 0) {
		for (int j = 0; j <= pLoop->nMaxfd; j++) {
			int mask = 0;
			IOEvent *e = pLoop->pEvents + j;

			if (e->mask == EV_IO_NONE) continue;
			if (e->mask & EV_IO_READ && FD_ISSET(j, &_rfds))
				mask |= EV_IO_READ;
			if (e->mask & EV_IO_WRITE && FD_ISSET(j, &_wfds))
				mask |= EV_IO_WRITE;
			
			pLoop->pFireds[nevents].fd = j;
			pLoop->pFireds[nevents].mask = mask;
			nevents++;
		}
	}

	return nevents;
}

#endif