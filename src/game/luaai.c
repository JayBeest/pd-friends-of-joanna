/**
 * Lua scripting layer for the action block (ailist) system.
 *
 * See game/luaai.h for the high level description. In short: each ailist is
 * transpiled (luaai_transpile.c) into a Lua chunk that drives execution by
 * calling back into the original C command handlers via the ctx:exec bridge.
 * This routes all action block execution through Lua, enabling external Lua
 * scripts to override lists (pd.register_ailist) and to invoke engine commands
 * directly (ctx:run), which is the foundation for scripted missions, custom
 * multiplayer maps and networking helpers.
 *
 * Safety: if Lua fails to initialise, execution falls back to the original
 * bytecode interpreter for everything. If one list fails (transpile, load or
 * run error), only that list is quarantined: it runs as bytecode until the
 * next stage, and every other list keeps running through Lua.
 *
 * Taken from the Perfect Dark Kai fork (be46717). Differences: messages go
 * through sysLogPrintf (pd.log + stdout/stderr) because this tree has no
 * in-game console, g_LuaAiEnabled defaults to 0, and a list error quarantines
 * that list instead of clearing g_LuaAiEnabled.
 */

#include <ultra64.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "game/luaai.h"
#include "types.h"
#include "system.h"

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

/* Off by default: nothing routes an ailist through Lua unless this is set.
 * Kai defaults it to 1. The layer clears it only when no Lua state can be
 * created, or when the quarantine table overflows. */
s32 g_LuaAiEnabled = 0;

/* Status codes returned by luaai_run_list(). */
#define LUAAI_TERMINAL 0
#define LUAAI_YIELD    1
#define LUAAI_SWITCH   2
#define LUAAI_ERR      (-1)

/* The opcode that ends an ailist (see commands.h: endlist). */
#ifndef CMD_END
#define CMD_END 0x0004
#endif

static lua_State *g_LuaState = NULL;
static s32 g_LuaCurStage = -0x7fffffff;
static s32 g_LuaInitFailed = 0;
/* Number of registered ailist overrides. When zero, the per-list override
 * lookup (and its ailist id scan) is skipped entirely on the hot path. */
static s32 g_LuaOverrideCount = 0;

/* Registry keys for our internal tables. */
static const char *const KEY_CHUNKS = "luaai.chunks";       /* lightuserdata(list) -> function */
static const char *const KEY_OVERRIDES = "luaai.overrides"; /* id (int) -> function */
static const char *const KEY_CTX = "luaai.ctx";             /* the shared ctx table */

/* Last error message from luaai_get_chunk / luaai_run_list, copied out of the
 * Lua stack so the caller can log it once after the stack has been cleaned. */
static char g_LuaErrMsg[256];
static const char *g_LuaErrWhat = "";

/* ------------------------------------------------------------------------- *
 * List quarantine
 *
 * A list whose chunk fails to build or raises an error is recorded here and
 * runs through chraiRunLoop from then on. Keyed by list pointer: that is what
 * luaaiExecute has on the hot path, with no id scan, and within one stage a
 * pointer names exactly one list. The table is cleared by luaaiReset, because
 * list pointers are reused across stages and the Lua state (with every
 * script) is rebuilt there anyway, so a stale entry would quarantine an
 * unrelated list and a fixed script deserves a fresh start.
 *
 * Phase 1 (per-mod _ENV) replaces the policy in luaai_quarantine and
 * luaai_is_quarantined with "disable the owning mod"; callers stay as they are.
 * ------------------------------------------------------------------------- */

#define LUAAI_QUARANTINE_MAX 64

struct luaaiquarantine {
	void *list;
	s32 id;
};

static struct luaaiquarantine g_LuaQuarantine[LUAAI_QUARANTINE_MAX];
static s32 g_LuaQuarantineCount = 0;

