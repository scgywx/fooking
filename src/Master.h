#pragma once
#include <map>
#include <list>
#include <vector>
#include <string>
#include "Object.h"
#include "EventLoop.h"
#include "Server.h"
#include "Worker.h"
#include "Atomic.h"
#include "Config.h"

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
private:
	Master();
	~Master();
public:
	void 			start(int argc, char **argv);
	static Master*	getInstance();
	void			setProcTitle(const char *title);
private:
	void			daemonize();
	bool			createGlobals();
	void			releaseGlobals();
	void 			setupSignal();
	static void 	procSignal(int sig);
	void			initProcTitle();
public:
	int				nArgc;
	char**			pArgv;
	Server*			pServer;
	Worker**		pWorkers;
	Config*			pConfig;
	Script*			pScript;
	GlobalData*		pGlobals;
	std::string		sConfigFile;//配置文件
	bool			bAcceptLock;
	//单例?
	static Master*	pInstance;
};
NS_END