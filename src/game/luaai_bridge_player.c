/**
 * chraiLua* bridges for the player group of the pd.* API: the local player's
 * health, shield, view, movement and stance, devices, and the input effects.
 *
 * From the Perfect Dark Kai fork (be46717), where the bridges sat in
 * src/game/chraction.c. The comments are Kai's, trimmed where they described
 * Kai-only history. Kai's net client tests are gone: this build has no
 * netplay. chrSetPos and chaosPlayerWarp were Kai engine helpers that fojo
 * does not have; they are static here.
 */

#include <ultra64.h>
#include <math.h>
#include "constants.h"
#include "types.h"
#include "bss.h"
#include "data.h"
#include "game/atan2f.h"
#include "game/bondgun.h"
#include "game/chaosstate.h"
#include "game/chr.h"
#include "game/chraction.h"
#include "game/game_0b0fd0.h"
#include "game/player.h"
#include "game/playermgr.h"
#include "game/prop.h"
#include "lib/collision.h"
#include "lib/model.h"
#include "input.h"
#include "luaai_api_internal.h"

// ---------------------------------------------------------------------------
// Local helpers
// ---------------------------------------------------------------------------

// Kai's chrSetPos (chraction.c), minus the netplay force-correct: hard-set a
// chr with a model to pos/rooms, facing theta (degrees). Guards the pointers
// it dereferences, so a model-less chr is refused instead of crashing.
static bool luaChrSetPos(struct chrdata *chr, struct coord *pos, RoomNum *rooms, f32 theta, bool findground)
{
	const f32 angle = BADDEG2RAD(360.0f - theta);
	bool newrooms = false;
	f32 ground;
	u16 nodetype;
	s32 i;

	if (chr == NULL || chr->prop == NULL || chr->model == NULL || pos == NULL || rooms == NULL) {
		return false;
	}

	if (findground) {
		const u32 oldhidden = chr->hidden;
		chr->hidden |= CHRHFLAG_WARPONSCREEN;
		chrMoveToPos(chr, pos, rooms, angle, true);
		if ((oldhidden & CHRHFLAG_WARPONSCREEN) == 0) {
			chr->hidden &= ~CHRHFLAG_WARPONSCREEN;
		}
	}

	for (i = 0; i < ARRAYCOUNT(chr->prop->rooms) && rooms[i] >= 0; ++i) {
		if (chr->prop->rooms[i] != rooms[i]) {
			newrooms = true;
			break;
		}
	}

	propSetPerimEnabled(chr->prop, false);

	chr->prop->pos = *pos;

	ground = cdFindGroundInfoAtCyl(pos, chr->radius, rooms, &chr->floorcol,
			&chr->floortype, NULL, &chr->floorroom, NULL, NULL);

	chr->ground = ground;
	chr->manground = ground;
	chr->sumground = ground * (PAL ? 8.4175090789795f : 9.999998f);

	if (newrooms) {
		propDeregisterRooms(chr->prop);
		roomsCopy(rooms, chr->prop->rooms);
		chr0f0220ac(chr);
	}

	modelSetRootPosition(chr->model, pos);

	nodetype = chr->model->definition->rootnode->type;

	if ((nodetype & 0xff) == MODELNODETYPE_CHRINFO) {
		union modelrwdata *rwdata = modelGetNodeRwData(chr->model, chr->model->definition->rootnode);
		rwdata->chrinfo.ground = ground;
	}

	chrSetLookAngle(chr, angle);

	if (chr->prop->type == PROPTYPE_PLAYER) {
		struct player *player = g_Vars.players[playermgrGetPlayerNumByProp(chr->prop)];
		player->vv_manground = ground;
		player->vv_ground = ground;
		player->vv_theta = theta;
		player->unk1c64 = 1;
	}

	propSetPerimEnabled(chr->prop, true);

	return true;
}