/* ------------------------------------------------------------------------- *
 * Instruction budget
 *
 * luaaiExecute arms one budget for the whole entity call, shared by every
 * list it runs and every switch between them, so a script cannot reset it by
 * returning and being called again. Calls into Lua made outside an entity
 * call (init.lua) go through luaai_pcall_budget, which arms its own. Once
 * the budget is spent the count hook raises a Lua error, so a runaway loop
 * lands in the quarantine path instead of hanging the frame.
 *
 * Sizing: a transpiled list costs about 2 instructions per dispatch entry it
 * walks past plus about 10 per command, so the largest list in the game
 * (~1400 commands) costs under 3000 instructions per command, and the budget
 * still allows several thousand commands in one call. Lists yield long before
 * that; one that does not is looping, and the bytecode loop's 100k-iteration
 * cap would have stopped it anyway. 10M instructions is tens of milliseconds
 * on current hardware, a one-off hitch before the list is quarantined.
 *
 * init.lua runs once per stage, on the stage's first AI tick, and may do real
 * setup work, so it gets twice that. It is not larger because it runs inside
 * a frame too.
 * ------------------------------------------------------------------------- */

#define LUAAI_INSTRUCTION_BUDGET      10000000
#define LUAAI_INIT_INSTRUCTION_BUDGET 20000000

/* The hook fires every this many instructions; the budget is exact to within
 * this. */
#define LUAAI_HOOK_INTERVAL 1000

/* Log lines per stage for lists that switch without end. */
#define LUAAI_SWITCH_WARNINGS_MAX 8

static s32 g_LuaSwitchWarnings = 0;

static s32 g_LuaBudgetArmed = 0;
static s32 g_LuaBudget = 0;
static s32 g_LuaBudgetLeft = 0;

extern u32 chraiGetCommandLength(u8 *ailist, u32 aioffset);
extern u32 chraiGetAilistLength(u8 *list);

static void luaai_set_error(const char *what, const char *msg)
{
	g_LuaErrWhat = what;
	snprintf(g_LuaErrMsg, sizeof(g_LuaErrMsg), "%s", msg ? msg : "(no message)");
}

static s32 luaai_is_quarantined(void *list)
{
	s32 i;

	for (i = 0; i < g_LuaQuarantineCount; i++) {
		if (g_LuaQuarantine[i].list == list) {
			return 1;
		}
	}

	return 0;
}

/* Coroutines copy the hook of the thread that created them and keep it, so
 * this can also run outside an armed call, or on a thread left firing every
 * instruction by an earlier exhausted call. Both cases are put back to the
 * normal interval and otherwise ignored. */
static void luaai_budget_hook(lua_State *L, lua_Debug *ar)
{
	s32 count = lua_gethookcount(L);

	if (g_LuaBudgetArmed && g_LuaBudgetLeft > 0) {
		g_LuaBudgetLeft -= count;
	}

	if (!g_LuaBudgetArmed || g_LuaBudgetLeft > 0) {
		if (count != LUAAI_HOOK_INTERVAL) {
			lua_sethook(L, luaai_budget_hook, LUA_MASKCOUNT, LUAAI_HOOK_INTERVAL);
		}
		return;
	}

	/* Exhausted. Fire on every instruction from now on, so a script that
	 * catches the error with pcall is stopped again by its next instruction
	 * and the error reaches luaai_pcall_budget. */
	if (count != 1) {
		lua_sethook(L, luaai_budget_hook, LUA_MASKCOUNT, 1);
	}

	if (lua_getinfo(L, "Sl", ar) && ar->currentline > 0) {
		luaL_error(L, "%s:%d: instruction budget exceeded (%d)",
				ar->short_src, ar->currentline, (int)g_LuaBudget);
	}

	luaL_error(L, "instruction budget exceeded (%d)", (int)g_LuaBudget);
}

static void luaai_budget_arm(lua_State *L, s32 budget)
{
	g_LuaBudget = budget;
	g_LuaBudgetLeft = budget;
	g_LuaBudgetArmed = 1;
	lua_sethook(L, luaai_budget_hook, LUA_MASKCOUNT, LUAAI_HOOK_INTERVAL);
}

static void luaai_budget_disarm(lua_State *L)
{
	g_LuaBudgetArmed = 0;
	lua_sethook(L, NULL, 0, 0);
}

/* lua_pcall with the instruction budget armed. Inside an armed call (an
 * entity call, or Lua reached again from a command handler) it shares that
 * budget. */
static int luaai_pcall_budget(lua_State *L, int nargs, int nresults, s32 budget)
{
	int status;

	if (g_LuaBudgetArmed) {
		return lua_pcall(L, nargs, nresults, 0);
	}

	luaai_budget_arm(L, budget);
	status = lua_pcall(L, nargs, nresults, 0);
	luaai_budget_disarm(L);

	return status;
}

