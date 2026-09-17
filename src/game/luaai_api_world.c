/**
 * pd.* API, world group: doors, environment, fog, weather, gas, rooms, props,
 * alarm, objectives, and audio (sound, music, SFX remaps, external files,
 * DSP).
 *
 * From the Perfect Dark Kai fork (be46717), where the whole API was one file,
 * src/game/luaai_api.c. The bindings and their doc comments are Kai's; the
 * behaviour lives in the chraiLua* bridges (luaai_bridge_world.c) and the
 * engine code they drive. The core registers this group through
 * luaApiRegisterWorld.
 */

#include <ultra64.h>
#include "types.h"
#include "luaai_api_internal.h"

/* pd.sound(sfxnum) -> bool. One-shot local sound (announcer stingers). */
static int l_pd_sound(lua_State *L)
{
	lua_pushboolean(L, chraiLuaPlaySound((s32)luaL_checkinteger(L, 1)) != 0);
	return 1;
}

/* pd.metronome_click() -> bool. A short click at half the music volume (Beat
 * game metronome). */
static int l_pd_metronome_click(lua_State *L)
{
	lua_pushboolean(L, chraiLuaMetronomeClick() != 0);
	return 1;
}

/* pd.door_traps(on) -> bool. Booby-trapped doors: any door that starts
 * opening detonates. */
