#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include "Script.h"
#include "Log.h"
#include "Buffer.h"
#include "Worker.h"

NS_USING;

static int luaBufferSize(lua_State *pState)
{
	if(!lua_isuserdata(pState, -1)){
		return luaL_error(pState, "invalid argument:1");
	}
	
	Buffer *pBuffer = (Buffer*)lua_touserdata(pState, -1);
	if(!pBuffer){
		return luaL_error(pState, "invalid buffer");
	}
	
	lua_pop(pState, 1);
	lua_pushinteger(pState, pBuffer->size());
	
	return 1;
}

static int luaBufferData(lua_State *pState)
{
	if(!lua_isuserdata(pState, -1)){
		lua_pushstring(pState, "invalid argument:1");
		lua_error(pState);
		return 1;
	}
	
	Buffer *pBuffer = (Buffer*)lua_touserdata(pState, -1);
	if(!pBuffer){
		lua_pushstring(pState, "invalid buffer");
		lua_error(pState);
		return 1;
	}
	
	lua_pop(pState, 1);
	lua_pushlstring(pState, pBuffer->data(), pBuffer->size());
	
	return 1;
}

static int luaBufferAppend(lua_State *pState)
{
	if(!lua_isuserdata(pState, -2)){
		return luaL_error(pState, "invalid argument:1");
	}
	
	if(!lua_isstring(pState, -1)){
		return luaL_error(pState, "invalid argument:2");
	}
	
	Buffer *pBuffer = (Buffer*)lua_touserdata(pState, -2);
	if(!pBuffer){
		return luaL_error(pState, "invalid buffer");
	}
	
	size_t len = 0;
	const char *s = lua_tolstring(pState, -1, &len);
	if(s && len){
		pBuffer->append(s, len);
	}
	
	lua_pop(pState, 2);
	
	return 0;
}

static int luaBufferSeek(lua_State *pState)
{
	if(!lua_isuserdata(pState, -2)){
		return luaL_error(pState, "invalid argument:1");
	}
	
	if(!lua_isnumber(pState, -1)){
		return luaL_error(pState, "invalid argument:2");
	}
	
	Buffer *pBuffer = (Buffer*)lua_touserdata(pState, -2);
	if(!pBuffer){
		return luaL_error(pState, "invalid buffer");
	}
	
	int n = lua_tointeger(pState, -1);
	if(n <= 0){
		return luaL_error(pState, "invalid number");
	}
	
	pBuffer->seek(n);
	
	lua_pop(pState, 2);
	
	return 0;
}

static int luaConnGetBuffer(lua_State *pState)
{
	if(!lua_isuserdata(pState, -1)){
		return luaL_error(pState, "invalid argument:1");
	}
	
	Connection *pConn = (Connection*)lua_touserdata(pState, -1);
	if(!pConn){
		return luaL_error(pState, "invalid connection");
	}
	
	lua_pop(pState, 1);
	
	Buffer *pBuffer = pConn->getBuffer();
	lua_pushlightuserdata(pState, pBuffer);
	
	return 1;
}

static int luaConnSend(lua_State *pState)
{
	if(!lua_isuserdata(pState, -2)){
		return luaL_error(pState, "invalid argument:1");
	}
	
	if(!lua_isstring(pState, -1)){
		return luaL_error(pState, "invalid argument:2");
	}
	
	Connection *pConn = (Connection*)lua_touserdata(pState, -2);
	if(!pConn){
		return luaL_error(pState, "invalid connection");
	}
	
	size_t len = 0;
	const char *s = lua_tolstring(pState, -1, &len);
	if(s && len){
		pConn->send(s, len);
	}
	
	lua_pop(pState, 2);
	
	return 0;
}

static int luaConnSessionId(lua_State *pState)
{
	Connection *pConn = (Connection*)lua_touserdata(pState, -1);
	if(!pConn){
		return luaL_error(pState, "invalid connection");
	}
	
	lua_pop(pState, 1);
	
	ClientContext *pCtx = (ClientContext*)pConn->getContext();
	if(pCtx){
		lua_pushlstring(pState, pCtx->session.getId(), SID_LENGTH);
	}
	
	return 1;
}

static int luaConnClose(lua_State *pState)
{
	Connection *pConn = (Connection*)lua_touserdata(pState, -1);
	if(!pConn){
		return luaL_error(pState, "invalid connection");
	}
	
	pConn->close();
		
	lua_pop(pState, 1);
	
	return 0;
}

static int luaopen_buffer(lua_State* l)
{
	const luaL_Reg func[] = {                            
		{"data", luaBufferData},
		{"size", luaBufferSize},
		{"append", luaBufferAppend},
		{"seek", luaBufferSeek},
		{NULL, NULL}
	};
    luaL_newlib(l, func);
    return 1;
}

