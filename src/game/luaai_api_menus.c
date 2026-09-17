/**
 * pd.* API, menus group: the Lua Director registry (pd.menu_add and
 * friends), the mid-mission menu drivers (pd.menu_lore, pd.game_over), the
 * fps/mem readout and the entry points of the overlay's Lua section.
 *
 * From the Perfect Dark Kai fork (be46717), where the whole API was one file,
 * src/game/luaai_api.c, and the console commands lived at its end. The
 * Director dialog that renders the registry is in mainmenu.c, as in Kai.
 * Kai's registry and menu drivers are ported as they were; the differences:
 *  - callbacks into Lua (menu actions, checkbox/slider get/set) run under the
 *    AI instruction budget (luaaiPcall), so a looping menu action cannot hang
 *    the menu;
 *  - there is no netplay, so the menu drivers drop Kai's net client test;
 *  - Kai's /fps and /mem console toggles and scripts/perf_overlay.lua become
 *    the overlay's Lua section and luaMenusHudRender;
 *  - Kai's /lua console command becomes luaMenusRunString, budgeted, with the
 *    result or error handed back to the caller instead of the console.
 */

#include <ultra64.h>
#include <stdio.h>
#include <string.h>
#include "constants.h"
#include "types.h"
#include "bss.h"
#include "data.h"
#include "system.h"
#include "video.h"
#include "game/chaosstate.h"
#include "game/endscreen.h"
#include "game/game_1531a0.h"
#include "game/gfxmemory.h"
#include "game/mainmenu.h"
#include "game/menu.h"
#include "game/music.h"
#include "game/player.h"
#include "game/training.h"
#include "lib/rng.h"
#include "lib/vi.h"
#include "luaai_api_internal.h"

/* ------------------------------------------------------------------------- *
 * Director menu registry (pd.menu_add / pd.menu_clear + C accessors)
 * ------------------------------------------------------------------------- */

/* Director menu registry: scripts register pause-menu entries via pd.menu_add,
 * the Lua Director dialog (mainmenu.c) renders them and dispatches selection back
 * to the stored Lua function by index. */
struct luamenuentry {
	char label[LUA_MENU_LABEL];
	char group[LUA_MENU_LABEL]; /* "" = root; else the submenu title it lives under */
	int luaref; /* action fn (kind 0); LUA_NOREF if unused */
	u8 kind;    /* 0 = action/selectable, 1 = checkbox, 2 = slider */
	int getref; /* checkbox/slider getter (kind 1/2); LUA_NOREF if none */
	int setref; /* checkbox/slider setter (kind 1/2); LUA_NOREF if none */
	s32 smin;   /* slider lower bound (kind 2) */
	s32 smax;   /* slider upper bound (kind 2) */
	char desc[LUA_MENU_DESC]; /* scroll-panel description text */
};

static struct luamenuentry g_LuaMenu[LUA_MENU_MAX];
static s32 g_LuaMenuCount = 0;

/* Copy src into a fixed buffer, always terminated. */
static void luaMenuCopy(char *dst, const char *src, u32 size)
{
	strncpy(dst, src, size - 1);
	dst[size - 1] = '\0';
	luaApiTextScrub(dst);
}

static void luaMenuClearAll(lua_State *L)
{
	s32 i;
	for (i = 0; i < g_LuaMenuCount; i++) {
		if (L && g_LuaMenu[i].luaref != LUA_NOREF) {
			luaL_unref(L, LUA_REGISTRYINDEX, g_LuaMenu[i].luaref);
		}
		if (L && g_LuaMenu[i].getref != LUA_NOREF) {
			luaL_unref(L, LUA_REGISTRYINDEX, g_LuaMenu[i].getref);
		}
		if (L && g_LuaMenu[i].setref != LUA_NOREF) {
			luaL_unref(L, LUA_REGISTRYINDEX, g_LuaMenu[i].setref);
		}
		g_LuaMenu[i].luaref = LUA_NOREF;
		g_LuaMenu[i].getref = LUA_NOREF;
		g_LuaMenu[i].setref = LUA_NOREF;
		g_LuaMenu[i].kind = 0;
		g_LuaMenu[i].label[0] = '\0';
		g_LuaMenu[i].group[0] = '\0';
		g_LuaMenu[i].desc[0] = '\0';
	}
	g_LuaMenuCount = 0;
	luaDirectorRebuild(); /* array back to just the terminator */
}

