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
#include "Utils.h"

NS_USING;

static sig_atomic_t gExit;
static sig_atomic_t gReload;
static sig_atomic_t gSignal;

Master::Master(int argc, char **argv):
	nArgc(argc),
	pArgv(argv),
	pServer(NULL),
	pWorkers(NULL),
	pScript(NULL),
	pGlobals(NULL)
{
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
	
	ShareMemory::free(&shm);
}

void Master::start()
{
	//init script
	Config *cc = Config::getInstance();
	if(!cc->sScriptFile.empty()){
		pScript = new Script();
		if(!pScript->load(cc->sScriptFile)){
			printf("init script failed\n");
			return ;
		}
	}
	
	//session initialize
	Session::init(cc->nServerId);
	
	//create share memory
	shm.size = sizeof(GlobalData) + sizeof(int) * cc->nWorkers;
	if(ShareMemory::alloc(&shm) == 0){
		pGlobals = static_cast<GlobalData*>(shm.addr);
	}else{
		return ;
	}
		
	//create server
	pServer = new Server(NULL);
	if(pServer->createTcpServer(cc->nPort) != 0){
		printf("create tcp server failed, port=%d, errno=%d, errstr=%s\n", 
			cc->nPort, errno, strerror(errno));
		return ;
	}
	
	//make pipes
	arrWorkerPipes = makePipes(cc->nWorkers);
	if(!arrWorkerPipes){
		printf("create worker pipe failed\n");
		return ;
	}
	
	//create worker
	pWorkers = (Worker**)zmalloc(cc->nWorkers * sizeof(Worker*));
	bUseAcceptMutex = cc->nWorkers > 1;
	for(int i = 0; i < cc->nWorkers; ++i){
		pWorkers[i] = new Worker(this, i);
		pWorkers[i]->setPipe(arrWorkerPipes[i]);
		pWorkers[i]->start();
	}
	
	//set title
	char title[256];
	snprintf(title, 256, "fooking gateway master, %s", cc->sFile.c_str());
	utils::setProcTitle(title);
		
	//init signal
	setupSignal();
	
	//started log
	LOG_INFO("server started, listenfd=%d, port=%d", pServer->getSocket().getFd(), cc->nPort);

	//process manager
	while(true)
	{
		if(gExit){
			gExit = 0;
			LOG_INFO("master receive exit signal, signo=%d", gSignal);
			PipeMsg msg = {CH_EXIT, 0};
			for(int i = 0; i < cc->nWorkers; ++i){
				int fd = pWorkers[i]->getPipefd();
				int ret = ::write(fd, &msg, sizeof(msg));
				if(ret == -1){
					LOG_ERR("send exit signal to worker failed, error=%d, errstr=%s", errno, strerror(errno));
				}
			}
			break;
		}
		
		if(gReload){
			gReload = 0;
			LOG_INFO("master receive reload signal, signo=%d", gSignal);
			PipeMsg msg = {CH_RELOAD, 0};
			for(int i = 0; i < cc->nWorkers; ++i){
				int fd = pWorkers[i]->getPipefd();
				int ret = ::write(fd, &msg, sizeof(msg));
				if(ret == -1){
					LOG_ERR("send reload signal to worker failed, error=%d, errstr=%s", errno, strerror(errno));
				}
			}
		}
		
		int ret = 0;
		int pid = ::wait(&ret);
		if(pid <= 0){
			continue;
		}
		
		int found = -1;
		for(int i = 0; i < cc->nWorkers; ++i){
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
			pWorkers[found]->setPipe(arrWorkerPipes[found]);
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
	gSignal = sig;
	switch(sig){
		case SIGUSR1:
			gReload = 1;
			break;
		default:
			gExit = 1;
			break;
	}
}

PipeType* Master::makePipes(int n)
{
	PipeType *pipes = new PipeType[n];
	bool fail = false;
	int i, j;
	
	for(i = 0; i < n; ++i){
		int *fds = pipes[i];
		fds[0] = fds[1] = -1;
		
		if(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1){
			fail = true;
			break;
		}
		
		for(j = 0; j < 2; ++j){
			//set nonblock
			int flags = ::fcntl(fds[j], F_GETFL, 0);
			flags |= O_NONBLOCK;
			
			int ret = ::fcntl(fds[j], F_SETFL, flags);
			if(ret < 0){
				fail = true;
				break;
			}
		}
		
		if(fail) break;
	}
	
	if(fail){
		for(int x = i; x >=0; --x){
			int *fds = pipes[x];
			if(fds[0] != -1) close(fds[0]);
			if(fds[1] != -1) close(fds[1]);
		}
		
		delete pipes;
		
		return NULL;
	}
	
	return pipes;
}