/* The single policy point for a failed list. Logs once per list per stage. */
static void luaai_quarantine(void *list)
{
	s32 id = chraiLuaGetListId(list);

	if (g_LuaQuarantineCount >= LUAAI_QUARANTINE_MAX) {
		sysLogPrintf(LOG_ERROR, "luaai: %s in list %d: %s", g_LuaErrWhat, id, g_LuaErrMsg);
		sysLogPrintf(LOG_ERROR, "luaai: %d lists quarantined this stage; disabling Lua AI",
				LUAAI_QUARANTINE_MAX);
		g_LuaAiEnabled = 0;
		return;
	}

	g_LuaQuarantine[g_LuaQuarantineCount].list = list;
	g_LuaQuarantine[g_LuaQuarantineCount].id = id;
	g_LuaQuarantineCount++;

	sysLogPrintf(LOG_ERROR, "luaai: %s in list %d (%p): %s", g_LuaErrWhat, id, list, g_LuaErrMsg);
	sysLogPrintf(LOG_WARNING, "luaai: list %d runs as bytecode until the next stage", id);
}

/* ------------------------------------------------------------------------- *
 * ctx bridge (the C side of the Lua "ctx" object)
 * ------------------------------------------------------------------------- */

/* ctx:cur() -> current program counter */
static int l_ctx_cur(lua_State *L)
{
	lua_pushinteger(L, (lua_Integer)chraiLuaGetOffset());
	return 1;
}

/* ctx:exec(off) -> 0 continue, 1 yield, 2 list changed/terminated */
static int l_ctx_exec(lua_State *L)
{
	lua_Integer arg = luaL_checkinteger(L, 2);
	void *before;
	s32 brk;
	void *after;

	/* Checked before the cast: -1 would become 0xffffffff. chraiLuaStep
	 * bounds what is left against the list. */
	luaL_argcheck(L, arg >= 0 && arg <= 0xffffffffLL, 2, "offset out of range");

	before = chraiLuaGetList();
	brk = chraiLuaStep((u32)arg);
	after = chraiLuaGetList();

	if (after != before || after == NULL) {
		lua_pushinteger(L, LUAAI_SWITCH);
		return 1;
	}

	lua_pushinteger(L, brk ? LUAAI_YIELD : LUAAI_TERMINAL);
	return 1;
}

/* ctx:self() -> table describing the chr currently running this ailist, or nil
 * if there is no current chr (e.g. an object-driven list). The table is a
 * read-only snapshot for this call; re-call each frame for fresh values. */
static int l_ctx_self(lua_State *L)
{
	struct luaaiselfinfo info;

	if (!chraiLuaGetSelf(&info) || !info.valid) {
		lua_pushnil(L);
		return 1;
	}

	luaApiPushChrInfo(L, &info); /* shared shape with pd.chr_info */
	return 1;
}

/* ctx:run(opcode, b0, b1, ...) -> 0 continue, 1 yield, 2 list changed
 * Invoke an arbitrary engine command from Lua with explicit operand bytes.
 * Same codes as ctx:exec: a command that switches the entity's list (set_ailist
 * on self, return) reports 2, and the chunk must return it so luaaiExecute
 * picks up the new list. */
static int l_ctx_run(lua_State *L)
{
	lua_Integer arg = luaL_checkinteger(L, 2);
	u32 opcode;
	void *before;
	void *after;
	s32 brk;
	u8 operands[60];
	int top = lua_gettop(L);
	int i;
	u32 n = 0;

	/* Opcodes are 16 bits. Anything else would be masked into a real one. */
	luaL_argcheck(L, arg >= 0 && arg <= 0xffff, 2, "opcode out of range");
	opcode = (u32)arg;

	for (i = 3; i <= top && n < (u32)sizeof(operands); i++) {
		operands[n++] = (u8)(luaL_checkinteger(L, i) & 0xff);
	}

	before = chraiLuaGetList();
	brk = chraiLuaRunSynthetic(opcode, operands, n);
	after = chraiLuaGetList();

	if (after != before || after == NULL) {
		lua_pushinteger(L, LUAAI_SWITCH);
		return 1;
	}

	lua_pushinteger(L, brk ? LUAAI_YIELD : LUAAI_TERMINAL);
	return 1;
}