/* Shared head for the registrars below: fill label/group/desc and clear the
 * refs of the next free entry. Returns its index. */
static s32 luaMenuStoreCommon(const char *label, const char *group, const char *desc)
{
	struct luamenuentry *e = &g_LuaMenu[g_LuaMenuCount];
	luaMenuCopy(e->label, label, LUA_MENU_LABEL);
	luaMenuCopy(e->group, group, LUA_MENU_LABEL);
	luaMenuCopy(e->desc, desc, LUA_MENU_DESC);
	e->kind = 0;
	e->luaref = LUA_NOREF;
	e->getref = LUA_NOREF;
	e->setref = LUA_NOREF;
	e->smin = 0;
	e->smax = 0;
	return g_LuaMenuCount;
}

/* Take a registry ref to the value at idx. luaL_ref can raise on OOM; the
 * pd.* callers run protected, and the entry is only counted afterwards, so a
 * failed ref leaves the registry as it was. */
static int luaMenuRef(lua_State *L, int idx)
{
	lua_pushvalue(L, idx);
	return luaL_ref(L, LUA_REGISTRYINDEX);
}

/* pd.menu_add(label, fn, [group], [desc]) -> index (or -1 if the registry is
 * full). Adds a Lua Director pause-menu entry; selecting it later calls fn(). If
 * group is a non-empty string the entry is placed under a submenu of that title
 * (the submenu opener appears at the top of the root list); omit it for a
 * root-level entry. desc, if given, is shown in the scroll panel when the row is
 * focused. */
static int l_pd_menu_add(lua_State *L)
{
	const char *label = luaL_checkstring(L, 1);
	const char *group;
	const char *desc;
	struct luamenuentry *e;

	luaL_checktype(L, 2, LUA_TFUNCTION);
	group = luaL_optstring(L, 3, "");
	desc = luaL_optstring(L, 4, "");

	if (g_LuaMenuCount >= LUA_MENU_MAX) {
		luaApiLog("menu_add: registry full");
		lua_pushinteger(L, -1);
		return 1;
	}

	luaMenuStoreCommon(label, group, desc);
	e = &g_LuaMenu[g_LuaMenuCount];
	e->luaref = luaMenuRef(L, 2);

	lua_pushinteger(L, g_LuaMenuCount);
	g_LuaMenuCount++;
	luaDirectorRebuild(); /* keep the menu items array valid + current */
	return 1;
}

/* pd.menu_add_checkbox(label, getfn, setfn, [group], [desc]) -> index. A native
 * checkbox row: getfn() returns the current bool, setfn(v) stores it. desc is
 * shown in the scroll panel when the row is focused. */
static int l_pd_menu_add_checkbox(lua_State *L)
{
	const char *label = luaL_checkstring(L, 1);
	const char *group, *desc;
	struct luamenuentry *e;

	luaL_checktype(L, 2, LUA_TFUNCTION);
	luaL_checktype(L, 3, LUA_TFUNCTION);
	group = luaL_optstring(L, 4, "");
	desc = luaL_optstring(L, 5, "");

	if (g_LuaMenuCount >= LUA_MENU_MAX) {
		luaApiLog("menu_add_checkbox: registry full");
		lua_pushinteger(L, -1);
		return 1;
	}

	luaMenuStoreCommon(label, group, desc);
	e = &g_LuaMenu[g_LuaMenuCount];
	e->kind = 1;
	e->getref = luaMenuRef(L, 2);
	e->setref = luaMenuRef(L, 3);

	lua_pushinteger(L, g_LuaMenuCount);
	g_LuaMenuCount++;
	luaDirectorRebuild();
	return 1;
}

