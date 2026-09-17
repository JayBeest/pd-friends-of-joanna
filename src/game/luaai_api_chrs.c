/**
 * pd.* API, chrs group: chr mutators (pd.chr_*, pd.civil_war, pd.yassify,
 * pd.body_snatch, pd.possess_spawn, ...) and spawns / explosions (pd.spawn*,
 * pd.clone_chr, pd.explosion*, pd.grenade, pd.nbomb, ...).
 *
 * From the Perfect Dark Kai fork (be46717), where the whole API was one file,
 * src/game/luaai_api.c. The bindings and their comments are Kai's; the
 * bridges they call are in luaai_bridge_chrs.c.
 */

#include <ultra64.h>
#include "types.h"
#include "luaai_api_internal.h"

/* luaai_bridge_chrs.c: its private copies of the weapons group's
 * chraiLuaSpawnAtPos and chraiLuaExplodeAtPos. */
s32 chrsApiSpawnAtPos(s32 refchrnum, s32 weaponnum, f32 x, f32 y, f32 z);
s32 chrsApiExplodeAtPos(f32 x, f32 y, f32 z, s32 type);

/* pd.spawn_at_chr(chrnum, weaponnum) -> true on success.
 * Spawns a weapon/item world object at that chr's location (server-side only).
 * The first mutating pd.* call; everything else above is read-only. */
static int l_pd_spawn_at_chr(lua_State *L)
{
	s32 chrnum = (s32)luaL_checkinteger(L, 1);
	s32 weaponnum = (s32)luaL_checkinteger(L, 2);
	lua_pushboolean(L, chraiLuaSpawnAtChr(chrnum, weaponnum) != 0);
	return 1;
}

/* pd.spawn(weaponnum, x, y, z, [ref_chrnum]) -> true on success.
 * Spawns a weapon/item object at an arbitrary world position; rooms are seeded
 * from ref_chrnum (or the local player's chr if omitted) and the object is
 * floor-snapped at the target. Server-side only. */
static int l_pd_spawn(lua_State *L)
{
	s32 weaponnum = (s32)luaL_checkinteger(L, 1);
	f32 x = (f32)luaL_checknumber(L, 2);
	f32 y = (f32)luaL_checknumber(L, 3);
	f32 z = (f32)luaL_checknumber(L, 4);
	s32 ref = (s32)luaL_optinteger(L, 5, -1);
	lua_pushboolean(L, chrsApiSpawnAtPos(ref, weaponnum, x, y, z) != 0);
	return 1;
}

/* pd.chr_anim(chrnum, animnum, [speed]) -> bool. Play an animation on a chr. */
static int l_pd_chr_anim(lua_State *L)
{
	s32 chrnum = (s32)luaL_checkinteger(L, 1);
	s32 animnum = (s32)luaL_checkinteger(L, 2);
	f32 speed = (f32)luaL_optnumber(L, 3, 1.0);
	lua_pushboolean(L, chraiLuaChrAnim(chrnum, animnum, speed) != 0);
	return 1;
}

/* pd.chr_set_shield(chrnum, value) -> bool. */
static int l_pd_chr_set_shield(lua_State *L)
{
	s32 chrnum = (s32)luaL_checkinteger(L, 1);
	f32 value = (f32)luaL_checknumber(L, 2);
	lua_pushboolean(L, chraiLuaChrSetShield(chrnum, value) != 0);
	return 1;
}

/* pd.chr_alert(chrnum) -> bool. Put the chr on alert / onto its shot list. */
static int l_pd_chr_alert(lua_State *L)
{
	s32 chrnum = (s32)luaL_checkinteger(L, 1);
	lua_pushboolean(L, chraiLuaChrAlert(chrnum) != 0);
	return 1;
}

/* pd.chr_set_body(chrnum, bodynum, [headnum]) -> bool. Runtime model swap.
 * Solo/missions only (no-op in Combat Sim); player props refused. headnum
 * omitted/<0 picks a head valid for the body. */
static int l_pd_chr_set_body(lua_State *L)
{
	s32 chrnum = (s32)luaL_checkinteger(L, 1);
	s32 bodynum = (s32)luaL_checkinteger(L, 2);
	s32 headnum = (s32)luaL_optinteger(L, 3, -1);
	lua_pushboolean(L, chraiLuaChrSetBody(chrnum, bodynum, headnum) != 0);
	return 1;
}

