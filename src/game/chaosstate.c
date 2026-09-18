/**
 * Definitions of the state behind Kai's pd.* effect API, and its per-stage
 * reset.
 *
 * From the Perfect Dark Kai fork (be46717), where each global sat in the
 * engine file that reads it; the comments are Kai's, moved with them. See
 * game/chaosstate.h.
 */

#ifndef PLATFORM_N64
#include <ultra64.h>
#include "constants.h"
#include "types.h"
#include "game/chaosstate.h"

/* ---- src/game/bondgun.c ---- */

// Chaos "backfire": local player's shots leave 180 degrees behind them (set
// via pd.backfire; applied at the end of bgunCalculatePlayerShotSpread).
s32 g_ChaosBackfire = 0;
// Chaos "Weapon jam" (pd.weapon_jam): 1 = every trigger pull dry-fires;
// 2 = "jam v2": ~35% of pulls dry-fire and a shot that DOES fire drains the
// rest of the magazine (reload to clear). See the HANDSTATE_ATTACKEMPTY
// reroute in bgunTickInc + the drain at the clip-decrement site.
s32 g_ChaosWeaponJam = 0;
// Chaos "Inflated bullets" (pd.ammo_cost): each shot spends this many rounds
// from the clip (1 = normal); topped up at the same decrement site.
s32 g_ChaosAmmoCost = 1;
// Chaos "Temu Magazine" (pd.temu_mag): a knockoff mag — a reload still costs the
// FULL amount from the reserve, but only chambers a random fraction of it, so
// reloading no longer tops you off. Applied in bgun0f098df8; local player only.
s32 g_ChaosTemuMag = 0;
// Temu partial-clip memory for EVERY weapon (user call 2026-07-29): vanilla
// only remembers the crossbow/shotgun/magnums' partial clips across switches
// (gunroundsspent, 4 slots), so switching away and back handed any other gun
// a fresh mag — a free, animation-less reload that gutted Temu Magazine.
// Same mechanism, chaos-scoped side table: [hand][weaponnum][ammoindex],
// same countdown encoding as gunroundsspent (missing rounds in the high
// bits, decayed in bgunTickUnequippedReload so holstered guns trickle-reload
// one round per ~4.3s like the magnum family). Only read/written while
// g_ChaosTemuMag is set; cleared on toggle (bgunChaosTemuSpentClear).
u16 g_ChaosTemuSpent[2][96][2];
// Chaos "Quad handed" (pd.double_shots): every fire event takes twice the
// shots (with dual-wield that's four barrels' worth); ammo drains to match.
s32 g_ChaosDoubleShots = 0;
// Chaos "Quad handed" (pd.quad_top): re-render the two viewmodel guns under a
// 180-degree-rotated projection so a second pair appears hanging from the top
// of the screen. Consumed in bgunRender; reset in lvInit.
s32 g_ChaosQuadTopGuns = 0;
// Chaos "One Bullet Mags" (pd.one_bullet): clip capacity forced to 1 at the
// equip-time bake — reload after every shot. Applied after the quad-handed
// doubling so it always wins; existing loaded rounds are untouched until the
// next reload.
s32 g_ChaosOneBulletMags = 0;
// Chaos "WAYTOODANK Viewmodel" (pd.gun_fov): override the viewmodel's Gun FOV
// in degrees; 0 = off (use the player's configured gunfovy).
f32 g_ChaosGunFovOverride = 0.0f;
// Chaos "Pinball rounds" (pd.pinball): fired physics projectiles (rockets,
// grenade rounds) are converted at launch into the grenade secondary's
// Proximity Pinball — ballistic, bouncy, proximity-armed. See the conversion
// in bgunCreateFiredProjectile.
s32 g_ChaosPinball = 0;
// Chaos "Chaos Weapon Spread" (pd.spread): scales every weapon's shot spread
// (and the matching crosshair bloom) by this factor. 1 = normal, 0 = laser,
// large = wild. The effect randomises it; reset to 1 per stage in lv.c.
f32 g_ChaosSpreadMult = 1.0f;
// Chaos "Gangster" (pd.gangsta): force the sideways pose on permanently. ORed
// with the autoaim-driven gunctrl.gangsta below so the existing rotate/revert
// animation and state checks run unchanged. Cleared in lv.c's per-stage reset.
s32 g_ChaosGangstaForce = 0;
// Chaos gun-hide (pd.gun_hide — Blind bag's mystery weapon): skip the whole
// viewmodel render pass. Render-only — bgunTick* still runs, so firing,
// reloads and ammo behave normally; the gun and hands are just not drawn.
// Reset with the other chaos C globals in lv.c's stage reset.
s32 g_BgunHideGun = 0;