/* pd.menu_add_slider(label, getfn, setfn, min, max, [group], [desc]) -> index. A
 * native slider row: getfn() returns the current int (clamped min..max), setfn(v)
 * stores it. */
static int l_pd_menu_add_slider(lua_State *L)
{
	const char *label = luaL_checkstring(L, 1);
	const char *group, *desc;
	s32 smin, smax;
	struct luamenuentry *e;

	luaL_checktype(L, 2, LUA_TFUNCTION);
	luaL_checktype(L, 3, LUA_TFUNCTION);
	smin = (s32)luaL_checkinteger(L, 4);
	smax = (s32)luaL_checkinteger(L, 5);
	group = luaL_optstring(L, 6, "");
	desc = luaL_optstring(L, 7, "");

	if (g_LuaMenuCount >= LUA_MENU_MAX) {
		luaApiLog("menu_add_slider: registry full");
		lua_pushinteger(L, -1);
		return 1;
	}

	luaMenuStoreCommon(label, group, desc);
	e = &g_LuaMenu[g_LuaMenuCount];
	e->kind = 2;
	e->smin = smin;
	e->smax = smax;
	e->getref = luaMenuRef(L, 2);
	e->setref = luaMenuRef(L, 3);

	lua_pushinteger(L, g_LuaMenuCount);
	g_LuaMenuCount++;
	luaDirectorRebuild();
	return 1;
}

/* pd.menu_clear(): drop all registered Director entries (e.g. before a script
 * re-registers them on reload). */
static int l_pd_menu_clear(lua_State *L)
{
	luaMenuClearAll(L);
	return 0;
}

/* pd.menu_set_label(index, text): rewrite an existing entry's label in place.
 * The Director menuitem holds a pointer to this buffer, so the on-screen text
 * updates live with no rebuild — lets a menu entry display an adjustable value
 * (chaos frequency / effect duration) that changes when it's selected. */
static int l_pd_menu_set_label(lua_State *L)
{
	lua_Integer i = luaL_checkinteger(L, 1);
	const char *label = luaL_checkstring(L, 2);

	if (i >= 0 && i < g_LuaMenuCount) {
		luaMenuCopy(g_LuaMenu[i].label, label, LUA_MENU_LABEL);
	}

	return 0;
}

/* C accessors used by the Lua Director dialog in mainmenu.c. */
s32 luaMenuCount(void)
{
	return g_LuaMenuCount;
}

const char *luaMenuLabel(s32 i)
{
	if (i < 0 || i >= g_LuaMenuCount) {
		return "";
	}
	return g_LuaMenu[i].label;
}

const char *luaMenuGroup(s32 i)
{
	if (i < 0 || i >= g_LuaMenuCount) {
		return "";
	}
	return g_LuaMenu[i].group;
}

/* Push the function behind ref. Returns 0, with nothing pushed, if the ref
 * does not hold a function. */
static s32 luaMenuPushRef(lua_State *L, int ref)
{
	lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
	if (lua_isfunction(L, -1)) {
		return 1;
	}
	lua_pop(L, 1);
	return 0;
}

void luaMenuInvoke(s32 i)
{
	lua_State *L = luaaiGetState();
	if (!L || i < 0 || i >= g_LuaMenuCount || g_LuaMenu[i].luaref == LUA_NOREF) {
		return;
	}
	if (luaMenuPushRef(L, g_LuaMenu[i].luaref)) {
		if (luaaiPcall(L, 0, 0) != LUA_OK) {
			luaApiLog2("menu item error: ", luaaiErrStr(L, -1));
			lua_pop(L, 1);
		}
	}
}

s32 luaMenuKind(s32 i)
{
	if (i < 0 || i >= g_LuaMenuCount) {
		return 0;
	}
	return (s32)g_LuaMenu[i].kind;
}

const char *luaMenuDesc(s32 i)
{
	if (i < 0 || i >= g_LuaMenuCount) {
		return "";
	}
	return g_LuaMenu[i].desc;
}