// Relocate a model-less first-person player's prop by hand: pos + room
// registration + the bondwalk view-height (vv_*) fields, which is all a
// model-less player needs. rooms is consumed (copied into the prop).
static void luaPlayerPropMove(struct chrdata *pl, struct coord *pos, RoomNum *rooms, bool levelpitch)
{
	struct prop *plprop = pl->prop;
	f32 ground;

	propSetPerimEnabled(plprop, false);

	plprop->pos = *pos;

	ground = cdFindGroundInfoAtCyl(&plprop->pos, pl->radius, rooms,
			&pl->floorcol, &pl->floortype, NULL, &pl->floorroom, NULL, NULL);
	pl->ground = ground;
	pl->manground = ground;
	pl->sumground = ground * (PAL ? 8.4175090789795f : 9.999998f);

	propDeregisterRooms(plprop);
	roomsCopy(rooms, plprop->rooms);
	chr0f0220ac(pl);

	if (plprop->type == PROPTYPE_PLAYER) {
		struct player *player = g_Vars.players[playermgrGetPlayerNumByProp(plprop)];
		player->vv_manground = ground;
		player->vv_ground = ground;
		if (levelpitch) {
			player->vv_verta = 0;
		}
		player->unk1c64 = 1;
	}

	propSetPerimEnabled(plprop, true);
}

// Warp the local player to pos/rooms (Kai's chaosPlayerWarp). The solo
// first-person player has no chr model, so move the prop by hand; a
// body-model player uses chrSetPos.
static void luaPlayerWarp(struct coord *pos, RoomNum *rooms)
{
	struct chrdata *pl = g_Vars.currentplayer->prop->chr;
	RoomNum tmp[8];

	if (pl == NULL) {
		return;
	}

	roomsCopy(rooms, tmp);

	if (pl->model != NULL) {
		luaChrSetPos(pl, pos, tmp, chrGetRotY(pl), true);
		return;
	}

	luaPlayerPropMove(pl, pos, tmp, false);
}

// ---------------------------------------------------------------------------
// Health and shield
// ---------------------------------------------------------------------------

// pd.player_heal(): restore the player to full health.
s32 chraiLuaPlayerHeal(f32 amount)
{
	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	if (amount <= 0.0f) {
		g_Vars.currentplayer->bondhealth = 1.0f; // full heal (default / no arg)
	} else {
		g_Vars.currentplayer->bondhealth += amount; // partial top-up (e.g. 0.5 = half)
		if (g_Vars.currentplayer->bondhealth > 1.0f) {
			g_Vars.currentplayer->bondhealth = 1.0f;
		}
	}
	playerDisplayHealth(); // silent HP change — pop the health bar
	return 1;
}

// pd.player_set_shield(frac [, silent]): set the shield 0..1. silent skips the
// health-bar pop: a per-tick caller would otherwise re-arm healthshowtime
// every frame, so the bar could never close or finish its fill animation.
s32 chraiLuaPlayerSetShield(f32 frac, s32 silent)
{
	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	if (frac < 0.0f) frac = 0.0f;
	if (frac > 1.0f) frac = 1.0f;
	playerSetShieldFrac(frac);
	if (!silent) {
		playerDisplayHealth();
	}
	return 1;
}

// pd.player_set_health(frac): set the player's health directly (0..1 of the
// bar). Floored just above zero — it scares, it doesn't execute; kills go
// through real damage paths so death bookkeeping stays consistent.
s32 chraiLuaPlayerSetHealth(f32 frac)
{
	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	if (frac < 0.01f) frac = 0.01f;
	if (frac > 1.0f) frac = 1.0f;
	g_Vars.currentplayer->bondhealth = frac;
	playerDisplayHealth(); // silent HP change — pop the health bar
	return 1;
}

// pd.show_health(): pop the health bar WITHOUT changing anything — lets an
// effect present the current value first, then animate to a new one on a
// later player_set_health.
s32 chraiLuaShowHealth(void)
{
	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	playerDisplayHealth();
	return 1;
}

// pd.player_health(): current health fraction (0..1), the same scale
// player_set_health writes.
f32 chraiLuaPlayerHealth(void)
{
	if (apLuaPlayerChr() == NULL) {
		return 0.0f;
	}
	return g_Vars.currentplayer->bondhealth;
}

// pd.player_shield(): the local player's current shield fraction (0..1).
f32 chraiLuaPlayerShield(void)
{
	if (apLuaPlayerChr() == NULL) {
		return 0.0f;
	}
	return playerGetShieldFrac();
}

// pd.player_damage(amount): hurt the local player through the real damage
// path (shield first, damage flash/sound, death) — ~1.0 is roughly one
// gunshot. Attacker is NULL (environment), so a death reads as a suicide.
s32 chraiLuaPlayerDamage(f32 amount)
{
	struct chrdata *pchr = apLuaPlayerChr();
	struct coord vec = {0, 0, 1};

	if (pchr == NULL || amount <= 0.0f) {
		return 0;
	}
	chrDamageByMisc(pchr, amount, &vec, NULL, NULL);
	return 1;
}

