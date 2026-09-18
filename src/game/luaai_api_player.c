/**
 * pd.* API, player group: the local player (health, shield, view, movement,
 * stance, teleports), devices, input effects, cheats and the character-model
 * swap.
 *
 * From the Perfect Dark Kai fork (be46717), where the whole API was one file,
 * src/game/luaai_api.c. The bindings and their comments are Kai's; the
 * bridges they call are in luaai_bridge_player.c. The core registers this
 * group through luaApiRegisterPlayer.
 */

#include <ultra64.h>
#include "constants.h"
#include "types.h"
#include "game/body.h"
#include "game/cheats.h"
#include "luaai_api_internal.h"
#include "romdata.h"

/* pd.player_heal([amount]) -> bool. No arg / <= 0 = full heal; otherwise add
 * amount (clamped to full). */
static int l_pd_player_heal(lua_State *L)
{
	f32 amount = luaApiOptNum(L, 1, 0.0f);
	lua_pushboolean(L, chraiLuaPlayerHeal(amount) != 0);
	return 1;
}

/* pd.player_set_shield(frac [, silent]) -> bool. frac 0..1 (>=1 = full). Pops
 * the health bar unless silent — pass silent for a per-tick trickle (Shield
 * Charge), which would otherwise re-arm the bar every frame and never let it
 * close or finish its fill animation. */
static int l_pd_player_set_shield(lua_State *L)
{
	f32 frac = luaApiOptNum(L, 1, 1.0f);
	s32 silent = lua_toboolean(L, 2);
	lua_pushboolean(L, chraiLuaPlayerSetShield(frac, silent) != 0);
	return 1;
}

/* pd.device_on(weaponnum) -> bool. Activate a device (e.g. WEAPON_CLOAKINGDEVICE). */
static int l_pd_device_on(lua_State *L)
{
	s32 weaponnum = (s32)luaL_checkinteger(L, 1);
	lua_pushboolean(L, chraiLuaDeviceOn(weaponnum) != 0);
	return 1;
}

/* pd.device_off(weaponnum) -> bool. Deactivate a device (device_on inverse). */
static int l_pd_device_off(lua_State *L)
{
	s32 weaponnum = (s32)luaL_checkinteger(L, 1);
	lua_pushboolean(L, chraiLuaDeviceOff(weaponnum) != 0);
	return 1;
}

/* pd.device_active(weaponnum) -> bool. Device currently switched on (worn). */
static int l_pd_device_active(lua_State *L)
{
	s32 weaponnum = (s32)luaL_checkinteger(L, 1);
	lua_pushboolean(L, chraiLuaDeviceActive(weaponnum) != 0);
	return 1;
}

/* pd.invincible(on) -> bool. Toggle invincibility (Lua manages any timer). */
static int l_pd_invincible(lua_State *L)
{
	s32 on = lua_toboolean(L, 1);
	lua_pushboolean(L, chraiLuaSetInvincible(on) != 0);
	return 1;
}

/* pd.cheat(cheat_id, on) -> bool. Flip a CHEAT_* active bit live. One binding
 * turns every cheat into an effect: invincibility, slo-mo, DK mode, ... */
static int l_pd_cheat(lua_State *L)
{
	s32 cheat_id = (s32)luaL_checkinteger(L, 1);
	s32 on = lua_toboolean(L, 2);
	/* two 32-bit active banks -> ids 0..63 */
	if (cheat_id < 0 || cheat_id >= 64) {
		lua_pushboolean(L, 0);
		return 1;
	}
	cheatSetActive(cheat_id, on);
	lua_pushboolean(L, 1);
	return 1;
}

/* pd.cheat_active(cheat_id) -> bool */
static int l_pd_cheat_active(lua_State *L)
{
	s32 cheat_id = (s32)luaL_checkinteger(L, 1);
	lua_pushboolean(L, cheat_id >= 0 && cheat_id < 64 && cheatIsActive(cheat_id));
	return 1;
}

/* pd.player_yaw() -> degrees 0..360. Look yaw (spin-around task sensor). */
static int l_pd_player_yaw(lua_State *L)
{
	lua_pushnumber(L, chraiLuaPlayerYaw());
	return 1;
}

/* pd.player_crouch() -> 0 stand / 1 duck / 2 squat. */
static int l_pd_player_crouch(lua_State *L)
{
	lua_pushinteger(L, chraiLuaPlayerCrouch());
	return 1;
}

/* pd.boost([secs]) -> bool. Speed Pill boost for N seconds (self-decaying);
 * secs <= 0 cancels an active boost. */
