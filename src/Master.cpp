#include <iostream>
#include <stdio.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>
#include "Master.h"
#include "Socket.h"
#include "Script.h"
#include "Atomic.h"
#include "Config.h"
#include "Log.h"

NS_USING;

static Master *gMaster;

Master::Master(int argc, char **argv):
	nArgc(argc),
	pArgv(argv),
	pServer(NULL),
	pWorkers(NULL),
	pScript(NULL),
	pGlobals(NULL)
{
	gMaster = this;
}

Master::~Master()
{
	delete pServer;
	delete pScript;
	
	Config *pConfig = Config::getInstance();
	if(pWorkers){
		for(int i = 0; i < pConfig->nWorkers; ++i){
			delete pWorkers[i];
		}
		zfree(pWorkers);
	}
	
	releaseGlobals();
}

void Master::start()
{
	//init script
	Config *pConfig = Config::getInstance();
	if(!pConfig->sScriptFile.empty()){
		pScript = new Script();
		if(!pScript->load(pConfig->sScriptFile)){
			printf("init script failed\n");
			return ;
		}
	}
	
	//session initialize
	Session::init();
	
	//create share memory
	if(!createGlobals()){
		return ;
	}
		
	//create server
	pServer = new Server(NULL);
	if(pServer->createTcpServer(pConfig->nPort) != 0){
		printf("create tcp server failed, port=%d, errno=%d, errstr=%s\n", 
			pConfig->nPort, errno, strerror(errno));
		return ;
	}
	
	//create worker
	pWorkers = (Worker**)zmalloc(pConfig->nWorkers * sizeof(Worker*));
	bUseAcceptMutex = pConfig->nWorkers > 1;
	for(int i = 0; i < pConfig->nWorkers; ++i){
		pWorkers[i] = new Worker(this, i);
		pWorkers[i]->start();
	}
	
	//set title
	char title[256];
	snprintf(title, 256, "fooking gateway master, %s", pConfig->sFile.c_str());
	utils::setProcTitle(title);
		
	//init signal
	setupSignal();
	
	//started log
	LOG_INFO("server started, listenfd=%d, port=%d", pServer->getSocket().getFd(), pConfig->nPort);

	//wait worker exit
	bRunning = true;
	while(bRunning)
	{
		int ret = 0;
		int pid = ::wait(&ret);
		if(pid <= 0){
			continue;
		}
		
		int found = -1;
		for(int i = 0; i < pConfig->nWorkers; ++i){
			Worker *pWorker = pWorkers[i];
			if(pWorker->getPid() == pid){
				found = pWorker->id();
				delete pWorker;
				break;
			}
		}
		
		if(found == -1){
			LOG_ERR("worker exited, not found workerid");
		}else{
			LOG_INFO("worker exited, id=%d, pid=%d", found, pid);
			atomic_fetch_sub(&pGlobals->clients, pGlobals->workerClients[found]);
			pGlobals->workerClients[found] = 0;
			
			//free lock
			if(bUseAcceptMutex){
				UnLockAcceptMutex(&pGlobals->lock, pid);
			}
			
			//restart
			pWorkers[found] = new Worker(this, found);
			pWorkers[found]->start();
		}
	}
}

void Master::setupSignal()
{
	struct sigaction act;
	
	signal(SIGHUP, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);
	
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	act.sa_handler = Master::procSignal;
	sigaction(SIGTERM, &act, NULL);
	sigaction(SIGINT, &act, NULL);
	sigaction(SIGUSR1, &act, NULL);
}

void Master::procSignal(int sig)
{
	LOG_INFO("master receive signal=%d", sig);
	switch(sig){
		case SIGUSR1:
			//RELOAD TODO
			break;
		default:
		{
			Config *cc = Config::getInstance();
			Master *pThis = gMaster;
			
			ChannelMsg msg = {CH_EXIT, 0};
			for(int i = 0; i < cc->nWorkers; ++i){
				pThis->pWorkers[i]->send(&msg);
			}
			
			pThis->bRunning = false;
			
			break;
		}
	}
}

bool Master::createGlobals()
{
	Config *pConfig = Config::getInstance();
	int size = sizeof(GlobalData) + sizeof(int) * pConfig->nWorkers;
	int id = shmget(IPC_PRIVATE, size, (SHM_R|SHM_W|IPC_CREAT));
	if (id == -1) {
		LOG_ERR("shmget(%u) failed", size);
		return false;
	}

	pGlobals = (GlobalData*)shmat(id, NULL, 0);
	if(pGlobals == (void*)-1){
		LOG_ERR("shmat() failed");
		return false;
	}

	if (shmctl(id, IPC_RMID, NULL) == -1) {
		LOG_ERR("shmctl(IPC_RMID) failed");
		return false;
	}

	return true;
}

void Master::releaseGlobals()
{
	if(shmdt(pGlobals) == -1) {
		LOG_ERR("shmdt(%p) failed", pGlobals);
	}
}