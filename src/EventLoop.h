#pragma once
#include <time.h>
#include <sys/time.h>
#include <map>
#include "fooking.h"

#define EV_ERROR 		-1
#define EV_IO_NONE		0
#define EV_IO_READ		1
#define EV_IO_WRITE 	2
#define EV_IO_ALL		(EV_IO_READ | EV_IO_WRITE)

#define EV_CB_INIT(_cb) _cb.object = NULL; _cb.method = NULL
#define EV_CB(_obj, _func) (EventHandler){_obj, (EventCallback)&_func}
#define EV_IO_CB(_obj, _func) (IOHandler){_obj, (IOCallback)&_func}
#define EV_TIMER_CB(_obj, _func) (TimerHandler){_obj, (TimerCallback)&_func}
#define EV_INVOKE(_cb, ...) do{\
	if(_cb.object && _cb.method){\
		(_cb.object->*_cb.method)(__VA_ARGS__);\
	}\
}while(0)

NS_BEGIN

class IEventPoll;

typedef void (Object::*EventCallback)(void*);
typedef void (Object::*TimerCallback)(unsigned long long, void*);
typedef void (Object::*IOCallback)(int, int, void*);

typedef struct{
	Object *object;
	EventCallback method;
}EventHandler;

typedef struct{
	Object *object;
	IOCallback method;
}IOHandler;

typedef struct{
	Object *object;
	TimerCallback method;
}TimerHandler;

typedef struct{
	void *data;
	EventHandler handler;
}BaseEvent;

typedef struct{
    int mask; /* one of AE_(READABLE|WRITABLE) */
	void *data;
    IOHandler cbRead;
    IOHandler cbWrite;
}IOEvent;

typedef struct{
	int fd;
	int mask;
}IOFired;

typedef struct TickEvent{
	EventHandler cb;
	void *data;
	TickEvent *next;
}TickEvent;

typedef unsigned long long Timestamp;
typedef Timestamp TimerId;
typedef struct TimerEvent{
	TimerId id;
	unsigned int milliseconds;
	void *data;
	TimerEvent *next;
	TimerHandler cb;
}TimerEvent;

typedef std::map<TimerId, TimerEvent*> TimerList;

class EventLoop :
	public Object
{
public:
	friend class EventEPoll;
	friend class EventKqueue;
	friend class EventSelect;
	EventLoop();
	~EventLoop();
public:
	void 					run();
	void 					stop();
	void 					addEventListener(int fd, int event, const IOHandler &cb, void *data = NULL);
	void 					removeEventListener(int fd, int event);
	TimerId 				setTimer(unsigned int millisecond, const TimerHandler &cb, void *data = NULL);
	void 					stopTimer(TimerId id);
	void					setLoopBefore(const EventHandler &cb, void *data){cbBefore = cb;pBeforeData = data;}
	void					nextTick(const EventHandler &cb, void *data = NULL);
	void					setMaxWaitTime(int millisecond){ nMaxWaitTime = millisecond;}
private:
	void					resize(int maxfd);
	int 					procIoEvent();
	int		 				procTimerEvent();
	int						procTickEvent();
private:
	int						nSize;
	int						nMaxfd;
	int						nMaxWaitTime;
	bool					bRunning;
	TimerList				arrTimers;
	IOEvent*				pEvents;
	IOFired*				pFireds;
	IEventPoll*				pPoll;
	TickEvent*				pTickHead;
	TickEvent*				pTickTail;
	EventHandler			cbBefore;
	void*					pBeforeData;
};

NS_END