static int l_pd_boost(lua_State *L)
{
	f32 secs = luaApiOptNum(L, 1, 10.0f);
	/* bgunAddBoost takes ticks; keep the product inside an s32. */
	if (secs > 3600.0f) {
		secs = 3600.0f;
	}
	lua_pushboolean(L, chraiLuaBoost(secs) != 0);
	return 1;
}

/* pd.player_set_health(frac) -> bool. Set health 0.01..1 (never kills). */
static int l_pd_player_set_health(lua_State *L)
{
	f32 frac = luaApiNum(L, 1);
	lua_pushboolean(L, chraiLuaPlayerSetHealth(frac) != 0);
	return 1;
}

/* pd.show_health() -> bool. Pop the health bar without changing health. */
static int l_pd_show_health(lua_State *L)
{
	lua_pushboolean(L, chraiLuaShowHealth() != 0);
	return 1;
}

/* pd.dizzy([amount]) -> bool. Tranquiliser screen-sway (decays naturally). */
static int l_pd_dizzy(lua_State *L)
{
	lua_Integer amount = luaL_optinteger(L, 1, 3000);
	/* the bridge clamps to 0..4000; clamp here so the s32 cast can't wrap */
	if (amount < 0) {
		amount = 0;
	} else if (amount > 4000) {
		amount = 4000;
	}
	lua_pushboolean(L, chraiLuaDizzy((s32)amount) != 0);
	return 1;
}

/* pd.teleport_to_chr(chrnum) -> bool. Snap the player next to a chr. */
static int l_pd_teleport_to_chr(lua_State *L)
{
	s32 chrnum = (s32)luaL_checkinteger(L, 1);
	lua_pushboolean(L, chraiLuaTeleportToChr(chrnum) != 0);
	return 1;
}

/* pd.player_health() -> number. Current health fraction (0..1), the scale
 * player_set_health writes. */
static int l_pd_player_health(lua_State *L)
{
	lua_pushnumber(L, chraiLuaPlayerHealth());
	return 1;
}

/* pd.player_shield() -> number. Current shield fraction (0..1), the scale
 * player_set_shield writes. */
static int l_pd_player_shield(lua_State *L)
{
	lua_pushnumber(L, chraiLuaPlayerShield());
	return 1;
}

/* pd.player_reloading() -> bool. True while the current gun is reloading. */
static int l_pd_player_reloading(lua_State *L)
{
	lua_pushboolean(L, chraiLuaPlayerReloading() != 0);
	return 1;
}

/* pd.player_activate() -> bool. True this frame if the use/activate button is
 * held (opening a door / interacting). */
static int l_pd_player_activate(lua_State *L)
{
	lua_pushboolean(L, chraiLuaPlayerActivate() != 0);
	return 1;
}

/* pd.model_swap(on) -> bool. Turn the character-model swap on/off: source
 * every character body/head model from the model-swap overlay ROM (on), or
 * the base ROM (off). Live chrs are rebuilt at once. Returns whether an
 * overlay ROM is loaded (false = none, nothing happened). */
static int l_pd_model_swap(lua_State *L)
{
	if (!modelSwapRomLoaded()) {
		lua_pushboolean(L, 0);
		return 1;
	}
	modelSwapSetActive(lua_toboolean(L, 1));
	lua_pushboolean(L, 1);
	return 1;
}

/* pd.model_rom_ok() -> bool. Whether a model-swap overlay ROM is loaded. */
static int l_pd_model_rom_ok(lua_State *L)
{
	lua_pushboolean(L, modelSwapRomLoaded());
	return 1;
}

/* pd.load_model_rom(path) -> bool. Load a model-swap overlay ROM at runtime.
 * `path` may be a ROM file OR a directory to scan for a ROM-sized file.
 * Safe when the folder/file is absent (no-op, returns false). */
static int l_pd_load_model_rom(lua_State *L)
{
	const char *path = luaL_checkstring(L, 1);
	lua_pushboolean(L, romdataLoadModelRom(path) != 0);
	return 1;
}

/* pd.player_damage(amount) -> bool. Hurt the local player through the real
 * damage path; ~1.0 is roughly one gunshot. */
static int l_pd_player_damage(lua_State *L)
{
	lua_pushboolean(L, chraiLuaPlayerDamage(luaApiNum(L, 1)) != 0);
	return 1;
}

/* pd.button_block(mask) -> bool. Strip these N64 pad buttons from gameplay
 * input (A 0x8000, B 0x4000, Z 0x2000, L 0x20, R 0x10, C-up 8, C-down 4,
 * C-left 2, C-right 1). 0 = give everything back. */
