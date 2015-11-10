#include <iostream>
#include "fooking.h"
#include "Router.h"
#include "Master.h"
#include "Config.h"
#include "Log.h"
#include "Utils.h"

ProcessInfo proc;

NS_USING;

static void daemonize()
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

int main(int argc, char **argv)
{
	//args check
	if(argc < 2){
		printf("usage: fooking config.lua\n");
		return -1;
	}
	
	//init process info
	proc.argc = argc;
	proc.argv = argv;
	
	srandom(time(NULL));
	
	//init config
	Config *cc = Config::getInstance();
	if(!cc->load(argv[1])){
		return -1;
	}
	
	//init log
	Log *pLog = Log::getInstance();
	if(!cc->sLogFile.empty()){
		if(cc->sLogFile == "stdout"){
			pLog->init(cc->nLogLevel, STDOUT_FILENO);
		}else if(cc->sLogFile == "stderr"){
			pLog->init(cc->nLogLevel, STDERR_FILENO);
		}else if(!pLog->init(cc->nLogLevel, cc->sLogFile.c_str())){
			printf("init log failed\n");
			return -1;
		}
	}
	
	//daemonize
	if(cc->bDaemonize){
		daemonize();
	}
	
	//title init
	utils::initProcTitle(argc, argv);
	
	//server
	if(cc->bRouter){
		//router server
		Router *pRouter = new Router(argc, argv);
		pRouter->start();
		delete pRouter;
	}else{
		//gateway server
		Master *pMaster = new Master(argc, argv);
		pMaster->start();
		delete pMaster;
	}
	
	//release config
	Config::release();
	
	return 0;
}