s32 luaMenuSliderMin(s32 i)
{
	if (i < 0 || i >= g_LuaMenuCount) {
		return 0;
	}
	return g_LuaMenu[i].smin;
}

s32 luaMenuSliderMax(s32 i)
{
	if (i < 0 || i >= g_LuaMenuCount) {
		return 0;
	}
	return g_LuaMenu[i].smax;
}

/* Call a checkbox/slider getter (ref) and return its result. Shared guarded
 * body; wantint selects boolean vs integer coercion. */
static s32 luaMenuGetValue(s32 i, s32 wantint)
{
	lua_State *L = luaaiGetState();
	s32 r = 0;

	if (!L || i < 0 || i >= g_LuaMenuCount || g_LuaMenu[i].getref == LUA_NOREF) {
		return 0;
	}
	if (luaMenuPushRef(L, g_LuaMenu[i].getref)) {
		if (luaaiPcall(L, 0, 1) == LUA_OK) {
			r = wantint ? (s32)lua_tointeger(L, -1) : (lua_toboolean(L, -1) ? 1 : 0);
			lua_pop(L, 1);
		} else {
			luaApiLog2("menu get error: ", luaaiErrStr(L, -1));
			lua_pop(L, 1);
		}
	}
	return r;
}

static void luaMenuSetValue(s32 i, s32 v, s32 wantint)
{
	lua_State *L = luaaiGetState();

	if (!L || i < 0 || i >= g_LuaMenuCount || g_LuaMenu[i].setref == LUA_NOREF) {
		return;
	}
	if (luaMenuPushRef(L, g_LuaMenu[i].setref)) {
		if (wantint) {
			lua_pushinteger(L, v);
		} else {
			lua_pushboolean(L, v);
		}
		if (luaaiPcall(L, 1, 0) != LUA_OK) {
			luaApiLog2("menu set error: ", luaaiErrStr(L, -1));
			lua_pop(L, 1);
		}
	}
}

s32 luaMenuGetBool(s32 i) { return luaMenuGetValue(i, 0); }
void luaMenuSetBool(s32 i, s32 v) { luaMenuSetValue(i, v, 0); }
s32 luaMenuGetInt(s32 i) { return luaMenuGetValue(i, 1); }
void luaMenuSetInt(s32 i, s32 v) { luaMenuSetValue(i, v, 1); }

/* luaApiResetFrame calls this before luaDirectorRebuild. The Lua state is
 * closing on reset, so the refs go with it; just drop the count + clear the
 * entries (don't luaL_unref against a dead state). */
void luaApiResetMenus(void)
{
	s32 i;
	for (i = 0; i < g_LuaMenuCount; i++) {
		g_LuaMenu[i].luaref = LUA_NOREF;
		g_LuaMenu[i].getref = LUA_NOREF;
		g_LuaMenu[i].setref = LUA_NOREF;
		g_LuaMenu[i].kind = 0;
		g_LuaMenu[i].label[0] = '\0';
		g_LuaMenu[i].group[0] = '\0';
		g_LuaMenu[i].desc[0] = '\0';
	}
	g_LuaMenuCount = 0;
}

/* --------------------------------------------------------------------------
 * Mid-mission menu drivers (pd.menu_lore / pd.game_over). Both use the exact
 * CI-terminal recipe func0f0f85e0(dialog, root): push a root dialog + pause
 * the live stage; the player closes it and func0f0fa6ac -> playerUnpause
 * resumes. This is the same machinery the Start-button pause and the CI hub
 * information terminal use, so it's a proven mid-stage path. Solo/co-op only
 * (never Combat Sim — restart/menu semantics don't apply).
 * ------------------------------------------------------------------------ */
#ifndef PLATFORM_N64
/* true only in a real solo/co-op mission with a live local pawn. Kai also
 * refused a net client; this build has no netplay. */
static bool chaosMenuAllowed(void)
{
	return !g_Vars.normmplayerisrunning
			&& g_Vars.currentplayer != NULL
			&& g_Vars.currentplayer->prop != NULL
			&& g_Vars.stagenum != STAGE_CITRAINING;
}