/* ---- src/game/bondmove.c ---- */

// Chaos "Gormless" (docs/PORT_CHAOS.md, pd.gormless): EVERYTHING backwards —
// movement (c1 stick, c2/twin-stick left stick, digital steps incl. dpad),
// look (stick + mouse both axes), and FIRE/AIM swapped. Applied at the input
// chokepoints in bmoveProcessInput: the c1+c2 stick negates, the mouse-look
// delta negate, the digital-step swap, and the shootbuttons/aimbuttons swap.
// (A 2026-07-28 detour made it look-only; user clarified full reversal is
// wanted — the real original bug was the c2 movement stick never flipping.)
s32 g_ChaosGormless = 0;
// Chaos "Australia mode" (pd.upside_down): the frame is rotated 180 by the
// renderer, so reverse the same movement + look axes as Gormless to keep the
// controls matched to the flipped view. Shares Gormless's chokepoints via OR.
s32 g_ChaosControlReverse = 0;
// Chaos "Inverted Look" (pd.invert_look): TOGGLE the player's own
// pitch-inversion setting at its single derivation point
// (movedata.invertpitch) so every consumer — mouse dy, stick analogpitch,
// aim-edge swivel — flips via the engine's own machinery, relative to
// however the player normally runs. (Formerly a parallel input-layer dy
// negate in input.c, which didn't ride the setting.)
s32 g_ChaosInvertLook = 0;
// Chaos "Cyclone Frenzy" (pd.gun_lock): for the effect's duration, force the
// SECONDARY fire function on both hands, hold the trigger down (auto-fire), and
// block weapon switching — the cycle offsets here + amOpen (the weapon menu) at
// its own definition. Local player, unpaused, alive.
s32 g_ChaosGunLock = 0;
// Chaos "Knife fight" (pd.knife_lock): block weapon switching so only the
// equipped knife can be used — but leave firing/functions alone (normal manual
// swings), unlike the Cyclone gun-lock. Shares the cycle-offset + amOpen chokes.
s32 g_ChaosKnifeLock = 0;
// Chaos "Secondaries only" (pd.force_secondary): pin both hands to the
// secondary weapon function each tick; trigger, switching and the weapon menu
// all stay normal (unlike gun_lock).
s32 g_ChaosForceSecondary = 0;
// Chaos "Button thief" (pd.button_block): these pad buttons are stripped from
// gameplay input reads (c1buttons below). Menus read the joy layer directly
// and keep working.
u32 g_ChaosButtonMask = 0;
// Chaos "Forced March" (pd.forced_march): the movement stick is pinned full
// forward each tick — the player cannot stop walking. Look input unaffected.
s32 g_ChaosForcedMarch = 0;
// Chaos "Itchy Trigger Finger" (pd.forced_fire): the trigger is held down for
// you — the equipped weapon fires continuously.
s32 g_ChaosForcedFire = 0;
// Chaos "Mag Dump" (pd.mag_dump): a single trigger press empties the whole clip
// — automatic weapons get the trigger held down, semi-autos get it rapidly
// pulsed (release/press every other tick, which each re-fires). Bullet weapons
// only (single/automatic SHOOT funcs); armed on the player's own press, disarmed
// when the clip runs dry (or a safety cap) so a held trigger just fires normally.
// g_ChaosMagDumpArmed is the live "currently dumping" latch. Both cleared in lvInit.
s32 g_ChaosMagDump = 0;
s32 g_ChaosMagDumpArmed = 0;
// Chaos "Take a break" (pd.player_freeze): zero the movement stick so the
// player is rooted in place. Mouse look and firing stay live — you can watch
// and shoot, you just can't move.
s32 g_ChaosPlayerFreeze = 0;
// Chaos "Trigger Happy" (pd.rapid_fire): while the player holds fire, the
// trigger is pulsed so semi-autos fire as fast as automatics. Unlike Mag Dump
// it never latches or stops at clip-empty — it only acts while you are actively
// firing, so it reads as "press = rapid fire". Cleared in lvInit.
s32 g_ChaosRapidFire = 0;
// Chaos "Permacrouch" (pd.forced_crouch): the stance is pinned to the lowest
// crouch (CROUCHPOS_SQUAT) each tick. Cleared in lvInit.
s32 g_ChaosForcedCrouch = 0;
// Chaos "Reload Denied" (pd.no_reload): every reload transition is refused in
// bgunSetState (bondgun.c externs this). Cleared in lvInit.
s32 g_ChaosNoReload = 0;
// Chaos "No Damage Except Headshots" (pd.headshots_only): chrDamage zeroes all
// non-HITPART_HEAD damage to the local human. Cleared in lvInit.
s32 g_ChaosHeadshotsOnly = 0;
// Chaos "Birthday party" (pd.headshot_boost): headshots land at x10 total
// instead of the vanilla x4 (x2 for Skedar), and every headshot emits a
// "headshot" Lua event from chrDamage. Cleared in lv.c's per-stage reset.
s32 g_ChaosHeadshotBoost = 0;

