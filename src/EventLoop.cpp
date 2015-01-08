#include <stdlib.h>
#include <iostream>
#include "EventLoop.h"
#include "EventPoll.h"
#include "Log.h"

NS_USING;

static void GetTime(long *seconds, long *milliseconds)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	*seconds = tv.tv_sec;
	*milliseconds = tv.tv_usec/1000;
}

static void AddMillisecondsToNow(long long milliseconds, long *sec, long *ms)
{
	long cur_sec, cur_ms, when_sec, when_ms;

	GetTime(&cur_sec, &cur_ms);
	when_sec = cur_sec + milliseconds/1000;
	when_ms = cur_ms + milliseconds%1000;
	if (when_ms >= 1000) {
		when_sec ++;
		when_ms -= 1000;
	}
	*sec = when_sec;
	*ms = when_ms;
}

EventLoop::EventLoop():
	nSize(10240),
	nMaxfd(-1),
	bRunning(false),
	nTimerNextId(0),
	nLastTime(time(NULL)),
	pTimerHead(NULL),
	pTickHead(NULL),
	pTickTail(NULL),
	pBeforeData(NULL)
{
	EV_CB_INIT(cbBefore);
	
	pEvents = (IOEvent*)zmalloc(sizeof(IOEvent) * nSize);
	pFireds = (IOFired*)zmalloc(sizeof(IOFired) * nSize);
	for (int i = 0; i < nSize; i++){
		pEvents[i].mask = EV_IO_NONE;
	}

#if HAVE_EPOLL
	pPoll = new EventEPoll(this);
#elif HAVE_KQUEUE
	pPoll = new EventKqueue(this);
#else
	pPoll = new EventSelect(this);
#endif
}

EventLoop::~EventLoop()
{
	zfree(pEvents);
	zfree(pFireds);
	delete pPoll;
}

void EventLoop::resize(int maxfd)
{
	int oldSize = nSize;
	int sz = static_cast<int>(nSize * 1.5);
	if(maxfd >= sz) sz = maxfd + 1;
	
	nSize = sz;
	pEvents = (IOEvent*)zrealloc(pEvents, sizeof(IOEvent) * nSize);
	pFireds = (IOFired*)zrealloc(pFireds, sizeof(IOFired) * nSize);
	for(int i = oldSize; i < nSize; ++i){
		pEvents[i].mask = EV_IO_NONE;
	}
	
	pPoll->resize();
}

void EventLoop::run()
{
	LOG("eventloop started, event driven is %s", pPoll->name());
	bRunning = true;
	while(bRunning)
	{
		EV_INVOKE(cbBefore, pBeforeData);
		procTickEvent();
        procIoEvent();
		procTimerEvent();
    }
}

void EventLoop::stop()
{
	bRunning = false;
}

//io event listener
void EventLoop::addEventListener(int fd, int event, const IOEventHandler &cb, void *data)
{
	if (fd >= nSize) {
		resize(fd);
	}

	IOEvent *ev = pEvents + fd;
	if(pPoll->add(fd, event) == -1){
		return ;
	}

	ev->mask|= event;
	ev->data = data;
	if (event & EV_IO_READ) ev->cbRead = cb;
	if (event & EV_IO_WRITE) ev->cbWrite = cb;
	if (fd > nMaxfd) nMaxfd = fd;
	
	return ;
}

void EventLoop::removeEventListener(int fd, int event)
{
	if (fd >= nSize) return;

	IOEvent *ev = pEvents + fd;
	if (ev->mask == EV_IO_NONE) return;

	ev->mask = ev->mask & (~event);
	pPoll->del(fd, event);
}

long long EventLoop::setTimer(int milliseconds, const TimerEventHandler &cb, void *data)
{
	long long id = ++nTimerNextId;
	TimerEvent *te = (TimerEvent*)zmalloc(sizeof(TimerEvent));
	if(te == NULL) return EV_ERROR;

	te->id = id;
	te->handler = cb;
	te->data = data;
	te->milliseconds = milliseconds;
	te->next = pTimerHead;
	AddMillisecondsToNow(milliseconds, &te->when_sec, &te->when_ms);
	
	pTimerHead = te;
	
	return id;
}

