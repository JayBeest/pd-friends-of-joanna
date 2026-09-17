-- Smoke test for the player group of the pd.* API
-- (src/game/luaai_api_player.c): player, devices, input, cheats, models.
-- The pd.* API it exercises is ported from Kai (be46717).
--
-- Run it as scripts/init.lua (relative to the game's working directory) and
-- load a solo stage. Once the player is live and the stage is ticking, a tick
-- handler walks a list of steps. Each step calls functions with sane
-- arguments, reads the effect back where Lua can see it (a getter after a
-- setter, or the player's position, stance or the game tick), clears the
-- effect again and logs a PASS or FAIL line per function through pd.log. The
-- run ends with "player smoke done N/M".
--
-- The last step is pd.trapdoor, which drops the player to their death.
--
-- Optional: set PLAYER_SMOKE_MODEL_ROM (a PD-layout ROM file, or a folder
-- holding one) before this file runs to exercise pd.load_model_rom and a real
-- pd.model_swap on/off. Without it only the no-ROM paths are checked.

local MODEL_ROM = rawget(_G, "PLAYER_SMOKE_MODEL_ROM")

local WEAPON_NIGHTVISION = 0x2d
local CHEAT_DKMODE = 7

local results = { pass = 0, total = 0 }
local seen = {} -- function name -> true once it has been called
local ticks = 0
local done = false

local function report(name, ok, detail)
	results.total = results.total + 1
	if ok then
		results.pass = results.pass + 1
	end
	pd.log(string.format("player smoke %s %s%s", ok and "PASS" or "FAIL", name,
		detail ~= nil and (" (" .. tostring(detail) .. ")") or ""))
end

local function check(name, fn)
	seen[name] = true
	local ok, res, detail = pcall(fn)
	if not ok then
		report(name, false, res)
	else
		report(name, res and true or false, detail)
	end
end

local function near(a, b, eps)
	return type(a) == "number" and math.abs(a - b) <= (eps or 0.01)
end

local function pos()
	local x, y, z = pd.player_pos()
	return x and { x = x, y = y, z = z } or nil
end

local function dist(a, b)
	if not a or not b then
		return -1
	end
	local dx, dy, dz = a.x - b.x, a.y - b.y, a.z - b.z
	return math.sqrt(dx * dx + dy * dy + dz * dz)
end

local function fmt(n)
	return type(n) == "number" and string.format("%.2f", n) or tostring(n)
end

-- Every function of the group, for the coverage line at the end.
local ALL = {
	"player_heal", "player_set_shield", "device_on", "device_off", "device_active",
	"invincible", "cheat", "cheat_active", "player_yaw", "player_crouch", "boost",
	"player_set_health", "show_health", "dizzy", "teleport_to_chr", "player_health",
	"player_shield", "player_reloading", "player_activate", "model_swap",
	"model_rom_ok", "load_model_rom", "player_damage", "button_block", "deadzone",
	"mark_home", "warp_home", "sens_boost", "player_freeze", "player_speed",
	"player_add_yaw", "player_slip", "player_pitch", "player_push", "invert_look",
	"input_delay", "forced_march", "forced_crouch", "headshots_only", "trapdoor",
	"ice_floor", "player_movespeed", "time_stop", "gormless",
}

---------------------------------------------------------------------------
-- Steps. Each is a function(state) returning true when finished; state.t is
-- the number of ticks spent in the step so far.
---------------------------------------------------------------------------

local home = nil

local steps = {}

-- Getters and setters with immediate read-back, plus the flag-only effects
-- (set, then clear).
steps[#steps + 1] = function()
	check("player_health", function()
		local h = pd.player_health()
		return type(h) == "number" and h > 0 and h <= 1, fmt(h)
	end)
	check("player_shield", function()
		local s = pd.player_shield()
		return type(s) == "number" and s >= 0 and s <= 1, fmt(s)
	end)
	check("player_set_health", function()
		local ok = pd.player_set_health(0.5)
		local h = pd.player_health()
		local ok2 = pd.player_set_health(0) -- floored, never kills
		local h2 = pd.player_health()
		return ok and near(h, 0.5) and ok2 and near(h2, 0.01, 0.001), fmt(h) .. " " .. fmt(h2)
	end)
	check("player_heal", function()
		local ok = pd.player_heal(0.25)
		local h = pd.player_health()
		local ok2 = pd.player_heal()
		local h2 = pd.player_health()
		return ok and near(h, 0.26) and ok2 and near(h2, 1), fmt(h) .. " " .. fmt(h2)
	end)
	check("player_set_shield", function()
		local ok = pd.player_set_shield(0.5, true)
		local s = pd.player_shield()
		local ok2 = pd.player_set_shield(0)
		local s2 = pd.player_shield()
		return ok and near(s, 0.5, 0.05) and ok2 and near(s2, 0), fmt(s) .. " " .. fmt(s2)
	end)
	check("show_health", function() return pd.show_health() == true end)
	check("invincible", function()
		return pd.invincible(true) == true and pd.invincible(false) == true
	end)
	check("player_yaw", function()
		local y = pd.player_yaw()
		return type(y) == "number" and y >= 0 and y < 360, fmt(y)
	end)
	check("player_add_yaw", function()
		local y0 = pd.player_yaw()
		local ok = pd.player_add_yaw(90)
		local y1 = pd.player_yaw()
		local ok2 = pd.player_add_yaw(-90)
		local y2 = pd.player_yaw()
		return ok and ok2 and near((y1 - y0) % 360, 90, 0.05) and near(y2, y0, 0.05),
			fmt(y0) .. "->" .. fmt(y1) .. "->" .. fmt(y2)
	end)
	check("player_pitch", function()
		local p0 = pd.player_pitch()
		local ok = pd.player_pitch(20)
		local p1 = pd.player_pitch()
		local okc = pd.player_pitch(500) -- clamped
		local pc = pd.player_pitch()
		pd.player_pitch(p0)
		return type(p0) == "number" and ok == true and near(p1, 20) and okc == true and near(pc, 90),
			fmt(p0) .. " " .. fmt(p1) .. " " .. fmt(pc)
	end)
	check("player_crouch", function()
		local c = pd.player_crouch()
		return c == 0 or c == 1 or c == 2, c
	end)
	check("player_reloading", function() return type(pd.player_reloading()) == "boolean" end)
	check("player_activate", function() return type(pd.player_activate()) == "boolean" end)
	check("player_movespeed", function()
		local m = pd.player_movespeed()
		return type(m) == "number" and m >= 0, fmt(m)
	end)
	check("device_on", function()
		return pd.device_on(WEAPON_NIGHTVISION) == true and pd.device_active(WEAPON_NIGHTVISION) == true
	end)
	check("device_active", function()
		return type(pd.device_active(WEAPON_NIGHTVISION)) == "boolean"
	end)
	check("device_off", function()
		return pd.device_off(WEAPON_NIGHTVISION) == true and pd.device_active(WEAPON_NIGHTVISION) == false
	end)
	check("cheat", function()
		local was = pd.cheat_active(CHEAT_DKMODE)
		local ok = pd.cheat(CHEAT_DKMODE, true)
		local on = pd.cheat_active(CHEAT_DKMODE)
		local ok2 = pd.cheat(CHEAT_DKMODE, was)
		local bad = pd.cheat(64, true)
		return ok and on and ok2 and pd.cheat_active(CHEAT_DKMODE) == was and bad == false,
			tostring(on)
	end)
	check("cheat_active", function()
		return type(pd.cheat_active(0)) == "boolean" and pd.cheat_active(-1) == false
	end)
	check("boost", function() return pd.boost(1) == true and pd.boost(0) == true end)
	check("dizzy", function() return pd.dizzy(2000) == true and pd.dizzy(0) == true end)
	check("button_block", function() return pd.button_block(0x8000) == true and pd.button_block() == true end)
	check("deadzone", function() return pd.deadzone(0.45) == true and pd.deadzone() == true end)
	check("sens_boost", function() return pd.sens_boost(3) == true and pd.sens_boost() == true end)
	check("input_delay", function() return pd.input_delay(5) == true and pd.input_delay() == true end)
	check("invert_look", function() return pd.invert_look(true) == true and pd.invert_look(false) == true end)
	check("headshots_only", function()
		return pd.headshots_only(true) == true and pd.headshots_only(false) == true
	end)
	check("ice_floor", function() return pd.ice_floor(0.2) == true and pd.ice_floor(1) == true end)
	check("gormless", function() return pd.gormless(true) == true and pd.gormless(false) == true end)
	check("player_speed", function() return pd.player_speed(2) == true and pd.player_speed() == true end)
	check("model_rom_ok", function() return type(pd.model_rom_ok()) == "boolean", tostring(pd.model_rom_ok()) end)
	if not pd.model_rom_ok() then
		check("load_model_rom refuses", function()
			-- only the game's folders, only regular ROM-sized files
			local took = {}
			for _, p in ipairs({ "/dev/zero", "/", "..", "../rom", "./scripts", "scripts/../scripts",
					"$H/rom", "$E/rom", "C:/rom", "scripts\\rom", "scripts/smoke" }) do
				if pd.load_model_rom(p) ~= false then
					took[#took + 1] = p
				end
			end
			return #took == 0, #took > 0 and table.concat(took, " ") or nil
		end)
	end
	check("mark_home", function()
		home = pos()
		return pd.mark_home() == true and home ~= nil
	end)
	return true
end

-- pd.player_damage: health or shield drops through the real damage path.
steps[#steps + 1] = function(st)
	if st.t == 0 then
		pd.invincible(false)
		pd.player_set_shield(0, true)
		pd.player_heal()
		st.h0 = pd.player_health()
		st.ok = pd.player_damage(0.5)
		return false
	end
	if st.t < 5 then
		return false
	end
	local h1 = pd.player_health()
	check("player_damage", function()
		return st.ok == true and h1 < st.h0 and h1 > 0 and pd.player_damage(0) == false,
			fmt(st.h0) .. "->" .. fmt(h1)
	end)
	pd.player_heal()
	return true
end

-- pd.forced_march: the player walks forward on their own.
steps[#steps + 1] = function(st)
	if st.t == 0 then
		st.p0 = pos()
		st.ok = pd.forced_march(true)
		st.maxspeed = 0
		return false
	end
	local ms = pd.player_movespeed()
	st.maxspeed = math.max(st.maxspeed, ms)
	if ms > 0.05 and not st.walk then
		st.walk = pos() -- walking under our own steam now
		st.wt = st.t
	end
	local d = st.walk and dist(st.walk, pos()) or 0
	-- A stage can open on an intro cutscene nobody skips in a headless run;
	-- the player only moves once it ends, so this step doubles as the wait
	-- for control. The later movement and time checks need it.
	if d < 20 and st.t < 6000 then
		if st.t % 600 == 0 then
			pd.log(string.format("player smoke waiting for control (%d ticks, lvupdate %d)", st.t, pd.lvupdate()))
		end
		return false
	end
	pd.forced_march(false)
	check("forced_march", function()
		return st.ok == true and d >= 20, string.format("moved %.1f in %d ticks after control at tick %s, movespeed %.2f",
			d, st.t - (st.wt or st.t), tostring(st.wt), st.maxspeed)
	end)
	return true
end

-- pd.player_freeze beats pd.forced_march: no movement. The window starts
-- late: the stage can still move the player once just after control returns.
steps[#steps + 1] = function(st)
	if st.t == 0 then
		st.ok = pd.player_freeze(true)
		pd.forced_march(true)
		st.maxspeed = 0
		return false
	end
	if st.t >= 10 then
		st.maxspeed = math.max(st.maxspeed, pd.player_movespeed())
	end
	if st.t == 90 then
		st.p0 = pos()
	end
	if st.t < 150 then
		return false
	end
	local d = dist(st.p0, pos())
	pd.forced_march(false)
	local ok2 = pd.player_freeze(false)
	check("player_freeze", function()
		return st.ok == true and ok2 == true and d >= 0 and d < 2 and st.maxspeed == 0,
			string.format("moved %.2f, movespeed %.2f", d, st.maxspeed)
	end)
	return true
end

-- pd.forced_crouch: stance pinned to a squat, released after.
steps[#steps + 1] = function(st)
	if st.t == 0 then
		st.ok = pd.forced_crouch(true)
		return false
	end
	if st.t == 10 then
		st.c = pd.player_crouch()
		st.ok2 = pd.forced_crouch(false)
		return false
	end
	if st.t < 10 then
		return false
	end
	-- Released, the stance is the player's own again: under the toggle crouch
	-- mode it stays where it is until they press crouch, so only the pinned
	-- value is asserted.
	check("forced_crouch", function()
		return st.ok == true and st.ok2 == true and st.c == 2,
			string.format("crouch %s while pinned, %s after", tostring(st.c), tostring(pd.player_crouch()))
	end)
	return true
end

-- pd.player_slip squats and shoves; pd.player_push shoves.
steps[#steps + 1] = function(st)
	if st.t == 0 then
		st.p0 = pos()
		st.ok = pd.player_slip(30)
		st.c = pd.player_crouch()
		return false
	end
	if st.t < 30 then
		return false
	end
	local d = dist(st.p0, pos())
	check("player_slip", function()
		return st.ok == true and st.c == 2 and d > 1, string.format("crouch %s, moved %.1f", tostring(st.c), d)
	end)
	return true
end

steps[#steps + 1] = function(st)
	if not st.p0 then
		st.p0 = pos()
		st.t0 = st.t
		st.ok = pd.player_push(-40)
		return false
	end
	if st.t - st.t0 < 30 then
		return false
	end
	local d = dist(st.p0, pos())
	check("player_push", function()
		return st.ok == true and d > 1, string.format("moved %.1f", d)
	end)
	return true
end

-- pd.teleport_to_chr moves the player next to a chr; pd.warp_home returns.
steps[#steps + 1] = function(st)
	if st.t == 0 then
		local p0 = pos()
		local tried, ok, target = 0, false, nil
		for _, c in ipairs(pd.all_chrs() or {}) do
			tried = tried + 1
			if pd.teleport_to_chr(c) then
				ok, target = true, c
				break
			end
			if tried >= 20 then
				break
			end
		end
		local p1 = pos()
		st.moved = dist(p0, p1)
		check("teleport_to_chr", function()
			return ok and st.moved > 1 and pd.teleport_to_chr(-1) == false,
				string.format("chr %s of %d tried, moved %.1f", tostring(target), tried, st.moved)
		end)
		return false
	end
	if st.t < 10 then
		return false
	end
	local ok = pd.warp_home()
	local d = dist(home, pos())
	check("warp_home", function()
		return ok == true and d >= 0 and d < 5, string.format("%.1f from home", d)
	end)
	return true
end

-- pd.time_stop: with no input the game tick stops; it runs again after.
steps[#steps + 1] = function(st)
	if st.t == 0 then
		st.ok = pd.time_stop(true)
		st.zero = 0
		return false
	end
	if st.t <= 60 then
		if pd.lvupdate() == 0 then
			st.zero = st.zero + 1
		end
		return false
	end
	if st.t == 61 then
		st.ok2 = pd.time_stop(false)
		st.running = 0
		return false
	end
	if st.t <= 90 then
		if pd.lvupdate() > 0 then
			st.running = st.running + 1
		end
		return false
	end
	check("time_stop", function()
		return st.ok == true and st.ok2 == true and st.zero >= 50 and st.running >= 25,
			string.format("%d/60 frozen frames, %d/29 running after", st.zero, st.running)
	end)
	return true
end

-- Model swap: the no-ROM paths, then a real overlay if one was given.
steps[#steps + 1] = function(st)
	if st.t == 0 then
		st.had = pd.model_rom_ok()
		check("load_model_rom", function()
			local r = pd.load_model_rom("scripts/no-such-model-rom-dir")
			if not MODEL_ROM then
				return r == false, "missing dir -> " .. tostring(r)
			end
			local r2 = pd.load_model_rom(MODEL_ROM)
			return r2 == true and pd.model_rom_ok() == true, "missing -> " .. tostring(r) .. ", " .. MODEL_ROM .. " -> " .. tostring(r2)
		end)
		st.swapon = pd.model_swap(true)
		return false
	end
	if st.t < 30 then
		return false
	end
	local off = pd.model_swap(false)
	check("model_swap", function()
		if pd.model_rom_ok() then
			return st.swapon == true and off == true, "on/off with overlay"
		end
		return st.swapon == false and off == false, "no overlay -> false"
	end)
	return true
end

-- pd.trapdoor: the floor goes; the player falls. Last, since it kills.
steps[#steps + 1] = function(st)
	if st.t == 0 then
		st.p0 = pos()
		st.ok = pd.trapdoor()
		return false
	end
	local p = pos()
	local drop = (st.p0 and p) and (st.p0.y - p.y) or 0
	if drop < 100 and st.t < 150 then
		return false
	end
	check("trapdoor", function()
		return st.ok == true and drop >= 100, string.format("fell %.0f in %d ticks", drop, st.t)
	end)
	return true
end

local cur = 1
local state = { t = 0 }
local ready = 0

pd.on("tick", function()
	ticks = ticks + 1
	if done then
		return
	end
	if pd.player_count() < 1 or pd.player_pos() == nil then
		ready = 0
		return
	end
	if cur == 1 and ready < 120 then
		-- let the stage settle (intro, fades): 120 ticks in a row with the
		-- game tick running. Later steps run every frame, frozen or not.
		ready = pd.lvupdate() > 0 and ready + 1 or 0
		return
	end
	local ok, fin = pcall(steps[cur], state)
	if not ok then
		report("step " .. cur, false, fin)
		fin = true
	end
	if fin then
		cur = cur + 1
		state = { t = 0 }
	else
		state.t = state.t + 1
	end
	if cur > #steps then
		local missing = {}
		for _, name in ipairs(ALL) do
			if not seen[name] then
				missing[#missing + 1] = name
			end
		end
		pd.log(string.format("player smoke coverage %d/%d%s", #ALL - #missing, #ALL,
			#missing > 0 and (" missing: " .. table.concat(missing, " ")) or ""))
		pd.log(string.format("player smoke done %d/%d", results.pass, results.total))
		done = true
	end
end)

pd.log("player smoke loaded")