static int l_pd_button_block(lua_State *L)
{
	lua_pushboolean(L, chraiLuaButtonMask((u32)luaL_optinteger(L, 1, 0)) != 0);
	return 1;
}

/* pd.deadzone(frac) -> bool. Analog deadzone floor, 0..1 of full deflection
 * (0.45 = the XBLA special). 0/none = off. */
static int l_pd_deadzone(lua_State *L)
{
	lua_pushboolean(L, chraiLuaDeadzone(luaApiOptNum(L, 1, 0.0f)) != 0);
	return 1;
}

/* pd.mark_home() -> bool. Record the player's position for pd.warp_home. */
static int l_pd_mark_home(lua_State *L)
{
	lua_pushboolean(L, chraiLuaMarkHome() != 0);
	return 1;
}

/* pd.warp_home() -> bool. Teleport back to the marked position. */
static int l_pd_warp_home(lua_State *L)
{
	lua_pushboolean(L, chraiLuaWarpHome() != 0);
	return 1;
}

/* pd.sens_boost(mult) -> bool. Overly sensitive: multiply the user's mouse +
 * stick sensitivity (config sliders untouched). 1 or no arg restores. */
static int l_pd_sens_boost(lua_State *L)
{
	lua_pushboolean(L, chraiLuaSensBoost(luaApiOptNum(L, 1, 1.0f)) != 0);
	return 1;
}

