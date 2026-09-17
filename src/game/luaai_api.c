/**
 * PLACEHOLDER for Kai's scripting API (luaai_api.c), which is not ported yet.
 *
 * luaai.c needs exactly four symbols from that file. This stub provides them
 * with the prototypes from game/luaai.h and nothing else: no pd.* functions,
 * no overlays, no event emitters, no X-ray recording. Replace this file
 * wholesale when the real API is ported.
 *
 * luaApiPushChrInfo builds the same table shape as Kai's (be46717), because
 * ctx:self() already depends on it and it costs nothing.
 */

#include <ultra64.h>

#include "types.h"
#include "game/luaai.h"

#include "lua.h"
#include "lauxlib.h"

/* Called with the pd table on the stack top (luaai.c builds it and holds
 * pd.register_ailist and pd.log). Registers nothing more. If no table is on
 * top, create an empty global `pd` so scripts can still test for it. */
void luaApiRegister(struct lua_State *L)
{
	if (!lua_istable(L, -1)) {
		lua_newtable(L);
		lua_setglobal(L, "pd");
	}
}

void luaApiResetFrame(void)
{
}

void luaApiRecordChr(s32 chrnum, s32 ailistid, s32 aioffset, s32 alertness, s32 islua)
{
	(void)chrnum;
	(void)ailistid;
	(void)aioffset;
	(void)alertness;
	(void)islua;
}

void luaApiPushChrInfo(struct lua_State *L, const struct luaaiselfinfo *info)
{
	lua_newtable(L);
	lua_pushinteger(L, info->chrnum);    lua_setfield(L, -2, "chrnum");
	lua_pushnumber(L, info->x);          lua_setfield(L, -2, "x");
	lua_pushnumber(L, info->y);          lua_setfield(L, -2, "y");
	lua_pushnumber(L, info->z);          lua_setfield(L, -2, "z");
	lua_pushinteger(L, info->room);      lua_setfield(L, -2, "room");
	lua_pushnumber(L, info->health);     lua_setfield(L, -2, "health");
	lua_pushnumber(L, info->maxhealth);  lua_setfield(L, -2, "maxhealth");
	lua_pushnumber(L, info->shield);     lua_setfield(L, -2, "shield");
	lua_pushinteger(L, info->alertness); lua_setfield(L, -2, "alertness");
	if (info->targetchrnum >= 0) {
		lua_pushinteger(L, info->targetchrnum);
		lua_setfield(L, -2, "target_chrnum");
	}
	if (info->targetplayernum >= 0) {
		lua_pushinteger(L, info->targetplayernum);
		lua_setfield(L, -2, "target_playernum");
	}
}
