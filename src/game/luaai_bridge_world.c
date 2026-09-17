/**
 * chraiLua* bridges for the world group of the pd.* API: doors, environment,
 * fog, weather, gas, rooms, props, objectives, and the audio calls (sound,
 * music, SFX remaps, external files, DSP).
 *
 * From the Perfect Dark Kai fork (be46717), where the bridges sat in
 * src/game/chraction.c. The comments are Kai's unless they say otherwise.
 * Kai's net client tests are gone: this build has no netplay, so every
 * "server-side only" bridge simply runs.
 */

#include <ultra64.h>
#include <math.h>
#include "constants.h"
#include "types.h"
#include "bss.h"
#include "data.h"
#include "audio.h"
#include "game/chaosstate.h"
#include "game/chraction.h"
#include "game/env.h"
#include "game/file.h"
#include "game/music.h"
#include "game/mplayer/mplayer.h"
#include "game/dlights.h"
#include "game/options.h"
#include "game/propobj.h"
#include "game/weather.h"
#include "lib/collision.h"
#include "lib/music.h"
#include "lib/rng.h"
#include "lib/snd.h"
#include "luaai_api_internal.h"

// Script forces end up in velocities the engine keeps integrating, where a NaN
// or inf never goes away. Refuse those and cap the rest.
static s32 luaForceOk(f32 *force)
{
	if (!isfinite(*force)) {
		return 0;
	}
	if (*force > 1000.0f) {
		*force = 1000.0f;
	} else if (*force < -1000.0f) {
		*force = -1000.0f;
	}
	return 1;
}

// pd.sound(sfxnum): play a one-shot sound locally (announcer stingers etc).
// sfxnum is a packed sound number, 0..0xffff (0x8000 set = audio config id).
// sndStart trusts its argument, so a script id is checked first: a config id
// must be inside g_AudioRussMappings, and an MP3 must name a real file. A
// config that maps to an MP3 is started by file number, because sndStartMp3
// would read the mapped value's high bits as another config id.
s32 chraiLuaPlaySound(s32 sfxnum)
{
	union soundnumhack req;

	if (sfxnum < 0 || sfxnum > 0xffff) {
		return 0;
	}

	req.packed = (s16)(u16)sfxnum;

	if (req.hasconfig) {
		union soundnumhack mapped;
		s32 sound = sndGetRussMappingSound(req.confignum);

		if (sound < 0) {
			return 0;
		}

		mapped.packed = (s16)(u16)sound;

		if (sndIsMp3(mapped.packed)) {
			if (fileGetRomSize(mapped.id) <= 0) {
				return 0;
			}

			sndStartMp3ByFilenum(mapped.id);
			return 1;
		}
	} else if (sndIsMp3(req.packed) && fileGetRomSize(req.id) <= 0) {
		return 0;
	}

	sndStart(var80095200, req.packed, NULL, -1, -1, -1, -1, -1);
	return 1;
}

// pd.metronome_click(): a short click for the "Beat game" metronome, played at
// HALF the in-game music volume. optionsGetMusicVolume is the music-slider level
// (0..0x5000); map it onto the SFX full-volume scale (0..0x7fff = AL_VOL_FULL) and
// halve, so at max music the click is half of full SFX (clearly audible) and it
// scales down with the player's music setting.
s32 chraiLuaMetronomeClick(void)
{
	s32 vol = (s32)optionsGetMusicVolume() * 0x7fff / 0x5000 / 2;

	sndStart(var80095200, (s16)SFX_MENU_FOCUS, NULL, vol, -1, -1, -1, -1);
	return 1;
}

// pd.door_traps(on): booby-trapped doors — any door that starts opening
// detonates (the doorSetMode hook in propobj.c). g_ChaosDoorOpenCount is
// bumped there unconditionally so pd.door_opens() can be a task sensor
// ("open a door" requirements) even with traps off.
s32 chraiLuaDoorTraps(s32 on)
{
	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	g_ChaosDoorTraps = on ? 1 : 0;
	return 1;
}

u32 chraiLuaDoorOpens(void)
{
	return g_ChaosDoorOpenCount;
}