/* pd.possess_spawn([bodynum]) -> chrnum | nil. Spawn a "cube" and fly it around
 * (free-fly). Solo/missions only; START/ESC or pd.unpossess() returns to Bond. */
static int l_pd_possess_spawn(lua_State *L)
{
	s32 bodynum = (s32)luaL_optinteger(L, 1, -1);
	s32 chrnum = chraiLuaPossessSpawn(bodynum);
	if (chrnum < 0) {
		lua_pushnil(L);
	} else {
		lua_pushinteger(L, chrnum);
	}
	return 1;
}

/* pd.unpossess(): stop possessing and return control to the player body. */
static int l_pd_unpossess(lua_State *L)
{
	(void)L;
	chraiLuaUnpossess();
	return 0;
}

/* pd.spawn_ally([weaponnum]) -> chrnum | nil. Spawn a friendly "Perfect Buddy",
 * wearing the player's Combat Sim profile body/head when one is set up. weaponnum omitted or <= 0 = Falcon 2. */
static int l_pd_spawn_ally(lua_State *L)
{
	s32 chrnum = chraiLuaSpawnAlly((s32)luaL_optinteger(L, 1, -1));
	if (chrnum < 0) {
		lua_pushnil(L);
	} else {
		lua_pushinteger(L, chrnum);
	}
	return 1;
}

/* pd.spawn_ally_clone([healthfrac], [yscale]) -> chrnum | nil. A friendly buddy
 * wearing the player's own body/head (a Jo clone), with health scaled by
 * healthfrac (default 0.5). yscale applies a vertical squash directly at spawn
 * (0/absent/1 = normal; 0.4 = the squat "Me and my son" clone). Backs that
 * chaos effect. */
static int l_pd_spawn_ally_clone(lua_State *L)
{
	f32 frac = (f32)luaL_optnumber(L, 1, 0.5);
	f32 yscale = (f32)luaL_optnumber(L, 2, 0.0);
	s32 chrnum = chraiLuaSpawnAllyClone(frac, yscale);
	if (chrnum < 0) {
		lua_pushnil(L);
	} else {
		lua_pushinteger(L, chrnum);
	}
	return 1;
}

/* pd.chr_yeet(chrnum, force) -> bool. Fling a chr away from the player. */
static int l_pd_chr_yeet(lua_State *L)
{
	s32 chrnum = (s32)luaL_checkinteger(L, 1);
	f32 force = (f32)luaL_optnumber(L, 2, 100.0);
	lua_pushboolean(L, chraiLuaYeetChr(chrnum, force) != 0);
	return 1;
}

/* pd.explosion(chrnum [, type]) -> bool. Detonate at a chr's feet. */
static int l_pd_explosion(lua_State *L)
{
	s32 chrnum = (s32)luaL_checkinteger(L, 1);
	s32 type = (s32)luaL_optinteger(L, 2, 9); /* a mid-size default type */
	lua_pushboolean(L, chraiLuaExplodeAtChr(chrnum, type) != 0);
	return 1;
}

/* pd.explosion_at(x, y, z [, type]) -> bool. Detonate at a position (rooms
 * portal-walked from the player). The Live Grenade / Martyrdom fuse boom. */
static int l_pd_explosion_at(lua_State *L)
{
	f32 x = (f32)luaL_checknumber(L, 1);
	f32 y = (f32)luaL_checknumber(L, 2);
	f32 z = (f32)luaL_checknumber(L, 3);
	s32 type = (s32)luaL_optinteger(L, 4, 9);
	lua_pushboolean(L, chrsApiExplodeAtPos(x, y, z, type) != 0);
	return 1;
}

/* pd.grenade(x, y, z) -> bool. Drop a LIVE armed grenade at a position (real
 * engine thrown-grenade: lands, arms, plays the pin/throw SFX, detonates on
 * its own fuse). */
static int l_pd_grenade(lua_State *L)
{
	f32 x = (f32)luaL_checknumber(L, 1);
	f32 y = (f32)luaL_checknumber(L, 2);
	f32 z = (f32)luaL_checknumber(L, 3);
	s32 chrnum = (s32)luaL_optinteger(L, 4, -1); /* room-search seed (martyrdom corpses) */
	lua_pushboolean(L, chraiLuaSpawnGrenade(x, y, z, chrnum) != 0);
	return 1;
}

