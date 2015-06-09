#pragma once
#include <string>
#include "fooking.h"
#include "Buffer.h"
#include "Connection.h"

extern "C"{
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

NS_BEGIN

class Script:
	public Object
{
public:
	Script();
	~Script();
public:
	bool			load(std::string &filename);
	bool			hasConnectProc(){ return bhConnect;}
	bool			hasInputProc(){ return bhInput;}
	bool			hasOutputProc(){ return bhOutput;}
	bool			hasCloseProc(){ return bhClose;}
	int				procConnect(Connection *conn);
	int				procInput(Connection *conn, int requestid, Buffer *input, Buffer *output);
	int				procOutput(Connection *conn, int requestid, Buffer *input, Buffer *output);
	int				procClose(Connection *conn);
private:
	lua_State*		pState;
	bool			bhConnect;
	bool			bhInput;
	bool			bhOutput;
	bool			bhClose;
};
NS_END