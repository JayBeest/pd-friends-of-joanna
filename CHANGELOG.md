# Changelog

## [friends-of-joanna-v0.4.0] - 2026-09-13

This one is mostly other people's work. @Winfro built the third person camera and
the stance system that hangs off it, @Zaknafein made getting shot and dying look
like something actually happened, and @JillyJane made the assets. Thank you.

### New Features

- **Third-person camera and a two-stance movement system** — The camera can step back behind Joanna, the body fades out when the camera is inside her, and movement splits into a hip-fire stance and an aim stance with the aim button as the door between them. Ships **off**: set `Stance.FojoMovement=1` in `pd.ini` to enable it. The camera toggle is bound to **V** by default (`CK_0400`). (Thank you @Winfro)
- **Jump, combat roll, flinch and melee for players and simulants** — Actions the engine only ever gave to solo Joanna are now available to players and simulants alike: jumping, the combat roll, flinching when shot, throwing with an arm, and the punch/kick melee chain. A roll now goes the way she is going, and a punch reaches from her rather than from the camera. (Thank you @Winfro and @Zaknafein)
- **Damage reactions that match the shot** — Deaths play the reaction the hit actually earned, blasts throw the body the way the blast should have, corpses are allowed to be corpses before they go, and a chr's bullet now carries a hit location. (Thank you @Zaknafein)
- **Reload animations** — The body reaches for the magazine, and the arms can do one thing while the legs do another. Off by default; `Stance.ReloadAnim=1` enables it and `Stance.ReloadAnimSpeed` tunes it (0.1–4.0). A reload is not something you do on the move.
- **Character proportions** — Per-character height, a per-axis matrix scale, and per-joint scale overrides, with an editor panel that names body parts from the model rather than from a hardcoded table. Type a height and it happens.
- **The no-pause mode** — A one-player multiplayer match no longer pauses, the pause option is hidden where it does not apply, and the solo menu can keep the world running behind it. The **Allow Pausing** cheat restores the old behaviour. Pause menu music drops to a fifth rather than cutting.
- **Drug blur as a dose** — Blur ramps out instead of dropping, and time spent in the pause menu accumulates a dose that decays normally once you unpause. Both curves are exposed on their own ImGui slider window.
- **ImGui debugger overlay** — A single-window overlay with panels for runtime state, memory, stage and time, entity lists with search and prop jump, a frame profiler with a flame graph and top view, an audio panel, a proportions panel, and a texture pipeline inspector that can compare a source texture against what the engine rendered, overlay UV triangles on a model, and export the result as PNG. Panel visibility persists between runs.
- **`mkfiletable` builds a mod's filetable** — A PDFT writer to pair with the reader, resolving `replaces` and `byId` entries, and resolving names against a source ROM so a mod can refer to vanilla files by name.
- **`mkfiletable` needs nothing but the ROM** — `replaces:` used to resolve against a snapshotted name map that lived outside this repo, so a plain clone could not build a mod's filetable at all. The base ROM carries its own file names and a file's slot in that table is its id, so the ROM you already have to supply is enough. `--vanilla` still works and still wins when given. Verified by building every shipped mod both ways and comparing bytes.
- **`modsetcheck` looks at a whole set of mods** — Checks an assembled mod set the way a release assembler would, rather than one mod at a time.
- **`pdsym` resolves a Windows crash log** — Turns the RVAs in a `pd.crash.log` into function names and source lines against the `pd*.exe` that produced it, and refuses the pairing when the binary does not explain the report. Builds are now stamped with the commit they were built from.
- **Mod texture ownership** — Each mod's texture slots start above the slots the previous mod used, so heads and bodies from different mods no longer render each other's textures. GEX heads load entirely from the GEX ROM, and a mod can declare a conditional JPN ROM source.
- **Character assets** — Willow Dark and Ace have head entries, and character bios move to a markdown source compiled by `tools/mkchrbios`. (Thank you @JillyJane for the raw asset files)

### Bug Fixes

