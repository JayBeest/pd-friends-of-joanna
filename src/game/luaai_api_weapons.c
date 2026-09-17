/**
 * pd.* API, weapons group: inventory and ammo (give/take/switch/drop, ammo
 * pools, dual wield), the weapon behaviour effects (jam, spread, backfire,
 * ammo cost/swap, pinball, reload and clip gags, weapon locks, viewmodel
 * FOV/hide/quad, zoom, gun sound, rename/censor) and the text gags (uwuify,
 * piglatin, buttsbot, text_scramble). pd.gangsta is the sideways-pistol pose.
 *
 * From the Perfect Dark Kai fork (be46717), where the whole API was one file,
 * src/game/luaai_api.c. The bindings and their comments are Kai's; the
 * bridges they call are in luaai_bridge_weapons.c.
 */

#include <ultra64.h>
#include "types.h"
#include "game/chaosstate.h"
#include "luaai_api_internal.h"

/* pd.refill_ammo() -> bool. Top all ammo to capacity (covers current weapon). */
static int l_pd_refill_ammo(lua_State *L)
{
	lua_pushboolean(L, chraiLuaRefillAmmo() != 0);
	return 1;
}

/* pd.give_mags([n]) -> bool. Stock every ammo type with n magazines (default 2)
 * instead of filling to capacity. Used by the gun-giving effects. */
static int l_pd_give_mags(lua_State *L)
{
	s32 mags = (s32)luaL_optinteger(L, 1, 2);
	lua_pushboolean(L, chraiLuaGiveMags(mags) != 0);
	return 1;
}

/* pd.give_ammo(ammotype, [qty]) -> bool. Grants ammo (+ the matching weapon). */
static int l_pd_give_ammo(lua_State *L)
{
	s32 ammotype = (s32)luaL_checkinteger(L, 1);
	s32 qty = (s32)luaL_optinteger(L, 2, 1);
	lua_pushboolean(L, chraiLuaGiveAmmo(ammotype, qty) != 0);
	return 1;
}

/* pd.give_weapon(weaponnum) -> bool. Add a weapon to the player's inventory. */
static int l_pd_give_weapon(lua_State *L)
{
	s32 weaponnum = (s32)luaL_checkinteger(L, 1);
	lua_pushboolean(L, chraiLuaGiveWeaponToPlayer(weaponnum) != 0);
	return 1;
}

/* pd.take_weapon(weaponnum) -> bool */
static int l_pd_take_weapon(lua_State *L)
{
	lua_pushboolean(L, chraiLuaTakeWeapon((s32)luaL_checkinteger(L, 1)) != 0);
	return 1;
}

/* pd.weapon_held() -> weaponnum | -1 */
static int l_pd_weapon_held(lua_State *L)
{
	lua_pushinteger(L, chraiLuaWeaponHeld());
	return 1;
}

/* pd.switch_weapon(weaponnum) -> bool */
static int l_pd_switch_weapon(lua_State *L)
{
	lua_pushboolean(L, chraiLuaSwitchWeapon((s32)luaL_checkinteger(L, 1)) != 0);
	return 1;
}

/* pd.has_weapon(weaponnum) -> bool. Weapon is in the player's inventory. */
static int l_pd_has_weapon(lua_State *L)
{
	lua_pushboolean(L, chraiLuaHasWeapon((s32)luaL_checkinteger(L, 1)) != 0);
	return 1;
}

/* pd.strip_ammo() -> bool. Zero every ammo pool (weapons stay). */
static int l_pd_strip_ammo(lua_State *L)
{
	lua_pushboolean(L, chraiLuaStripAmmo() != 0);
	return 1;
}

/* pd.set_ammo(ammotype, qty) -> bool. Set one ammo pool to an exact quantity. */
static int l_pd_set_ammo(lua_State *L)
{
	s32 ammotype = (s32)luaL_checkinteger(L, 1);
	s32 qty = (s32)luaL_checkinteger(L, 2);
	lua_pushboolean(L, chraiLuaSetAmmo(ammotype, qty) != 0);
	return 1;
}

/* pd.gun_hide(on) -> bool. Hide the player's viewmodel entirely (render-only;
 * firing/reloads still work). Blind bag's mystery weapon. */
