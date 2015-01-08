#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include "Script.h"
#include "Log.h"
#include "Buffer.h"

NS_USING;

Script::Script():
	pState(NULL)
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
	pState = lua_open();
	luaL_openlibs(pState);
	if(luaL_dofile(pState, filename.c_str())){
		printf("parse config file error=%s\n", lua_tostring(pState, -1));
		return false;
	}
	
	regLib();
	
	return true;
}

void Script::regLib()
{
	const luaL_Reg buff_methods[] = {                            
		{"data", luaBufferData},
		{"size", luaBufferSize},
		{"append", luaBufferAppend},
		{"seek", luaBufferSeek},
		{NULL, NULL}
	};
	
	luaL_register(pState, "fb", buff_methods);  
}

int	Script::luaBufferSize(lua_State *pState)
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

int	Script::luaBufferData(lua_State *pState)
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

int	Script::luaBufferAppend(lua_State *pState)
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

int Script::luaBufferSeek(lua_State *pState)
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

int Script::procInput(Buffer *input, Buffer *output)
{
	lua_getglobal(pState, "inputHandler");
	lua_pushlightuserdata(pState, input);
	lua_pushlightuserdata(pState, output);
	int ret = lua_pcall(pState, 2, 1, 0);
	if(ret != 0){
		LOG_ERR("call lua func inputHandler error, err=%s", lua_tostring(pState, -1));
		return -1;
	}
	
	int n = lua_tointeger(pState, -1);
	LOG("call lua func inputHandler, ret=%d", n);
	lua_pop(pState, 1);
	
	return n;
}

int Script::procOutput(Buffer *input, Buffer *output)
{
	lua_getglobal(pState, "outputHandler");
	lua_pushlightuserdata(pState, input);
	lua_pushlightuserdata(pState, output);
	int ret = lua_pcall(pState, 2, 1, 0);
	if(ret != 0){
		LOG_ERR("call lua func outputHandler error, err=%s", lua_tostring(pState, -1));
		return -1;
	}
	
	int n = lua_tointeger(pState, -1);
	LOG("call lua func outputHandler, ret=%d", n);
	lua_pop(pState, 1);
	
	return n;
}