// pd.invincible(on): toggle player invincibility (Lua manages any timer).
s32 chraiLuaSetInvincible(s32 on)
{
	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	g_Vars.currentplayer->invincible = on ? 1 : 0;
	return 1;
}

// pd.headshots_only(on): "No Damage Except Headshots". Player side: every
// non-head hit is zeroed. NPC side: body/limb fire is clamped below
// maxdamage so it can never be the fatal hit, and routed down the light
// flinch branch so it never staggers them. Explosions are exempt on the NPC
// side. See the three sites in chrDamage.
s32 chraiLuaHeadshotsOnly(s32 on)
{
	g_ChaosHeadshotsOnly = on ? 1 : 0;
	return 1;
}

// pd.trapdoor(): "Trapdoor" — open a hole under the local player for ~2s so the
// floor vanishes and they fall to their death (bondwalk.c g_ChaosTrapdoorTicks).
s32 chraiLuaTrapdoor(void)
{
	g_ChaosTrapdoorTicks = 120; // ~2s of no floor -> fall past the death plane
	return 1;
}

// pd.boost(secs): grant `secs` seconds of the Combat Boost / Speed Pill
// (bgunAddBoost owns the want-flag, activation sting, and the zoom-blur
// ramp; bgunTickBoost decays the time and shuts it off — no cleanup needed).
// secs <= 0 cancels an active boost immediately.
s32 chraiLuaBoost(f32 secs)
{
	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	if (secs > 0.0f) {
		bgunAddBoost((s32)(secs * TICKS(60)));
	} else {
		g_Vars.speedpilltime = 0;
		g_Vars.speedpillwant = false;
	}
	return 1;
}

// pd.dizzy(amount): apply the tranquiliser screen-sway to the local player
// (chr->blurdrugamount — the same accumulator tranq/psychosis rounds feed;
// decays naturally). Capped below the TICKS(5000) knockout band the drugged
// paths key on. amount is in blur units, ~2000-4000 is a solid wobble.
s32 chraiLuaDizzy(s32 amount)
{
	struct chrdata *chr = apLuaPlayerChr();

	if (chr == NULL) {
		return 0;
	}
	if (amount < 0) amount = 0;
	if (amount > 4000) amount = 4000;
	if (chr->blurdrugamount < TICKS(amount)) {
		chr->blurdrugamount = TICKS(amount);
	}
	return 1;
}

// ---------------------------------------------------------------------------
// Devices
// ---------------------------------------------------------------------------

// pd.device_on(weaponnum): activate a device (e.g. WEAPON_CLOAKINGDEVICE).
s32 chraiLuaDeviceOn(s32 weaponnum)
{
	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	currentPlayerSetDeviceActive(weaponnum, true);
	return 1;
}

// pd.device_off(weaponnum): deactivate a device (the pd.device_on inverse —
// currentPlayerSetDeviceActive with active=false clears the devicesactive
// bit, which is all device_on ever set).
s32 chraiLuaDeviceOff(s32 weaponnum)
{
	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	currentPlayerSetDeviceActive(weaponnum, false);
	return 1;
}

// pd.device_active(weaponnum): whether a device is currently switched ON
// (DEVICESTATE_ACTIVE — wearing the eyewear, not merely owning it).
s32 chraiLuaDeviceActive(s32 weaponnum)
{
	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	return currentPlayerGetDeviceState(weaponnum) == DEVICESTATE_ACTIVE;
}

// ---------------------------------------------------------------------------
// View and movement
// ---------------------------------------------------------------------------

// pd.player_yaw(): the player's look yaw in degrees (0..360).
f32 chraiLuaPlayerYaw(void)
{
	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	return g_Vars.currentplayer->vv_theta;
}

// pd.player_add_yaw(deg): rotate the local player's view yaw by deg degrees
// (spins the actual player: view, aim and movement heading). Additive with
// normal look input; wrapped 0..360 the same way bondwalk's rotate path does.
s32 chraiLuaPlayerAddYaw(f32 deg)
{
	f32 angle;

	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	// A NaN or huge value would never leave the wrap loops below.
	if (!(deg > -1000000.0f && deg < 1000000.0f)) {
		return 0;
	}
	angle = g_Vars.currentplayer->vv_theta + deg;
	while (angle < 0) {
		angle += 360;
	}
	while (angle >= 360) {
		angle -= 360;
	}
	g_Vars.currentplayer->vv_theta = angle;
	return 1;
}