- **Combat Simulator crashed on entry on Windows** — A stage whose setup carries no `INTROCMD_SPAWN` left the room list in `playerReset` uninitialised and handed it to the collision code anyway. Same defect on every platform; only the Windows build's stack residue indexed far enough out to fault. See the wiki's Spawn Pad Crash page.
- **Textures were misaligned on stock content** — A texture's uploaded dimensions and the divisor its UVs were normalised by had drifted apart: the import path sized from the tile rect while the renderer still divided by the TMEM stride, which `G_SETTILE` rounds up to a multiple of 8 bytes. Any texture whose row was not a whole number of 8-byte words was scaled slightly and slid across the surface. The uploaded size is now recorded on the texture cache entry and used as the divisor, so the two cannot disagree — including on a cache hit and for PNG override textures, whose size the tile never knew.
- **Out-of-bounds reads across the collision and mod paths** — Room numbers are now checked at both ends rather than only against the upper bound, the weather's room walk is bounded by the array it is walking, and a mod-replaced segment is no longer read past its end. (Thank you @Winfro for the latter two)
- **Runaway and malformed AI lists** — `chraiGetAilistLength` and `stageLoadAllAilistModels` compare the full opcode rather than a truncated one, the `CMD_PRINT` terminator scan is bounded, null aicmd handlers are no longer called, and an ailist that never ends is capped at 100k iterations.
- **Tagged file ids were truncated** — Mod file ids kept their full width through the lookup path, with accessors for the owner and local halves instead of hand-rolled masking.
- **Pink mod textures** — The per-mod texture slot allocator started inside the JPN ROM's vanilla texid range, so the renderer conflated a mod's slot with a vanilla one.
- **Menu volume lagged a step behind** the slider, and the menu model now buys its own memory instead of borrowing.
- **Stage pool and filetable context fixes** — Host stage pool selection, Fojo setup filetable contexts, and default-profile boot loading when the intro is skipped.

### Refactors and Cleanup

- **The all-solos switch is retired**, and `use_mod_files` and `force_vanilla` are deprecated. They are still parsed and ignored with a warning for this release so older configs keep loading, and **they are removed after 0.4.0** — delete the lines. Note that an unrecognised stage key aborts the whole config file, not just that block.
- **Character bios moved to markdown** — `src/game/chrbios.md` is the source of truth; `tools/mkchrbios` compiles it into `training.c`, which should never be hand-edited inside the sentinels.
- **`tools/wt`** gives each working session its own sparse worktree and sweeps the lock files that otherwise wedge the repository.
- **Groundwork for the Lua scripting layer, dormant** — a vendored Lua 5.4 runtime linked into the binary, and the `chraiLua*` accessors a script layer needs to step an AI list. Both land ahead of the code that uses them, so the intake can arrive in reviewable pieces. Nothing calls either in this release, and no mod can ship a script yet.

### Renamed / Identified Functions

- **`mpPauseIsAllowed` renamed to `pauseIsAllowed`** and moved to `cheats.c`, since it is no longer multiplayer-specific.
- **`CK_1000` identified as the jump button**, and `CK_0400` as the third-person toggle.
- **`bond2.unk1c` renamed to `bond2.look`** — it is the look vector.

### Full Changelog

<https://github.com/cylonicboom/pd-friends-of-joanna/compare/friends-of-joanna-v0.3.1...friends-of-joanna-v0.4.0>

## [friends-of-joanna-v0.3.1] - 2026-08-01

### Refactors and Cleanup

- **Removed duplicated player-2 mission option menu sections** — Consolidated the separate player-2 control/display menu blocks in `mainmenu.c` into shared handlers that resolve the active player at runtime.
- **Removed hardcoded global subtitle/mission-time toggles in menu flow** — Replaced direct global option wiring with per-player profile-backed accessors in the options and multiplayer setup path.

### Renamed / Identified Functions

- **`menudialog00103608` renamed to `endscreenAcceptMissionHandleDialog`** to clarify that it handles Accept Mission dialog flow for the endscreen retry/accept path.
- **Player-scoped subtitle helpers introduced**: `optionsGetInGameSubtitlesForPlayer`, `optionsGetCutsceneSubtitlesForPlayer`, `optionsSetInGameSubtitlesForPlayer`, `optionsSetCutsceneSubtitlesForPlayer`.

## [friends-of-joanna-v0.2.3] - 2026-03-14

### New Features

- **Classic Sight option in multiplayer settings** — Classic Sight and Show Lives are now configurable per-player in the multiplayer setup screen. Settings are stored in the extended INI rather than in the save file, so they no longer risk corrupting the save format.
- **Team lives HUD display** — The HUD now shows team lives remaining during team game modes, giving players a clear at-a-glance view of how many respawns the team has left.
- **Improved respawn logic for team missions** — Respawn behaviour in team modes now correctly accounts for the number of player lives remaining, so the game handles out-of-lives situations properly instead of allowing infinite respawns.
- **CITRAINING stage support for team modes** — Mission stage handling for team game modes now includes the CITRAINING stage.

### Bug Fixes

- **AI null-pointer crash on respawn** — AI characters could retain a stale reference to a player that was respawning, leading to a null-pointer access and potential crash. References are now cleared when a player respawns.
- **Keyboard input fix + vi insert shortcut** — Corrected a regression in keyboard input handling in the menu system and added a vi-style insert-mode shortcut for text entry fields.

### Full Changelog

<https://github.com/cylonicboom/pd-friends-of-joanna/compare/friends-of-joanna-v0.2.2...friends-of-joanna-v0.2.3>