/* ---- src/game/bondwalk.c ---- */

// Chaos "Gotta go fast" (pd.player_speed): a straight multiplier on the local
// player's walk + strafe speed. 1.0 = normal. Applied in bwalkApplyMoveData
// after the vanilla speed multipliers (1.08 * speedboost), so it scales the
// real movement velocity — unlike the Combat Boost, which is bullet-time + a
// mere 1.25x forward ramp.
f32 g_ChaosPlayerSpeed = 1.0f;
// Chaos "Trapdoor" (pd.trapdoor): countdown of ticks during which the floor
// under the local player is forced far below them (bwalkUpdateVertical), so
// they fall through to the death plane. Set by chraiLuaTrapdoor, reset in lv.c.
s32 g_ChaosTrapdoorTicks = 0;
// Chaos "Ice Floor" (pd.ice_floor): scales the walk accel/decel (bwalkUpdateSpeed*)
// so the player accelerates slowly and keeps sliding. 1.0 = normal. Reset in lv.c.
f32 g_ChaosIceAccel = 1.0f;
// Separate decel/slide scale — see bwalkUpdateSpeedForwards. 1.0 = vanilla
// friction; lower = longer slides. pd.ice_floor mirrors accel into it when
// only one argument is given.
f32 g_ChaosIceDecel = 1.0f;

/* ---- src/game/chr.c ---- */

s32 g_ChaosYassify = 0;
f32 g_ChaosYassifyWaist = 0.75f;    // waist XZ cinch
f32 g_ChaosYassifyShoulder = 1.45f; // shoulder/bust XZ flare
f32 g_ChaosYassifyNeck = 1.25f;     // head scale
// Chaos "Freeze!" (pd.chr_freeze, docs/PORT_CHAOS.md): while set, every
// non-player chr's anim playback pauses (gate below) and NPC firing is
// suppressed (chrTickShoot, chraction.c).
s32 g_ChaosChrFreeze = 0;
// Chaos chr speed (pd.chr_speed): scales every non-player chr's animation
// playback rate — and with it their movement (anim root motion) and attack
// cadence. > 1 = Benny Hill guards, < 1 = zombie shuffle. 1.0 = off.
f32 g_ChaosChrSpeedMult = 1.0f;
// Chaos "Bayblade!" (pd.beyblade): while set, every non-player chr's model
// yaw is stomped each tick with an absolute frame-derived angle (~2 rev/s,
// per-chr phase from the chrnum so they desync) — absolute so AI facing
// writes can't unwind the spin. AI/movement/aim logic keeps running; only
// the rendered facing spins.
s32 g_ChaosBeyblade = 0;
// Chaos "Weeping Skedar" (pd.chr_freeze_one): freeze exactly ONE chr by
// chrnum (-1 = none) — the stalker only advances while unobserved; the Lua
// tick sets/clears the target from a view-cone test. Same anim-gate freeze
// as g_ChaosChrFreeze, plus the chrTickShoot fire gate.
s32 g_ChaosFreezeChrnum = -1;
// Chaos "Now you see me..." (pd.cloak_lock): while set, the player's cloak is
// unbreakable — firing doesn't drop it (chrUncloakTemporarily no-ops) and it
// neither drains nor requires cloak ammo (chrUpdateCloak gate below). Reset
// per stage in lvResetChaosPerStage.
s32 g_ChaosCloakLock = 0;
// Chaos "blood colour" (pd.blood_colour): 0xRRGGBB00|1 when set. Every body
// bleeds this colour — sparks, hit splats and floor drips all derive their
// palette from this function.
u32 g_ChaosBloodColour = 0;