// pd.player_pitch([deg]): get (no arg) or set the player's view pitch in
// degrees (vv_verta, +up, clamped to the engine's +/-90).
f32 chraiLuaPlayerPitchGet(void)
{
	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	return g_Vars.currentplayer->vv_verta;
}

s32 chraiLuaPlayerPitchSet(f32 deg)
{
	if (apLuaPlayerChr() == NULL || isnan(deg)) {
		return 0;
	}
	if (deg > 90.0f) {
		deg = 90.0f;
	} else if (deg < -90.0f) {
		deg = -90.0f;
	}
	g_Vars.currentplayer->vv_verta = deg;
	return 1;
}

// pd.player_slip(push, pitchdeg): banana peel — full squat plus a forward
// shove, optional pitch (pitchdeg > 180 leaves the pitch alone). The shove
// rides bondshotspeed (the explosion-knockback vector bwalk integrates with
// collision + decay); world forward = (-vv_sintheta, 0, vv_costheta).
s32 chraiLuaPlayerSlip(f32 push, f32 pitchdeg)
{
	struct player *pl;

	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	pl = g_Vars.currentplayer;
	pl->crouchpos = CROUCHPOS_SQUAT;
	if (pitchdeg <= 180.0f) {
		if (pitchdeg > 90.0f) {
			pitchdeg = 90.0f;
		} else if (pitchdeg < -90.0f) {
			pitchdeg = -90.0f;
		}
		pl->vv_verta = pitchdeg;
	}
	pl->bondshotspeed.x += -pl->vv_sintheta * push;
	pl->bondshotspeed.z += pl->vv_costheta * push;
	return 1;
}

// pd.player_push(mag): shove the player along their facing (positive =
// forward, negative = backward). The banana-peel knockback without the
// squat/pitch.
s32 chraiLuaPlayerPush(f32 mag)
{
	struct player *pl;

	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	pl = g_Vars.currentplayer;
	pl->bondshotspeed.x += -pl->vv_sintheta * mag;
	pl->bondshotspeed.z += pl->vv_costheta * mag;
	return 1;
}

// pd.player_crouch(): 0 stand / 1 duck / 2 squat. The engine's CROUCHPOS_*
// run the OTHER way (SQUAT=0, DUCK=1, STAND=2), so remap to the documented
// low-is-standing order.
s32 chraiLuaPlayerCrouch(void)
{
	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	return CROUCHPOS_STAND - g_Vars.currentplayer->crouchpos;
}

// pd.player_movespeed(): the local player's current normalised move speed
// (0..~1), max of the forward/strafe components.
f32 chraiLuaPlayerMoveSpeed(void)
{
	f32 f, s;

	if (apLuaPlayerChr() == NULL) {
		return 0.0f;
	}
	f = g_Vars.currentplayer->speedforwards;
	s = g_Vars.currentplayer->speedstrafe;
	if (f < 0.0f) f = -f;
	if (s < 0.0f) s = -s;
	return (f > s) ? f : s;
}

// pd.player_reloading(): true while either hand is in the reload state.
s32 chraiLuaPlayerReloading(void)
{
	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	return (g_Vars.currentplayer->hands[HAND_RIGHT].state == HANDSTATE_RELOAD
			|| g_Vars.currentplayer->hands[HAND_LEFT].state == HANDSTATE_RELOAD) ? 1 : 0;
}

// pd.player_activate(): true this frame if the use/activate button is held
// (opening a door / interacting). Mirrors lv.c's JO_ACTION_ACTIVATE test.
s32 chraiLuaPlayerActivate(void)
{
	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	return (g_Vars.currentplayer->bondactivateorreload & JO_ACTION_ACTIVATE) ? 1 : 0;
}

// pd.player_speed(mult): scale the local player's real walk + strafe speed
// ("Gotta go fast"). 1 = normal. Consumed in bwalkApplyMoveData.
s32 chraiLuaPlayerSpeed(f32 mult)
{
	if (!(mult >= 0.1f)) mult = 0.1f; // also catches NaN
	if (mult > 5.0f) mult = 5.0f;
	g_ChaosPlayerSpeed = mult;
	return 1;
}

