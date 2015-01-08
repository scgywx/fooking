#pragma once
#include <time.h>
#include <sys/time.h>
#include "Object.h"

#define EV_ERROR 		-1
#define EV_IO_NONE		0
#define EV_IO_READ		1
#define EV_IO_WRITE 	2
#define EV_IO_ALL		(EV_IO_READ | EV_IO_WRITE)

#define EV_CB_INIT(_cb) _cb.object = NULL; _cb.method = NULL
#define EV_CB(_obj, _func) (EventHandler){_obj, (EventCallback)&_func}
#define EV_IO_CB(_obj, _func) (IOEventHandler){_obj, (IOEventCallback)&_func}
#define EV_TIMER_CB(_obj, _func) (TimerEventHandler){_obj, (TimerEventCallback)&_func}
#define EV_INVOKE(_cb, ...) do{\
	if(_cb.object && _cb.method){\
		(_cb.object->*_cb.method)(__VA_ARGS__);\
	}\
}while(0)

NS_BEGIN

class IEventPoll;

typedef void (Object::*EventCallback)(void*);
typedef void (Object::*TimerEventCallback)(long long, void*);
typedef void (Object::*IOEventCallback)(int , int, void*);

typedef struct{
	Object *object;
	EventCallback method;
}EventHandler;

typedef struct{
	Object *object;
	IOEventCallback method;
}IOEventHandler;

typedef struct{
	Object *object;
	TimerEventCallback method;
}TimerEventHandler;

typedef struct{
	void *data;
	EventHandler handler;
}BaseEvent;

typedef struct{
    int mask; /* one of AE_(READABLE|WRITABLE) */
	void *data;
    IOEventHandler cbRead;
    IOEventHandler cbWrite;
}IOEvent;

typedef struct{
	int fd;
	int mask;
}IOFired;

typedef struct TimerEvent{
	long long id;
	long when_sec; /* seconds */
    long when_ms; /* milliseconds */
	long milliseconds;
	void *data;
	TimerEvent *next;
	TimerEventHandler handler;
}TimerEvent;

typedef struct TickEvent{
	EventHandler handler;
	void *data;
	TickEvent *next;
}TickEvent;

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
	void 					addEventListener(int fd, int event, const IOEventHandler &cb, void *data = NULL);
	void 					removeEventListener(int fd, int event);
	long long 				setTimer(int millisecond, const TimerEventHandler &cb, void *data = NULL);
	void 					stopTimer(long long id);
	void					setLoopBefore(const EventHandler &cb, void *data){cbBefore = cb;pBeforeData = data;}
	void					nextTick(const EventHandler &cb, void *data = NULL);
private:
	void					resize(int maxfd);
	int 					procIoEvent();
	int		 				procTimerEvent();
	int						procTickEvent();
	TimerEvent*				searchNearestTimer();
private:
	int						nSize;
	int						nMaxfd;
	bool					bRunning;
	long long				nTimerNextId;
	time_t					nLastTime;
	IOEvent*				pEvents;
	IOFired*				pFireds;
	TimerEvent*				pTimerHead;
	IEventPoll*				pPoll;
	TickEvent*				pTickHead;
	TickEvent*				pTickTail;
	EventHandler			cbBefore;
	void*					pBeforeData;
};

NS_END