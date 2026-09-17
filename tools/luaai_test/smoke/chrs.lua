-- Smoke test for the chrs pd.* functions (src/game/luaai_api_chrs.c): chr
-- mutators, spawns and explosions.
-- The pd.* API it exercises is ported from Kai (be46717).
--
-- Run it as scripts/init.lua (relative to the game's working directory) and
-- load a solo stage with guards. Once a player is live the "tick" handler
-- works through four phases half a second apart: state toggles and per-chr
-- mutators, then the calls that need a later tick to undo (wake, unpossess,
-- unsnatch), then the hostile spawns and explosions, then the cleanup. Each
-- function logs a PASS or FAIL line through pd.log; where the effect is
-- readable from Lua (chr_info, chr_weapon, chr_pos) the check reads it back.
-- The last line is "chrs smoke done N/M".

local WEAPON_UNARMED, WEAPON_FALCON2, WEAPON_CMP150 = 0x01, 0x02, 0x0a
local BODY_DD_GUARD, BODY_SKEDAR = 0x6e, 0x5c
local ANIM_SURRENDER_002E = 0x2e

local results = { pass = 0, total = 0 }
local seen = {}
local ticks = 0
local phase = 0
local phasetick = 0
local st = {} -- state carried between phases

local function report(name, ok, detail)
	seen[name] = true
	results.total = results.total + 1
	if ok then
		results.pass = results.pass + 1
	end
	pd.log(string.format("chrs smoke %s %s%s", ok and "PASS" or "FAIL", name,
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

local function dist2(ax, az, bx, bz)
	return (ax - bx) * (ax - bx) + (az - bz) * (az - bz)
end

-- Living non-player chrs, nearest first.
local function npcs()
	local px, py, pz = pd.player_pos()
	local list = {}
	for _, c in ipairs(pd.all_chrs()) do
		local t = pd.chr_info(c)
		if t and t.health > 0 and dist2(t.x, t.z, px, pz) > 4 then
			list[#list + 1] = { c = c, d = dist2(t.x, t.z, px, pz) }
		end
	end
	table.sort(list, function(a, b) return a.d < b.d end)
	local out = {}
	for i, e in ipairs(list) do out[i] = e.c end
	return out
end

local function pick(list, i)
	return list[((i - 1) % #list) + 1]
end

local ALL = {
	"spawn_at_chr", "spawn", "chr_anim", "chr_set_shield", "chr_alert", "chr_set_body",
	"possess_spawn", "unpossess", "spawn_ally", "spawn_ally_clone", "chr_yeet",
	"explosion", "explosion_at", "grenade", "chr_cloak", "chr_give_weapon", "chr_weapon",
	"blood_colour", "max_blood", "spawn_chopper", "headshot_boost", "chr_freeze",
	"no_drops", "damage_scale", "chr_speed", "chr_damage", "chr_scale", "chr_yscale",
	"chr_hum", "chr_armor", "chr_armor_clear", "clone_chr", "chr_freeze_one", "beyblade",
	"chr_ko", "chr_wake", "explosions_around", "nbomb", "yassify", "spawn_body",
	"body_snatch", "body_unsnatch", "chr_target", "chr_calm", "civil_war", "chr_summon",
	"one_punch", "spawn_bike", "space_program", "frag_out", "spawn_sentry",
}

local function phase1()
	local n = npcs()
	pd.log("chrs smoke: " .. #n .. " npcs")
	local a, b = pick(n, 1), pick(n, 2)
	local px, py, pz = pd.player_pos()

	check("registered", function()
		local missing = {}
		for _, k in ipairs(ALL) do
			if type(pd[k]) ~= "function" then missing[#missing + 1] = k end
		end
		return #missing == 0 and #ALL == 51, #missing == 0 and "51 functions" or table.concat(missing, ",")
	end)

	-- Global toggles: set, then clear.
	for _, k in ipairs({ "headshot_boost", "chr_freeze", "no_drops", "max_blood", "beyblade",
			"yassify", "one_punch", "space_program", "frag_out" }) do
		check(k, function() return pd[k](true) and pd[k](false) end)
	end
	check("damage_scale", function() return pd.damage_scale(0.5) and pd.damage_scale(20) and pd.damage_scale() end)
	check("chr_speed", function() return pd.chr_speed(2) and pd.chr_speed(0.01) and pd.chr_speed(1) end)
	check("blood_colour", function() return pd.blood_colour(255, 0, 255) and pd.blood_colour() end)
	check("chr_freeze_one", function() return pd.chr_freeze_one(a) and pd.chr_freeze_one() end)
	check("civil_war", function()
		local on = pd.civil_war(true)
		local off = pd.civil_war(false)
		return off, "on=" .. tostring(on)
	end)

	-- Per-chr mutators with a read-back where Lua can see the result.
	check("chr_anim", function() return pd.chr_anim(a, ANIM_SURRENDER_002E, 1.0) and not pd.chr_anim(-1, 0) end)
	check("chr_set_shield", function()
		local before = pd.chr_info(a).shield
		local ok = pd.chr_set_shield(a, 4)
		local after = pd.chr_info(a).shield
		pd.chr_set_shield(a, before)
		return ok and math.abs(after - 4) < 0.01, string.format("%.2f -> %.2f", before, after)
	end)
	check("chr_alert", function()
		local ok = pd.chr_alert(a)
		return ok and pd.chr_info(a).alertness == 100, "alertness " .. pd.chr_info(a).alertness
	end)
	check("chr_target", function()
		local ok = pd.chr_target(a, b)
		local t = pd.chr_info(a)
		return ok and t.target_chrnum == b and not pd.chr_target(a, a),
			"target " .. tostring(t.target_chrnum) .. " want " .. b
	end)
	check("chr_calm", function()
		local ok = pd.chr_calm(a)
		local t = pd.chr_info(a)
		return ok and t.alertness == 0 and t.target_chrnum == nil, "alertness " .. t.alertness
	end)
	check("chr_cloak", function() return pd.chr_cloak(a, true) and pd.chr_cloak(a, false) end)
	check("chr_weapon", function()
		local w = pd.chr_weapon(a)
		st.origweapon = w
		return type(w) == "number" and w >= 0 and pd.chr_weapon(-7) == -1, "weapon " .. w
	end)
	check("chr_give_weapon", function()
		local ok = pd.chr_give_weapon(a, WEAPON_CMP150, true)
		local now = pd.chr_weapon(a)
		local back = pd.chr_give_weapon(a, st.origweapon)
		return ok and now == WEAPON_CMP150 and pd.chr_weapon(a) == st.origweapon
				and pd.chr_give_weapon(a, 200) == false and pd.chr_give_weapon(a, -3) == false,
			string.format("got %d, restored %s -> %d", now, tostring(back), pd.chr_weapon(a))
	end)
	check("chr_scale", function() return pd.chr_scale(a, 2) and pd.chr_scale(a, 0.5) end)
	check("chr_yscale", function() return pd.chr_yscale(a, 0.4) and pd.chr_yscale(a, 1) end)
	check("chr_hum", function() return pd.chr_hum(a) and pd.chr_hum(a, false) end)
	check("chr_armor", function()
		local h0 = pd.chr_info(b).health
		local ok = pd.chr_armor(b, 30)
		st.armorhealth = pd.chr_info(b).health
		return ok and st.armorhealth > h0 + 29, string.format("%.1f -> %.1f", h0, st.armorhealth)
	end)
	check("chr_armor_clear", function()
		local ok = pd.chr_armor_clear(b)
		local t = pd.chr_info(b)
		return ok and t.health <= t.maxhealth + 0.01, string.format("%.1f -> %.1f", st.armorhealth, t.health)
	end)
	check("chr_damage", function()
		local h0 = pd.chr_info(b).health
		local ok = pd.chr_damage(b, 0.5)
		local h1 = pd.chr_info(b).health
		return ok and h1 < h0 and not pd.chr_damage(b, 0), string.format("%.2f -> %.2f", h0, h1)
	end)
	check("chr_summon", function()
		local c = pick(n, 3)
		local ok = pd.chr_summon(c, 120, 0)
		local x, y, z = pd.chr_pos(c)
		return ok and dist2(x, z, px, pz) < 400 * 400,
			string.format("ok=%s dist %.0f", tostring(ok), math.sqrt(dist2(x, z, px, pz)))
	end)
	check("chr_yeet", function()
		return pd.chr_yeet(pick(n, 3), 50) and not pd.chr_yeet(pick(n, 3), 0 / 0)
	end)
	check("chr_set_body", function()
		local c = pick(n, 4)
		local ok = pd.chr_set_body(c, BODY_DD_GUARD)
		return ok and pd.chr_info(c) ~= nil, "ok=" .. tostring(ok)
	end)

	-- Spawns (friendly or inert).
	check("spawn_at_chr", function() return pd.spawn_at_chr(a, WEAPON_FALCON2) and not pd.spawn_at_chr(-3, WEAPON_FALCON2) end)
	check("spawn", function() return pd.spawn(WEAPON_FALCON2, px + 40, py, pz) end)
	check("spawn_ally", function()
		local c = pd.spawn_ally(WEAPON_CMP150)
		return c ~= nil and pd.chr_info(c) ~= nil and pd.chr_weapon(c) == WEAPON_CMP150, "chr " .. tostring(c)
	end)
	check("spawn_ally_clone", function()
		local c = pd.spawn_ally_clone(0.5, 0.4)
		return c ~= nil and pd.chr_info(c) ~= nil, "chr " .. tostring(c)
	end)
	check("clone_chr", function()
		local x, y, z = pd.chr_pos(b)
		local c = pd.clone_chr(b, x + 60, y, z)
		st.clone = c
		return c ~= nil and pd.chr_info(c) ~= nil, "chr " .. tostring(c)
	end)
	check("spawn_body", function()
		local got, tries = -1, 0
		for _, off in ipairs({ { 300, 0 }, { -300, 0 }, { 0, 300 }, { 0, -300 }, { 150, 150 } }) do
			tries = tries + 1
			got = pd.spawn_body(BODY_DD_GUARD, WEAPON_FALCON2, off[1], off[2])
			if got >= 0 then break end
		end
		local sk = pd.spawn_body(BODY_SKEDAR, -1, 0, 200)
		st.twin = got
		return got >= 0 and pd.chr_info(got) ~= nil,
			string.format("chr %d after %d tries; skedar %d", got, tries, sk)
	end)

	-- Things undone next phase.
	st.ko = pick(n, 5)
	check("chr_ko", function()
		local ok = pd.chr_ko(st.ko)
		return ok and not pd.chr_ko(st.ko), "chr " .. st.ko
	end)
	check("possess_spawn", function()
		st.cube = pd.possess_spawn()
		return st.cube ~= nil and pd.chr_info(st.cube) ~= nil, "chr " .. tostring(st.cube)
	end)
	check("possess_spawn twice", function()
		-- one cube at a time
		return pd.possess_spawn() == nil
	end)
end

local function phase2()
	check("chr_wake", function()
		local ok = pd.chr_wake(st.ko)
		return ok and not pd.chr_wake(st.ko), "chr " .. st.ko
	end)
	check("unpossess", function()
		local r = pd.unpossess()
		return r == nil and pd.player_pos() ~= nil
	end)
	check("body_snatch", function()
		local n = npcs()
		local c, ok, tried = nil, false, {}
		local cx, cy, cz
		st.homex, st.homey, st.homez = pd.player_pos()
		for i = 6, #n do
			c = n[i]
			cx, cy, cz = pd.chr_pos(c)
			ok = pd.body_snatch(c)
			if ok then break end
			local t = pd.chr_info(c)
			tried[#tried + 1] = string.format("%d(hp %.1f alert %d)", c, t and t.health or -1, t and t.alertness or -1)
		end
		pd.log("chrs smoke: body_snatch refused " .. table.concat(tried, ","))
		local x, y, z = pd.player_pos()
		st.snatched = ok
		return ok and pd.chr_info(c) == nil and dist2(x, z, cx, cz) < 1,
			string.format("ok=%s chr %d, moved %.0f, off target %.1f", tostring(ok), c,
				math.sqrt(dist2(x, z, st.homex, st.homez)), math.sqrt(dist2(x, z, cx, cz)))
	end)
end

local function phase3()
	check("body_unsnatch", function()
		local ok = pd.body_unsnatch()
		local x, y, z = pd.player_pos()
		return ok and dist2(x, z, st.homex, st.homez) < 1,
			string.format("back within %.1f", math.sqrt(dist2(x, z, st.homex, st.homez)))
	end)
	local n = npcs()
	local px, py, pz = pd.player_pos()
	local far = n[#n]
	check("explosion", function() return pd.explosion(far, 9) and not pd.explosion(-2) end)
	check("explosion_at", function()
		local x, y, z = pd.chr_pos(pick(n, #n - 1))
		return pd.explosion_at(x, y, z, 9)
	end)
	check("grenade", function()
		local x, y, z = pd.chr_pos(pick(n, #n - 2))
		return pd.grenade(x, y, z, pick(n, #n - 2))
	end)
	check("spawn_chopper", function() return pd.spawn_chopper(0) and pd.spawn_chopper(1) and pd.spawn_chopper(0) end)
	check("spawn_bike", function() return pd.spawn_bike() and pd.spawn_bike() end)
	check("spawn_sentry", function() return pd.spawn_sentry(200, 0) and pd.spawn_sentry(-200, 0) end)
	check("explosions_around", function() return pd.explosions_around(true) end)
	check("nbomb", function() return pd.nbomb() end)
end

local function phase4()
	check("explosions_around_off", function() return pd.explosions_around(false) end)
	local missing = {}
	for _, k in ipairs(ALL) do
		if not seen[k] then missing[#missing + 1] = k end
	end
	pd.log(string.format("chrs smoke done %d/%d%s", results.pass, results.total,
		#missing > 0 and (" (not called: " .. table.concat(missing, ",") .. ")") or ""))
end

pd.on("tick", function()
	ticks = ticks + 1
	if phase == 4 and ticks - phasetick == 120 then
		-- The game is still ticking after the hostile spawns and explosions.
		local x = pd.player_pos()
		pd.log(string.format("chrs smoke alive %d ticks after the last phase (player %s)",
			ticks - phasetick, x and "present" or "gone"))
	end
	if phase >= 4 or pd.player_count() < 1 or pd.player_pos() == nil or ticks < 30 then
		return
	end
	if ticks - phasetick < 30 and phase > 0 then
		return
	end
	phase = phase + 1
	phasetick = ticks
	local fn = ({ phase1, phase2, phase3, phase4 })[phase]
	local ok, err = pcall(fn)
	if not ok then
		report("phase" .. phase, false, err)
	end
end)