// pd.ice_floor(accel [, decel]): "Ice Floor" — scale the walk accel/decel
// (bondwalk.c). <1 = slow to start, slow to stop (slippery). 1 = normal.
s32 chraiLuaIceFloor(f32 accel, f32 decel)
{
	// decel < 0 = "same as accel", so a one-argument call still means "scale
	// grip in both directions by this".
	if (decel < 0.0f) {
		decel = accel;
	}

	if (!(accel >= 0.02f)) accel = 0.02f; // also catches NaN
	if (accel > 4.0f) accel = 4.0f;
	if (!(decel >= 0.02f)) decel = 0.02f;
	if (decel > 4.0f) decel = 4.0f;

	g_ChaosIceAccel = accel;
	g_ChaosIceDecel = decel;
	return 1;
}

// pd.player_freeze(on): root the local player in place (bondmove.c).
s32 chraiLuaPlayerFreeze(s32 on)
{
	g_ChaosPlayerFreeze = on ? 1 : 0;
	return 1;
}

// pd.forced_march(on): the movement stick is pinned full forward
// (bondmove.c g_ChaosForcedMarch).
s32 chraiLuaForcedMarch(s32 on)
{
	g_ChaosForcedMarch = on ? 1 : 0;
	return 1;
}

// pd.forced_crouch(on): "Permacrouch" — the stance is pinned to the LOWEST
// crouch, CROUCHPOS_SQUAT (bondmove.c g_ChaosForcedCrouch).
s32 chraiLuaForcedCrouch(s32 on)
{
	g_ChaosForcedCrouch = on ? 1 : 0;
	return 1;
}

// pd.gormless(on): EVERYTHING backwards — movement (both sticks, dpad,
// keyboard steps), look (stick + mouse), and the fire/aim buttons swapped,
// all at the bmoveProcessInput chokepoints.
s32 chraiLuaGormless(s32 on)
{
	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	g_ChaosGormless = on ? 1 : 0;
	return 1;
}

// pd.time_stop(on): SUPERHOT — a literal time stop. While set, lvTick
// freezes the game tick (lvupdate240 = 0, the pause mechanism, so
// chrs/projectiles/everything hold still) whenever the player gives no
// input; any input lets frames tick.
s32 chraiLuaTimeStop(s32 on)
{
	g_ChaosTimeStop = on ? 1 : 0;
	return 1;
}

// ---------------------------------------------------------------------------
// Teleports
// ---------------------------------------------------------------------------

