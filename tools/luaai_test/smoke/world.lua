-- Smoke test for the world pd.* functions (src/game/luaai_api_world.c):
-- doors, environment, fog, weather, gas, rooms, props, alarm, objectives and
-- audio.
-- The pd.* API it exercises is ported from Kai (be46717).
--
-- Run it as scripts/init.lua (relative to the game's working directory) and
-- load a stage. Once a player is live, the "tick" handler calls every world
-- function with sane arguments, holds the effects for a couple of seconds so
-- the engine code behind them runs, then clears them. It logs a PASS or FAIL
-- line per check through pd.log and ends with "world smoke done N/M". Where a
-- setting can be read back from Lua, the check reads it back.
--
-- Optional sound files, used by the pd.play_file checks when present:
--   scripts/world_smoke.wav   (bare relative path: base dir, then working dir)
--   scripts/world_smoke.mp3
--   $S/world_smoke.wav        (the save dir, through fs.c's $S prefix)
-- A missing file makes play_file return false, which the check reports.

local results = { pass = 0, total = 0 }
local seen = {}
local ticks = 0
local live = nil -- tick count when the player was first seen
local phase = 0
local state = {}

local function report(name, ok, detail)
	results.total = results.total + 1
	if ok then
		results.pass = results.pass + 1
	end
	seen[name] = true
	pd.log(string.format("world smoke %s %s%s", ok and "PASS" or "FAIL", name,
		detail ~= nil and (" (" .. tostring(detail) .. ")") or ""))
end

local function check(name, fn)
	local ok, res, detail = pcall(fn)
	if not ok then
		report(name, false, res)
	else
		report(name, res and true or false, detail)
	end
end

local function isint(v)
	return type(v) == "number" and math.type(v) == "integer"
end

-- Phase 1: switch everything on.
local function setall()
	state.stage = pd.stage()
	state.rooms = pd.room_count()

	check("door_traps", function()
		-- traps on and straight off again: no door moves in between
		local on = pd.door_traps(true)
		local off = pd.door_traps(false)
		return on == true and off == true
	end)
	state.opens0 = pd.door_opens()
	check("doors_all", function()
		state.doors = pd.doors_all(true)
		return isint(state.doors) and state.doors > 0, state.doors .. " doors"
	end)
	check("door_opens", function()
		local n = pd.door_opens()
		return isint(n) and n >= state.opens0 and n > 0, state.opens0 .. " -> " .. n
	end)
	check("doors_hold", function()
		state.held = pd.doors_hold(true)
		return state.held == state.doors, state.held .. " held"
	end)
	check("doors_speeds", function()
		local n = pd.doors_speeds(true)
		local again = pd.doors_speeds(true) -- already armed: must refuse
		state.speeds = n
		return n == state.doors and again == 0, n .. " doors, re-arm " .. again
	end)
	check("doors_lock", function()
		state.locked = pd.doors_lock(true)
		return state.locked == state.doors, state.locked .. " locked"
	end)
	check("doors_shuffle", function()
		local n = pd.doors_shuffle(100)
		return n == state.doors, n .. " told to move"
	end)

	check("env_colours", function() return pd.env_colours(255, 0, 255, 0, 255, 0) == true end)
	check("env", function() return pd.env(0x0b) == true end)
	check("fog", function() return pd.fog(300, 700, 255, 64, 64) == true end)
	check("alarm", function() return pd.alarm(true) == true end)
	check("nitro", function() return pd.nitro(true) == true end)
	check("objective_force", function()
		local a = pd.objective_force(0, 1)
		local b = pd.objective_force(99, 1) -- past MAX_OBJECTIVES
		return a == true and b == false
	end)
	check("items_shuffle", function()
		local n = pd.items_shuffle()
		return isint(n) and n >= 0, n .. " moved"
	end)
	check("haunt", function()
		local n = pd.haunt(200)
		return isint(n) and n >= 0 and n <= 3, n .. " thrown"
	end)
	check("gust", function() return pd.gust(150) == true end)
	check("rubber_objects", function() return pd.rubber_objects(true) == true end)
	check("weather", function()
		local off = pd.weather(0)
		local rain = pd.weather(1, 2)
		local off2 = pd.weather(0)
		local snow = pd.weather(2, 3)
		return off and rain and off2 and snow
	end)
	check("gas", function() return pd.gas(true) == true end)
	check("room_tint", function() return pd.room_tint(255, 96, 96) == true end)
	check("room_highlight", function()
		local a = pd.room_highlight(1, 0, 255, 0)
		local b = pd.room_highlight(2)
		local bad = pd.room_highlight(state.rooms) -- room numbers stop at count-1
		return a == true and b == true and bad == false
	end)

	-- audio
	check("sound", function() return pd.sound(1) == true end)
	check("metronome_click", function() return pd.metronome_click() == true end)
	check("ext_volume", function()
		state.extvol = pd.ext_volume()
		local set = pd.ext_volume(40)
		local get = pd.ext_volume()
		return isint(state.extvol) and set == 40 and get == 40, state.extvol .. " -> " .. get
	end)
	check("play_file", function()
		local wav = pd.play_file("scripts/world_smoke.wav", true)
		local mp3 = pd.play_file("scripts/world_smoke.mp3", false, true)
		local sav = pd.play_file("$S/world_smoke.wav")
		local missing = pd.play_file("scripts/world_smoke_missing.wav")
		state.voice = wav
		return isint(wav) and wav > 0 and isint(mp3) and isint(sav) and missing == false,
			string.format("wav %s mp3 %s save %s missing %s", tostring(wav), tostring(mp3),
				tostring(sav), tostring(missing))
	end)
	check("music_bpm", function()
		state.bpm = pd.music_bpm()
		return type(state.bpm) == "number" and state.bpm >= 0, string.format("%.1f", state.bpm)
	end)
	check("music_beat", function()
		local b = pd.music_beat()
		return b == nil or (b >= 0 and b < 1), tostring(b)
	end)
	check("music_rate", function()
		local ok = pd.music_rate(2)
		local bpm2 = pd.music_bpm()
		if state.bpm > 0 then
			return ok == true and math.abs(bpm2 / state.bpm - 2) < 0.02,
				string.format("%.1f -> %.1f", state.bpm, bpm2)
		end
		return ok == true, "no sequenced music playing"
	end)
	check("instrument_shuffle", function() return pd.instrument_shuffle(true) == true end)
	check("song", function() return pd.song(0, 0.5) == true end)
	check("sfx_shuffle", function() return pd.sfx_shuffle(true) == true and pd.sound(1) == true end)
	check("sfx_replace", function() return pd.sfx_replace(1, 2) == true end)
	check("audio_crush", function() return pd.audio_crush(4, 6) == true end)
	check("audio_radio", function() return pd.audio_radio(true) == true end)
	check("audio_reverb", function() return pd.audio_reverb(0.6) == true end)
	check("audio_reverse", function() return pd.audio_reverse(true) == true end)
	check("audio_pitch", function() return pd.audio_pitch(1.5) == true end)
	check("mute", function() return pd.mute(true) == true end)
end

-- Phase 2: switch everything off again and read back what can be read.
local function clearall()
	check("doors_speeds off", function()
		local n = pd.doors_speeds(false)
		return n == state.speeds, n .. " restored"
	end)
	check("doors_hold off", function()
		local n = pd.doors_hold(false)
		return n == state.held, n .. " released"
	end)
	check("doors_lock off", function() return pd.doors_lock(false) == state.locked end)
	check("doors_all close", function() return pd.doors_all(false) == state.doors end)
	check("fog off", function() return pd.fog() == true end)
	check("env off", function() return pd.env() == true end)
	check("alarm off", function() return pd.alarm(false) == true end)
	check("nitro off", function() return pd.nitro(false) == true end)
	check("objective_force off", function() return pd.objective_force() == true end)
	check("rubber_objects off", function() return pd.rubber_objects(false) == true end)
	check("weather again", function()
		-- off, then on again: the parked weather block must come back
		return pd.weather(0) == true and pd.weather(1, 1) == true and pd.weather(0) == true
	end)
	check("gas off", function() return pd.gas(false) == true end)
	check("room_tint off", function() return pd.room_tint() == true end)
	check("room_highlight off", function() return pd.room_highlight() == true end)
	check("mute off", function() return pd.mute(false) == true end)
	check("stop_file", function()
		if state.voice then
			pd.stop_file(state.voice) -- the looping one by id
		end
		pd.stop_file(123456)      -- a stale id is a no-op
		pd.stop_file()            -- the rest
		return true
	end)
	check("ext_volume restore", function() return pd.ext_volume(state.extvol) == state.extvol end)
	check("music_rate off", function()
		local ok = pd.music_rate()
		local bpm = pd.music_bpm()
		if state.bpm > 0 then
			return ok == true and math.abs(bpm - state.bpm) < 0.5, string.format("%.1f", bpm)
		end
		return ok == true
	end)
	check("instrument_shuffle off", function() return pd.instrument_shuffle(false) == true end)
	check("song off", function() return pd.song() == true end)
	check("stage_music", function()
		return pd.stage_music(false) == true and pd.stage_music(true) == true
	end)
	check("sfx_shuffle off", function() return pd.sfx_shuffle(false) == true end)
	check("sfx_replace off", function() return pd.sfx_replace() == true end)
	check("audio_crush off", function() return pd.audio_crush() == true end)
	check("audio_radio off", function() return pd.audio_radio(false) == true end)
	check("audio_reverb off", function() return pd.audio_reverb() == true end)
	check("audio_reverse off", function() return pd.audio_reverse(false) == true end)
	check("audio_pitch off", function() return pd.audio_pitch() == true end)
end

local FUNCS = {
	"door_traps", "door_opens", "env_colours", "alarm", "nitro", "objective_force",
	"env", "fog", "items_shuffle", "haunt", "weather", "gas", "room_tint",
	"room_highlight", "gust", "rubber_objects", "doors_all", "doors_lock",
	"doors_hold", "doors_speeds", "doors_shuffle",
	"sound", "metronome_click", "mute", "play_file", "stop_file", "ext_volume",
	"song", "stage_music", "sfx_shuffle", "sfx_replace", "instrument_shuffle",
	"music_bpm", "music_rate", "music_beat", "audio_crush", "audio_radio",
	"audio_reverb", "audio_reverse", "audio_pitch",
}

pd.on("tick", function()
	ticks = ticks + 1
	if phase >= 3 then
		return
	end
	if live == nil then
		if pd.player_count() >= 1 and pd.player_pos() ~= nil then
			live = ticks
		end
		return
	end
	if phase == 0 and ticks - live >= 60 then
		for _, name in ipairs(FUNCS) do
			if type(pd[name]) ~= "function" then
				report("registered " .. name, false, "missing")
			end
		end
		setall()
		phase = 1
	elseif phase == 1 and ticks - live >= 240 then
		-- the effects have now run through the engine for ~3 seconds
		clearall()
		phase = 2
	elseif phase == 2 and ticks - live >= 300 then
		check("still live", function() return pd.player_pos() ~= nil end)
		local missing = {}
		for _, name in ipairs(FUNCS) do
			if not seen[name] then
				missing[#missing + 1] = name
			end
		end
		if #missing > 0 then
			pd.log("world smoke unchecked: " .. table.concat(missing, " "))
		end
		pd.log(string.format("world smoke done %d/%d (%d functions)", results.pass, results.total, #FUNCS))
		phase = 3
	end
end)

pd.log("world smoke loaded")