/* pd.register_ailist(id, fn): register a Lua override for an ailist id. */
static int l_pd_register_ailist(lua_State *L)
{
	lua_Integer id = luaL_checkinteger(L, 1);
	luaL_checktype(L, 2, LUA_TFUNCTION);

	lua_getfield(L, LUA_REGISTRYINDEX, KEY_OVERRIDES);
	lua_pushinteger(L, id);
	lua_pushvalue(L, 2);
	lua_settable(L, -3);
	lua_pop(L, 1);

	g_LuaOverrideCount++;
	return 0;
}

/* pd.log(msg) */
static int l_pd_log(lua_State *L)
{
	const char *s = luaL_optstring(L, 1, "");
	sysLogPrintf(LOG_NOTE, "luaai: %s", s);
	return 0;
}

/* ------------------------------------------------------------------------- *
 * State setup
 * ------------------------------------------------------------------------- */

static unsigned int luaai_cmdlen(const unsigned char *list, unsigned int off)
{
	return (unsigned int)chraiGetCommandLength((u8 *)list, off);
}

static void luaai_load_external_scripts(lua_State *L)
{
	/* Best-effort: if a scripts/init.lua exists in the working dir, run it. It
	 * may require/dofile additional files and call pd.register_ailist. Errors
	 * are logged and ignored so a broken mod cannot crash the game. */
	FILE *f = fopen("scripts/init.lua", "rb");
	if (!f) {
		return;
	}
	fclose(f);

	if (luaL_loadfilex(L, "scripts/init.lua", "t") != LUA_OK
			|| luaai_pcall_budget(L, 0, 0, LUAAI_INIT_INSTRUCTION_BUDGET) != LUA_OK) {
		sysLogPrintf(LOG_ERROR, "luaai: error loading scripts/init.lua: %s",
				lua_tostring(L, -1));
		lua_pop(L, 1);
	} else {
		sysLogPrintf(LOG_NOTE, "luaai: loaded scripts/init.lua");
	}
}

static void luaai_build_ctx(lua_State *L)
{
	/* ctx = { cur=..., exec=..., run=... } stored in registry. */
	lua_newtable(L);

	lua_pushcfunction(L, l_ctx_cur);
	lua_setfield(L, -2, "cur");
	lua_pushcfunction(L, l_ctx_exec);
	lua_setfield(L, -2, "exec");
	lua_pushcfunction(L, l_ctx_run);
	lua_setfield(L, -2, "run");
	lua_pushcfunction(L, l_ctx_self);
	lua_setfield(L, -2, "self");

	lua_setfield(L, LUA_REGISTRYINDEX, KEY_CTX);
}

static void luaai_build_pd(lua_State *L)
{
	lua_newtable(L); /* pd */

	lua_pushcfunction(L, l_pd_register_ailist);
	lua_setfield(L, -2, "register_ailist");
	lua_pushcfunction(L, l_pd_log);
	lua_setfield(L, -2, "log");

	luaApiRegister(L); /* adds pd.on / draw_box / draw_text / each_chr */

	lua_setglobal(L, "pd");
}

// Sandboxed standard-library set. We deliberately do NOT call luaL_openlibs():
// that opens os (os.execute / os.remove), io (file write / io.popen), package
// (package.loadlib -> dlopen native code) and debug (sandbox-escape
// introspection), which would let any scripts/*.lua on disk — or the /lua
// console — run arbitrary native code with the game's privileges. We open only
// the compute/data libraries, then nil out anything that can shell out or load
// native code. dofile/loadfile/load are kept so the modding system can still
// chain scripts; with os/io/package gone they can only run further sandboxed
// Lua, not escape. (Kai's netplay code review, CR-7.) They are wrapped to load
// source text only, and setmetatable to refuse finalizers; see above.
/* setmetatable, minus finalizers. Lua runs __gc with debug hooks off
 * (lgc.c, GCTM), so a finalizer escapes the instruction budget: an object
 * whose __gc loops, or resurrects itself, stalls the game with nothing to stop
 * it. A __gc field is fixed when setmetatable runs: luaC_checkfinalizer marks
 * the object only if the metatable holds a non-nil __gc at that moment, and a
 * __gc added to the metatable later is never looked at for that object. Any
 * non-nil value is refused, false included, because false already marks the
 * object and a function stored over it later would then run.
 *
 * __close is left alone: it runs through an ordinary call with hooks on, and
 * once the budget is spent the hook fires on every instruction, so a looping
 * __close is stopped like any other code. */