/* ---- Game Over: the REAL mission-failed screen, indistinguishable from the
 * engine's, but with our two choices (Accept = restart, Decline = resume). It
 * mirrors the genuine two-screen flow:
 *   1) g_ChaosFailedStatsDialog — a clone of g_SoloMissionEndscreenFailedMenuDialog:
 *      MENUDIALOGTYPE_DANGER, the real "<Stage>: Failed" title, and the REAL
 *      stats item list (g_MissionEndscreenMenuItems). Those text functions read
 *      LIVE run stats (mission time, kills, accuracy, shots, difficulty, weapon
 *      of choice) which are valid mid-mission; the cheat-availability lines hide
 *      because we zero endscreen.cheatinfo. We deliberately do NOT call
 *      endscreenResetModels / configure a menumodel — that pool IS the live
 *      viewmodel gun-mem and reusing it mid-mission would corrupt it; a DANGER
 *      dialog draws no 3D model on its own, so skipping it is safe.
 *   2) On any input it pushes g_ChaosRetryDialog — the real retry look
 *      (objectives + Accept/Decline, "Retry: <Stage>" title, the genuine
 *      endscreenHandleRetryMission for Start=accept / Back=resume). Accept runs
 *      the real menuhandlerAcceptMission (restart); Decline resumes the LIVE,
 *      still-paused mission instead of quitting to the menu.
 * ---------------------------------------------------------------------------- */
extern struct menuitem g_MissionEndscreenMenuItems[]; /* the real failed/complete stats list (endscreen.c) */

/* Close both game-over dialogs -> the menu-close path (func0f0fa6ac) unpauses
 * the still-live mission, back to exactly where we were. Two pops = the
 * stats+retry stack depth; the second is a safe no-op if only one is open. */
static MenuItemHandlerResult chaosGameOverResume(s32 operation, struct menuitem *item, union handlerdata *data)
{
	(void)item;
	(void)data;

	if (operation == MENUOP_SET) {
		playerSetFadeColour(0, 0, 0, 0.0f); // clear the game-over black screen
		g_ChaosGameOverStatus = 0;          // stop forcing Unknown/Missing
		menuPopDialog();
		menuPopDialog();
	}
	return 0;
}

/* Retry screen — Accept / Decline. We do NOT reuse the engine's
 * g_RetryMissionMenuItems (its Objectives item) or endscreenHandleRetryMission:
 * that handler delegates to menudialog00103608, whose MENUOP_OPEN calls
 * setupLoadBriefing into the (unconfigured) menumodel buffer and DMAs the
 * briefing file into a garbage address — the mid-mission crash. Our handler
 * loads nothing; Start selects the focused item (STARTSELECTS), Back resumes. */
static struct menuitem g_ChaosRetryItems[] = {
	{ MENUITEMTYPE_SELECTABLE, 0, 0, L_OPTIONS_298 /* Accept */,  0, menuhandlerAcceptMission },
	{ MENUITEMTYPE_SELECTABLE, 0, 0, L_OPTIONS_299 /* Decline */, 0, chaosGameOverResume },
	{ MENUITEMTYPE_END, 0, 0, 0, 0, NULL },
};

static MenuDialogHandlerResult chaosRetryHandle(s32 operation, struct menudialogdef *dialogdef, union handlerdata *data)
{
	if (operation == MENUOP_TICK
			&& g_Menus[g_MpPlayerNum].curdialog
			&& g_Menus[g_MpPlayerNum].curdialog->definition == dialogdef) {
		struct menuinputs *inputs = data->dialog2.inputs;
		if (inputs->back) {
			inputs->back = false;
			chaosGameOverResume(MENUOP_SET, NULL, NULL); // clear fade + pop2 -> resume
		}
	}
	return 0;
}