/* pd.chr_cloak(chrnum, on) -> bool. Toggle a chr's cloaking device flag. */
static int l_pd_chr_cloak(lua_State *L)
{
	s32 chrnum = (s32)luaL_checkinteger(L, 1);
	lua_pushboolean(L, chraiLuaChrCloak(chrnum, lua_toboolean(L, 2)) != 0);
	return 1;
}

/* pd.chr_give_weapon(chrnum, weaponnum [, dual]) -> bool. Replace an NPC's
 * held weapons with this one (right hand; dual = one in each hand). */
static int l_pd_chr_give_weapon(lua_State *L)
{
	s32 chrnum = (s32)luaL_checkinteger(L, 1);
	s32 weaponnum = (s32)luaL_checkinteger(L, 2);
	s32 dual = lua_toboolean(L, 3);
	lua_pushboolean(L, chraiLuaChrGiveWeapon(chrnum, weaponnum, dual) != 0);
	return 1;
}

/* pd.chr_weapon(chrnum) -> weaponnum. The NPC's current weapon (-1 if invalid).
 * Snapshot before chr_give_weapon so a timed effect can restore it. */
static int l_pd_chr_weapon(lua_State *L)
{
	s32 chrnum = (s32)luaL_checkinteger(L, 1);
	lua_pushinteger(L, chraiLuaChrWeapon(chrnum));
	return 1;
}

/* pd.blood_colour(r, g, b) -> bool. Everyone bleeds this colour; pd.blood_colour()
 * restores the per-body palettes. */
static int l_pd_blood_colour(lua_State *L)
{
	if (lua_gettop(L) == 0) {
		lua_pushboolean(L, chraiLuaBloodColour(0, 0, 0, 0) != 0);
		return 1;
	}
	lua_pushboolean(L, chraiLuaBloodColour(
			(s32)luaL_checkinteger(L, 1),
			(s32)luaL_checkinteger(L, 2),
			(s32)luaL_checkinteger(L, 3), 1) != 0);
	return 1;
}