static int l_pd_door_traps(lua_State *L)
{
	lua_pushboolean(L, chraiLuaDoorTraps(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.door_opens() -> int. Doors opened this stage (task sensor). */
static int l_pd_door_opens(lua_State *L)
{
	lua_pushinteger(L, (lua_Integer)chraiLuaDoorOpens());
	return 1;
}

/* pd.env_colours(skyr,skyg,skyb, cloudr,cloudg,cloudb) -> bool. Override the
 * stage sky + cloud colours (0-255 each). pd.env() restores. */
static int l_pd_env_colours(lua_State *L)
{
	lua_pushboolean(L, chraiLuaEnvColours(
			(s32)luaL_checkinteger(L, 1), (s32)luaL_checkinteger(L, 2),
			(s32)luaL_checkinteger(L, 3), (s32)luaL_checkinteger(L, 4),
			(s32)luaL_checkinteger(L, 5), (s32)luaL_checkinteger(L, 6)) != 0);
	return 1;
}

/* pd.alarm(on) -> bool. Stage alarm on/off. */
static int l_pd_alarm(lua_State *L)
{
	lua_pushboolean(L, chraiLuaSetAlarm(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.nitro(on) -> bool. Every destroyed object explodes like the Crash Site
 * ship. */
static int l_pd_nitro(lua_State *L)
{
	lua_pushboolean(L, chraiLuaNitro(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.objective_force(index, state) -> bool. state 0 = real status,
 * 1 = force INCOMPLETE, 2 = force COMPLETE. index -1 clears all. */
static int l_pd_objective_force(lua_State *L)
{
	lua_pushboolean(L, chraiLuaObjectiveForce(
			(s32)luaL_optinteger(L, 1, -1),
			(s32)luaL_optinteger(L, 2, 0)) != 0);
	return 1;
}

/* pd.env(stagenum) -> bool. Apply another stage's sky/fog/cloud environment;
 * pd.env() restores the current stage's own. */
static int l_pd_env(lua_State *L)
{
	lua_pushboolean(L, chraiLuaEnv((s32)luaL_optinteger(L, 1, -1)) != 0);
	return 1;
}

/* pd.fog(fogmin, fogmax, r, g, b) -> bool. Custom fog overlay: fogmin/fogmax
 * are per-mille of the z-range (stock stages ~950..1050, lower = closer wall);
 * r,g,b = the fog/sky colour. pd.fog() restores the stage's environment. */
static int l_pd_fog(lua_State *L)
{
	if (lua_gettop(L) == 0) {
		lua_pushboolean(L, chraiLuaEnv(-1) != 0);
		return 1;
	}
	lua_pushboolean(L, chraiLuaFog(
			(s32)luaL_checkinteger(L, 1),
			(s32)luaL_checkinteger(L, 2),
			(s32)luaL_optinteger(L, 3, 200),
			(s32)luaL_optinteger(L, 4, 200),
			(s32)luaL_optinteger(L, 5, 210)) != 0);
	return 1;
}

/* pd.items_shuffle() -> int. Shuffle every loose weapon pickup's position;
 * returns how many moved. */
static int l_pd_items_shuffle(lua_State *L)
{
	lua_pushinteger(L, chraiLuaItemsShuffle());
	return 1;
}

/* pd.mute(on) -> bool. Master audio mute (SFX + music). */
static int l_pd_mute(lua_State *L)
{
	lua_pushboolean(L, chraiLuaMute(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.play_file(path, [loop], [follow_music]) -> id | false. Play an external
 * WAV/MP3 (e.g. scripts/chaos/sounds/ring.wav) through the device stream.
 * follow_music makes the track pause with the game. Every external sound is
 * scaled by the music slider and pd.ext_volume. The path may use fs.c's $B/,
 * $S/, $M/ prefixes; a bare relative path is looked up under the mod and base
 * dirs, then the working directory. */
static int l_pd_play_file(lua_State *L)
{
	const char *path = luaL_checkstring(L, 1);
	s32 loop = lua_toboolean(L, 2);         /* pd.play_file(path, loop) */
	s32 followMusic = lua_toboolean(L, 3);  /* pd.play_file(path, loop, follow_music) */
	s32 voice = chraiLuaPlayFile(path, loop, followMusic);

	/* Returns the VOICE ID on success, for pd.stop_file(id). Failure must stay
	 * FALSE and never 0: 0 is truthy in Lua, and callers chain fallbacks
	 * as `play_file(a.wav) or play_file(a.mp3)`. */
	if (voice > 0) {
		lua_pushinteger(L, voice);
	} else {
		lua_pushboolean(L, 0);
	}
	return 1;
}

/* pd.stop_file([id]). Stop external sound started by pd.play_file. With the id
 * play_file returned, stops ONLY that voice — use this for a looping sound, or
 * the no-id form will silence every other external sound too. No arg = stop
 * all. */
static int l_pd_stop_file(lua_State *L)
{
	chraiLuaStopFile((s32)luaL_optinteger(L, 1, 0));
	return 0;
}

/* pd.ext_volume([pct]) -> pct. Volume of all external sounds as a % of the
 * music slider (the slider is the ceiling). With arg: set 0..100 (persisted
 * as Audio.ExtVolume). Always returns the current value. */
static int l_pd_ext_volume(lua_State *L)
{
	s32 pct = (s32)luaL_optinteger(L, 1, -1);
	lua_pushinteger(L, chraiLuaExtVolume(pct));
	return 1;
}

/* pd.music_rate(mult) -> bool. Scale the sequenced music's TEMPO: 1 = normal,
 * 2 = double speed. Real tempo, so pd.music_bpm reports it and BPM mode follows —
 * unlike pd.audio_pitch, which shifts pitch at constant tempo. */
static int l_pd_music_rate(lua_State *L)
{
	lua_pushboolean(L, chraiLuaMusicRate(luaApiOptNum(L, 1, 1.0f)) != 0);
	return 1;
}

/* pd.haunt(force) -> count. Hurl up to a few LOS-visible props at the player. */
static int l_pd_haunt(lua_State *L)
{
	lua_pushinteger(L, chraiLuaHaunt(luaApiOptNum(L, 1, 200.0f)));
	return 1;
}

/* pd.music_bpm() -> number. Tempo of the current sequenced music track in
 * beats/min, or 0 if none is playing (menu, or an external pd.play_file track). */
static int l_pd_music_bpm(lua_State *L)
{
	lua_pushnumber(L, chraiLuaMusicBpm());
	return 1;
}

/* pd.music_beat() -> number | nil. Position within the current beat as [0,1)
 * (0 = on the beat); nil if no sequenced track is playing. */
static int l_pd_music_beat(lua_State *L)
{
	f32 phase = chraiLuaMusicBeat();
	if (phase < 0.0f) {
		lua_pushnil(L);
	} else {
		lua_pushnumber(L, phase);
	}
	return 1;
}

/* pd.audio_crush(step, bits) -> bool. Crunch all audio: sample-and-hold every
 * `step`th output frame (device rate 22 kHz / step) masked to `bits` bit
 * depth. pd.audio_crush() restores clean audio. */
static int l_pd_audio_crush(lua_State *L)
{
	lua_pushboolean(L, chraiLuaAudioCrush(
			(s32)luaL_optinteger(L, 1, 1),
			(s32)luaL_optinteger(L, 2, 16)) != 0);
	return 1;
}

/* pd.audio_radio(on) -> bool. AM-radio voicing: ~400..2800Hz bandpass +
 * overdrive on everything. */
static int l_pd_audio_radio(lua_State *L)
{
	lua_pushboolean(L, chraiLuaAudioRadio(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.audio_reverb(wet) -> bool. Cathedral reverb wash, wet 0..1;
 * pd.audio_reverb() turns it off. */
static int l_pd_audio_reverb(lua_State *L)
{
	/* a wet/dry fraction */
	lua_pushboolean(L, chraiLuaAudioReverb(luaApiOptNumR(L, 1, 0.0f, 0.0f, 1.0f)) != 0);
	return 1;
}

/* pd.audio_reverse(on) -> bool. All audio plays backwards in ~0.74s
 * granules (with that much latency). */
static int l_pd_audio_reverse(lua_State *L)
{
	lua_pushboolean(L, chraiLuaAudioReverse(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.audio_pitch(rate) -> bool. Pitch shift at constant tempo: 1.5 =
 * helium, 0.65 = demon. pd.audio_pitch() restores normal pitch. */
static int l_pd_audio_pitch(lua_State *L)
{
	/* a resample ratio: 0 would divide by zero in the shifter, 1 = off */
	lua_pushboolean(L, chraiLuaAudioPitch(luaApiOptNumR(L, 1, 1.0f, 0.05f, 8.0f)) != 0);
	return 1;
}

/* pd.weather(type, intensity) -> bool. 0 = off, 1 = rain, 2 = snow — on any
 * stage (unconfigured stages rain indoors too; that's the joke). */
static int l_pd_weather(lua_State *L)
{
	s32 type = (s32)luaL_optinteger(L, 1, 0);
	s32 intensity = (s32)luaL_optinteger(L, 2, 2);
	lua_pushboolean(L, chraiLuaWeather(type, intensity) != 0);
	return 1;
}

/* pd.gas(on) -> bool. The Investigation nerve gas anywhere: green env wash,
 * coughing, positional hiss, periodic damage. */
static int l_pd_gas(lua_State *L)
{
	lua_pushboolean(L, chraiLuaGas(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.room_tint(r, g, b) -> bool. Tint every room's lighting (0..255 per
 * channel). pd.room_tint() with no args turns the tint off. */
static int l_pd_room_tint(lua_State *L)
{
	if (lua_gettop(L) == 0 || lua_isnil(L, 1)) {
		lua_pushboolean(L, chraiLuaRoomTint(255, 255, 255, 0) != 0);
		return 1;
	}
	lua_pushboolean(L, chraiLuaRoomTint(
			(s32)luaL_checkinteger(L, 1),
			(s32)luaL_checkinteger(L, 2),
			(s32)luaL_checkinteger(L, 3), 1) != 0);
	return 1;
}

/* pd.room_highlight(room, r, g, b) -> bool. Mark ONE room in a colour, the
 * KotH hill-green mechanism (sets the room's lightop to LIGHTOP_HIGHLIGHT and
 * overrides the colour the reshade would use). pd.room_highlight() with no args
 * clears every chaos highlight and restores the rooms' original lightops. */
static int l_pd_room_highlight(lua_State *L)
{
	if (lua_gettop(L) == 0 || lua_isnil(L, 1)) {
		lua_pushboolean(L, chraiLuaRoomHighlight(0, 0, 0, 0, 0) != 0);
		return 1;
	}
	lua_pushboolean(L, chraiLuaRoomHighlight(
			(s32)luaL_checkinteger(L, 1),
			(s32)luaL_optinteger(L, 2, 255),
			(s32)luaL_optinteger(L, 3, 64),
			(s32)luaL_optinteger(L, 4, 64), 1) != 0);
	return 1;
}

/* pd.gust([force]) -> bool. Shove everything in one random direction. */
static int l_pd_gust(lua_State *L)
{
	f32 force = luaApiOptNum(L, 1, 150.0f);
	lua_pushboolean(L, chraiLuaGust(force) != 0);
	return 1;
}

/* pd.rubber_objects(on) -> bool. Rubber Objects: items dropped into the world
 * while this is on bounce like rubber instead of settling. Props already lying
 * on the floor are unaffected. */
static int l_pd_rubber_objects(lua_State *L)
{
	lua_pushboolean(L, chraiLuaRubberObjects(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.song(slot [, frac]) / pd.song() -> bool. Play an unlocked Combat Sim
 * track over the stage music (slot wraps into range), frac > 0 starting that
 * far into it; no arg stops it. */
static int l_pd_song(lua_State *L)
{
	s32 slot = (s32)luaL_optinteger(L, 1, -1);
	f32 frac = luaApiOptNum(L, 2, 0.0f);
	lua_pushboolean(L, chraiLuaPlaySong(slot, frac) != 0);
	return 1;
}

/* pd.stage_music(on) -> bool. Stop (on=false) or restart (on=true) the current
 * stage's music, e.g. to play an external file underneath instead. */
static int l_pd_stage_music(lua_State *L)
{
	lua_pushboolean(L, chraiLuaStageMusic(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.doors_all(open) -> count. Open (true) / close (false) every door. */
static int l_pd_doors_all(lua_State *L)
{
	lua_pushinteger(L, chraiLuaDoorsAll(lua_toboolean(L, 1)));
	return 1;
}

/* pd.doors_speeds(on) -> count. Give every door its own random open/close speed
 * (per-door accel + maxspeed). Restores the authored values when turned off. */
static int l_pd_doors_speeds(lua_State *L)
{
	lua_pushinteger(L, chraiLuaDoorsSpeeds(lua_toboolean(L, 1)));
	return 1;
}

/* pd.doors_shuffle([pct]) -> count. Each door independently has a pct% chance of
 * being told to open or close (50/50) right now — call on a timer for irregular
 * per-door rhythm instead of the whole level moving in unison. */
static int l_pd_doors_shuffle(lua_State *L)
{
	lua_pushinteger(L, chraiLuaDoorsShuffle((s32)luaL_optinteger(L, 1, 30)));
	return 1;
}

/* pd.doors_hold(on) -> count. Hold every door OPEN for as long as it is set
 * (OBJFLAG_DOOR_KEEPOPEN), instead of pd.doors_all's one-shot request. Turning it
 * off restores ONLY the doors this call changed, so mission doors that were
 * already propped open stay that way. */
static int l_pd_doors_hold(lua_State *L)
{
	lua_pushinteger(L, chraiLuaDoorsHold(lua_toboolean(L, 1)));
	return 1;
}

/* pd.doors_lock(on) -> count. Lockdown: lock (true) / unlock (false) every door. */
static int l_pd_doors_lock(lua_State *L)
{
	lua_pushinteger(L, chraiLuaDoorsLock(lua_toboolean(L, 1)));
	return 1;
}

/* pd.sfx_shuffle(on) -> bool. Every SFX plays as a random other SFX. */
static int l_pd_sfx_shuffle(lua_State *L)
{
	lua_pushboolean(L, chraiLuaSfxShuffle(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.sfx_replace(from, to) -> bool. Play sound id `from` as `to`; no args
 * clears. */
static int l_pd_sfx_replace(lua_State *L)
{
	s32 from = (s32)luaL_optinteger(L, 1, -1);
	s32 to = (s32)luaL_optinteger(L, 2, -1);
	lua_pushboolean(L, chraiLuaSfxReplace(from, to) != 0);
	return 1;
}

/* pd.instrument_shuffle(on) -> bool. Random instruments on program change. */
static int l_pd_instrument_shuffle(lua_State *L)
{
	lua_pushboolean(L, chraiLuaInstrumentShuffle(lua_toboolean(L, 1)) != 0);
	return 1;
}

static const luaL_Reg g_LuaApiWorld[] = {
	/* world */
	{ "door_traps",         l_pd_door_traps },
	{ "door_opens",         l_pd_door_opens },
	{ "env_colours",        l_pd_env_colours },
	{ "alarm",              l_pd_alarm },
	{ "nitro",              l_pd_nitro },
	{ "objective_force",    l_pd_objective_force },
	{ "env",                l_pd_env },
	{ "fog",                l_pd_fog },
	{ "items_shuffle",      l_pd_items_shuffle },
	{ "haunt",              l_pd_haunt },
	{ "weather",            l_pd_weather },
	{ "gas",                l_pd_gas },
	{ "room_tint",          l_pd_room_tint },
	{ "room_highlight",     l_pd_room_highlight },
	{ "gust",               l_pd_gust },
	{ "rubber_objects",     l_pd_rubber_objects },
	{ "doors_all",          l_pd_doors_all },
	{ "doors_lock",         l_pd_doors_lock },
	{ "doors_hold",         l_pd_doors_hold },
	{ "doors_speeds",       l_pd_doors_speeds },
	{ "doors_shuffle",      l_pd_doors_shuffle },
	/* audio */
	{ "sound",              l_pd_sound },
	{ "metronome_click",    l_pd_metronome_click },
	{ "mute",               l_pd_mute },
	{ "play_file",          l_pd_play_file },
	{ "stop_file",          l_pd_stop_file },
	{ "ext_volume",         l_pd_ext_volume },
	{ "song",               l_pd_song },
	{ "stage_music",        l_pd_stage_music },
	{ "sfx_shuffle",        l_pd_sfx_shuffle },
	{ "sfx_replace",        l_pd_sfx_replace },
	{ "instrument_shuffle", l_pd_instrument_shuffle },
	{ "music_bpm",          l_pd_music_bpm },
	{ "music_rate",         l_pd_music_rate },
	{ "music_beat",         l_pd_music_beat },
	{ "audio_crush",        l_pd_audio_crush },
	{ "audio_radio",        l_pd_audio_radio },
	{ "audio_reverb",       l_pd_audio_reverb },
	{ "audio_reverse",      l_pd_audio_reverse },
	{ "audio_pitch",        l_pd_audio_pitch },
	{ NULL, NULL },
};

void luaApiRegisterWorld(lua_State *L)
{
	luaL_setfuncs(L, g_LuaApiWorld, 0);
}