// pd.env_colours(skyr,skyg,skyb, cloudr,cloudg,cloudb): override the current
// stage environment's sky + cloud colours in place (random bright skies).
// Restore = pd.env() — chraiLuaEnv(-1) re-applies the stage's authored
// environment wholesale, colours included.
s32 chraiLuaEnvColours(s32 sr, s32 sg, s32 sb, s32 cr, s32 cg, s32 cb)
{
	struct environment *env = envGetCurrent();

	if (apLuaPlayerChr() == NULL || env == NULL) {
		return 0;
	}
	env->sky_r = (u8)(sr < 0 ? 0 : sr > 255 ? 255 : sr);
	env->sky_g = (u8)(sg < 0 ? 0 : sg > 255 ? 255 : sg);
	env->sky_b = (u8)(sb < 0 ? 0 : sb > 255 ? 255 : sb);
	env->clouds_r = (cr < 0 ? 0 : cr > 255 ? 255 : cr) * (1.0f / 255.0f);
	env->clouds_g = (cg < 0 ? 0 : cg > 255 ? 255 : cg) * (1.0f / 255.0f);
	env->clouds_b = (cb < 0 ? 0 : cb > 255 ? 255 : cb) * (1.0f / 255.0f);
	return 1;
}

// pd.music_rate(mult): scale the sequenced music's TEMPO (lib/music.c
// sndChaosSetMusicRate). 1 = normal, 2 = double speed, 0.5 = half. Unlike
// pd.audio_pitch — a granular pitch shift at CONSTANT tempo — this moves the
// sequence player's uspt, so the music genuinely speeds up and slows down AND
// pd.music_bpm/music_beat report the new tempo.
s32 chraiLuaMusicRate(f32 mult)
{
	sndChaosSetMusicRate(mult);
	return 1;
}

// pd.haunt(force): "Paranormal Activity" prop-throw — hurl up to a few pushable
// props that have a clear line of sight to the player AT the player (with a
// slight upward arc). Only the blessed objApplyMomentum mutation is used (it
// never touches the prop list), so it can't corrupt activeprops.
s32 chraiLuaHaunt(f32 force)
{
	struct prop *prop;
	struct prop *plprop;
	s32 thrown = 0;

	if (apLuaPlayerChr() == NULL || !luaForceOk(&force)) {
		return 0;
	}
	plprop = g_Vars.currentplayer->prop;
	if (plprop == NULL) {
		return 0;
	}

	for (prop = g_Vars.activeprops; prop && thrown < 3; prop = prop->next) {
		if ((prop->type == PROPTYPE_OBJ || prop->type == PROPTYPE_WEAPON) && prop->obj) {
			struct defaultobj *obj = prop->obj;
			struct coord speed;
			f32 dx, dy, dz, dist;

			if ((obj->hidden & OBJHFLAG_MOUNTED) || (obj->hidden & OBJHFLAG_GRABBED)
					|| !(obj->flags3 & OBJFLAG3_PUSHABLE)) {
				continue;
			}
			dx = plprop->pos.x - prop->pos.x;
			dy = plprop->pos.y - prop->pos.y;
			dz = plprop->pos.z - prop->pos.z;
			dist = sqrtf(dx * dx + dy * dy + dz * dz);
			if (dist < 1.0f || dist > 2000.0f) {
				continue;
			}
			if (!cdTestLos05(&prop->pos, prop->rooms, &plprop->pos, plprop->rooms,
					CDTYPE_BG, GEOFLAG_BLOCK_SIGHT)) {
				continue;
			}
			speed.x = dx / dist * force * 0.05f;
			speed.y = dy / dist * force * 0.05f + force * 0.02f; // arc up a touch
			speed.z = dz / dist * force * 0.05f;
			objApplyMomentum(obj, &speed, 0.0f, false, true);
			thrown++;
		}
	}
	return thrown;
}

// pd.alarm(on): raise/clear the stage alarm (klaxon + every alarm-conditional
// AI script).
s32 chraiLuaSetAlarm(s32 on)
{
	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	if (on) {
		alarmActivate();
	} else {
		alarmDeactivate();
	}
	return 1;
}