/* pd.max_blood(on) -> bool. Every hit splatters, and hard. */
static int l_pd_max_blood(lua_State *L)
{
	lua_pushboolean(L, chraiLuaMaxBlood(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.spawn_chopper([kind[, extrascale]]) -> bool. A hostile chopper appears
 * near the player and opens fire. kind 0 (default) = the dD hovercopter,
 * 1 = the A51 manned interceptor (a native chopper type — the gunfire code
 * special-cases its model scale). extrascale: 256 = full size; omitted uses
 * the per-kind default (copter 64 = quarter, interceptor 256 — its modeldef
 * is natively ~0.1 scale, don't shrink it further). Solo only. */
static int l_pd_spawn_chopper(lua_State *L)
{
	lua_pushboolean(L, chraiLuaSpawnChopper(
			(s32)luaL_optinteger(L, 1, 0),
			(s32)luaL_optinteger(L, 2, 0)) != 0);
	return 1;
}

/* pd.headshot_boost(on) -> bool. Chaos Birthday party: headshots land at x10
 * and every head hit emits a "headshot" (chrnum, attackerplayernum) event. */
static int l_pd_headshot_boost(lua_State *L)
{
	lua_pushboolean(L, chraiLuaHeadshotBoost(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.chr_freeze(on) -> bool. Pause every non-player chr's animation + firing. */
static int l_pd_chr_freeze(lua_State *L)
{
	lua_pushboolean(L, chraiLuaChrFreeze(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.no_drops(on) -> bool. Dead chrs keep their weapons in hand. */
static int l_pd_no_drops(lua_State *L)
{
	lua_pushboolean(L, chraiLuaNoDrops(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.damage_scale(frac) -> bool. Scale all chr/player damage (1 = normal). */
static int l_pd_damage_scale(lua_State *L)
{
	lua_pushboolean(L, chraiLuaDamageScale((f32)luaL_optnumber(L, 1, 1.0)) != 0);
	return 1;
}

/* pd.chr_speed(mult) -> bool. Scale every non-player chr's anim playback
 * (movement + attack cadence follow). 1 = normal. */
static int l_pd_chr_speed(lua_State *L)
{
	lua_pushboolean(L, chraiLuaChrSpeed((f32)luaL_optnumber(L, 1, 1.0)) != 0);
	return 1;
}

/* pd.chr_damage(chrnum, amount) -> bool. Hurt any chr via the real damage
 * path; ~1.0 is roughly one gunshot. */
static int l_pd_chr_damage(lua_State *L)
{
	s32 chrnum = (s32)luaL_checkinteger(L, 1);
	lua_pushboolean(L, chraiLuaChrDamage(chrnum, (f32)luaL_checknumber(L, 2)) != 0);
	return 1;
}

/* pd.chr_scale(chrnum, mult) -> bool. Multiply a chr's visual scale; undo by
 * calling again with the inverse. */
static int l_pd_chr_scale(lua_State *L)
{
	s32 chrnum = (s32)luaL_checkinteger(L, 1);
	lua_pushboolean(L, chraiLuaChrScale(chrnum, (f32)luaL_checknumber(L, 2)) != 0);
	return 1;
}

/* pd.chr_yscale(chrnum, mult) -> bool. Non-uniform vertical squash: scales only
 * the chr's height, keeping width/depth (mult 0.4 = 40% tall, full width). */
static int l_pd_chr_yscale(lua_State *L)
{
	s32 chrnum = (s32)luaL_checkinteger(L, 1);
	lua_pushboolean(L, chraiLuaChrYscale(chrnum, (f32)luaL_checknumber(L, 2)) != 0);
	return 1;
}

/* pd.chr_hum(chrnum [, on]) -> bool. Attach the Chicago interceptor's engine
 * loops (hover hum + thrust) to a chr as positional repeating sounds. Must be
 * re-issued every tick — the create is idempotent, but it refuses to start
 * past ~3000u, so this is what resumes the loops as the player closes in.
 * on defaults to true; pass false to stop both layers. */
static int l_pd_chr_hum(lua_State *L)
{
	s32 chrnum = (s32)luaL_checkinteger(L, 1);
	s32 on = lua_isnoneornil(L, 2) ? 1 : lua_toboolean(L, 2);
	lua_pushboolean(L, chraiLuaChrHum(chrnum, on) != 0);
	return 1;
}

/* pd.chr_armor(chrnum, amount) -> bool. Armor Guard: give an NPC body armor. */
static int l_pd_chr_armor(lua_State *L)
{
	s32 chrnum = (s32)luaL_checkinteger(L, 1);
	f32 amount = (f32)luaL_optnumber(L, 2, 30.0);
	lua_pushboolean(L, chraiLuaChrArmor(chrnum, amount) != 0);
	return 1;
}

/* pd.chr_armor_clear(chrnum) -> bool. Strip chaos body armor again (timed armor
 * effects ending). Zeroes the negative-damage overflow rather than subtracting
 * the granted amount back, so it can never injure or kill the chr. */
static int l_pd_chr_armor_clear(lua_State *L)
{
	lua_pushboolean(L, chraiLuaChrArmorClear((s32)luaL_checkinteger(L, 1)) != 0);
	return 1;
}

/* pd.clone_chr(chrnum, x, y, z) -> chrnum | nil. Spawn a copy of a chr at a
 * position: body, head, ACTION BLOCK, team, squadron, voicebox and held weapon.
 * ⚠ Call from a tick, never from the "kill" event — it inserts a prop, and the
 * death callback runs inside the prop tick. Snapshot the position at kill time
 * and clone on the next tick (Hydra). nil = template gone or no room. */
static int l_pd_clone_chr(lua_State *L)
{
	s32 c = chraiLuaCloneChr((s32)luaL_checkinteger(L, 1),
			(f32)luaL_checknumber(L, 2),
			(f32)luaL_checknumber(L, 3),
			(f32)luaL_checknumber(L, 4));

	if (c < 0) {
		lua_pushnil(L);
	} else {
		lua_pushinteger(L, c);
	}
	return 1;
}

/* pd.chr_freeze_one(chrnum | -1) -> bool. Statue exactly one chr. */
static int l_pd_chr_freeze_one(lua_State *L)
{
	s32 chrnum = (s32)luaL_optinteger(L, 1, -1);
	lua_pushboolean(L, chraiLuaChrFreezeOne(chrnum) != 0);
	return 1;
}

/* pd.beyblade(on) -> bool. Bayblade!: spin every NPC's model yaw at ~2 rev/s
 * (visual only — AI keeps running). */
static int l_pd_beyblade(lua_State *L)
{
	lua_pushboolean(L, chraiLuaBeyblade(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.chr_ko(chrnum) -> bool. Tranquiliser-style knockout: the chr collapses
 * and drops its weapon. The body is parked un-reaped (engine KOs are
 * otherwise permanent, and reaped chrs read as eliminated to mission
 * scripts) — wake it with pd.chr_wake. */
static int l_pd_chr_ko(lua_State *L)
{
	lua_pushboolean(L, chraiLuaChrKo((s32)luaL_checkinteger(L, 1)) != 0);
	return 1;
}

/* pd.chr_wake(chrnum) -> bool. Recover a KO'd chr: blends back to standing
 * over ~half a second and normal AI resumes (unarmed — the KO dropped their
 * weapons). No-op unless the chr is in one of the drugged states. */
static int l_pd_chr_wake(lua_State *L)
{
	lua_pushboolean(L, chraiLuaChrWake((s32)luaL_checkinteger(L, 1)) != 0);
	return 1;
}

/* pd.explosions_around(on) -> bool. The Air Force One crash sequence:
 * staggered explosions surround the player until turned off. */
static int l_pd_explosions_around(lua_State *L)
{
	lua_pushboolean(L, chraiLuaPlayerExplosions(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.nbomb() -> bool. N-Bomb storm on the player. */
static int l_pd_nbomb(lua_State *L)
{
	lua_pushboolean(L, chraiLuaNbomb() != 0);
	return 1;
}

/* pd.yassify(on) -> bool. Yassify: cinched waist, broader shoulders, bigger
 * head/cheekbones. Humans only; cosmetic (render-side joint shaping). */
static int l_pd_yassify(lua_State *L)
{
	lua_pushboolean(L, chraiLuaYassify(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.spawn_body(bodynum [, weaponnum, dx, dz, sunglasses]) -> chrnum | -1.
 * Spawn a hostile chr of the given body at the player plus a horizontal
 * offset. sunglasses=true forces the head's shades variant (Terminator). */
static int l_pd_spawn_body(lua_State *L)
{
	s32 bodynum = (s32)luaL_checkinteger(L, 1);
	s32 weaponnum = (s32)luaL_optinteger(L, 2, -1);
	f32 dx = (f32)luaL_optnumber(L, 3, 0.0);
	f32 dz = (f32)luaL_optnumber(L, 4, 0.0);
	s32 sunglasses = lua_toboolean(L, 5);
	f32 mindist = (f32)luaL_optnumber(L, 6, 0.0); /* > 0: fail a placement that slid closer than this */
	lua_pushinteger(L, chraiLuaSpawnBody(bodynum, weaponnum, dx, dz, sunglasses, mindist));
	return 1;
}

/* pd.body_snatch(chrnum) -> bool. Lite Counter-Op takeover: take the guard's
 * place (its weapon + position + disguise; the guard is removed). Solo only. */
static int l_pd_body_snatch(lua_State *L)
{
	s32 chrnum = (s32)luaL_checkinteger(L, 1);
	lua_pushboolean(L, chraiLuaBodySnatch(chrnum) != 0);
	return 1;
}

/* pd.body_unsnatch(): end the snatch — drop the disguise and teleport home. */
static int l_pd_body_unsnatch(lua_State *L)
{
	lua_pushboolean(L, chraiLuaBodyUnsnatch() != 0);
	return 1;
}

/* pd.chr_target(chrnum, victimchrnum) -> bool. Point a chr's AI at a chr. */
static int l_pd_chr_target(lua_State *L)
{
	s32 chrnum = (s32)luaL_checkinteger(L, 1);
	s32 victim = (s32)luaL_checkinteger(L, 2);
	lua_pushboolean(L, chraiLuaChrTarget(chrnum, victim) != 0);
	return 1;
}

/* pd.chr_calm(chrnum) -> bool. Zero a chr's alertness and target. */
static int l_pd_chr_calm(lua_State *L)
{
	s32 chrnum = (s32)luaL_checkinteger(L, 1);
	lua_pushboolean(L, chraiLuaChrCalm(chrnum) != 0);
	return 1;
}

/* pd.civil_war(on) -> bool. Turn NPCs on each other (nearest neighbour, hostile
 * teams); call again with true to re-assert, false to restore. */
static int l_pd_civil_war(lua_State *L)
{
	lua_pushboolean(L, chraiLuaCivilWar(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.chr_summon(chrnum [, dx, dz]) -> bool. Teleport a chr to the player. */
static int l_pd_chr_summon(lua_State *L)
{
	s32 chrnum = (s32)luaL_checkinteger(L, 1);
	f32 dx = (f32)luaL_optnumber(L, 2, 0.0);
	f32 dz = (f32)luaL_optnumber(L, 3, 0.0);
	lua_pushboolean(L, chraiLuaChrSummon(chrnum, dx, dz) != 0);
	return 1;
}

/* pd.one_punch(on) -> bool. Unarmed strikes: lethal + mega knockback. */
static int l_pd_one_punch(lua_State *L)
{
	lua_pushboolean(L, chraiLuaOnePunch(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.spawn_bike() -> bool. Half-size hoverbike at the player (solo only). */
static int l_pd_spawn_bike(lua_State *L)
{
	lua_pushboolean(L, chraiLuaSpawnBike() != 0);
	return 1;
}

/* pd.space_program(on) -> bool. Every player bullet is a one-hit kill that
 * launches the victim with massive knockback (one_punch for guns). */
static int l_pd_space_program(lua_State *L)
{
	lua_pushboolean(L, chraiLuaSpaceProgram(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.frag_out(on) -> bool. Human enemies throw a grenade whenever they would
 * fire a weapon. */
static int l_pd_frag_out(lua_State *L)
{
	lua_pushboolean(L, chraiLuaFragOut(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.spawn_sentry(dx, dz) -> bool. Deploy a hostile laptop sentry gun at the
 * player's position plus a horizontal offset (floor-snapped). */
static int l_pd_spawn_sentry(lua_State *L)
{
	f32 dx = (f32)luaL_optnumber(L, 1, 0.0);
	f32 dz = (f32)luaL_optnumber(L, 2, 0.0);
	lua_pushboolean(L, chraiLuaSpawnSentry(dx, dz) != 0);
	return 1;
}

static const luaL_Reg s_ChrsFuncs[] = {
	{"spawn_at_chr", l_pd_spawn_at_chr},
	{"spawn", l_pd_spawn},
	{"chr_anim", l_pd_chr_anim},
	{"chr_set_shield", l_pd_chr_set_shield},
	{"chr_alert", l_pd_chr_alert},
	{"chr_set_body", l_pd_chr_set_body},
	{"possess_spawn", l_pd_possess_spawn},
	{"unpossess", l_pd_unpossess},
	{"spawn_ally", l_pd_spawn_ally},
	{"spawn_ally_clone", l_pd_spawn_ally_clone},
	{"chr_yeet", l_pd_chr_yeet},
	{"explosion", l_pd_explosion},
	{"explosion_at", l_pd_explosion_at},
	{"grenade", l_pd_grenade},
	{"chr_cloak", l_pd_chr_cloak},
	{"chr_give_weapon", l_pd_chr_give_weapon},
	{"chr_weapon", l_pd_chr_weapon},
	{"blood_colour", l_pd_blood_colour},
	{"max_blood", l_pd_max_blood},
	{"spawn_chopper", l_pd_spawn_chopper},
	{"headshot_boost", l_pd_headshot_boost},
	{"chr_freeze", l_pd_chr_freeze},
	{"no_drops", l_pd_no_drops},
	{"damage_scale", l_pd_damage_scale},
	{"chr_speed", l_pd_chr_speed},
	{"chr_damage", l_pd_chr_damage},
	{"chr_scale", l_pd_chr_scale},
	{"chr_yscale", l_pd_chr_yscale},
	{"chr_hum", l_pd_chr_hum},
	{"chr_armor", l_pd_chr_armor},
	{"chr_armor_clear", l_pd_chr_armor_clear},
	{"clone_chr", l_pd_clone_chr},
	{"chr_freeze_one", l_pd_chr_freeze_one},
	{"beyblade", l_pd_beyblade},
	{"chr_ko", l_pd_chr_ko},
	{"chr_wake", l_pd_chr_wake},
	{"explosions_around", l_pd_explosions_around},
	{"nbomb", l_pd_nbomb},
	{"yassify", l_pd_yassify},
	{"spawn_body", l_pd_spawn_body},
	{"body_snatch", l_pd_body_snatch},
	{"body_unsnatch", l_pd_body_unsnatch},
	{"chr_target", l_pd_chr_target},
	{"chr_calm", l_pd_chr_calm},
	{"civil_war", l_pd_civil_war},
	{"chr_summon", l_pd_chr_summon},
	{"one_punch", l_pd_one_punch},
	{"spawn_bike", l_pd_spawn_bike},
	{"space_program", l_pd_space_program},
	{"frag_out", l_pd_frag_out},
	{"spawn_sentry", l_pd_spawn_sentry},
	{NULL, NULL},
};

void luaApiRegisterChrs(lua_State *L)
{
	luaL_setfuncs(L, s_ChrsFuncs, 0);
}