void EventLoop::stopTimer(long long id)
{
	TimerEvent *prev = NULL;
	TimerEvent *te = pTimerHead;
	while(te) {
		if (te->id == id) {
			if (prev == NULL)
				pTimerHead = te->next;
			else
				prev->next = te->next;
			zfree(te);
			return ;
		}
		prev = te;
		te = te->next;
	}
}

TimerEvent* EventLoop::searchNearestTimer()
{
	TimerEvent *te = pTimerHead;
	TimerEvent *nearest = NULL;

	while(te) {
		if (!nearest || te->when_sec < nearest->when_sec ||
			(te->when_sec == nearest->when_sec &&
			te->when_ms < nearest->when_ms))
		{
			nearest = te;
		}
		te = te->next;
	}
	return nearest;
}

int EventLoop::procIoEvent()
{
	int processed = 0;
	TimerEvent *shortest = searchNearestTimer();
	struct timeval tv, *tvp;
	if (shortest) {
		long now_sec, now_ms;

		/* Calculate the time missing for the nearest
		 * timer to fire. */
		GetTime(&now_sec, &now_ms);
		tvp = &tv;
		tvp->tv_sec = shortest->when_sec - now_sec;
		if (shortest->when_ms < now_ms) {
			tvp->tv_usec = ((shortest->when_ms+1000) - now_ms)*1000;
			tvp->tv_sec --;
		} else {
			tvp->tv_usec = (shortest->when_ms - now_ms)*1000;
		}
		if (tvp->tv_sec < 0) tvp->tv_sec = 0;
		if (tvp->tv_usec < 0) tvp->tv_usec = 0;
	} else {
		tvp = NULL; /* wait forever */
	}

	//poll
	int numevents = pPoll->poll(tvp);
	for (int j = 0; j < numevents; j++) {
		int mask = pFireds[j].mask;
		int fd = pFireds[j].fd;
		IOEvent *io = pEvents + fd;
		if (io->mask & mask & EV_IO_READ) {
			EV_INVOKE(io->cbRead, fd, mask, io->data);
		}
		
		if (io->mask & mask & EV_IO_WRITE) {
			EV_INVOKE(io->cbWrite, fd, mask, io->data);
		}
		
		processed++;
	}
	
    return processed;
}

int EventLoop::procTimerEvent()
{
	int processed = 0;
	TimerEvent *te;
	time_t now = time(NULL);

	if(now < nLastTime){
		te = pTimerHead;
		while(te){
			te->when_sec = 0;
			te = te->next;
		}
	}

	nLastTime = now;
	te = pTimerHead;
	while(te){
		long now_sec, now_ms;
		long long id;

		GetTime(&now_sec, &now_ms);
		if (now_sec > te->when_sec || 
			(now_sec == te->when_sec && now_ms >= te->when_ms))
		{
			id = te->id;
			TimerEvent *next = te->next;
			AddMillisecondsToNow(te->milliseconds, &te->when_sec, &te->when_ms);
			EV_INVOKE(te->handler, id, te->data);
			processed++;
			te = next;
		}else{
			te = te->next;
		}
	}

	return processed;
}

void EventLoop::nextTick(const EventHandler &cb, void *data)
{
	TickEvent *tick = (TickEvent*)zmalloc(sizeof(TickEvent));
	if(tick == NULL){
		LOG("nextTick error");
		return ;
	}
	
	tick->handler = cb;
	tick->data = data;
	tick->next = NULL;
	
	if(pTickTail){
		pTickTail->next = tick;
		pTickTail = tick;
	}else{
		pTickHead = pTickTail = tick;
	}
}

int EventLoop::procTickEvent()
{
	TickEvent *tick = pTickHead;
	int n = 0;
	while(tick){
		TickEvent *next = tick->next;
		EV_INVOKE(tick->handler, tick->data);
		zfree(tick);
		tick = next;
		n++;
	}
	
	pTickHead = pTickTail = NULL;
	
	return n;
}
