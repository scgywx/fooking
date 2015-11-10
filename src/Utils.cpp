#include "Utils.h"

extern "C"{
	extern char **environ;
}

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
		proc.argvLast = argv[0];
		proc.argvCopy = (char**)zmalloc(sizeof(char*) * argc);
		
		for(int i = 0; i < argc; ++i){
			if(proc.argvLast == argv[i]){
				int n = strlen(argv[i]);
				
				proc.argvLast = argv[i] + n + 1;
				
				proc.argvCopy[i] = (char*)zmalloc(n + 1);
				memcpy(proc.argvCopy[i], argv[i], n + 1);
			}
		}
		
		size_t size = 0;
		for (int i = 0; environ[i]; ++i) {
			size += strlen(environ[i]) + 1;
			if(proc.argvLast == environ[i]){
				proc.argvLast = environ[i] + strlen(environ[i]) + 1;
			}
		}
		
		proc.argvLast--;

		char *raw = (char*)zmalloc(size);
		for(int i = 0; environ[i]; ++i) {
			int envlen = strlen(environ[i]) + 1;
			
			memcpy(raw, environ[i], envlen);
			environ[i] = raw;
			raw+= envlen;
		}
	}

	void setProcTitle(const char *title)
	{
		char *p = proc.argv[0];
		int tlen = std::min<int>(strlen(title), proc.argvLast - p);
		strncpy(p, title, tlen);
		memset(p + tlen, 0, proc.argvLast - p - tlen);
	}
}
NS_END;