static int l_pd_gun_hide(lua_State *L)
{
	g_BgunHideGun = lua_toboolean(L, 1);
	lua_pushboolean(L, 1);
	return 1;
}

/* pd.bag_boom() -> bool. Bag bomb: detonate + free the dropped suitcase at
 * its resting position (see chraiLuaBagBoom). */
static int l_pd_bag_boom(lua_State *L)
{
	lua_pushboolean(L, chraiLuaBagBoom() != 0);
	return 1;
}

/* pd.bag_convert() -> bool. Bag bomb: turn the player's thrown+armed dragon
 * into the defused suitcase pickup at its resting spot (see
 * chraiLuaBagConvert; 0 while the throw is still airborne). */
static int l_pd_bag_convert(lua_State *L)
{
	lua_pushboolean(L, chraiLuaBagConvert() != 0);
	return 1;
}

/* pd.weapon_censor(weaponnum, on) -> bool. Render the weapon's manufacturer,
 * description and fire-mode names as "?????" everywhere (Blind bag). Pairs
 * with pd.weapon_rename for the name; on=false clears. */
static int l_pd_weapon_censor(lua_State *L)
{
	s32 weaponnum = (s32)luaL_optinteger(L, 1, -1);
	s32 on = lua_toboolean(L, 2);
	lua_pushboolean(L, chraiLuaWeaponCensor(weaponnum, on) != 0);
	return 1;
}

/* pd.weapon_jam(mode) -> bool. 1/true = every trigger pull dry-fires;
 * 2 = "jam v2": ~35% of pulls dry-fire, and a shot that fires drains the rest
 * of the magazine (reload to clear). false/0 = off. */
static int l_pd_weapon_jam(lua_State *L)
{
	s32 mode;
	if (lua_isnumber(L, 1)) {
		mode = (s32)lua_tointeger(L, 1);
		if (mode < 0) {
			mode = 0;
		} else if (mode > 2) {
			mode = 2;
		}
	} else {
		mode = lua_toboolean(L, 1) ? 1 : 0;
	}
	lua_pushboolean(L, chraiLuaWeaponJam(mode) != 0);
	return 1;
}