static int luaopen_connection(lua_State* l)
{
	const luaL_Reg func[] = {
		{"buffer", luaConnGetBuffer},
		{"send", luaConnSend},
		{"id", luaConnSessionId},
		{"close", luaConnClose},
		{NULL, NULL}
	};
    luaL_newlib(l, func);
    return 1;
}

Script::Script():
	pState(NULL),
	bhConnect(false),
	bhRead(false),
	bhWrite(false),
	bhClose(false)
{

}

Script::~Script()
{
	if(pState){
		lua_close(pState);
	}
}

bool Script::load(std::string &filename)
{
	pState = luaL_newstate();
	luaL_openlibs(pState);
	
	//reg library
	luaL_requiref(pState, "fooking.buffer", luaopen_buffer, 0);
	luaL_requiref(pState, "fooking.connection", luaopen_connection, 0);
	
	//get real dir
	char realdir[PATH_MAX];
	realpath(filename.c_str(), realdir);
	int len = strlen(realdir);
	for(int i = len - 1; i >= 0; --i){
		if(realdir[i] == '/'){
			realdir[i] = 0;
			break;
		}
	}
	
	//set package path
	lua_getglobal(pState, "package");
	lua_getfield(pState, -1, "path");
	std::string pkgpath = lua_tostring(pState, -1);
	pkgpath.append(";");
	pkgpath.append(realdir);
	pkgpath.append("/?.lua");
	lua_pop(pState, 1 );
	lua_pushstring(pState, pkgpath.c_str());
	lua_setfield(pState, -2, "path");
	lua_pop(pState, 1);
	
	//dofile
	if(luaL_dofile(pState, filename.c_str())){
		printf("parse config file error=%s\n", lua_tostring(pState, -1));
		return false;
	}
	
	//check read handler
	lua_getglobal(pState, "onRead");
	if(lua_isfunction(pState, -1)){
		bhRead = true;
	}
	
	//check write handler
	lua_getglobal(pState, "onWrite");
	if(lua_isfunction(pState, -1)){
		bhWrite = true;
	}
	
	//check connect handler
	lua_getglobal(pState, "onConnect");
	if(lua_isfunction(pState, -1)){
		bhConnect = true;
	}
	
	//check close handler
	lua_getglobal(pState, "onClose");
	if(lua_isfunction(pState, -1)){
		bhClose = true;
	}
	
	//clear stack
	lua_pop(pState, 4);
	
	return true;
}

int Script::procRead(Connection *conn, int requestid, Buffer *input, Buffer *output)
{
	if(!bhRead){
		return 0;
	}
	
	LOG("call lua func onRead start");
	lua_getglobal(pState, "onRead");
	lua_pushlightuserdata(pState, conn);
	lua_pushinteger(pState, requestid);
	lua_pushlightuserdata(pState, input);
	lua_pushlightuserdata(pState, output);
	int ret = lua_pcall(pState, 4, 1, 0);
	if(ret != 0){
		LOG_ERR("call lua func onRead error, err=%s", lua_tostring(pState, -1));
		return 0;//不能返回-1,如果lua调用错误，可能导致内存急剧增长
	}
	
	int n = lua_tointeger(pState, -1);
	LOG("call lua func onRead, ret=%d", n);
	lua_pop(pState, 1);
	
	return n;
}

int Script::procWrite(Connection *conn, int requestid, Buffer *input, Buffer *output)
{
	if(!bhWrite){
		return 0;
	}
	
	LOG("call lua func onWrite start");
	lua_getglobal(pState, "onWrite");
	lua_pushlightuserdata(pState, conn);
	lua_pushinteger(pState, requestid);
	lua_pushlightuserdata(pState, input);
	lua_pushlightuserdata(pState, output);
	int ret = lua_pcall(pState, 4, 1, 0);
	if(ret != 0){
		LOG_ERR("call lua func onWrite error, err=%s", lua_tostring(pState, -1));
		return 0;//同上
	}
	
	int n = lua_tointeger(pState, -1);
	LOG("call lua func onWrite, ret=%d", n);
	lua_pop(pState, 1);
	
	return n;
}

int Script::procConnect(Connection *conn)
{
	if(!bhConnect){
		return 0;
	}
	
	lua_getglobal(pState, "onConnect");
	lua_pushlightuserdata(pState, conn);
	int ret = lua_pcall(pState, 1, 0, 0);
	if(ret != 0){
		LOG_ERR("call lua func onConnect error, err=%s", lua_tostring(pState, -1));
		return 0;//同上
	}
	
	return 0;
}

int Script::procClose(Connection *conn)
{
	if(!bhClose){
		return 0;
	}
	
	lua_getglobal(pState, "onClose");
	lua_pushlightuserdata(pState, conn);
	int ret = lua_pcall(pState, 1, 0, 0);
	if(ret != 0){
		LOG_ERR("call lua func onClose error, err=%s", lua_tostring(pState, -1));
		return 0;//同上
	}
	
	return 0;
}