static struct menudialogdef g_ChaosRetryDialog = {
	MENUDIALOGTYPE_DANGER,
	(uintptr_t)&endscreenMenuTitleRetryMission,   /* real "Retry: <Stage>" */
	g_ChaosRetryItems,
	chaosRetryHandle,
	MENUDIALOGFLAG_STARTSELECTS | MENUDIALOGFLAG_DISABLEITEMSCROLL,
	NULL,
};

/* Failed stats screen handler: on any input, advance to the retry screen —
 * the same transition the real endscreenHandle2PFailed makes. */
static MenuDialogHandlerResult chaosFailedStatsHandle(s32 operation, struct menudialogdef *dialogdef, union handlerdata *data)
{
	if (operation == MENUOP_TICK
			&& g_Menus[g_MpPlayerNum].curdialog
			&& g_Menus[g_MpPlayerNum].curdialog->definition == dialogdef) {
		struct menuinputs *inputs = data->dialog2.inputs;
		if (inputs->select || inputs->back || inputs->start) {
			inputs->select = inputs->back = inputs->start = false;
			menuPushDialog(&g_ChaosRetryDialog);
		}
	}
	return 0;
}

static struct menudialogdef g_ChaosFailedStatsDialog = {
	MENUDIALOGTYPE_DANGER,
	(uintptr_t)&endscreenMenuTitleStageFailed,    /* real "<Stage>: Failed" */
	g_MissionEndscreenMenuItems,                  /* real live-stat lines */
	chaosFailedStatsHandle,
	MENUDIALOGFLAG_DISABLEITEMSCROLL | MENUDIALOGFLAG_SMOOTHSCROLLABLE,
	NULL,
};
#endif

/* pd.menu_lore() -> bool. Open ONE random unlocked CI bio (character profile
 * or misc file) over the paused mission — the real hub-terminal reader
 * pushed directly as the menu root, so closing it resumes the mission. */
static int l_pd_menu_lore(lua_State *L)
{
#ifndef PLATFORM_N64
	extern struct menudialogdef g_BioProfileMenuDialog;
	extern struct menudialogdef g_BioTextMenuDialog;
	s32 nchr = ciGetNumUnlockedChrBios();
	s32 nmisc = ciGetNumUnlockedMiscBios();

	if (chaosMenuAllowed() && g_Menus[g_MpPlayerNum].curdialog == NULL && nchr + nmisc > 0) {
		// g_ChrBioSlot is the selector the profile/text dialogs read — the
		// same global the Information list menu sets on selection.
		g_ChrBioSlot = (u8)(rngRandom() % (u32)(nchr + nmisc));
		func0f0f85e0(g_ChrBioSlot < nchr ? &g_BioProfileMenuDialog : &g_BioTextMenuDialog,
				MENUROOT_TRAINING);
		lua_pushboolean(L, 1);
		return 1;
	}
#endif
	lua_pushboolean(L, 0);
	return 1;
}

/* pd.game_over() -> bool. Show the real mission-failed screen; Accept
 * restarts, Decline resumes where you were. */
static int l_pd_game_over(lua_State *L)
{
#ifndef PLATFORM_N64
	if (chaosMenuAllowed() && g_Menus[g_MpPlayerNum].curdialog == NULL) {
		// Minimal endscreen-state prep, matching endscreenPrepare's non-model
		// bits: zero cheatinfo (hides the "New Cheat Available" lines),
		// point stageindex at the current stage (title + difficulty lines),
		// player 0. NO endscreenResetModels / menumodel — see the dialog note.
		g_Menus[g_MpPlayerNum].endscreen.cheatinfo = 0;
		g_Menus[g_MpPlayerNum].endscreen.isfirstcompletion = false;
		g_Menus[g_MpPlayerNum].endscreen.stageindex = g_MissionConfig.stageindex;
		g_Menus[g_MpPlayerNum].playernum = 0;
		playerSetFadeColour(0, 0, 0, 1.0f);              // black out the world behind the screen
		musicStartTrackAsMenu(MUSIC_MISSION_FAILED);      // the mission-failed jingle
		g_ChaosGameOverStatus = 1;                        // Mission: Unknown / Agent: Missing
		func0f0f85e0(&g_ChaosFailedStatsDialog, MENUROOT_MAINMENU);
		lua_pushboolean(L, 1);
		return 1;
	}
#endif
	lua_pushboolean(L, 0);
	return 1;
}