/* pd.player_freeze(on) -> bool. Root the local player in place. */
static int l_pd_player_freeze(lua_State *L)
{
	lua_pushboolean(L, chraiLuaPlayerFreeze(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.player_speed(mult) -> bool. Scale the local player's walk/strafe speed
 * ("Gotta go fast"). 1 = normal. */
static int l_pd_player_speed(lua_State *L)
{
	lua_pushboolean(L, chraiLuaPlayerSpeed(luaApiOptNum(L, 1, 1.0f)) != 0);
	return 1;
}

/* pd.player_add_yaw(deg) -> bool. Speen: rotate the player's view yaw by deg
 * degrees (spins the real player — view, aim, heading). */
static int l_pd_player_add_yaw(lua_State *L)
{
	f32 deg = luaApiNum(L, 1);
	lua_pushboolean(L, chraiLuaPlayerAddYaw(deg) != 0);
	return 1;
}

/* pd.player_slip(push [, pitch_deg]) -> bool. Banana peel: full squat +
 * forward shove (knockback-style, collision-respecting); pitch only when
 * given (the effect glides it via pd.player_pitch instead). */
static int l_pd_player_slip(lua_State *L)
{
	f32 push = luaApiOptNum(L, 1, 25.0f);
	f32 pitch = luaApiOptNum(L, 2, 999.0f); /* > 180 = leave pitch alone */
	lua_pushboolean(L, chraiLuaPlayerSlip(push, pitch) != 0);
	return 1;
}

/* pd.player_push(mag) -> bool. Shove the player along their facing (positive
 * forward, negative backward) — knockback-style, collision-respecting. */
static int l_pd_player_push(lua_State *L)
{
	f32 mag = luaApiNum(L, 1);
	lua_pushboolean(L, chraiLuaPlayerPush(mag) != 0);
	return 1;
}

/* pd.invert_look(on) -> bool. Toggle the player's pitch-inversion setting
 * (movedata.invertpitch) — flips relative to however they normally run. */
static int l_pd_invert_look(lua_State *L)
{
	lua_pushboolean(L, chraiLuaInvertLook(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.input_delay(frames) -> bool. Stadia Mode: pad reads and mouse look
 * served N frames late (max 63); 0 = off. */
static int l_pd_input_delay(lua_State *L)
{
	lua_Integer frames = luaL_optinteger(L, 1, 0);
	/* input.c clamps to 0..63; clamp here so the s32 cast can't wrap */
	if (frames < 0) {
		frames = 0;
	} else if (frames > 63) {
		frames = 63;
	}
	lua_pushboolean(L, chraiLuaInputDelay((s32)frames) != 0);
	return 1;
}

/* pd.forced_march(on) -> bool. Movement stick pinned full forward. */
static int l_pd_forced_march(lua_State *L)
{
	lua_pushboolean(L, chraiLuaForcedMarch(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.forced_crouch(on) -> bool. Permacrouch: the stance is pinned to a crouch. */
static int l_pd_forced_crouch(lua_State *L)
{
	lua_pushboolean(L, chraiLuaForcedCrouch(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.headshots_only(on) -> bool. No Damage Except Headshots (player and NPCs). */
static int l_pd_headshots_only(lua_State *L)
{
	lua_pushboolean(L, chraiLuaHeadshotsOnly(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.trapdoor() -> bool. Drop the local player through the floor to their death. */
static int l_pd_trapdoor(lua_State *L)
{
	lua_pushboolean(L, chraiLuaTrapdoor() != 0);
	return 1;
}

/* pd.ice_floor(accel [, decel]) -> bool. Ice Floor grip scales, 1.0 = vanilla.
 * accel governs getting going, decel governs stopping AND the slide (the same
 * rate drives the decay toward a target speed of 0). Lower = icier. decel
 * omitted = same as accel. */
static int l_pd_ice_floor(lua_State *L)
{
	lua_pushboolean(L, chraiLuaIceFloor(luaApiOptNum(L, 1, 1.0f),
			luaApiOptNum(L, 2, -1.0f)) != 0);
	return 1;
}

/* pd.player_movespeed() -> number. Local player's normalised move speed 0..~1. */
static int l_pd_player_movespeed(lua_State *L)
{
	lua_pushnumber(L, chraiLuaPlayerMoveSpeed());
	return 1;
}

/* pd.player_pitch([deg]) -> deg | bool. No arg: current view pitch (+up).
 * With arg: set it (clamped +/-90). */
static int l_pd_player_pitch(lua_State *L)
{
	if (lua_gettop(L) < 1 || lua_isnil(L, 1)) {
		lua_pushnumber(L, chraiLuaPlayerPitchGet());
	} else {
		lua_pushboolean(L, chraiLuaPlayerPitchSet(luaApiNum(L, 1)) != 0);
	}
	return 1;
}

/* pd.time_stop(on) -> bool. SUPERHOT: freeze the game tick while no input. */
static int l_pd_time_stop(lua_State *L)
{
	lua_pushboolean(L, chraiLuaTimeStop(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.gormless(on) -> bool. Invert movement + look axes, swap fire and aim. */
static int l_pd_gormless(lua_State *L)
{
	lua_pushboolean(L, chraiLuaGormless(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* Kai's registration order (luaApiRegister). */
static const luaL_Reg s_PlayerFuncs[] = {
	{ "player_heal", l_pd_player_heal },
	{ "player_set_shield", l_pd_player_set_shield },
	{ "device_on", l_pd_device_on },
	{ "device_off", l_pd_device_off },
	{ "device_active", l_pd_device_active },
	{ "invincible", l_pd_invincible },
	{ "cheat", l_pd_cheat },
	{ "cheat_active", l_pd_cheat_active },
	{ "player_yaw", l_pd_player_yaw },
	{ "player_crouch", l_pd_player_crouch },
	{ "boost", l_pd_boost },
	{ "player_set_health", l_pd_player_set_health },
	{ "show_health", l_pd_show_health },
	{ "dizzy", l_pd_dizzy },
	{ "teleport_to_chr", l_pd_teleport_to_chr },
	{ "player_health", l_pd_player_health },
	{ "player_shield", l_pd_player_shield },
	{ "player_reloading", l_pd_player_reloading },
	{ "player_activate", l_pd_player_activate },
	{ "model_swap", l_pd_model_swap },
	{ "model_rom_ok", l_pd_model_rom_ok },
	{ "load_model_rom", l_pd_load_model_rom },
	{ "player_damage", l_pd_player_damage },
	{ "button_block", l_pd_button_block },
	{ "deadzone", l_pd_deadzone },
	{ "mark_home", l_pd_mark_home },
	{ "warp_home", l_pd_warp_home },
	{ "sens_boost", l_pd_sens_boost },
	{ "player_freeze", l_pd_player_freeze },
	{ "player_speed", l_pd_player_speed },
	{ "player_add_yaw", l_pd_player_add_yaw },
	{ "player_slip", l_pd_player_slip },
	{ "player_pitch", l_pd_player_pitch },
	{ "player_push", l_pd_player_push },
	{ "invert_look", l_pd_invert_look },
	{ "input_delay", l_pd_input_delay },
	{ "forced_march", l_pd_forced_march },
	{ "forced_crouch", l_pd_forced_crouch },
	{ "headshots_only", l_pd_headshots_only },
	{ "trapdoor", l_pd_trapdoor },
	{ "ice_floor", l_pd_ice_floor },
	{ "player_movespeed", l_pd_player_movespeed },
	{ "time_stop", l_pd_time_stop },
	{ "gormless", l_pd_gormless },
	{ NULL, NULL },
};

void luaApiRegisterPlayer(lua_State *L)
{
	luaL_setfuncs(L, s_PlayerFuncs, 0);
}