static int l_safe_setmetatable(lua_State *L)
{
	if (lua_type(L, 2) == LUA_TTABLE) {
		lua_pushliteral(L, "__gc");
		if (lua_rawget(L, 2) != LUA_TNIL) {
			return luaL_error(L, "setmetatable: __gc finalizers are not allowed in AI scripts");
		}
		lua_pop(L, 1);
	}

	lua_pushvalue(L, lua_upvalueindex(1));
	lua_insert(L, 1);
	lua_call(L, lua_gettop(L) - 1, 1);
	return 1;
}

/* The loaders, text only. A binary chunk is bytecode the undump code trusts
 * without verifying, and hand-made bytecode can corrupt the VM, so nothing
 * here accepts one: the mode argument is forced to "t" whatever the script
 * passed, and string.dump, the only producer, is gone. */

/* load(chunk [, chunkname [, mode [, env]]]). env keeps its absent-or-nil
 * distinction, which load cares about. */
static int l_safe_load(lua_State *L)
{
	if (lua_gettop(L) < 3) {
		lua_settop(L, 3);
	}

	lua_pushliteral(L, "t");
	lua_replace(L, 3);

	lua_pushvalue(L, lua_upvalueindex(1));
	lua_insert(L, 1);
	lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
	return lua_gettop(L);
}

/* loadfile([filename [, mode [, env]]]) */
static int l_safe_loadfile(lua_State *L)
{
	if (lua_gettop(L) < 2) {
		lua_settop(L, 2);
	}

	lua_pushliteral(L, "t");
	lua_replace(L, 2);

	lua_pushvalue(L, lua_upvalueindex(1));
	lua_insert(L, 1);
	lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
	return lua_gettop(L);
}

/* dofile([filename]), as lbaselib.c's but with a text-only load. */
static int l_safe_dofile(lua_State *L)
{
	const char *fname = luaL_optstring(L, 1, NULL);

	lua_settop(L, 1);

	if (luaL_loadfilex(L, fname, "t") != LUA_OK) {
		return lua_error(L);
	}

	lua_call(L, 0, LUA_MULTRET);
	return lua_gettop(L) - 1;
}

static void luaai_wrap_global(lua_State *L, const char *name, lua_CFunction fn)
{
	lua_getglobal(L, name);
	lua_pushcclosure(L, fn, 1);
	lua_setglobal(L, name);
}

static void luaai_open_safe_libs(lua_State *L)
{
	static const luaL_Reg libs[] = {
		{ LUA_GNAME,       luaopen_base },
		{ LUA_TABLIBNAME,  luaopen_table },
		{ LUA_STRLIBNAME,  luaopen_string },
		{ LUA_MATHLIBNAME, luaopen_math },
		{ LUA_COLIBNAME,   luaopen_coroutine },
		{ LUA_UTF8LIBNAME, luaopen_utf8 },
		{ NULL, NULL },
	};
	for (const luaL_Reg *lib = libs; lib->func; ++lib) {
		luaL_requiref(L, lib->name, lib->func, 1);
		lua_pop(L, 1);
	}
	// Defensive: ensure the dangerous libraries / loaders aren't reachable even
	// if a future change pulls them in. (os/io/package/debug aren't opened above,
	// so these are normally already nil.)
	static const char *banned[] = {
		"os", "io", "package", "require", "debug", NULL,
	};
	for (const char **g = banned; *g; ++g) {
		lua_pushnil(L);
		lua_setglobal(L, *g);
	}

	luaai_wrap_global(L, "setmetatable", l_safe_setmetatable);
	luaai_wrap_global(L, "load", l_safe_load);
	luaai_wrap_global(L, "loadfile", l_safe_loadfile);

	lua_pushcfunction(L, l_safe_dofile);
	lua_setglobal(L, "dofile");

	lua_getglobal(L, "string");
	lua_pushnil(L);
	lua_setfield(L, -2, "dump");
	lua_pop(L, 1);
}

