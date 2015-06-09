#include <stdlib.h>
#include <iostream>
#include "EventLoop.h"
#include "EventPoll.h"
#include "Log.h"

NS_USING;

static Timestamp GetTime()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	Timestamp tm = (Timestamp)tv.tv_sec * 1000000 + tv.tv_usec;
	return tm;
}

EventLoop::EventLoop():
	nSize(10240),
	nMaxfd(-1),
	nMaxWaitTime(0),
	bRunning(false),
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
void EventLoop::addEventListener(int fd, int event, const IOHandler &cb, void *data)
{
	if(fd < 0) return;
	if(fd >= nSize) resize(fd);

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
	if(fd < 0) return;
	if(fd >= nSize) return;

	IOEvent *ev = pEvents + fd;
	if (ev->mask == EV_IO_NONE) return;

	ev->mask = ev->mask & (~event);
	pPoll->del(fd, event);
}

TimerId EventLoop::setTimer(unsigned int milliseconds, const TimerHandler &cb, void *data)
{
	TimerId id = GetTime() + milliseconds * 1000;
	TimerEvent *te = (TimerEvent*)zmalloc(sizeof(TimerEvent));
	if(te == NULL) return EV_ERROR;

	te->id = id;
	te->cb = cb;
	te->data = data;
	te->milliseconds = milliseconds;
	
	arrTimers.insert(TimerList::value_type(id, te));
	
	return id;
}

void EventLoop::stopTimer(TimerId id)
{
	TimerList::iterator it = arrTimers.find(id);
	if(it != arrTimers.end()){
		zfree(it->second);
		arrTimers.erase(it);
	}
}

int EventLoop::procIoEvent()
{
	int processed = 0;
	
	//timer
	struct timeval tv, *tvp = NULL;
	TimerList::const_iterator it = arrTimers.begin();
	if(it != arrTimers.end()){
		TimerEvent *te = it->second;
		Timestamp now = GetTime();
		tvp = &tv;
		if(now >= te->id){
			tvp->tv_sec = 0;
			tvp->tv_usec = 0;
		}else{
			tvp->tv_sec = (te->id - now) / 1000000;
			tvp->tv_usec = (te->id - now) % 1000000;
		}
	} else {
		tvp = NULL; /* wait forever */
	}
	
	if(nMaxWaitTime){
		int sec = nMaxWaitTime / 1000;
		int usec = nMaxWaitTime % 1000 * 1000;
		if(tvp){
			if(tvp->tv_sec > sec || (tvp->tv_sec == sec && tvp->tv_usec > usec)){
				tvp->tv_sec = sec;
				tvp->tv_usec = usec;
			}
		}else{
			tv.tv_sec = sec;
			tv.tv_usec = usec;
			tvp = &tv;
		}
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
	Timestamp now = GetTime();
	
	for(TimerList::iterator it = arrTimers.begin();
		it != arrTimers.end();)
	{
		TimerEvent *te = it->second;
		if(te->id < now){
			arrTimers.erase(it++);
			EV_INVOKE(te->cb, te->id, te->data);
			processed++;
			zfree(te);
		}else{
			it++;
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
	
	tick->cb = cb;
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
		EV_INVOKE(tick->cb, tick->data);
		zfree(tick);
		tick = next;
		n++;
	}
	
	pTickHead = pTickTail = NULL;
	
	return n;
}