/* ------------------------------------------------------------------------- *
 * fps / mem readout (Kai's scripts/perf_overlay.lua, drawn from C)
 *
 * Kai drew these from Lua, toggled by its /fps and /mem console commands.
 * This build toggles g_LuaShowFps / g_LuaShowMem from the overlay's Lua
 * section and draws them here; luaHudRender calls it. pd.perf() still
 * reports both flags, so a script can draw its own instead.
 * ------------------------------------------------------------------------- */

#define LUA_PERF_X     230 /* right column, clear of the X-ray (left column) */
#define LUA_PERF_Y_FPS 80
#define LUA_PERF_Y_MEM 88

#define LUA_PERF_WHITE 0xffffffff
#define LUA_PERF_GREEN 0x40ff40ff
#define LUA_PERF_YELL  0xffe040ff
#define LUA_PERF_RED   0xff5050ff

Gfx *luaMenusHudRender(Gfx *gdl)
{
#ifndef PLATFORM_N64
	char buf[64];
	s32 x, y;

	if (!g_LuaShowFps && !g_LuaShowMem) {
		return gdl;
	}
	if (!g_FontHandelGothicXs || !g_CharsHandelGothicXs) {
		return gdl;
	}

	gdl = text0f153628(gdl);

	if (g_LuaShowFps) {
		f32 fps = videoGetAverageFPS();
		f32 ms = fps > 0.0f ? 1000.0f / fps : 0.0f;
		// colour by frame time: green < 17ms (60fps), yellow < 33ms, red otherwise
		u32 c = ms < 17.0f ? LUA_PERF_GREEN : ms < 33.0f ? LUA_PERF_YELL : LUA_PERF_RED;

		snprintf(buf, sizeof(buf), "%.0ffps %.1fms", fps, ms);
		x = LUA_PERF_X;
		y = LUA_PERF_Y_FPS;
		gdl = textRenderProjected(gdl, &x, &y, buf, g_CharsHandelGothicXs, g_FontHandelGothicXs,
				(s32)c, viGetWidth(), viGetHeight(), 0, 0);
	}

	if (g_LuaShowMem) {
		u32 total = gfxGetVtxPoolSize();
		u32 freev = gfxGetFreeVtx();
		u32 used = freev <= total ? total - freev : total;
		f32 pct = total > 0 ? 100.0f * (f32)used / (f32)total : 0.0f;
		u32 c = pct < 75.0f ? LUA_PERF_WHITE : pct < 90.0f ? LUA_PERF_YELL : LUA_PERF_RED;

		snprintf(buf, sizeof(buf), "vtx %.0f/%.0fK", (f32)used / 1024.0f, (f32)total / 1024.0f);
		x = LUA_PERF_X;
		y = LUA_PERF_Y_MEM;
		gdl = textRenderProjected(gdl, &x, &y, buf, g_CharsHandelGothicXs, g_FontHandelGothicXs,
				(s32)c, viGetWidth(), viGetHeight(), 0, 0);
	}

	gdl = text0f153780(gdl);
#endif
	return gdl;
}

/* ------------------------------------------------------------------------- *
 * Reload and run-a-string (Kai's /lua console command)
 *
 * Kai ran these from its in-game console. Here the overlay's Lua section
 * calls them, after it has drawn, which is between two game frames: no Lua
 * call and no AI tick is on the stack.
 * ------------------------------------------------------------------------- */

/* Reload scripts now: close the state and build a new one, which re-runs
 * scripts/init.lua. Effects the old scripts left on stay on, as in Kai; the
 * next stage load clears them. */
void luaaiReload(void)
{
	if (!g_LuaAiEnabled) {
		luaApiLog("reload: lua is off");
		return;
	}
	luaaiReset();
	luaaiEnsureState();
	luaApiLog("reloaded scripts/init.lua");
}

