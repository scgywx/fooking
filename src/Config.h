#pragma once
#include <vector>
#include <string>
#include "fooking.h"
#include "Hashtable.h"

extern "C"{
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

NS_BEGIN

typedef struct{
	int type;
	std::string host;
	short port;
	short weight;
}BackendOption;

typedef std::vector<BackendOption> BackendServer;
typedef hash_map<std::string, std::string> FastcgiParams;

class Config:
	public Object
{
private:
	Config();
	~Config();
public:
	static Config* getInstance()
	{
		if(!psObj){
			psObj = new Config();
		}
		return psObj;
	}
	bool				load(const char *filename);
private:
	int 				readInt(const char *key);
	bool 				readBoolean(const char *key);
	const char*			readString(const char *key);
	bool 				addBackendServer(const char *str, int weight);
private:
	lua_State*			pState;
	static Config*		psObj;
public:
	std::string			sFile;//log文件名
	std::string			sHost;//服务器IP
	short				nPort;//服务器端口
	bool				bDaemonize;//是否守护进行
	bool				bRouter;//是否router server
	bool				bEventConnect;//新连接是否通知
	bool				bEventClose;//关闭连接是否通知
	short				nWorkers;//进程数
	int					nServerId;//服务器id
	int					nMaxClients;//最大连接数
	int					nProtocol;//协议
	int					nSendBufferSize;//最大发送缓存区大小
	int					nRecvBufferSize;//最大接受缓冲区大小
	std::string			sLogFile;//日志文件
	int					nLogLevel;//日志级别
	std::string			sRouterHost;//路由服务器ip
	short				nRouterPort;//路由服务器port
	std::string			sScriptFile;//script
	BackendServer		arrBackendServer;//后端服务器
	int					nBackendTimeout;//后端超时时间		
	int					nMaxBackendWeights;//后端服务器总权重值
	std::string			sFastcgiRoot;
	std::string			sFastcgiFile;
	FastcgiParams		arFastcgiParams;
	int					nIdleTime;//空闲时间
};
NS_END