static int luaai_ensure_state(void)
{
	lua_State *L;

	if (g_LuaState) {
		return 1;
	}
	if (g_LuaInitFailed) {
		return 0;
	}

	L = luaL_newstate();
	if (!L) {
		g_LuaInitFailed = 1;
		g_LuaAiEnabled = 0;
		sysLogPrintf(LOG_ERROR, "luaai: failed to create Lua state; disabling Lua AI");
		return 0;
	}

	luaai_open_safe_libs(L);

	/* registry tables */
	lua_newtable(L);
	lua_setfield(L, LUA_REGISTRYINDEX, KEY_CHUNKS);
	lua_newtable(L);
	lua_setfield(L, LUA_REGISTRYINDEX, KEY_OVERRIDES);

	luaai_build_ctx(L);
	luaai_build_pd(L);

	g_LuaState = L;

	luaai_load_external_scripts(L);
	return 1;
}

void luaaiReset(void)
{
	if (g_LuaState) {
		lua_close(g_LuaState);
		g_LuaState = NULL;
	}
	g_LuaInitFailed = 0;
	g_LuaOverrideCount = 0;
	g_LuaQuarantineCount = 0; /* pointers are reused by the next stage */
	g_LuaSwitchWarnings = 0;
	luaApiResetFrame();
}

struct lua_State *luaaiGetState(void)
{
	return g_LuaState;
}

s32 luaaiEnsureState(void)
{
	return luaai_ensure_state();
}

/* ------------------------------------------------------------------------- *
 * Chunk cache / lookup
 *
 * On return, the chunk function for `list` is left on top of the Lua stack.
 * Returns 1 on success, 0 on failure (nothing pushed).
 * ------------------------------------------------------------------------- */

static int luaai_get_chunk(lua_State *L, void *list)
{
	char *src;
	u32 listlen;

	/* 1) Lua override by ailist id. Consulted only when overrides are actually
	 * registered (g_LuaOverrideCount), and never on a net client: AI is
	 * server-authoritative, so clients always run the deterministic transpiled
	 * chunk regardless of any locally-registered overrides. Skipping this when
	 * there are no overrides also avoids a per-entity, per-frame id scan. */
	if (g_LuaOverrideCount > 0 && chraiLuaOverridesAllowed()) {
		s32 id = chraiLuaGetListId(list);
		if (id >= 0) {
			lua_getfield(L, LUA_REGISTRYINDEX, KEY_OVERRIDES);
			lua_pushinteger(L, id);
			lua_gettable(L, -2);
			if (lua_isfunction(L, -1)) {
				lua_remove(L, -2); /* remove overrides table, keep function */
				return 1;
			}
			lua_pop(L, 2); /* nil + overrides table */
		}
	}

	/* 2) cached transpiled chunk */
	lua_getfield(L, LUA_REGISTRYINDEX, KEY_CHUNKS);
	lua_pushlightuserdata(L, list);
	lua_gettable(L, -2);
	if (lua_isfunction(L, -1)) {
		lua_remove(L, -2); /* remove chunks table */
		return 1;
	}
	lua_pop(L, 1); /* nil */
	/* chunks table still on stack at -1 */

	/* 3) transpile now. Bound the walk to the real list length so a missing end
	 * marker cannot read past the buffer; the 0xffff cap is only a fallback if
	 * the length is somehow unknown. */
	listlen = chraiGetAilistLength((u8 *)list);
	src = luaaiTranspile((const unsigned char *)list, listlen ? listlen : 0xffffu, luaai_cmdlen, CMD_END);
	if (!src) {
		luaai_set_error("transpile failure", "out of memory");
		lua_pop(L, 1); /* chunks table */
		return 0;
	}

	if (luaL_loadstring(L, src) != LUA_OK) {
		luaai_set_error("transpile load error", lua_tostring(L, -1));
		free(src);
		lua_pop(L, 2); /* error + chunks table */
		return 0;
	}
	free(src);

	/* run the chunk to obtain the function it returns */
	if (luaai_pcall_budget(L, 0, 1, LUAAI_INSTRUCTION_BUDGET) != LUA_OK) {
		luaai_set_error("transpile run error", lua_tostring(L, -1));
		lua_pop(L, 2); /* error + chunks table */
		return 0;
	}

	if (!lua_isfunction(L, -1)) {
		luaai_set_error("transpile run error", "chunk did not return a function");
		lua_pop(L, 2); /* result + chunks table */
		return 0;
	}

	/* cache: chunks[lightuserdata(list)] = function */
	lua_pushlightuserdata(L, list);
	lua_pushvalue(L, -2); /* the function */
	lua_settable(L, -4);  /* chunks table */

	lua_remove(L, -2); /* remove chunks table, keep function */
	return 1;
}