/* Protected: [source] -> result text. Tries the source as an expression
 * first ("return <src>") so "pd.stage()" shows its value, then as a
 * statement block. Runs the chunk and joins its results with spaces. */
static int luaMenusRunStringP(lua_State *L)
{
	size_t len;
	const char *src = lua_tolstring(L, 1, &len);
	luaL_Buffer b;
	int base, n, i;

	lua_pushfstring(L, "return %s", src);
	if (luaL_loadbuffer(L, lua_tostring(L, -1), lua_rawlen(L, -1), "=overlay") != LUA_OK) {
		lua_pop(L, 2); /* the error, the prefixed source */
		if (luaL_loadbuffer(L, src, len, "=overlay") != LUA_OK) {
			return lua_error(L);
		}
	} else {
		lua_remove(L, -2); /* the prefixed source */
	}

	base = lua_gettop(L) - 1;
	lua_call(L, 0, LUA_MULTRET);
	n = lua_gettop(L) - base;

	luaL_buffinit(L, &b);
	if (n == 0) {
		luaL_addstring(&b, "ok");
	}
	for (i = 1; i <= n; i++) {
		if (i > 1) {
			luaL_addchar(&b, ' ');
		}
		luaL_tolstring(L, base + i, NULL);
		luaL_addvalue(&b);
	}
	luaL_pushresult(&b);
	return 1;
}

/* Run src in the live state under the instruction budget. Writes the result
 * (or the error) to out and returns 1 on success, 0 on error. Also logged. */
s32 luaMenusRunString(const char *src, char *out, u32 outlen)
{
	lua_State *L;
	s32 ok;
	const char *msg;

	if (out && outlen) {
		out[0] = '\0';
	}

	if (!src || !*src) {
		return 0;
	}

	if (!g_LuaAiEnabled) {
		msg = "lua is off";
		ok = 0;
	} else if (!luaaiEnsureState() || (L = luaaiGetState()) == NULL) {
		msg = "no lua state";
		ok = 0;
	} else {
		lua_pushcfunction(L, luaMenusRunStringP);
		lua_pushstring(L, src);
		ok = luaaiPcall(L, 1, 1) == LUA_OK;
		msg = ok ? lua_tostring(L, -1) : luaaiErrStr(L, -1);
		if (!msg) {
			msg = "?";
		}
		if (out && outlen) {
			snprintf(out, outlen, "%s", msg);
		}
		luaApiLog2(ok ? "run: " : "run error: ", msg);
		lua_pop(L, 1);
		return ok;
	}

	if (out && outlen) {
		snprintf(out, outlen, "%s", msg);
	}
	luaApiLog2("run error: ", msg);
	return ok;
}

/* Run a Lua string now; logs the result or error. */
void luaaiDoString(const char *expr)
{
	if (!expr || !*expr) {
		luaApiLog("usage: reload | <lua>");
		return;
	}
	luaMenusRunString(expr, NULL, 0);
}

/* Kai's "/lua reload" and "/lua <expr>". */
void luaaiConsoleCommand(const char *args)
{
	if (args && strncmp(args, "reload", 6) == 0) {
		luaaiReload();
	} else {
		luaaiDoString(args);
	}
}

/* ------------------------------------------------------------------------- *
 * Registration
 * ------------------------------------------------------------------------- */

static const luaL_Reg g_LuaApiMenusFuncs[] = {
	{ "menu_add",          l_pd_menu_add },
	{ "menu_add_checkbox", l_pd_menu_add_checkbox },
	{ "menu_add_slider",   l_pd_menu_add_slider },
	{ "menu_clear",        l_pd_menu_clear },
	{ "menu_set_label",    l_pd_menu_set_label },
	{ "menu_lore",         l_pd_menu_lore },
	{ "game_over",         l_pd_game_over },
	{ NULL, NULL },
};

void luaApiRegisterMenus(lua_State *L)
{
	luaL_setfuncs(L, g_LuaApiMenusFuncs, 0);
}