/* pd.force_secondary(on) -> bool. Pin both hands to the secondary function. */
static int l_pd_force_secondary(lua_State *L)
{
	lua_pushboolean(L, chraiLuaForceSecondary(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.ammo_cost(mult) -> bool. Each shot spends mult rounds; 1 = normal. */
static int l_pd_ammo_cost(lua_State *L)
{
	lua_pushboolean(L, chraiLuaAmmoCost((s32)luaL_optinteger(L, 1, 1)) != 0);
	return 1;
}

/* pd.autoaim(on) -> bool. Force aim assist on regardless of the option. */
static int l_pd_autoaim(lua_State *L)
{
	lua_pushboolean(L, chraiLuaAutoAim(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.double_shots(on) -> bool. Every fire event takes twice the shots. */
static int l_pd_double_shots(lua_State *L)
{
	lua_pushboolean(L, chraiLuaDoubleShots(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.quad_top(on) -> bool. A second pair of viewmodel guns hangs upside-down
 * from the top of the screen. */
static int l_pd_quad_top(lua_State *L)
{
	lua_pushboolean(L, chraiLuaQuadTop(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.text_scramble(on) -> bool. Text overload: every letter becomes a random
 * other letter (stable per string; %-format specs preserved). */
static int l_pd_text_scramble(lua_State *L)
{
	lua_pushboolean(L, chraiLuaTextScramble(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.gangsta(on) -> bool. Gangster: force the close-range sideways-pistol
 * viewmodel pose on permanently (rides the vanilla gangsta animation). */
static int l_pd_gangsta(lua_State *L)
{
	lua_pushboolean(L, chraiLuaGangsta(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.zoom_scale(mult) -> bool. Scale weapon aim-zoom FOV; >1 zooms OUT. */
static int l_pd_zoom_scale(lua_State *L)
{
	lua_pushboolean(L, chraiLuaZoomScale(luaApiOptNum(L, 1, 1.0f)) != 0);
	return 1;
}

/* pd.gun_sound(weaponnum) -> bool. Every gun fires with this weapon's shoot
 * sound; pd.gun_sound() restores. */
static int l_pd_gun_sound(lua_State *L)
{
	lua_pushboolean(L, chraiLuaGunSound((s32)luaL_optinteger(L, 1, 0)) != 0);
	return 1;
}

/* pd.weapon_rename(weaponnum [, name]). Relabel a weapon everywhere it's
 * shown; no name / nil restores the real one. */
static int l_pd_weapon_rename(lua_State *L)
{
	s32 weaponnum = (s32)luaL_checkinteger(L, 1);
	const char *name = luaL_optstring(L, 2, NULL);
	lua_pushboolean(L, chraiLuaWeaponRename(weaponnum, name) != 0);
	return 1;
}

/* pd.one_bullet(on) -> bool. One Bullet Mags: clip capacity 1 (equip-baked). */
static int l_pd_one_bullet(lua_State *L)
{
	lua_pushboolean(L, chraiLuaOneBullet(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.uwuify(on) -> bool. Evewy stwing in the game, uwuified. */
static int l_pd_uwuify(lua_State *L)
{
	lua_pushboolean(L, chraiLuaUwuify(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.piglatin(on) -> bool. Everyway ingstray, igpay atinlay. */
static int l_pd_piglatin(lua_State *L)
{
	lua_pushboolean(L, chraiLuaPigLatin(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.forced_fire(on) -> bool. Itchy Trigger Finger: trigger held for you. */
static int l_pd_forced_fire(lua_State *L)
{
	lua_pushboolean(L, chraiLuaForcedFire(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.rapid_fire(on) -> bool. Trigger Happy: while you hold fire, semi-autos
 * fire as fast as automatics. */
static int l_pd_rapid_fire(lua_State *L)
{
	lua_pushboolean(L, chraiLuaRapidFire(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.no_reload(on) -> bool. Reload Denied: every reload transition is refused. */
static int l_pd_no_reload(lua_State *L)
{
	lua_pushboolean(L, chraiLuaNoReload(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.spread(mult) -> bool. Chaos Weapon Spread: scale weapon shot spread. */
static int l_pd_spread(lua_State *L)
{
	lua_pushboolean(L, chraiLuaSpread(luaApiOptNum(L, 1, 1.0f)) != 0);
	return 1;
}

/* pd.drop_weapon(weaponnum) -> bool. Drop a player weapon as a collectable
 * pickup + remove it from inventory (Sonic Mode toss). */
static int l_pd_drop_weapon(lua_State *L)
{
	lua_pushboolean(L, chraiLuaDropWeapon((s32)luaL_checkinteger(L, 1)) != 0);
	return 1;
}

/* pd.gun_fov(deg) -> bool. Viewmodel FOV override; 0 restores. */
static int l_pd_gun_fov(lua_State *L)
{
	f32 deg = luaApiOptNum(L, 1, 0.0f);
	lua_pushboolean(L, chraiLuaGunFov(deg) != 0);
	return 1;
}

/* pd.buttsbot(on) -> bool. Random-but-stable words become butt. */
static int l_pd_buttsbot(lua_State *L)
{
	lua_pushboolean(L, chraiLuaButtsbot(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.temu_mag(on) -> bool. Reloading pays the full ammo cost but only partly
 * refills the magazine (random fraction). */
static int l_pd_temu_mag(lua_State *L)
{
	lua_pushboolean(L, chraiLuaTemuMag(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.pinball(on) -> bool. Fired physics projectiles (rockets, grenade rounds)
 * become bouncing proximity pinballs. */
static int l_pd_pinball(lua_State *L)
{
	lua_pushboolean(L, chraiLuaPinball(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.ammo_swap(weaponnum) -> bool. Held guns fire this weapon's primary
 * rounds (must be a SHOOT-type function). pd.ammo_swap() turns it off. */
static int l_pd_ammo_swap(lua_State *L)
{
	s32 weaponnum = (s32)luaL_optinteger(L, 1, -1);
	lua_pushboolean(L, chraiLuaAmmoSwap(weaponnum) != 0);
	return 1;
}

/* pd.backfire(on) -> bool. Shots leave 180 degrees behind the player. */
static int l_pd_backfire(lua_State *L)
{
	lua_pushboolean(L, chraiLuaBackfire(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.dual_wield(weaponnum [, funcnum]) -> bool. Dual-equip a weapon with
 * full ammo; funcnum 0/1 also forces that fire function on both hands. */
static int l_pd_dual_wield(lua_State *L)
{
	s32 weaponnum = (s32)luaL_checkinteger(L, 1);
	s32 funcnum = (s32)luaL_optinteger(L, 2, -1);
	lua_pushboolean(L, chraiLuaDualWield(weaponnum, funcnum) != 0);
	return 1;
}

/* pd.gun_lock(on) -> bool. Cyclone Frenzy: force secondary fire on both hands,
 * hold the trigger (auto-fire), and block weapon switching. */
static int l_pd_gun_lock(lua_State *L)
{
	lua_pushboolean(L, chraiLuaGunLock(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.mag_dump(on) -> bool. Mag Dump: one trigger press empties the whole clip —
 * automatic weapons hold the trigger, semi-autos rapidly pulse it. */
static int l_pd_mag_dump(lua_State *L)
{
	lua_pushboolean(L, chraiLuaMagDump(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.knife_lock(on) -> bool. Knife fight: block weapon switching only. */
static int l_pd_knife_lock(lua_State *L)
{
	lua_pushboolean(L, chraiLuaKnifeLock(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.cloak_lock(on) -> bool. Unbreakable no-ammo player cloak. */
static int l_pd_cloak_lock(lua_State *L)
{
	lua_pushboolean(L, chraiLuaCloakLock(lua_toboolean(L, 1)) != 0);
	return 1;
}

static const luaL_Reg g_LuaApiWeaponsFuncs[] = {
	{ "refill_ammo", l_pd_refill_ammo },
	{ "give_mags", l_pd_give_mags },
	{ "give_ammo", l_pd_give_ammo },
	{ "give_weapon", l_pd_give_weapon },
	{ "take_weapon", l_pd_take_weapon },
	{ "weapon_held", l_pd_weapon_held },
	{ "switch_weapon", l_pd_switch_weapon },
	{ "has_weapon", l_pd_has_weapon },
	{ "strip_ammo", l_pd_strip_ammo },
	{ "set_ammo", l_pd_set_ammo },
	{ "gun_hide", l_pd_gun_hide },
	{ "bag_boom", l_pd_bag_boom },
	{ "bag_convert", l_pd_bag_convert },
	{ "weapon_censor", l_pd_weapon_censor },
	{ "weapon_jam", l_pd_weapon_jam },
	{ "force_secondary", l_pd_force_secondary },
	{ "ammo_cost", l_pd_ammo_cost },
	{ "autoaim", l_pd_autoaim },
	{ "double_shots", l_pd_double_shots },
	{ "quad_top", l_pd_quad_top },
	{ "text_scramble", l_pd_text_scramble },
	{ "gangsta", l_pd_gangsta },
	{ "zoom_scale", l_pd_zoom_scale },
	{ "gun_sound", l_pd_gun_sound },
	{ "weapon_rename", l_pd_weapon_rename },
	{ "one_bullet", l_pd_one_bullet },
	{ "uwuify", l_pd_uwuify },
	{ "piglatin", l_pd_piglatin },
	{ "forced_fire", l_pd_forced_fire },
	{ "rapid_fire", l_pd_rapid_fire },
	{ "no_reload", l_pd_no_reload },
	{ "spread", l_pd_spread },
	{ "drop_weapon", l_pd_drop_weapon },
	{ "gun_fov", l_pd_gun_fov },
	{ "buttsbot", l_pd_buttsbot },
	{ "pinball", l_pd_pinball },
	{ "ammo_swap", l_pd_ammo_swap },
	{ "backfire", l_pd_backfire },
	{ "dual_wield", l_pd_dual_wield },
	{ "gun_lock", l_pd_gun_lock },
	{ "mag_dump", l_pd_mag_dump },
	{ "knife_lock", l_pd_knife_lock },
	{ "cloak_lock", l_pd_cloak_lock },
	{ "temu_mag", l_pd_temu_mag },
	{ NULL, NULL },
};

void luaApiRegisterWeapons(lua_State *L)
{
	luaL_setfuncs(L, g_LuaApiWeaponsFuncs, 0);
}