/* Run the chunk for `list`. Returns LUAAI_* status. */
static int luaai_run_list(lua_State *L, void *list)
{
	int status;

	if (!luaai_get_chunk(L, list)) {
		return LUAAI_ERR;
	}

	/* push ctx argument */
	lua_getfield(L, LUA_REGISTRYINDEX, KEY_CTX);

	if (luaai_pcall_budget(L, 1, 1, LUAAI_INSTRUCTION_BUDGET) != LUA_OK) {
		luaai_set_error("run error", lua_tostring(L, -1));
		lua_pop(L, 1);
		return LUAAI_ERR;
	}

	status = (int)lua_tointeger(L, -1);
	lua_pop(L, 1);
	return status;
}

/* ------------------------------------------------------------------------- *
 * Public entry point
 * ------------------------------------------------------------------------- */

/* Drive the prepared entity's lists through Lua, with the budget armed. */
static void luaai_run_entity(lua_State *L)
{
	int guard = 0;

	while (chraiLuaGetList() != NULL) {
		void *list = chraiLuaGetList();
		int r;

		if (g_LuaQuarantineCount > 0 && luaai_is_quarantined(list)) {
			/* Bytecode for the rest of this frame, from the current offset. */
			chraiRunLoop();
			return;
		}

		r = luaai_run_list(L, list);

		if (r == LUAAI_SWITCH && chraiLuaGetList() == list) {
			/* 2 promises a new list. Returning it with the list unchanged
			 * would run the same chunk again, up to the switch guard, every
			 * frame; an override doing that is broken, so treat it as one. */
			luaai_set_error("run error", "returned 2 (list changed) but the list did not change");
			r = LUAAI_ERR;
		}

		if (r == LUAAI_ERR) {
			/* Quarantine this list and let the bytecode interpreter carry
			 * on from wherever the chunk stopped. Other lists are
			 * unaffected. */
			luaai_quarantine(list);
			chraiRunLoop();
			return;
		}

		if (r == LUAAI_SWITCH) {
			if (++guard > 1024) {
				/* Runaway list switching; bail to avoid hanging. */
				if (g_LuaSwitchWarnings < LUAAI_SWITCH_WARNINGS_MAX) {
					sysLogPrintf(LOG_WARNING, "luaai: chr %d switched lists 1024 times in one tick, stopping at list %d",
							chraiLuaGetChrNum(), chraiLuaGetListId(chraiLuaGetList()));
				} else if (g_LuaSwitchWarnings == LUAAI_SWITCH_WARNINGS_MAX) {
					sysLogPrintf(LOG_WARNING, "luaai: further list switch runaways this stage not logged");
				}
				g_LuaSwitchWarnings++;
				return;
			}
			continue;
		}

		/* LUAAI_YIELD or LUAAI_TERMINAL: done for this frame. */
		return;
	}
}

void luaaiExecute(void *entity, s32 proptype)
{
	lua_State *L;
	s32 stage;

	/* Reset cached state when the stage changes (ailist pointers are reused
	 * across stages, so stale transpiled chunks must be discarded). */
	stage = chraiLuaGetStageNum();
	if (stage != g_LuaCurStage) {
		luaaiReset();
		g_LuaCurStage = stage;
	}

	/* Resolve entity + apply list-switch logic. */
	chraiPrepare(entity, proptype);
	if (chraiLuaGetList() == NULL) {
		return;
	}

	/* Sample this chr's live AI state for the pd.each_chr X-ray overlay. */
	luaApiRecordChr(chraiLuaGetChrNum(),
			chraiLuaGetListId(chraiLuaGetList()),
			chraiLuaGetOffset(),
			chraiLuaGetAlertness(), 1);

	if (!luaai_ensure_state()) {
		/* No Lua available: run the bytecode loop from the prepared state. */
		chraiRunLoop();
		return;
	}

	L = g_LuaState;

	/* Armed after luaai_ensure_state, so init.lua keeps its own budget. */
	luaai_budget_arm(L, LUAAI_INSTRUCTION_BUDGET);
	luaai_run_entity(L);
	luaai_budget_disarm(L);
}
