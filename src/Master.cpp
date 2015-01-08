#include <iostream>
#include <stdio.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>
#include "Master.h"
#include "Router.h"
#include "Socket.h"
#include "Log.h"
#include "Script.h"
#include "Utils.h"
#include "Atomic.h"

NS_USING;

extern char **environ;

Master* Master::pInstance = NULL;

Master::Master():
	pServer(NULL),
	pWorkers(NULL),
	pConfig(NULL),
	pScript(NULL),
	pGlobals(NULL)
{
}

Master::~Master()
{
	delete pServer;
	delete pConfig;
	delete pScript;
	
	if(pWorkers){
		for(int i = 0; i < pConfig->nWorkers; ++i){
			delete pWorkers[i];
		}
		zfree(pWorkers);
	}
	
	releaseGlobals();
}

Master*	Master::getInstance()
{
	if(!pInstance){
		pInstance = new Master();
	}
	return pInstance;
}

void Master::start(int argc, char **argv)
{
	//args check
	if(argc < 2){
		printf("usage: fooking config.lua\n");
		return ;
	}
	
	nArgc = argc;
	pArgv = argv;
	initProcTitle();
	
	//config init
	sConfigFile = argv[1];
	pConfig = new Config();
	if(!pConfig->load(argv[1])){
		return ;
	}
	
	//log init
	if(!pConfig->sLogFile.empty()){
		if(pConfig->sLogFile == "stdout"){
			Log::init(pConfig->nLogLevel, STDOUT_FILENO);
		}else if(pConfig->sLogFile == "stderr"){
			Log::init(pConfig->nLogLevel, STDERR_FILENO);
		}else if(!Log::init(pConfig->nLogLevel, pConfig->sLogFile.c_str())){
			printf("init log failed\n");
			return ;
		}
	}
	
	//init script
	if(!pConfig->sScriptFile.empty()){
		pScript = new Script();
		if(!pScript->load(pConfig->sScriptFile)){
			printf("init script failed\n");
			return ;
		}
	}
	
	//daemonize
	if(pConfig->bDaemonize){
		daemonize();
	}
	
	//session initialize
	Session::init();
	
	//server
	if(pConfig->bRouter){
		//create router
		setProcTitle("fooking router server");
		Router *pRouter = new Router(this);
		pRouter->start();
	}else{
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
		bAcceptLock = pConfig->nWorkers > 1;
		for(int i = 0; i < pConfig->nWorkers; ++i){
			pWorkers[i] = new Worker(this, i);
			pWorkers[i]->start();
		}
		
		setProcTitle("fooking gateway master");
		LOG_INFO("server started, listenfd=%d", pServer->getSocket().getFd());
		
		//init signal
		setupSignal();

		//wait worker exit
		while(true)
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
				if(bAcceptLock){
					UnLockAcceptMutex(&pGlobals->lock, pid);
				}
				
				//restart
				pWorkers[found] = new Worker(this, found);
				pWorkers[found]->start();
			}
		}
	}
}

void Master::daemonize()
{
	if (fork() != 0){
		exit(0); /* parent exit */
	}

	setsid();

	int fd;
	if ((fd = open("/dev/null", O_RDWR, 0)) != -1) {
		dup2(fd, STDIN_FILENO);
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		if (fd > STDERR_FILENO) close(fd);
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
	sigaction(SIGUSR1, &act, NULL);
}

void Master::procSignal(int sig)
{
	Master *pThis = Master::getInstance();
	LOG_INFO("master receive signal=%d", sig);
	switch(sig){
		case SIGUSR1:
			for(int i = 0; i < pThis->pConfig->nWorkers; ++i){
				ChannelMsg ch;
				ch.type = CH_RELOAD;
				ch.fd = 0;
				ch.pid = 0;
				pThis->pWorkers[i]->send(&ch);
			}
			break;
		default:
			kill(0, SIGKILL);
			break;
	}
}

bool Master::createGlobals()
{
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


void Master::initProcTitle()
{
	size_t size = 0;
	for (int i = 0; environ[i]; ++i) {
		size += strlen(environ[i]) + 1; 
	}

	char *raw = new char[size];
	for(int i = 0; environ[i]; ++i) {
		int envlen = strlen(environ[i]) + 1;
		memcpy(raw, environ[i], envlen);
		environ[i] = raw;
		raw+= envlen;
	}
}

void Master::setProcTitle(const char *title)
{
	char *p = pArgv[0];
	int tlen = strlen(title);
	strncpy(p, title, tlen);
	p[tlen] = '\0';
}