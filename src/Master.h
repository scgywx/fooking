#pragma once
#include <map>
#include <list>
#include <vector>
#include <string>
#include "fooking.h"
#include "EventLoop.h"
#include "Server.h"
#include "Worker.h"
#include "Atomic.h"
#include "Script.h"

#define LockAcceptMutex(lock, val) atomic_cmp_set(lock, 0, val)
#define UnLockAcceptMutex(lock, val) atomic_cmp_set(lock, val, 0)

NS_BEGIN

typedef struct{
	atomic_t	lock;
	int			clients;
	int			workerClients[0];
}GlobalData;

class Master:
	public Object
{
public:
	Master(int argc, char **argv);
	~Master();
public:
	void 			start();
	void			addClient(int id)
	{
		pGlobals->workerClients[id]++;
		atomic_fetch_add(&pGlobals->clients, 1);
	}
	void			delClient(int id)
	{
		pGlobals->workerClients[id]--;
		atomic_fetch_sub(&pGlobals->clients, 1);
	}
private:
	bool			createGlobals();
	void			releaseGlobals();
	void 			setupSignal();
	static void 	procSignal(int sig);
public:
	int				nArgc;
	char**			pArgv;
	Server*			pServer;
	Worker**		pWorkers;
	Script*			pScript;
	GlobalData*		pGlobals;
	bool			bUseAcceptMutex;
};
NS_END