// pd.gust(force): shove the whole map in one random compass direction — every
// living chr (chrYeetFromPos from a virtual point behind them, so they all
// fly the same way), every pushable object (the explosion-knockback gate:
// !MOUNTED && !GRABBED && OBJFLAG3_PUSHABLE -> objApplyMomentum), and the
// local player (bondshotspeed, the shot-knockback velocity bondwalk decays).
s32 chraiLuaGust(f32 force)
{
	struct prop *prop;
	struct coord dir;
	f32 angle;

	if (apLuaPlayerChr() == NULL || !luaForceOk(&force)) {
		return 0;
	}

	angle = RANDOMFRAC() * M_BADTAU;
	dir.x = sinf(angle);
	dir.y = 0;
	dir.z = cosf(angle);

	for (prop = g_Vars.activeprops; prop; prop = prop->next) {
		if (prop->type == PROPTYPE_CHR && prop->chr && prop->chr->model && !chrIsDead(prop->chr)) {
			struct coord from;
			from.x = prop->pos.x - dir.x * 100.0f;
			from.y = prop->pos.y;
			from.z = prop->pos.z - dir.z * 100.0f;
			chrYeetFromPos(prop->chr, &from, force);
		} else if ((prop->type == PROPTYPE_OBJ || prop->type == PROPTYPE_WEAPON) && prop->obj) {
			struct defaultobj *obj = prop->obj;

			if ((obj->hidden & OBJHFLAG_MOUNTED) == 0
					&& (obj->hidden & OBJHFLAG_GRABBED) == 0
					&& (obj->flags3 & OBJFLAG3_PUSHABLE)) {
				struct coord speed;
				speed.x = dir.x * force * 0.05f;
				speed.y = 0;
				speed.z = dir.z * force * 0.05f;
				objApplyMomentum(obj, &speed, 0.0f, true, true);
			}
		}
	}

	g_Vars.currentplayer->bondshotspeed.x += dir.x * force * 0.2f;
	g_Vars.currentplayer->bondshotspeed.z += dir.z * force * 0.2f;
	return 1;
}

// pd.rubber_objects(on): "Rubber Objects" — items dropped into the world
// bounce like rubber instead of settling after the vanilla 6 bounces. The mark
// is stamped in objSetDropped and read in projectileTick, both in propobj.c, so
// props already lying on the floor are untouched. Turning it off settles
// everything on its next contact; also cleared on stage load.
s32 chraiLuaRubberObjects(s32 on)
{
	g_ChaosRubberObjects = on ? 1 : 0;
	return 1;
}

// pd.song(slot [, frac]): play an unlocked Combat Sim track as a menu track
// over the stage music (musicStartTrackAsMenu — the credits-roll mechanism;
// the stage music pauses underneath and resumes when the menu track ends).
// slot is wrapped into the unlocked-track range; no arg / negative stops the
// song. frac > 0 starts the song that far into its length (the seqPlay seek
// latch, snd.c seqSeekToFrac) — only armed when the start will actually be
// queued (not suppressed, not the already-playing menu track), so a stale
// latch can't hijack a later pause-menu track start.
s32 chraiLuaPlaySong(s32 slot, f32 frac)
{
	extern s32 g_MenuTrack; // game/music.c, the menu track now playing
	s32 numtracks;
	s32 tracknum;

	if (slot < 0) {
		musicEndMenu();
		return 1;
	}
	numtracks = mpGetNumUnlockedTracks();
	if (numtracks <= 0) {
		return 0;
	}
	tracknum = mpGetTrackMusicNum(slot % numtracks);
	if (frac > 0.0f && !g_MusicSuppressed && tracknum != g_MenuTrack) {
		if (frac > 0.95f) {
			frac = 0.95f;
		}
		seqSetNextSeek(tracknum, frac);
	}
	musicStartTrackAsMenu(tracknum);
	return 1;
}

// pd.stage_music(on): stop (on=false) or restart (on=true) the CURRENT stage's
// music. Unlike pd.song, which layers a menu track over the paused stage music,
// this genuinely silences the level track — for an effect that plays its own
// external track (pd.play_file) with the mission music killed underneath.
// Restart re-derives primary + ambient from the live stage number.
s32 chraiLuaStageMusic(s32 on)
{
	if (on) {
		// Clear the suppress latch BEFORE restarting so the start paths aren't
		// blocked by their own guard.
		g_MusicSuppressed = 0;
		musicSetStageAndStartMusic(g_Vars.stagenum);
	} else {
		// Latch music off, then stop what's playing. The latch blocks every
		// restart path (musicEndMenu on pause-menu close, NRG combat re-trigger,
		// ambient) for the whole effect, so the game music can't creep back.
		g_MusicSuppressed = 1;
		musicStop();
	}
	return 1;
}

// pd.music_bpm(): tempo of the current sequenced music track in beats/min, or 0
// if no sequenced track is playing (e.g. the menu, or a pd.play_file MP3, which
// carries no tempo).
f32 chraiLuaMusicBpm(void)
{
	f32 bpm = 0.0f;
	sndGetMusicBeat(&bpm, NULL);
	return bpm;
}

// pd.music_beat(): position within the current musical beat as a fraction
// [0, 1) (0 = on the beat), or -1 if no sequenced track is playing.
f32 chraiLuaMusicBeat(void)
{
	f32 phase = -1.0f;
	sndGetMusicBeat(NULL, &phase);
	return phase;
}

