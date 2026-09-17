#ifndef _IN_GAME_CHAOSSTATE_H
#define _IN_GAME_CHAOSSTATE_H
#include <ultra64.h>
#include "constants.h"

#ifndef PLATFORM_N64
/*
 * State behind Kai's pd.* effect API (Perfect Dark Kai fork, be46717).
 *
 * Kai defined these next to the engine code that reads them, spread over 17
 * files; here every one is defined in game/chaosstate.c so the Lua bridges and
 * the engine read sites can land independently. The names and defaults are
 * Kai's. Every default is the "effect off" value, so nothing changes in play
 * until a script sets one.
 *
 * Grouped by the Kai file that defined the global.
 */

/* Rooms past this index are never highlighted (dlights.c). */
#define CHAOS_ROOMHL_MAX 512


/* src/game/bondgun.c */
extern s32 g_ChaosBackfire;
extern s32 g_ChaosWeaponJam;
extern s32 g_ChaosAmmoCost;
extern s32 g_ChaosTemuMag;
extern u16 g_ChaosTemuSpent[2][96][2];
extern s32 g_ChaosDoubleShots;
extern s32 g_ChaosQuadTopGuns;
extern s32 g_ChaosOneBulletMags;
extern f32 g_ChaosGunFovOverride;
extern s32 g_ChaosPinball;
extern f32 g_ChaosSpreadMult;
extern s32 g_ChaosGangstaForce;
extern s32 g_BgunHideGun;

/* src/game/bondmove.c */
extern s32 g_ChaosGormless;
extern s32 g_ChaosControlReverse;
extern s32 g_ChaosInvertLook;
extern s32 g_ChaosGunLock;
extern s32 g_ChaosKnifeLock;
extern s32 g_ChaosForceSecondary;
extern u32 g_ChaosButtonMask;
extern s32 g_ChaosForcedMarch;
extern s32 g_ChaosForcedFire;
extern s32 g_ChaosMagDump;
extern s32 g_ChaosMagDumpArmed;
extern s32 g_ChaosPlayerFreeze;
extern s32 g_ChaosRapidFire;
extern s32 g_ChaosForcedCrouch;
extern s32 g_ChaosNoReload;
extern s32 g_ChaosHeadshotsOnly;
extern s32 g_ChaosHeadshotBoost;

/* src/game/bondwalk.c */
extern f32 g_ChaosPlayerSpeed;
extern s32 g_ChaosTrapdoorTicks;
extern f32 g_ChaosIceAccel;
extern f32 g_ChaosIceDecel;

/* src/game/chr.c */
extern s32 g_ChaosYassify;
extern f32 g_ChaosYassifyWaist; // waist XZ cinch
extern f32 g_ChaosYassifyShoulder; // shoulder/bust XZ flare
extern f32 g_ChaosYassifyNeck; // head scale
extern s32 g_ChaosChrFreeze;
extern f32 g_ChaosChrSpeedMult;
extern s32 g_ChaosBeyblade;
extern s32 g_ChaosFreezeChrnum;
extern s32 g_ChaosCloakLock;
extern u32 g_ChaosBloodColour;

/* src/game/chraction.c */
extern s32 g_ChaosOnePunch;
extern s32 g_ChaosSpaceProgram;
extern s32 g_ChaosFragOut;
extern f32 g_ChaosDamageScale;
extern s32 g_ChaosNoDrops;
extern s16 g_ChaosTwinChrnums[8];
extern s32 g_ChaosSnatchActive;
extern s32 g_ChaosLuaHomeValid;
extern s32 g_ChaosCivilWarCount;

/* src/game/dlights.c */
extern f32 g_ChaosRoomTintFrac[3];
extern s32 g_ChaosRoomTintOn;
extern u8 g_ChaosRoomHlMask[CHAOS_ROOMHL_MAX / 8];
extern s32 g_ChaosRoomHlCol[3];
extern s32 g_ChaosRoomHlOn;

/* src/game/endscreen.c */
extern s32 g_ChaosGameOverStatus;
extern s32 g_ChaosMissionComplete;

/* src/game/game_0b0fd0.c */
extern s32 g_ChaosAmmoSwapWeapon;
extern f32 g_ChaosZoomMult;
extern s32 g_ChaosGunSfxOverride;

/* src/game/lang.c */
extern s32 g_ChaosLangOverrideId;
extern s32 g_ChaosLangOverrideId2; // the weapon's SHORT name id (weapon wheel, scenario lines)
extern s32 g_ChaosRenamedWeapon; // weaponnum being renamed — mainmenu hides its inventory model
extern char g_ChaosLangOverrideStr[64];
extern s32 g_ChaosLangCensorIds[8];
extern s32 g_ChaosUwuMode; // 0 off, 1 uwu, 2 pig latin, 3 buttsbot, 4 scramble

/* src/game/lv.c */
extern s32 g_ChaosFakeCrash240;
extern s32 g_ChaosLoadSerial;
extern s32 g_ChaosTerminator;
extern s32 g_ChaosTimeStop;
extern f32 g_ChaosLookBankX;
extern f32 g_ChaosLookBankY;

/* src/game/objectives.c */
extern u8 g_ChaosObjectiveForce[MAX_OBJECTIVES];

/* src/game/options.c */
extern s32 g_ChaosAutoAim;

/* src/game/player.c */
extern s32 g_ChaosHudOff;

/* src/game/playermgr.c */
extern f32 g_ChaosFovMult;
extern f32 g_ChaosAspectMult;

/* src/game/prop.c */
extern s32 g_ChaosWireframeChrs;
extern s32 g_ChaosIpodAd;
extern u8 g_ChaosIpodWall[3];

/* src/game/propobj.c */
extern s32 g_ChaosRubberObjects;
extern s32 g_ChaosNitro;
extern s32 g_ChaosDoorTraps;
extern u32 g_ChaosDoorOpenCount;
extern s32 g_ChaosGasOn;

/* src/game/splat.c */
extern s32 g_ChaosMaxBlood;

/* src/game/wallhit.c */
extern s32 g_ChaosPaintball;

/* src/lib/anim.c */
extern s32 g_ChaosTPose;

/* src/lib/music.c */
extern f32 g_ChaosMusicRate;

/* src/lib/naudio/n_csplayer.c */
extern u8 g_ChaosInstrumentShuffle;

/* src/lib/snd.c */
extern s32 g_ChaosSfxShuffle;
extern s32 g_ChaosSfxReplaceFrom;
extern s32 g_ChaosSfxReplaceTo;

/* luaai_api.c: overlay toggles read by pd.perf(). Kai flips them from its
 * console (/fps, /mem); here only scripts see them. */
extern s32 g_LuaShowFps;
extern s32 g_LuaShowMem;

/*
 * Clear the effect state that must not survive a stage change. Called from
 * lvReset. Covers the globals above only; effects that also hold renderer,
 * audio or input state reset that next to their own code. Bumps
 * g_ChaosLoadSerial.
 */
void chaosStateResetPerStage(void);
#endif

#endif
