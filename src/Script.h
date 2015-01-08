#pragma once
#include <string>
#include "Common.h"
#include "Object.h"
#include "Buffer.h"

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
	int				procInput(Buffer *input, Buffer *output);
	int				procOutput(Buffer *input, Buffer *output);
private:
	void			regLib();
	static int		luaBufferData(lua_State *L);
	static int		luaBufferSize(lua_State *L);
	static int		luaBufferAppend(lua_State *L);
	static int		luaBufferSeek(lua_State *L);
private:
	lua_State*		pState;
};
NS_END