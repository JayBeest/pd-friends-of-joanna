#ifndef _IN_GAME_LUAAI_API_INTERNAL_H
#define _IN_GAME_LUAAI_API_INTERNAL_H

/*
 * Shared by the pd.* API files (luaai_api.c, luaai_api_<group>.c) and the
 * chraiLua* bridge files (luaai_bridge_<group>.c). Not a public header: the
 * engine talks to the Lua layer through game/luaai.h.
 *
 * The API is Kai's (Perfect Dark Kai fork, be46717), where it was one file,
 * luaai_api.c, and the bridges lived in chraction.c. Here it is split by
 * group; every file registers its own functions into the pd table from
 * luaApiRegister.
 */

#include <ultra64.h>
#include "types.h"
#include "bss.h"
#include "game/luaai.h"

#include "lua.h"
#include "lauxlib.h"

/* ------------------------------------------------------------------------- *
 * Group registrars. Each is called by luaApiRegister with the pd table on the
 * stack top and must leave the stack as it found it.
 * ------------------------------------------------------------------------- */

void luaApiRegisterPlayer(lua_State *L);
void luaApiRegisterWeapons(lua_State *L);
void luaApiRegisterChrs(lua_State *L);
void luaApiRegisterWorld(lua_State *L);
void luaApiRegisterFx(lua_State *L);
void luaApiRegisterMenus(lua_State *L);

/* ------------------------------------------------------------------------- *
 * Hooks the core calls into group files.
 * ------------------------------------------------------------------------- */

/* luaApiResetFrame, before luaDirectorRebuild: drop per-state C data the
 * group owns (fx: pd.load_image buffers; menus: the Director registry). The
 * Lua state is closing, so do not luaL_unref against it. */
void luaApiResetFx(void);
void luaApiResetMenus(void);

/* luaHudRender, inside the text bracket: draw one OVL_IMAGE overlay
 * (pd.draw_image). handle is the pd.load_image handle; (cx, cy) is the
 * centre; angle is in radians. */
Gfx *luaApiDrawImage(Gfx *gdl, s32 handle, s32 cx, s32 cy, s32 w, s32 h, f32 angle, u32 color);

/* luaHudRender: the text an OVL_TEXT overlay is drawn with. Kai passes it
 * through langChaosTransform so the text gags cover overlays too. */
const char *luaApiOverlayText(const char *text);

/* ------------------------------------------------------------------------- *
 * Logging. Both go to sysLogPrintf (pd.log and the terminal); Kai also wrote
 * them to its in-game console.
 * ------------------------------------------------------------------------- */

void luaApiLog(const char *s);
void luaApiLog2(const char *prefix, const char *s);

/* Log "pd.<name> is unavailable in this build" once per process. */
void luaApiLogUnavailable(const char *name);

/* ------------------------------------------------------------------------- *
 * Calling back into Lua (luaai.c)
 * ------------------------------------------------------------------------- */

/* lua_pcall with the AI instruction budget armed, for callbacks a pd.*
 * function or an event makes (pd.all_chrs(fn), menu actions, ...). Inside an
 * armed call it shares that budget, so a looping callback ends in an error
 * instead of a hung frame. */
s32 luaaiPcall(lua_State *L, s32 nargs, s32 nresults);

/* The error value at idx as text, without lua_tostring's number conversion
 * (which allocates). */
const char *luaaiErrStr(lua_State *L, s32 idx);

/* ------------------------------------------------------------------------- *
 * 2D overlay list (drawn by luaHudRender, aged by luaTick)
 * ------------------------------------------------------------------------- */

enum { OVL_BOX, OVL_TEXT, OVL_SPRITE, OVL_IMAGE };

/* Queue an overlay. secs <= 0 draws it for one frame. texnum is the
 * g_TexWallhitConfigs index for OVL_SPRITE and the image handle for
 * OVL_IMAGE; angle (radians) is OVL_IMAGE only, where (x, y) is the centre.
 * Drops the overlay silently when the list is full. */
void luaOverlayAdd(s32 kind, s32 x, s32 y, s32 w, s32 h, u32 color,
		const char *text, s32 texnum, f32 angle, f32 secs);

/* ------------------------------------------------------------------------- *
 * Bridge helpers
 * ------------------------------------------------------------------------- */

/* Resolve the local player's chr, or NULL if there isn't one right now
 * (title, menus, cutscene without a pawn). Kai's apLuaPlayerChr, minus the
 * net client test: this build has no netplay. */
static inline struct chrdata *apLuaPlayerChr(void)
{
	if (g_Vars.currentplayer == NULL || g_Vars.currentplayer->prop == NULL) {
		return NULL;
	}
	return g_Vars.currentplayer->prop->chr;
}

#endif