/* ---- src/game/chraction.c ---- */

// Chaos "one punch" (docs/PORT_CHAOS.md, pd.one_punch): while set, a player's
// unarmed strike in chrDamage is lethal + launches the victim. Defined here
// (not in the pd helper block below) because chrDamage reads it first.
s32 g_ChaosOnePunch = 0;
// Chaos "Space Program" (pd.space_program): like one_punch but for GUN shots —
// every player bullet is a one-hit kill through armour and launches the victim
// with massive knockback. Read in chrDamage next to one_punch.
s32 g_ChaosSpaceProgram = 0;
// Chaos "Frag Out" (pd.frag_out): human enemies lob a grenade whenever they
// would fire — chrConsiderGrenadeThrow skips its probability/range gates.
s32 g_ChaosFragOut = 0;
// Chaos "Paintball" (pd.damage_scale): multiplies every chrDamage amount.
// 1.0 = off. Applied in chrDamage before the net broadcast.
f32 g_ChaosDamageScale = 1.0f;
// Chaos "No drops" (pd.no_drops): dead chrs keep their weapons in hand
// instead of dropping them (gate in chrBeginDeath's drop-items block).
s32 g_ChaosNoDrops = 0;
// Chaos Evil-twin/clone registry: chrnums of live twins, used to make them
// psychosis-immune (a twin damaged by the player must stay hostile, not flip to
// the psychosised "friendly" AI list — see chaosIsTwin's use in chraiLuaChrDamage).
s16 g_ChaosTwinChrnums[8];
// Non-static so lvResetChaosPerStage() can clear it on stage load (lvInit runs
// only once at boot in this port) — a mid-snatch stage change
// tears down the Lua state without calling stop(), and a stale "active" flag
// would keep guards from ever firing (and break real disguise missions).
s32 g_ChaosSnatchActive = 0;
s32 g_ChaosLuaHomeValid = 0;
// Non-static so lvResetChaosPerStage() can clear it on stage load (lvInit runs
// only once at boot in this port) — a mid-effect stage change
// tears down the Lua state without calling stop(), and a stale count would let a
// later restore write teams onto the wrong (recycled-chrnum) chrs.
s32 g_ChaosCivilWarCount = 0;

/* ---- src/game/dlights.c ---- */

// Chaos room tint (docs/PORT_CHAOS.md): a global colour multiplier applied to
// every room's lighting — the KotH hill-highlight math (kohHighlightRoom)
// generalised to all rooms, independent of lightop. Written by
// chraiLuaRoomTint (pd.room_tint), which also dirties every room so the
// reshade actually re-runs.
f32 g_ChaosRoomTintFrac[3] = {1.0f, 1.0f, 1.0f};
s32 g_ChaosRoomTintOn = 0;
u8 g_ChaosRoomHlMask[CHAOS_ROOMHL_MAX / 8];
s32 g_ChaosRoomHlCol[3] = {255, 64, 64};
s32 g_ChaosRoomHlOn = 0;

/* ---- src/game/endscreen.c ---- */

// Chaos "Game over?" (pd.game_over): while the fake mid-mission failed screen
// is up, force Mission Status = "Unknown" and Agent Status = "Missing" (set in
// l_pd_game_over, cleared on resume/restart). No effect on the real endscreen.
s32 g_ChaosGameOverStatus = 0;
// Chaos: latched true the moment the game pushes a COMPLETED solo/co-op mission
// endscreen (mission won, not failed/aborted). Read by chaos.lua to tear down
// all active effects + visual modes on success, just like returning to the hub.
// Cleared on the next stage load (lvInit).
s32 g_ChaosMissionComplete = 0;

/* ---- src/game/game_0b0fd0.c ---- */