// Environment/fog changes only take visual effect on geometry and props as
// they RE-SHADE, which normally happens per room as rooms come on screen —
// so a mid-stage pd.fog/pd.env left already-shaded rooms (and the props/chrs
// standing in them) looking pre-change until you looked away and back. Dirty
// every room so the reshade sweeps the whole stage immediately, on APPLY and
// on RESTORE alike; props/chrs/the gun re-shade off the room dirty (the
// room_tint mechanism).
//
// Kai also flushed its display-list cache here (chraiLuaFogCacheFlush) because
// it records fog into cached room geometry. This build has no such cache, so
// there is nothing to flush.
static void chraiLuaDirtyAllRooms(void)
{
	s32 i;

	// A reset path can call pd.env()/pd.fog() where no stage is loaded:
	// g_Rooms is NULL (MEMPOOL_STAGE alloc) and roomcount can be stale from
	// the previous stage. Nothing to reshade without a stage.
	if (g_Rooms == NULL || g_Vars.roomcount <= 0) {
		return;
	}

	for (i = 1; i < g_Vars.roomcount; i++) {
		g_Rooms[i].flags |= ROOMFLAG_BRIGHTNESS_DIRTY_TEMP;
	}
}

// pd.env(stagenum): apply another stage's sky/fog/cloud environment.
// pd.env() / stagenum -1 restores the current stage's own environment.
s32 chraiLuaEnv(s32 stagenum)
{
	envChooseAndApply(stagenum >= 0 ? stagenum : chraiLuaGetStageNum(), false);
	chraiLuaDirtyAllRooms();
	return 1;
}

// pd.fog(fogmin, fogmax, r, g, b): overlay a custom fog on the current stage
// (env.c envChaosFog — works on no-fog stages too). fogmin/fogmax are
// per-mille of the z-range (stock stages sit around 950..1050; lower = the
// wall starts closer). pd.fog() restores via envChooseAndApply.
s32 chraiLuaFog(s32 fogmin, s32 fogmax, s32 r, s32 g, s32 b)
{
	// The env stores s16s and gSPFogPosition divides by (max - min) every
	// frame, which traps on x86 when they are equal: keep max above min.
	if (fogmin < -32768) {
		fogmin = -32768;
	}
	if (fogmin > 32766) {
		fogmin = 32766;
	}
	if (fogmax > 32767) {
		fogmax = 32767;
	}
	if (fogmax <= fogmin) {
		fogmax = fogmin + 1;
	}

	envChaosFog(chraiLuaGetStageNum(), fogmin, fogmax, (u8)r, (u8)g, (u8)b);
	chraiLuaDirtyAllRooms();
	return 1;
}

// pd.items_shuffle(): every weapon pickup lying on the ground trades places
// with another (propobj.c chaosItemsShuffle). Returns how many moved.
s32 chraiLuaItemsShuffle(void)
{
	return chaosItemsShuffle();
}

// pd.doors_all(open): request every door on the stage to open (1) or close
// (0) — doorsRequestMode, the same call the AI door commands use. Closing is
// transient (walking up re-triggers them); opening everything at once is the
// tactical chaos.
s32 chraiLuaDoorsAll(s32 open)
{
	struct prop *prop;
	s32 n = 0;

	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	for (prop = g_Vars.activeprops; prop; prop = prop->next) {
		if (prop->type == PROPTYPE_DOOR && prop->door) {
			doorsRequestMode(prop->door, open ? DOORMODE_OPENING : DOORMODE_CLOSING);
			n++;
		}
	}
	return n;
}

// pd.doors_speeds(on): "Paranormal Activity" — give every door its OWN open/close
// speed, so they creak and slam at wildly different rates instead of moving in
// lockstep. doorobj carries per-door `accel` and `maxspeed` (consumed by
// applySpeed/applyRotation in doorTick), so this is the engine's own motion model
// rather than anything faked.
//
// Same ownership discipline as doors_hold: the ORIGINAL values are saved per door
// and restored on the way out, because these are authored per level (heavy blast
// doors are deliberately slow) and a level whose doors keep a random speed after
// the effect ends is quietly broken. Restore matches by ADDRESS while re-walking
// activeprops, so a door destroyed mid-effect is never written through.
#define CHAOS_DOORSPD_MAX 256
static struct doorobj *g_ChaosDoorSpdDoor[CHAOS_DOORSPD_MAX];
static f32 g_ChaosDoorSpdAccel[CHAOS_DOORSPD_MAX];
static f32 g_ChaosDoorSpdMax[CHAOS_DOORSPD_MAX];
static s32 g_ChaosDoorSpdCount = 0;

