-- Smoke test for the weapons group of pd.* (src/game/luaai_api_weapons.c).
-- The pd.* API it exercises is ported from Kai (be46717).
--
-- Run it as scripts/init.lua (relative to the game's working directory) and
-- load a stage. Once the player holds a gun (the hands are in use, so any
-- intro cutscene and menu is over) the "tick" handler walks a few phases:
-- inventory and ammo calls, then every weapon effect switched on while the
-- trigger is held for you (so the fire paths the effects hook really run),
-- then everything switched off again. Each function gets a PASS or FAIL line
-- through pd.log, and the run ends with "weapons smoke done N/M".

local W = {
	FALCON2 = 0x02, DY357 = 0x08, CMP150 = 0x0a, DRAGON = 0x0f,
	ROCKETLAUNCHER = 0x18, COMBATKNIFE = 0x1a, SUITCASE = 0x4d,
}
local AMMO_PISTOL = 0x01

local results = { pass = 0, total = 0 }
local fired = 0
local seen = {}
local ticks, live, phase, phasestart = 0, 0, 0, 0
local done = false

local function report(name, ok, detail)
	results.total = results.total + 1
	seen[name] = true
	if ok then
		results.pass = results.pass + 1
	end
	pd.log(string.format("weapons smoke %s %s%s", ok and "PASS" or "FAIL", name,
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

-- a setter that returns true for any sane argument
local function setter(name, ...)
	local args = { ... }
	local n = select("#", ...)
	check(name, function() return pd[name](table.unpack(args, 1, n)) == true end)
end

-- Hostile-float helper. Every pd.* float argument now goes through
-- luaApiNum/luaApiOptNum (src/game/luaai_api_internal.h): a non-finite value
-- is an argument error, not a silent 0, so a script that means it can pcall.
-- refused(fn, ...) is true when the call was refused that way.
local function refused(fn, ...)
	local ok, err = pcall(fn, ...)
	return (not ok) and tostring(err):find("finite") ~= nil
end

local NAN, INF = 0 / 0, 1 / 0

local function phase0()
	-- inventory
	check("give_weapon", function() return pd.give_weapon(W.CMP150) == true end)
	check("has_weapon", function()
		return pd.has_weapon(W.CMP150) == true and pd.has_weapon(W.SUITCASE) == false
	end)
	check("switch_weapon", function() return pd.switch_weapon(W.CMP150) == true end)
	check("give_weapon+take_weapon", function()
		pd.give_weapon(W.DY357)
		local had = pd.has_weapon(W.DY357)
		return had and pd.take_weapon(W.DY357) == true and pd.has_weapon(W.DY357) == false
	end)
	seen["take_weapon"] = true
	check("drop_weapon", function()
		pd.give_weapon(W.FALCON2)
		return pd.drop_weapon(W.FALCON2) == true and pd.has_weapon(W.FALCON2) == false
	end)
	check("dual_wield", function()
		return pd.dual_wield(W.DY357, 0) == true and pd.has_weapon(W.DY357)
			and pd.dual_wield(500) == false
	end)
	-- ammo
	check("give_ammo", function() return pd.give_ammo(AMMO_PISTOL, 10) == true end)
	check("set_ammo", function()
		return pd.set_ammo(AMMO_PISTOL, 5) == true and pd.set_ammo(0, 5) == false
	end)
	setter("strip_ammo")
	check("give_mags", function() return pd.give_mags() == true and pd.give_mags(3) == true end)
	setter("refill_ammo")
	-- bag bomb: nothing thrown or dropped yet
	check("bag_convert", function() return pd.bag_convert() == false end)
end

local function phase1()
	-- every effect on; the trigger is held for the whole phase
	check("weapon_held", function()
		-- dual_wield came last in phase 0
		local w = pd.weapon_held()
		return w == W.DY357, string.format("held 0x%x", w)
	end)
	setter("gun_hide", true)
	check("weapon_censor", function()
		return pd.weapon_censor(W.CMP150, true) == true and pd.weapon_censor(500, true) == false
	end)
	setter("weapon_jam", 2)
	setter("ammo_cost", 3)
	setter("autoaim", true)
	setter("double_shots", true)
	setter("quad_top", true)
	setter("gangsta", true)
	setter("zoom_scale", 2.5)
	setter("gun_sound", W.DY357)
	setter("weapon_rename", W.CMP150, "Nokia \226\128\148 3315\195") -- non-ASCII, scrubbed
	setter("weapon_rename", W.CMP150, "Nokia 3315")
	check("float and int bounds", function()
		-- NaN into a weapons float is an argument error now, not a stored NaN
		local rej = refused(pd.spread, NAN) and refused(pd.zoom_scale, INF)
			and refused(pd.gun_fov, NAN)
		local clamped = pd.spread(1e9) == true and pd.zoom_scale(1e9) == true
			and pd.gun_fov(1e9) == true
		return rej and clamped and pd.ammo_cost(0x7fffffff) == true
			and pd.spread(4.0) == true and pd.ammo_cost(3) == true
	end)
	setter("one_bullet", true)
	setter("forced_fire", true)
	setter("rapid_fire", true)
	setter("no_reload", true)
	setter("spread", 4.0)
	setter("gun_fov", 120)
	setter("backfire", true)
	setter("mag_dump", true)
	setter("temu_mag", true)
	setter("cloak_lock", true)
	check("ammo_swap", function()
		return pd.ammo_swap(W.COMBATKNIFE) == false and pd.ammo_swap(W.DY357) == true
	end)
	setter("uwuify", true)
end

local function phase2()
	-- the trigger was held for the whole of phase 1
	report("fire paths ran", fired > 0, fired .. " weaponfire events")
	-- the second half: text gags, locks, the projectile swap (trigger off)
	setter("forced_fire", false)
	setter("piglatin", true)
	setter("buttsbot", true)
	setter("text_scramble", true)
	setter("force_secondary", true)
	setter("gun_lock", true)
	setter("knife_lock", true)
	setter("pinball", true)
	check("ammo_swap rocket", function() return pd.ammo_swap(W.ROCKETLAUNCHER) == true end)
	-- Hostile: 60- and 200-character renames. langGet returns this string
	-- wherever the weapon's name is drawn, and amGetSlotDetails copies a name
	-- into its caller's char[32]. The bridge caps what it stores; switch to
	-- the renamed gun so the HUD name path actually renders it.
	check("weapon_rename long", function()
		local ok60 = pd.weapon_rename(W.CMP150, string.rep("R", 60)) == true
		local ok200 = pd.weapon_rename(W.DY357, string.rep("Q", 200)) == true
		pd.give_weapon(W.CMP150)
		pd.switch_weapon(W.CMP150)
		pd.hud_message(string.rep("R", 60))
		pd.weapon_rename(W.DY357)
		return ok60 and ok200
	end)
	setter("weapon_jam", 1)
end

local function phase3()
	-- everything off again; the gun must still be the one we switched to
	local offs = {
		{ "gun_hide", false }, { "weapon_jam", 0 }, { "ammo_cost", 1 },
		{ "autoaim", false }, { "double_shots", false }, { "quad_top", false },
		{ "gangsta", false }, { "zoom_scale", 1.0 }, { "gun_sound" },
		{ "one_bullet", false }, { "forced_fire", false }, { "rapid_fire", false },
		{ "no_reload", false }, { "spread", 1.0 }, { "gun_fov", 0 },
		{ "backfire", false }, { "mag_dump", false }, { "temu_mag", false },
		{ "cloak_lock", false }, { "ammo_swap" }, { "text_scramble", false },
		{ "force_secondary", false }, { "gun_lock", false }, { "knife_lock", false },
		{ "pinball", false },
	}
	local bad = {}
	for _, o in ipairs(offs) do
		local ok, r = pcall(pd[o[1]], table.unpack(o, 2))
		if not ok or r ~= true then
			bad[#bad + 1] = o[1]
		end
	end
	report("effects off", #bad == 0, #bad > 0 and table.concat(bad, ",") or nil)
	check("weapon_rename restore", function() return pd.weapon_rename(W.CMP150) == true end)
	check("weapon_censor off", function() return pd.weapon_censor(-1, false) == true end)
	check("uwuify/piglatin/buttsbot off", function()
		return pd.uwuify(false) and pd.piglatin(false) and pd.buttsbot(false)
	end)
	check("weapon_held after", function()
		local w = pd.weapon_held()
		return w >= 0, string.format("held 0x%x", w)
	end)
	check("drop_weapon no model", function() return pd.drop_weapon(0x3e) == false end)
end

local function phase4()
	check("refill_ammo after", function() return pd.refill_ammo() == true end)
	-- last, the bag bomb: drop the suitcase and detonate it where it lies,
	-- in the same tick (standing on it, the player would pick it back up).
	-- The blast goes off at the player's feet.
	pd.give_weapon(W.SUITCASE)
	check("drop_weapon suitcase", function() return pd.drop_weapon(W.SUITCASE) == true end)
	check("bag_boom", function() return pd.bag_boom() == true and pd.bag_boom() == false end)

	local all = { "refill_ammo", "give_mags", "give_ammo", "give_weapon", "take_weapon",
		"weapon_held", "switch_weapon", "has_weapon", "strip_ammo", "set_ammo", "gun_hide",
		"bag_boom", "bag_convert", "weapon_censor", "weapon_jam", "force_secondary",
		"ammo_cost", "autoaim", "double_shots", "quad_top", "text_scramble", "gangsta",
		"zoom_scale", "gun_sound", "weapon_rename", "one_bullet", "uwuify", "piglatin",
		"forced_fire", "rapid_fire", "no_reload", "spread", "drop_weapon", "gun_fov",
		"buttsbot", "pinball", "ammo_swap", "backfire", "dual_wield", "gun_lock",
		"mag_dump", "knife_lock", "cloak_lock", "temu_mag" }
	local missing = {}
	for _, n in ipairs(all) do
		if type(pd[n]) ~= "function" then
			missing[#missing + 1] = n .. "(unregistered)"
		elseif not seen[n] then
			missing[#missing + 1] = n .. "(untested)"
		end
	end
	report("all 44 registered and called", #missing == 0 and #all == 44,
		#missing > 0 and table.concat(missing, ",") or (#all .. " functions"))
	pd.log(string.format("weapons smoke done %d/%d", results.pass, results.total))
	done = true
end

local phases = {
	{ at = 0, fn = phase0 },
	{ at = 120, fn = phase1 },
	{ at = 240, fn = phase2 },
	{ at = 360, fn = phase3 },
	{ at = 420, fn = phase4 },
}

pd.on("tick", function()
	ticks = ticks + 1
	if done then
		return
	end
	if pd.player_count() < 1 or pd.player_pos() == nil or pd.weapon_held() <= 0 then
		return
	end
	live = live + 1
	if live < 60 then
		return
	end
	local p = phases[phase + 1]
	if p and live - 60 >= p.at then
		phase = phase + 1
		local ok, err = pcall(p.fn)
		if not ok then
			report("phase " .. phase, false, err)
		end
		if phase == #phases and not done then
			pd.log("weapons smoke done " .. results.pass .. "/" .. results.total)
			done = true
		end
	end
end)

pd.on("weaponfire", function()
	if phase == 2 then
		fired = fired + 1
	end
end)

pd.log("weapons smoke loaded")