// Chaos ammo swap (docs/PORT_CHAOS.md, pd.ammo_swap): while >= 0, every held
// gun fires this weapon's shot ("Everything Rockets"), but keeps its OWN
// animation, fire rate, and hand behaviour (like Paintball mode — we no longer
// swap the fire FUNCTION, which is what drove the animation/rate). Instead the
// swap is applied at SHOT CREATION only: gsetPopulateFromCurrentPlayer presents
// the swap weapon for hitscan shots + firing noise (Farsight/Tranq/LX), and the
// projectile path (bgunCreateFiredProjectile, driven from prop.c when the swap
// weapon is a projectile launcher) spawns rockets/grenades. Held gun-range
// weapons only (FALCON2..CROSSBOW, knife excluded); target validated at set time.
s32 g_ChaosAmmoSwapWeapon = -1;
// Chaos zoom scale (pd.zoom_scale): multiplies every weapon's aim-zoom FOV.
// > 1 = "negative zoom" (aiming zooms OUT); clamped so the result stays a
// renderable FOV. 1.0 = off.
f32 g_ChaosZoomMult = 1.0f;
// Chaos gun-sound override (pd.gun_sound / docs/PORT_CHAOS.md): while > 0,
// every weapon's fire sound resolves to this SFX. This function is the single
// chokepoint every fire-sound consumer reads (player hands in bondgun.c, NPC
// fire in chrUpdateFireslot, the demo player), so one hook covers them all.
// Weapons with NO shoot sound (melee/throwables return 0) stay silent.
s32 g_ChaosGunSfxOverride = 0;

/* ---- src/game/lang.c ---- */