s32 chraiLuaDoorsSpeeds(s32 on)
{
	struct prop *prop;
	s32 n = 0;
	s32 i;

	if (apLuaPlayerChr() == NULL) {
		return 0;
	}

	if (on) {
		if (g_ChaosDoorSpdCount > 0) {
			return 0; // already armed; re-arming would save the randomised values
		}

		for (prop = g_Vars.activeprops; prop; prop = prop->next) {
			if (prop->type == PROPTYPE_DOOR && prop->door
					&& g_ChaosDoorSpdCount < CHAOS_DOORSPD_MAX) {
				struct doorobj *door = prop->door;
				// 0.15x .. 3.0x, rolled per door and per field so a door can be
				// slow to start yet quick once moving (and vice versa).
				f32 am = 0.15f + (rngRandom() % 286) * 0.01f;
				f32 sm = 0.15f + (rngRandom() % 286) * 0.01f;

				g_ChaosDoorSpdDoor[g_ChaosDoorSpdCount] = door;
				g_ChaosDoorSpdAccel[g_ChaosDoorSpdCount] = door->accel;
				g_ChaosDoorSpdMax[g_ChaosDoorSpdCount] = door->maxspeed;
				g_ChaosDoorSpdCount++;

				door->accel *= am;
				door->maxspeed *= sm;
				n++;
			}
		}
		return n;
	}

	for (prop = g_Vars.activeprops; prop; prop = prop->next) {
		if (prop->type == PROPTYPE_DOOR && prop->door) {
			for (i = 0; i < g_ChaosDoorSpdCount; i++) {
				if (g_ChaosDoorSpdDoor[i] == prop->door) {
					prop->door->accel = g_ChaosDoorSpdAccel[i];
					prop->door->maxspeed = g_ChaosDoorSpdMax[i];
					n++;
					break;
				}
			}
		}
	}

	g_ChaosDoorSpdCount = 0;
	return n;
}

// Stage-change reset: forget the saved speeds without touching the pointers (the
// old stage's props are freed; new doors carry their authored values).
void chraiLuaDoorsSpeedsReset(void)
{
	g_ChaosDoorSpdCount = 0;
}

// pd.doors_shuffle(pct): each door INDEPENDENTLY has a pct% chance of being told
// to open (or close, 50/50) right now. Called on a timer, that gives every door
// its own irregular rhythm — the haunted-house feel — rather than the whole level
// slamming in unison like pd.doors_all does.
s32 chraiLuaDoorsShuffle(s32 pct)
{
	struct prop *prop;
	s32 n = 0;

	if (apLuaPlayerChr() == NULL) {
		return 0;
	}

	if (pct < 1) pct = 1;
	if (pct > 100) pct = 100;

	for (prop = g_Vars.activeprops; prop; prop = prop->next) {
		if (prop->type == PROPTYPE_DOOR && prop->door) {
			if ((s32)(rngRandom() % 100) < pct) {
				doorsRequestMode(prop->door,
						(rngRandom() & 1) ? DOORMODE_OPENING : DOORMODE_CLOSING);
				n++;
			}
		}
	}
	return n;
}

// pd.doors_hold(on): "Open sesame" — hold every door OPEN for the duration
// instead of the transient one-shot request of pd.doors_all.
//
// OBJFLAG_DOOR_KEEPOPEN is the engine's own "never auto-close" flag: doorTick's
// autoclose block skips any door carrying it (propobj.c), and missions set it on
// doors they want propped open.
//
// Which is exactly why turning it off again cannot just clear the bit from
// every door: doing that would permanently un-prop the mission's own held-open
// doors and quietly break level design. It is a single shared bit with no owner,
// so we remember the doors WE changed — the ones that did NOT already have it —
// and clear only those. Same discipline as the Lockdown lockbit, which ORs into
// keyflags and clears only its own.
//
// The teardown re-walks activeprops and matches by ADDRESS rather than
// dereferencing the saved pointers: a door can be destroyed mid-effect, and a
// freed doorobj must never be written through.
#define CHAOS_DOORHOLD_MAX 256
static struct doorobj *g_ChaosDoorHeld[CHAOS_DOORHOLD_MAX];
static s32 g_ChaosDoorHeldCount = 0;

