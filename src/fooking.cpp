#include <iostream>
#include "fooking.h"
#include "Router.h"
#include "Master.h"
#include "Config.h"
#include "Log.h"

extern char **environ;

char**	fk_pArgv;
int		fk_nArgc;
char*	fk_pLastArgv;

NS_BEGIN
namespace utils
{
	SocketOption parseSocket(const char *string)
	{
		char *str = (char*)string;
		int len = strlen(string);
		SocketOption opt;
		memset(&opt, 0, sizeof(SocketOption));
		
		if(strncmp(str, "unix:", 5) == 0){//unix
			opt.type = SOCKET_UNIX;
			memcpy(opt.unix_name, str + 5, len - 5);
			return opt;
		}
		
		//tcp tok
		int pos = 0;
		if(strncmp(str, "tcp://", 6) == 0){
			pos = 6;
		}
		
		char *sep = strchr(str + pos, ':');
		if(!sep){
			return opt;
		}
		
		//host length
		int nhost = sep - str + pos;
		
		//port
		int port = atoi(sep + 1);
		if(port < 1){
			return opt;
		}
		
		opt.type = SOCKET_TCP;
		opt.tcp_port = port;
		memcpy(opt.tcp_host, str + pos, nhost);
		
		return opt;
	}

	void initProcTitle(int argc, char **argv)
	{
		fk_nArgc = argc;
		fk_pArgv = argv;
		fk_pLastArgv = argv[0];
		
		for(int i = 0; i < argc; ++i){
			if(fk_pLastArgv == argv[i]){
				fk_pLastArgv = argv[i] + strlen(argv[i]) + 1;
			}
		}
		
		size_t size = 0;
		for (int i = 0; environ[i]; ++i) {
			size += strlen(environ[i]) + 1;
			if(fk_pLastArgv == environ[i]){
				fk_pLastArgv = environ[i] + strlen(environ[i]) + 1;
			}
		}
		
		fk_pLastArgv--;

		char *raw = new char[size];
		for(int i = 0; environ[i]; ++i) {
			int envlen = strlen(environ[i]) + 1;
			memcpy(raw, environ[i], envlen);
			environ[i] = raw;
			raw+= envlen;
		}
	}

	void setProcTitle(const char *title)
	{
		char *p = fk_pArgv[0];
		int tlen = std::min<int>(strlen(title), fk_pLastArgv - p);
		strncpy(p, title, tlen);
		memset(p + tlen, 0, fk_pLastArgv - p - tlen);
	}

	void daemonize()
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

}
NS_END;

NS_USING;

int main(int argc, char **argv)
{
	//args check
	if(argc < 2){
		printf("usage: fooking config.lua\n");
		return -1;
	}
	
	//init config
	Config *pConfig = Config::getInstance();
	if(!pConfig->load(argv[1])){
		return -1;
	}
	
	//int log
	Log *pLog = Log::getInstance();
	if(!pConfig->sLogFile.empty()){
		if(pConfig->sLogFile == "stdout"){
			pLog->init(pConfig->nLogLevel, STDOUT_FILENO);
		}else if(pConfig->sLogFile == "stderr"){
			pLog->init(pConfig->nLogLevel, STDERR_FILENO);
		}else if(!pLog->init(pConfig->nLogLevel, pConfig->sLogFile.c_str())){
			printf("init log failed\n");
			return -1;
		}
	}
	
	//daemonize
	if(pConfig->bDaemonize){
		utils::daemonize();
	}
	
	//title init
	utils::initProcTitle(argc, argv);
	
	//server
	if(pConfig->bRouter){
		//router server
		Router *pRouter = new Router(argc, argv);
		pRouter->start();
	}else{
		//gateway server
		Master *pMaster = new Master(argc, argv);
		pMaster->start();
	}
	
	return 0;
}