// Chaos single-string override (pd.weapon_rename): while g_ChaosLangOverrideId
// is a real text id, langGet returns g_ChaosLangOverrideStr for it instead of
// the bank string. Used to relabel the "phone" (Psychosis Gun -> Nokia 3315)
// for the duration of the Phone Call effect; cleared when it ends. langGet is
// the single choke every name-display path funnels through, so one hook covers
// the HUD label, inventory menu, and pickup toast uniformly.
s32 g_ChaosLangOverrideId = -1;
s32 g_ChaosLangOverrideId2 = -1; // the weapon's SHORT name id (weapon wheel, scenario lines)
s32 g_ChaosRenamedWeapon = -1;   // weaponnum being renamed — mainmenu hides its inventory model
char g_ChaosLangOverrideStr[64] = { 0 };
// Chaos multi-id censor (pd.weapon_censor — Blind bag): every listed text id
// renders as "?????" wherever it appears. Covers the mystery gun's
// MANUFACTURER, DESCRIPTION and FIRE-MODE names (inventory menu + the HUD
// function overlay); the weapon NAME itself rides the single-override rename
// above. -1 = empty slot; ids[0] < 0 short-circuits the whole check.
s32 g_ChaosLangCensorIds[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
// Chaos text transforms (pd.uwuify / pd.piglatin): while set, every langGet
// string is transformed — mode 1 = UwUify (r/l -> w, R/L -> W, n+vowel ->
// ny+vowel), mode 2 = Pig Latin (leading consonants rotate to the tail +
// "ay"; vowel-initial words get "way"). %-sequences are copied VERBATIM in
// both modes — several callers use langGet results as sprintf FORMAT
// strings, so mangling a %s would be a crash, not a joke. Output lives in
// rotating pools of static buffers sized for everything one frame renders:
// a small pool for labels/messages plus a few BIG slots for long text
// (mission briefings, CI bios). Only absurdly long strings (> ~5KB) pass
// through untransformed (both transforms expand text, so the slots leave
// headroom; a pathological all-short-word string may truncate at the slot
// cap rather than overflow).
s32 g_ChaosUwuMode = 0; // 0 off, 1 uwu, 2 pig latin, 3 buttsbot, 4 scramble

/* ---- src/game/lv.c ---- */

// Chaos "Fake Crash" (pd.fake_crash, chraction.c): remaining REAL-TIME length of
// a hard sim freeze, in 240ths. Counted down in lvTick off diffframe240 and it
// releases itself — see the hook for why the release cannot live in Lua.
s32 g_ChaosFakeCrash240 = 0;
// Chaos level-load serial (pd.load_serial). Bumped once per level load, in the
// clear-on-load block above. chaos.lua's "did we leave gameplay" test used to be
// "did a tick OBSERVE a NULL player prop", which a same-mission restart can hide
// entirely: the stage number doesn't change, so the lua_State survives and the
// script's whole state rides through the reload. A monotonic counter can't be
// missed — the script compares it each tick and derives the reload instead of
// trying to catch it. Deliberately NOT reset here: it must keep climbing for the
// lifetime of the process, or a rollover to the same value reads as "no reload".
s32 g_ChaosLoadSerial = 0;
// Chaos "Terminator Vision" (pd.terminator): two render/aim overrides that have
// no natural home in the effect's Lua half.
//  - suppresses the IR goggle LENS + BINOCULAR frame (player.c) so the infrared
//    filter fills the whole view instead of being masked into a goggle cutout;
//  - forces the CMP150's threat detector on (the FUNCFLAG_THREATDETECTOR gate
//    below), so the red target boxes track enemies whatever gun is held.
// Cleared per stage with the rest.
s32 g_ChaosTerminator = 0;
// Chaos SUPERHOT (pd.time_stop, chraction.c): while set, lvTick freezes the
// game tick (lvupdate240 = 0, the pause mechanism) whenever the local player
// is giving no input. Non-static: chraiLuaTimeStop writes it; cleared above
// per stage and by the effect's stop().
s32 g_ChaosTimeStop = 0;
// SUPERHOT look continuity (consumed in bondmove.c): frames that emit zero
// sim ticks never run bondmove, so their mouse delta would be dropped —
// bank it here and bondmove adds it on the next ticking frame. That is ALL
// the compensation needed: bondmove's mlookscale (4/lvupdate240) already
// makes hip look tick-rate-independent, so the net camera motion tracks
// the mouse 1:1 at any time rate. (An additional inverse scale here was
// the "frozen look 4-5x too sensitive" regression — never re-add it.)
f32 g_ChaosLookBankX = 0.0f;
f32 g_ChaosLookBankY = 0.0f;

/* ---- src/game/objectives.c ---- */

// Chaos "objective scramble" (pd.objective_force): per-objective status
// override consulted in objectiveCheck. 0 = off, 1 = force INCOMPLETE,
// 2 = force COMPLETE. Cleared in lvReset + by chaos.lua's reset.
u8 g_ChaosObjectiveForce[MAX_OBJECTIVES];

/* ---- src/game/options.c ---- */

// Chaos "XBLA mode" (pd.autoaim): force aim assist on regardless of the
// player's option. Read-only override — the saved option is untouched.
s32 g_ChaosAutoAim = 0;

/* ---- src/game/player.c ---- */

// Chaos "No HUD" (pd.hud_off): skip rendering the HUD elements entirely —
// the same seven element sites the HUDVD brackets wrap. Gameplay untouched.
s32 g_ChaosHudOff = 0;

/* ---- src/game/playermgr.c ---- */

// Chaos FOV multiplier (docs/PORT_CHAOS.md, pd.fov_scale): same self-
// restoring setter-hook pattern as g_ChaosAspectMult below — playerTick
// re-sets the FOV every tick, so this is the only stable interception point.
f32 g_ChaosFovMult = 1.0f;
// Chaos aspect scale (docs/PORT_CHAOS.md, pd.aspect_scale): multiplier on the
// projection aspect ratio, applied here because playerTick recomputes and
// re-sets the natural aspect every tick (a one-shot write elsewhere would be
// immediately overwritten — and conversely, clearing the multiplier
// self-restores on the next tick). >1 stretches the world wide, <1 tall.
f32 g_ChaosAspectMult = 1.0f;

/* ---- src/game/prop.c ---- */

// Chaos "wireframe enemies" (pd.chr_wireframe): hostile chrs render as
// polygon outlines via the G_CHRWIREFRAME_EXT bracket in propRender.
s32 g_ChaosWireframeChrs = 0;
// Chaos "iPod Ad" (pd.ipod_ad): silhouette mode — walls a bright solid colour,
// chrs black, objects/weapons white, white wireframe edges. propRender brackets
// each prop class with a G_FLATFILL_EXT colour scope. g_ChaosIpodWall is the
// bright wall colour (0..255 RGB), synced to the renderer in bgTickPortals.
s32 g_ChaosIpodAd = 0;
u8 g_ChaosIpodWall[3] = { 0, 217, 140 };

/* ---- src/game/propobj.c ---- */

/**
 * Chaos "Rubber Objects" (pd.rubber_objects).
 *
 * Objects stamped with PROJECTILEFLAG_CHAOSRUBBER at their drop keep hopping
 * instead of settling after the vanilla 6 bounces. Read in the bounce handler
 * below and written in objSetDropped; cleared in lvResetChaosPerStage.
 *
 * Gating on the live global as well as the per-object mark means switching the
 * effect off settles everything on its next contact, so no mark sweep is
 * needed. Single-player only (see docs/PORT_CHAOS.md).
 */
s32 g_ChaosRubberObjects = 0;
// Chaos "Nitroglycerin" (pd.nitro): objCheckDestroyed above upgrades every
// destroyed object's explosion to the Crash Site ship blast while set.
s32 g_ChaosNitro = 0;
// Chaos "Booby-trapped doors" (pd.door_traps, chraction.c): a door that
// STARTS opening detonates. The open counter always counts (task sensor for
// the EULA/CAPTCHA "open a door" requirements via pd.door_opens()).
s32 g_ChaosDoorTraps = 0;
u32 g_ChaosDoorOpenCount = 0;
// Non-static so lvResetChaosPerStage (lv.c) can clear it: this one gates a
// RENDER path, so an effect still running at a stage change would otherwise
// leave the un-gate latched for the rest of the process.
s32 g_ChaosGasOn = false;

/* ---- src/game/splat.c ---- */

// Chaos "Max blood" (pd.max_blood): every hit splatters (the stock 1-in-3
// dry roll is bypassed) with a bigger burst, chrs jump straight to the max
// wounded-drip rate, and the hit spray is tripled (chr.c).
s32 g_ChaosMaxBlood = 0;

/* ---- src/game/wallhit.c ---- */

// Chaos "Paintball" (pd.paintball, docs/PORT_CHAOS.md): force paintball
// visuals for everyone regardless of the per-player Combat Sim option.
s32 g_ChaosPaintball = 0;

/* ---- src/lib/anim.c ---- */

// Chaos "Assert Authority" (pd.t_pose, docs/PORT_CHAOS.md): while set, every
// sampled joint rotation reads as zero, so all skeletal models render in
// their bind pose (the T-pose). Translations/scales are left alone — root
// motion still applies, so T-posed chrs glide around the level, which is
// the entire joke.
s32 g_ChaosTPose = 0;

/* ---- src/lib/music.c ---- */

// Chaos "DJ" (pd.music_rate): scale the sequenced music's TEMPO, not its pitch.
// Tempo lives in seqp->uspt (microseconds per sequence tick) — smaller = faster.
f32 g_ChaosMusicRate = 1.0f;

/* ---- src/lib/naudio/n_csplayer.c ---- */

// Chaos instrument shuffle (docs/PORT_CHAOS.md, pd.instrument_shuffle): while
// set, every MIDI program change picks a random instrument from the loaded
// bank instead of the one the song asked for. Same 1-byte extern contract as
// g_SndTonalInversion above. Program changes fire when a track (re)starts,
// so the chaos effect pairs the toggle with starting a song. Local xorshift-
// style LCG (audio thread — never call game RNG from here).
u8 g_ChaosInstrumentShuffle = 0;

/* ---- src/lib/snd.c ---- */

// Chaos SFX shuffle master switch (docs/PORT_CHAOS.md, pd.sfx_shuffle):
// applied in sndStart just before the sound-id validity check.
s32 g_ChaosSfxShuffle = 0;
// Chaos targeted SFX replace (pd.sfx_replace): ONE sound id plays as another
// (Mediguns swaps the weapon-pickup jingle for the keycard blip so pickups
// sound like health kits). -1 = off.
s32 g_ChaosSfxReplaceFrom = -1;
s32 g_ChaosSfxReplaceTo = -1;

/* ---- src/game/luaai_api.c ---- */

s32 g_LuaShowFps = 0; /* toggled by /fps; read by scripts/perf_overlay.lua via pd.perf() */
s32 g_LuaShowMem = 0; /* toggled by /mem */

/**
 * Kai's lvResetChaosPerStage plus the plain global clears from its lvReset,
 * in one place.
 *
 * Kai's note on why this runs from lvReset rather than lvInit: in the port
 * lvInit runs exactly ONCE, at boot, so clears placed there never ran on a
 * stage change and an effect still running at one latched forever (the worst
 * case was body-snatch, which made every non-player chr stop firing for the
 * rest of the process). The Lua state is torn down on a stage change without
 * running any effect's stop(), so the C side has to be self-sufficient.
 *
 * Only globals are cleared here. Kai's lvReset also resets renderer, audio
 * and input state (screen roll, vertex wobble, held audio, input delay, the
 * texture override, ...); each of those is reset by the code that owns it.
 */
void chaosStateResetPerStage(void)
{
	s32 i;

	/* lvResetChaosPerStage */
	g_ChaosSnatchActive = 0;
	g_ChaosCivilWarCount = 0;
	g_ChaosControlReverse = 0;
	g_ChaosGunLock = 0;
	g_ChaosKnifeLock = 0;
	g_ChaosCloakLock = 0;
	g_ChaosTimeStop = 0;
	g_ChaosMagDump = 0;
	g_ChaosMagDumpArmed = 0;
	g_ChaosPlayerSpeed = 1.0f;
	g_ChaosRubberObjects = 0;
	g_ChaosYassify = 0;
	g_ChaosGasOn = 0;
	g_ChaosFakeCrash240 = 0;
	g_ChaosTerminator = 0;
	g_ChaosLoadSerial++; // pd.load_serial: lets a script DERIVE a reload (see decl)

	for (i = 0; i < ARRAYCOUNT(g_ChaosTwinChrnums); i++) {
		g_ChaosTwinChrnums[i] = -1;
	}

	/* lvReset */
	g_ChaosMissionComplete = 0;
	g_ChaosLuaHomeValid = 0;

	for (i = 0; i < MAX_OBJECTIVES; i++) {
		g_ChaosObjectiveForce[i] = 0;
	}

	g_ChaosForceSecondary = 0;
	g_ChaosButtonMask = 0;
	g_ChaosAmmoCost = 1;
	g_ChaosAutoAim = 0;
	g_ChaosNitro = 0;
	g_ChaosMaxBlood = 0;
	g_ChaosBloodColour = 0;
	g_ChaosWeaponJam = 0;
	g_ChaosWireframeChrs = 0;
	g_ChaosDoubleShots = 0;
	g_ChaosQuadTopGuns = 0;
	g_ChaosDoorTraps = 0;
	g_ChaosDoorOpenCount = 0;
	g_ChaosLangOverrideId = -1; // weapon rename — never persist
	g_ChaosLangOverrideId2 = -1;
	g_ChaosRenamedWeapon = -1;
	g_ChaosGameOverStatus = 0;
	g_BgunHideGun = 0;

	for (i = 0; i < ARRAYCOUNT(g_ChaosLangCensorIds); i++) {
		g_ChaosLangCensorIds[i] = -1;
	}

	g_ChaosBeyblade = 0;
	g_ChaosOneBulletMags = 0;
	g_ChaosForcedMarch = 0;
	g_ChaosForcedFire = 0;
	g_ChaosRapidFire = 0;
	g_ChaosForcedCrouch = 0;
	g_ChaosNoReload = 0;
	g_ChaosHeadshotsOnly = 0;
	g_ChaosTrapdoorTicks = 0;
	g_ChaosIceAccel = 1.0f;
	// Not in Kai: pd.ice_floor sets both, so a stage change mid-effect left
	// the slide latched.
	g_ChaosIceDecel = 1.0f;
	g_ChaosSpreadMult = 1.0f;
	g_ChaosHudOff = 0;
	g_ChaosGunFovOverride = 0.0f;
	g_ChaosFreezeChrnum = -1;
	g_ChaosIpodAd = 0;
	g_ChaosUwuMode = 0;
	g_ChaosInvertLook = 0;
	g_ChaosSpaceProgram = 0;
	g_ChaosFragOut = 0;
	g_ChaosTemuMag = 0;
	g_ChaosHeadshotBoost = 0;
	g_ChaosGangstaForce = 0;

	// Not in Kai, which only ever reset what lvResetChaos touched. Everything
	// below is a chaos effect a script can leave latched: restarting a failed
	// mission used to keep the player frozen, the guns silent, the screen
	// stretched or every drop suppressed, with no script left running to turn
	// any of it off. Effects whose engine side re-reads the global every tick
	// (the FOV and aspect multipliers) self-restore once it is back to 1.
	g_ChaosPlayerFreeze = 0;
	g_ChaosGormless = 0;
	g_ChaosChrFreeze = 0;
	g_ChaosChrSpeedMult = 1.0f;
	g_ChaosDamageScale = 1.0f;
	g_ChaosOnePunch = 0;
	g_ChaosNoDrops = 0;
	g_ChaosBackfire = 0;
	g_ChaosPinball = 0;
	g_ChaosAmmoSwapWeapon = -1;
	g_ChaosZoomMult = 1.0f;
	g_ChaosGunSfxOverride = 0;
	g_ChaosFovMult = 1.0f;
	g_ChaosAspectMult = 1.0f;
	g_ChaosPaintball = 0;
	g_ChaosTPose = 0;
	g_ChaosSfxShuffle = 0;
	g_ChaosSfxReplaceFrom = -1;
	g_ChaosSfxReplaceTo = -1;
	g_ChaosInstrumentShuffle = 0;
	g_ChaosRoomTintOn = 0;

	for (i = 0; i < ARRAYCOUNT(g_ChaosRoomTintFrac); i++) {
		g_ChaosRoomTintFrac[i] = 1.0f;
	}
}
#endif