s32 chraiLuaDoorsHold(s32 on)
{
	struct prop *prop;
	s32 n = 0;
	s32 i;

	if (apLuaPlayerChr() == NULL) {
		return 0;
	}

	if (on) {
		g_ChaosDoorHeldCount = 0;

		for (prop = g_Vars.activeprops; prop; prop = prop->next) {
			if (prop->type == PROPTYPE_DOOR && prop->door) {
				// Only take ownership of doors that were not already held open,
				// so the restore can put things back exactly as they were.
				if ((prop->door->base.flags & OBJFLAG_DOOR_KEEPOPEN) == 0
						&& g_ChaosDoorHeldCount < CHAOS_DOORHOLD_MAX) {
					g_ChaosDoorHeld[g_ChaosDoorHeldCount++] = prop->door;
					prop->door->base.flags |= OBJFLAG_DOOR_KEEPOPEN;
				}

				doorsRequestMode(prop->door, DOORMODE_OPENING);
				n++;
			}
		}
		return n;
	}

	for (prop = g_Vars.activeprops; prop; prop = prop->next) {
		if (prop->type == PROPTYPE_DOOR && prop->door) {
			for (i = 0; i < g_ChaosDoorHeldCount; i++) {
				if (g_ChaosDoorHeld[i] == prop->door) {
					prop->door->base.flags &= ~OBJFLAG_DOOR_KEEPOPEN;
					// Let them swing shut now rather than waiting for someone to
					// walk past and re-trigger the autoclose timer.
					doorsRequestMode(prop->door, DOORMODE_CLOSING);
					n++;
					break;
				}
			}
		}
	}

	g_ChaosDoorHeldCount = 0;
	return n;
}

// Stage-change reset: forget the held-door list WITHOUT touching any of it. The
// pointers belong to the old stage's MEMPOOL_STAGE props, which are already gone,
// and every new door starts with its own authored flags anyway.
void chraiLuaDoorsHoldReset(void)
{
	g_ChaosDoorHeldCount = 0;
}

// pd.doors_lock(on): "Lockdown" — actually lock every door shut, not just the
// transient close of pd.doors_all. Each locked door gets a fake key flag the
// player can never hold (CHAOS_DOOR_LOCKBIT), so doorIsUnlocked returns false
// for both interaction and auto-open (propobj.c: keyflags==0 gates opening).
// We only OR/clear our own bit, so a door's real mission keyflags are preserved.
// on = lock (set the bit + request close); off = unlock (clear the bit).
#define CHAOS_DOOR_LOCKBIT 0x80000000u
s32 chraiLuaDoorsLock(s32 on)
{
	struct prop *prop;
	s32 n = 0;

	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	for (prop = g_Vars.activeprops; prop; prop = prop->next) {
		if (prop->type == PROPTYPE_DOOR && prop->door) {
			if (on) {
				prop->door->keyflags |= CHAOS_DOOR_LOCKBIT;
				doorsRequestMode(prop->door, DOORMODE_CLOSING);
			} else {
				prop->door->keyflags &= ~CHAOS_DOOR_LOCKBIT;
			}
			n++;
		}
	}
	return n;
}

// pd.sfx_replace(from, to): ONE sound id plays as another (snd.c sndStart);
// no args (both -1) = off.
s32 chraiLuaSfxReplace(s32 from, s32 to)
{
	g_ChaosSfxReplaceFrom = from;
	g_ChaosSfxReplaceTo = to;
	return 1;
}

// pd.sfx_shuffle(on): every one-shot sound effect plays as a random other
// sound (remapped inside sndStart, always to a valid sound-table id). Turning it
// OFF also hard-stops every playing sample sound — a one-shot remapped to a
// looping sound would otherwise loop forever (needing a game restart).
s32 chraiLuaSfxShuffle(s32 on)
{
	g_ChaosSfxShuffle = on ? 1 : 0;
	if (!on) {
		sndStopAll();
	}
	return 1;
}

// pd.instrument_shuffle(on): every MIDI program change picks a random
// instrument from the loaded bank. Applies when a track (re)starts — pair
// with pd.song() to hear it immediately. (g_ChaosInstrumentShuffle is a u8.)
s32 chraiLuaInstrumentShuffle(s32 on)
{
	g_ChaosInstrumentShuffle = on ? 1 : 0;
	return 1;
}

// pd.nitro(on): every destroyed object explodes like the Crash Site ship
// (propobj.c objCheckDestroyed exptype override).
s32 chraiLuaNitro(s32 on)
{
	g_ChaosNitro = on ? 1 : 0;
	return 1;
}

