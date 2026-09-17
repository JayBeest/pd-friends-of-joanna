-- Smoke test for the menus pd.* functions (src/game/luaai_api_menus.c).
-- The pd.* API it exercises is ported from Kai (be46717).
--
-- Run it as scripts/init.lua (relative to the game's working directory) and
-- load a solo stage. Once a player is live it calls every menus function from
-- the "tick" handler, logs a PASS or FAIL line for each through pd.log, and
-- ends with "menus smoke done N/M".
--
-- pd.game_over and pd.menu_lore each open a menu that pauses the stage, and
-- only one can open at a time. The script alternates between runs with a
-- persisted key: the first run opens the game-over screen, the next the lore
-- reader, and each checks that the other refuses while its menu is up.
--
-- It leaves three Director entries registered (an action, a checkbox under
-- "Smoke" and a slider under "Smoke/Sub") so the pause menu's Director tab
-- has something to show; their callbacks log when the menu calls them.

local results = { pass = 0, total = 0 }
local ticks = 0
local live = 0
local stepat = nil
local done = false

local function report(name, ok, detail)
	results.total = results.total + 1
	if ok then
		results.pass = results.pass + 1
	end
	pd.log(string.format("menus smoke %s %s%s", ok and "PASS" or "FAIL", name,
		detail and (" (" .. tostring(detail) .. ")") or ""))
end

local function check(name, fn)
	local ok, res, detail = pcall(fn)
	if not ok then
		report(name, false, res)
	else
		report(name, res, detail)
	end
end

local phase = pd.persist_get("menus_smoke_phase") == "lore" and "lore" or "gameover"
pd.persist_set("menus_smoke_phase", phase == "lore" and "gameover" or "lore")
pd.log("menus smoke loaded, phase " .. phase)

-- State the Director callbacks read and write.
local checked = false
local level = 3

local function register()
	local a = pd.menu_add("Smoke action", function()
		pd.log("menus smoke director action called")
	end, "", "Logs a line when selected.")
	local b = pd.menu_add_checkbox("Smoke checkbox", function()
		pd.log("menus smoke director checkbox get " .. tostring(checked))
		return checked
	end, function(v)
		checked = v
		pd.log("menus smoke director checkbox set " .. tostring(v))
	end, "Smoke", "A checkbox in a submenu.")
	local c = pd.menu_add_slider("Smoke slider", function()
		return level
	end, function(v)
		level = v
		pd.log("menus smoke director slider set " .. tostring(v))
	end, 0, 10, "Smoke/Sub", "A slider in a nested submenu.")
	return a, b, c
end

local function registry()
	check("menu_clear", function()
		pd.menu_clear()
		-- After a clear the next entry is index 0 again.
		local i = pd.menu_add("probe", function() end)
		pd.menu_clear()
		local j = pd.menu_add("probe", function() end)
		pd.menu_clear()
		return i == 0 and j == 0, i .. "," .. j
	end)

	local a, b, c
	check("menu_add", function()
		a, b, c = register()
		local bad = pcall(pd.menu_add, "no function", 5)
		return a == 0 and not bad, "index " .. tostring(a)
	end)
	check("menu_add_checkbox", function()
		local bad = pcall(pd.menu_add_checkbox, "no setter", function() end)
		return b == 1 and not bad, "index " .. tostring(b)
	end)
	check("menu_add_slider", function()
		local bad = pcall(pd.menu_add_slider, "no range", function() end, function() end)
		return c == 2 and not bad, "index " .. tostring(c)
	end)
	check("menu_set_label", function()
		pd.menu_set_label(a, "Smoke action (renamed)")
		pd.menu_set_label(99, "out of range")
		pd.menu_set_label(-1, "out of range")
		local bad = pcall(pd.menu_set_label, 0)
		-- A failed registration must not have taken a slot: the next is 3.
		local d = pd.menu_add("Smoke probe", function() end)
		return not bad and d == 3, "next index " .. tostring(d)
	end)
	check("menu_add_slider bounds", function()
		-- a max of 0 would divide by zero in the menu: clamped, still added
		local z = pd.menu_add_slider("zero max", function() return 0 end, function() end, 0, 0)
		pd.menu_set_label(z, "Smoke \226\128\148 label") -- non-ASCII, scrubbed
		return z == 4, "index " .. tostring(z)
	end)
	check("registry full", function()
		local last, n = nil, 5
		while true do
			if n == 300 and pd.menu_add_slider("late", function() return 0 end, function() end, 0, 1) ~= -1 then
				return false, "slider past entry 255 accepted"
			end
			last = pd.menu_add("fill " .. n, function() end, "Fill")
			if last < 0 then
				break
			end
			n = n + 1
			if n > 10000 then
				return false, "never filled"
			end
		end
		pd.menu_clear()
		local a2, b2, c2 = register()
		return n == 640 and a2 == 0 and b2 == 1 and c2 == 2, n .. " entries"
	end)
end

local function opener(name, fn)
	local r = fn()
	report(name, r == true, "returned " .. tostring(r))
	return r
end

local function refuses(name, fn)
	local r = fn()
	report(name .. " refuses under a menu", r == false, "returned " .. tostring(r))
end

pd.on("tick", function()
	ticks = ticks + 1
	if done then
		return
	end
	if not pd.player_pos() then
		live = 0
		return
	end
	live = live + 1

	if live == 60 then
		pd.log("menus smoke start, lvupdate " .. tostring(pd.lvupdate()))
		registry()
		if phase == "gameover" then
			opener("game_over", pd.game_over)
			refuses("menu_lore", pd.menu_lore)
			refuses("game_over", pd.game_over)
		else
			opener("menu_lore", pd.menu_lore)
			refuses("game_over", pd.game_over)
			refuses("menu_lore", pd.menu_lore)
		end
		stepat = ticks
	elseif stepat and ticks == stepat + 30 then
		-- The menu is still up a moment later, so both still refuse. (This
		-- build keeps the world running behind menus, so the level clock
		-- is no evidence either way.)
		check(phase .. " menu stays up", function()
			local g, l = pd.game_over(), pd.menu_lore()
			return g == false and l == false,
				"game_over " .. tostring(g) .. ", menu_lore " .. tostring(l)
		end)
		pd.log(string.format("menus smoke done %d/%d", results.pass, results.total))
		done = true
	end
end)