// pd.teleport_to_chr(chrnum): snap the local player next to a chr.
//
// The solo first-person player has no chr model (pl->model == NULL), and
// chrSetPos/chrMoveToPos dereference it, so for that case the prop is
// relocated directly.
s32 chraiLuaTeleportToChr(s32 chrnum)
{
	struct chrdata *pl = apLuaPlayerChr();
	struct chrdata *chr = (chrnum < 0) ? NULL : chrFindByLiteralId(chrnum);
	struct coord target;
	RoomNum rooms[8];
	f32 dx, dz, len, dist, angle;

	if (pl == NULL || pl->prop == NULL) {
		return 0;
	}
	if (chr == NULL || chr->prop == NULL || chr->prop == pl->prop) {
		return 0;
	}

	// Ideal landing spot: beside the target, not inside it — offset
	// horizontally by the two radii plus a margin, along the direction the
	// player is coming from (so they arrive on their own side, roughly facing
	// the target). Degenerate overlap falls back to a fixed direction.
	target = chr->prop->pos;
	dx = pl->prop->pos.x - chr->prop->pos.x;
	dz = pl->prop->pos.z - chr->prop->pos.z;
	len = sqrtf(dx * dx + dz * dz);
	dist = pl->radius + chr->radius + 30.0f;
	if (len > 0.001f) {
		target.x += dx / len * dist;
		target.z += dz / len * dist;
		angle = atan2f(dx, dz);
	} else {
		target.x += dist;
		angle = 0;
	}

	// Validate the spot against walls AND physics objects: chrAdjustPosForSpawn
	// tests CDTYPE_ALL at the ideal point and, if it collides, nudges through
	// a ring of 8 directions looking for a clear one. If nothing is safe,
	// reject (return 0) so the Lua side retries another chr. Landing exactly
	// on the NPC's own position embedded the player in furniture (sitting
	// guards legitimately intersect their desks).
	roomsCopy(chr->prop->rooms, rooms);
#if VERSION >= VERSION_NTSC_1_0
	if (!chrAdjustPosForSpawn(pl->radius, &target, rooms, angle, true, false, false)) {
#else
	if (!chrAdjustPosForSpawn(pl->radius, &target, rooms, angle, true, false)) {
#endif
		return 0;
	}

	// OOB guard: require a real floor under the final spot before committing.
	// A ring nudge can pass the point-collision test on the far side of a
	// thin wall or over void. Probe with copies — reject, don't mutate.
	{
		struct coord probe = target;
		RoomNum proberooms[8];
		f32 floory;
		u16 floorcol;
		s32 floorroom;

		roomsCopy(rooms, proberooms);
#if VERSION >= VERSION_NTSC_1_0
		floorroom = cdFindFloorRoomYColourFlagsAtPos(&probe, proberooms, &floory, &floorcol, NULL);
#else
		floorroom = cdFindFloorRoomYColourFlagsAtPos(&probe, proberooms, &floory, &floorcol);
#endif
		if (floorroom <= 0) {
			return 0;
		}
	}

	if (pl->model != NULL) {
		return luaChrSetPos(pl, &target, rooms, chrGetRotY(pl), true) ? 1 : 0;
	}

	// Model-less first-person player: move the prop by hand.
	luaPlayerPropMove(pl, &target, rooms, true);
	return 1;
}

// pd.mark_home() / pd.warp_home(): record the player's position, then
// teleport back to it. lvReset clears g_ChaosLuaHomeValid so a home marked on
// the previous stage is never used.
static struct coord g_ChaosLuaHome;
static RoomNum g_ChaosLuaHomeRooms[8];

s32 chraiLuaMarkHome(void)
{
	if (g_Vars.currentplayer == NULL || g_Vars.currentplayer->prop == NULL) {
		return 0;
	}
	g_ChaosLuaHome = g_Vars.currentplayer->prop->pos;
	roomsCopy(g_Vars.currentplayer->prop->rooms, g_ChaosLuaHomeRooms);
	g_ChaosLuaHomeValid = 1;
	return 1;
}

s32 chraiLuaWarpHome(void)
{
	if (!g_ChaosLuaHomeValid
			|| g_Vars.currentplayer == NULL || g_Vars.currentplayer->prop == NULL) {
		return 0;
	}
	luaPlayerWarp(&g_ChaosLuaHome, g_ChaosLuaHomeRooms);
	return 1;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

// pd.button_block(mask): named pad buttons vanish from gameplay input
// (bondmove.c c1buttons strip; menus read the joy layer directly and are
// unaffected). mask 0 = off.
s32 chraiLuaButtonMask(u32 mask)
{
	g_ChaosButtonMask = mask;
	return 1;
}

// pd.invert_look(on): flip vertical look (bondmove.c toggles
// movedata.invertpitch, so it rides the player's own setting on every look
// path).
s32 chraiLuaInvertLook(s32 on)
{
	g_ChaosInvertLook = on ? 1 : 0;
	return 1;
}

// pd.input_delay(frames): "Stadia Mode" — every pad read (buttons, kbm keys,
// sticks) and the mouse look delta are served N frames late (input.c ring).
// 0 = off.
s32 chraiLuaInputDelay(s32 frames)
{
	inputSetChaosInputDelay(frames);
	return 1;
}

// pd.sens_boost(mult): "Overly sensitive" — multiply the user's mouse + stick
// sensitivity at the input read sites. Deliberately NOT a write to the
// config-backed sliders: quitting mid-effect must not persist a maxed
// sensitivity to pd.ini. 1 (or no arg) restores.
s32 chraiLuaSensBoost(f32 mult)
{
	if (isnan(mult)) {
		mult = 1.0f;
	}
	inputSetChaosSensMult(mult);
	return 1;
}

// pd.deadzone(frac): analog deadzone floor, 0..1 of full deflection
// (input.c inputAxisScale). 0 = off.
s32 chraiLuaDeadzone(f32 frac)
{
	if (!(frac >= 0)) { // also catches NaN
		frac = 0;
	}
	if (frac > 0.95f) {
		frac = 0.95f;
	}
	inputSetChaosDeadzone((s32)(frac * 32768.0f));
	return 1;
}