// pd.objective_force(index, state): 0 = off (real status), 1 = force
// INCOMPLETE, 2 = force COMPLETE. index -1 + state 0 clears all
// (objectives.c objectiveCheck override — display AND the all-complete check
// both route through it, so a held-down objective blocks mission end until
// released).
s32 chraiLuaObjectiveForce(s32 index, s32 state)
{
	s32 i;

	if (index < 0) {
		for (i = 0; i < MAX_OBJECTIVES; i++) {
			g_ChaosObjectiveForce[i] = 0;
		}
		return 1;
	}
	if (index >= MAX_OBJECTIVES) {
		return 0;
	}
	g_ChaosObjectiveForce[index] = (u8)(state < 0 ? 0 : (state > 2 ? 2 : state));
	return 1;
}

// pd.mute(on) / pd.play_file(path): port audio layer (port/src/audio.c).
s32 chraiLuaMute(s32 on)
{
	audioSetMuted(on ? 1 : 0);
	return 1;
}

// The path goes through audio.c's resolver: fs.c's $X placeholders, absolute
// and ./ paths as given, bare paths under the mod and base dirs, then the
// working directory (where scripts/init.lua is loaded from).
s32 chraiLuaPlayFile(const char *path, s32 loop, s32 followMusic)
{
	return audioPlayExternal(path, loop, followMusic);
}

// pd.ext_volume([pct]): volume of ALL external sounds as a percentage OF
// the music slider (the slider stays the ceiling). pct < 0 = just read.
// Persists as Audio.ExtVolume in pd.ini.
s32 chraiLuaExtVolume(s32 pct)
{
	if (pct >= 0) {
		audioSetExtVolume(pct);
	}
	return audioGetExtVolume();
}

// pd.stop_file([id]): stop external sound started by pd.play_file. With the
// voice id play_file returned, stops ONLY that voice — required for a LOOPING
// sound, because the no-id form frees the whole pool and would silence every
// other external sound in play. id <= 0 keeps the stop-everything behaviour.
void chraiLuaStopFile(s32 id)
{
	if (id > 0) {
		audioStopExternalVoice(id);
	} else {
		audioStopExternal();
	}
}

// pd.audio_crush(step, bits): sample-and-hold + bit-depth crush on the device
// stream (audio.c push point, the pd.mute mechanism). 1, 16 (or no args) = off.
s32 chraiLuaAudioCrush(s32 step, s32 bits)
{
	audioSetCrush(step, bits);
	return 1;
}

// pd.audio_radio(on): AM-radio voicing (bandpass + overdrive) on all audio.
s32 chraiLuaAudioRadio(s32 on)
{
	audioSetRadio(on ? 1 : 0);
	return 1;
}

// pd.audio_reverb(wet): cathedral reverb wash, wet 0..1; 0/no args = off.
s32 chraiLuaAudioReverb(f32 wet)
{
	audioSetReverb(wet);
	return 1;
}

// pd.audio_reverse(on): everything plays backwards in ~0.74s granules.
s32 chraiLuaAudioReverse(s32 on)
{
	audioSetReverse(on ? 1 : 0);
	return 1;
}

// pd.audio_pitch(rate): pitch shift at constant tempo; 1 (or no args) = off.
s32 chraiLuaAudioPitch(f32 rate)
{
	audioSetPitch(rate);
	return 1;
}

// pd.weather(type, intensity): 0 off / 1 rain / 2 snow, on any stage.
s32 chraiLuaWeather(s32 type, s32 intensity)
{
	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	return weatherChaosSet(type, intensity);
}

// pd.gas(on): the Investigation nerve gas anywhere — green env wash (fog
// stages), coughing, positional hiss, damage every ~3.75s (gasTick).
s32 chraiLuaGas(s32 on)
{
	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	gasChaosSet(on ? 1 : 0);
	return 1;
}

// pd.room_tint(r,g,b) / pd.room_tint(): tint every room's lighting by an RGB
// multiplier (0..255 per channel = 0..1x) — the KotH hill-highlight effect
// applied stage-wide. Dirties all rooms so the reshade re-runs; rooms
// recompute as they come on screen.
s32 chraiLuaRoomTint(s32 r, s32 g, s32 b, s32 on)
{
	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	g_ChaosRoomTintFrac[0] = (r < 0 ? 0 : r > 255 ? 255 : r) * (1.0f / 255.0f);
	g_ChaosRoomTintFrac[1] = (g < 0 ? 0 : g > 255 ? 255 : g) * (1.0f / 255.0f);
	g_ChaosRoomTintFrac[2] = (b < 0 ? 0 : b > 255 ? 255 : b) * (1.0f / 255.0f);
	g_ChaosRoomTintOn = on ? 1 : 0;

	chraiLuaDirtyAllRooms();
	return 1;
}

