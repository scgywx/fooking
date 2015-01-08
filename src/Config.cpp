#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include "Config.h"
#include "Utils.h"

NS_USING;

Config::Config():
	pState(NULL)
{

}

Config::~Config()
{
	lua_close(pState);
}

bool Config::load(const char *filename)
{
	pState = lua_open();
	if(luaL_dofile(pState, filename)){
		printf("parse config file error=%s\n", lua_tostring(pState, -1));
		return false;
	}
	
	//basic option
	sHost = readString("HOST");//host
	nPort = readInt("PORT");//port
	bDaemonize = readBoolean("DAEMONIZE");
	sLogFile = readString("LOG_FILE");//log file
	nLogLevel = readInt("LOG_LEVEL");//log level
	bRouter = readBoolean("ROUTER");//router server
	
	//router == 1
	if(bRouter){
		return true;
	}
	
	//worker option
	nServerId = readInt("SERVER_ID");
	bEventConnect = readBoolean("EVENT_CONNECT");
	bEventClose = readBoolean("EVENT_CLOSE");
	nWorkers = readInt("WORKER_NUM");
	if(nWorkers < 1){
		printf("WORKER_NUM Invalid(WORKER_NUM>=1)\n");
		return false;
	}
	
	nMaxClients = readInt("MAX_CLIENT_NUM");
	if(nMaxClients < 1){
		printf("MAX_CLIENT_NUM Invalid(MAX_CLIENT_NUM>=1)\n");
		return false;
	}
	
	nSendBufferSize = readInt("SEND_BUFF_SIZE");
	if(nSendBufferSize < 1){
		printf("SEND_BUFF_SIZE Invalid(SEND_BUFF_SIZE>=1)\n");
		return false;
	}
	
	nRecvBufferSize = readInt("RECV_BUFF_SIZE");
	if(nRecvBufferSize < 1){
		printf("RECV_BUFF_SIZE Invalid(RECV_BUFF_SIZE>=1)\n");
		return false;
	}
	
	//暂时只能fastcgi
	nProtocol = readInt("PROTOCOL");
	
	//backend server list
	nBackendTimeout = readInt("BACKEND_TIMEOUT");
	lua_getglobal(pState, "BACKEND_SERVER");
	if(lua_isstring(pState, -1)){
		const char *val = lua_tostring(pState, -1);
		if(!addBackendServer(val, 1)){
			printf("BACKEND_SERVER Invalid\n");
			return false;
		}
	}else if(lua_istable(pState, -1)){
		lua_pushnil(pState);
		while(lua_next(pState, -2))
		{
			const char *str = NULL;
			int weight = 0;
			if(lua_isstring(pState, -2)){
				str = lua_tostring(pState, -2);
			}
			if(lua_isnumber(pState, -1)){
				weight = lua_tonumber(pState, -1);;
			}
			if(str && weight > 0){
				if(!addBackendServer(str, weight)){
					printf("BACKEND_SERVER Invalid\n");
					return false;
				}
			}
			lua_pop(pState, 1);
		}
		lua_pop(pState, 1);
	}else{
		printf("BACKEND_SERVER invalid\n");
		return false;
	}
	
	//router option
	sScriptFile = readString("SCRIPT_FILE");
	sRouterHost = readString("ROUTER_HOST");
	nRouterPort = readInt("ROUTER_PORT");
	
	//fastcgi
	sFastcgiRoot = readString("FASTCGI_ROOT");
	sFastcgiFile = readString("FASTCGI_FILE");
	
	//fastcgi params
	lua_getglobal(pState, "FASTCGI_PARAMS");
	if(lua_istable(pState, -1)){
		lua_pushnil(pState);
		while(lua_next(pState, -2))
		{
			const char *key = NULL;
			const char *val = NULL;
			if(lua_isstring(pState, -2)){
				key = lua_tostring(pState, -2);
			}
			if(lua_isstring(pState, -1)){
				val = lua_tostring(pState, -1);
			}
			if(key && val){
				arFastcgiParams[key] = val;
			}
			lua_pop(pState, 1);
		}
		lua_pop(pState, 1);
	}else{
		printf("FASTCGI_PARAMS invalid\n");
		return false;
	}
	
	return true;
}

bool Config::addBackendServer(const char *str, int weight)
{
	SocketOption opt = Utils::parseSocket(str);
	BackendOption backend;
	switch(opt.type){
		case SOCKET_TCP:
			backend.type = SOCKET_TCP;
			backend.host = opt.tcp_host;
			backend.port = opt.tcp_port;
			break;
		case SOCKET_UNIX:
			backend.type = SOCKET_UNIX;
			backend.host = opt.unix_name;
			break;
		default:
			//Nothing todo;
			return false;
	}
	
	backend.weight = weight;
	arrBackendServer.push_back(backend);
	
	nMaxBackendWeights+= weight;
	
	return true;
}

int Config::readInt(const char *key)
{
	lua_getglobal(pState, key);
	int n = lua_tonumber(pState, -1);
	lua_pop(pState, 1);
	
	return n;
}

bool Config::readBoolean(const char *key)
{
	int n = readInt(key);
	return n ? true : false;
}

const char* Config::readString(const char *key)
{
	lua_getglobal(pState, key);
	const char *str = lua_tostring(pState, -1);
	lua_pop(pState, 1);
	if(str) return str;
	return "";
}