// pd.room_highlight(room [, r, g, b]) / pd.room_highlight(): mark ONE room in a
// colour — the KotH hill-green mechanism driven from Lua (hot rooms glow red).
// No args = clear every chaos highlight.
//
// Two halves are needed, because the engine's highlight path is scenario-shaped:
//   1. the room's lightop must be LIGHTOP_HIGHLIGHT, which is the ONLY way the
//      reshade enters the highlight branch — so set it here, remembering the
//      previous op per room so clearing puts it back (rooms carry real lightops
//      from their setup: flicker loops, lights-off, transitions);
//   2. the colour normally comes from scenarioHighlightRoom, whose vtable entry
//      is NULL outside Combat Sim — g_ChaosRoomHlMask overrides it at both
//      reshade sites (dlights.c).
// Then dirty the room so the reshade actually re-runs for it.
//
// roomSetLightOp early-returns entirely while CHEAT_PERFECTDARKNESS is active,
// so a highlight requested during "Lights out" silently won't take. That is the
// engine's own rule (the blackout owns every room's lighting) and it self-corrects
// on the next call after the blackout ends.
static u8 g_ChaosRoomHlPrevOp[CHAOS_ROOMHL_MAX];
static u8 g_ChaosRoomHlSaved[CHAOS_ROOMHL_MAX / 8];

s32 chraiLuaRoomHighlight(s32 roomnum, s32 r, s32 g, s32 b, s32 on)
{
	s32 i;

	if (g_Rooms == NULL || g_Vars.roomcount <= 0) {
		return 0;
	}

	if (!on) {
		// Restore every lightop we touched, then drop the whole mask.
		for (i = 1; i < CHAOS_ROOMHL_MAX && i < g_Vars.roomcount; i++) {
			if ((g_ChaosRoomHlSaved[i >> 3] >> (i & 7)) & 1) {
				roomSetLightOp(i, g_ChaosRoomHlPrevOp[i], 0, 0, 0);
				g_Rooms[i].flags |= ROOMFLAG_BRIGHTNESS_DIRTY_TEMP;
			}
		}
		for (i = 0; i < CHAOS_ROOMHL_MAX / 8; i++) {
			g_ChaosRoomHlMask[i] = 0;
			g_ChaosRoomHlSaved[i] = 0;
		}
		g_ChaosRoomHlOn = 0;
		return 1;
	}

	if (roomnum < 1 || roomnum >= CHAOS_ROOMHL_MAX || roomnum >= g_Vars.roomcount) {
		return 0;
	}

	g_ChaosRoomHlCol[0] = r < 0 ? 0 : r > 255 ? 255 : r;
	g_ChaosRoomHlCol[1] = g < 0 ? 0 : g > 255 ? 255 : g;
	g_ChaosRoomHlCol[2] = b < 0 ? 0 : b > 255 ? 255 : b;
	g_ChaosRoomHlOn = 1;

	// Save the original op once per room — re-marking a room already highlighted
	// must not overwrite the saved op with LIGHTOP_HIGHLIGHT.
	if (((g_ChaosRoomHlSaved[roomnum >> 3] >> (roomnum & 7)) & 1) == 0) {
		g_ChaosRoomHlPrevOp[roomnum] = (u8)g_Rooms[roomnum].lightop;
		g_ChaosRoomHlSaved[roomnum >> 3] |= (u8)(1 << (roomnum & 7));
	}

	g_ChaosRoomHlMask[roomnum >> 3] |= (u8)(1 << (roomnum & 7));
	roomSetLightOp(roomnum, LIGHTOP_HIGHLIGHT, 0, 0, 0);
	g_Rooms[roomnum].flags |= ROOMFLAG_BRIGHTNESS_DIRTY_TEMP;
	return 1;
}

// Stage-change reset, called from lvReset. Deliberately does NOT restore
// lightops the way the clear path does: room numbers are per-stage and g_Rooms
// has already been torn down and rebuilt, so the saved ops belong to rooms that
// no longer exist and every new room already carries its authored op. All that
// has to happen is forgetting the mask — otherwise an effect still running at a
// stage change would leave arbitrary rooms in the NEW stage glowing.
void chraiLuaRoomHighlightReset(void)
{
	s32 i;

	for (i = 0; i < CHAOS_ROOMHL_MAX / 8; i++) {
		g_ChaosRoomHlMask[i] = 0;
		g_ChaosRoomHlSaved[i] = 0;
	}
	g_ChaosRoomHlOn = 0;
}
