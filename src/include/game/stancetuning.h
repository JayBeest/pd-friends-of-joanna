#ifndef _IN_GAME_STANCETUNING_H
#define _IN_GAME_STANCETUNING_H
#include <ultra64.h>
#include "data.h"
#include "types.h"

#ifndef PLATFORM_N64

/**
 * The numbers behind the stance system, live rather than compiled in.
 *
 * Every one of these started as a #define in constants.h, and every one of them
 * was a guess that only play could settle. They are variables so that play can
 * settle them without a rebuild: the Fojo Stance panel in the overlay moves
 * them, and pd.ini's Stance.* keys set where they start. The #defines are still
 * there and are still the defaults - see constants.h for what each one means in
 * the code that reads it, and stance-tuning.md for what each one does to the
 * game.
 *
 * Anything here is read every frame by the thing it governs, so moving one
 * takes effect on the next.
 */

// The low ready: what fraction of her walk she keeps while aiming.
extern f32 g_AimStanceSpeed;

// A flinch: the fraction she is cut to at the moment of the hit, the window
// used when the flinch starts no animation, and the ceiling no reel may exceed.
extern f32 g_FlinchSpeed;
extern s32 g_FlinchBusy;
extern s32 g_FlinchBusyMax;

// Melee: how far past the weapon's own range a swing reaches from her body, and
// the cosine of the half angle either side of where she is looking. The panel
// shows the angle; this is what the sweep compares against.
extern f32 g_MeleeBodyReach;
extern f32 g_MeleeConeCos;

// The third person camera: how far back it wants to be, how far short of a wall
// it stops, and the distance under which it gives up and sits on the eye.
extern f32 g_ThirdPersonCamDist;
extern f32 g_ThirdPersonCamClearance;
extern f32 g_ThirdPersonCamMinDist;

// The body fade: the distance the fade starts at, and how much of her alpha it
// takes at its deepest. A floor of 1 would make her vanish outright.
extern f32 g_BodyFadeStart;
extern f32 g_BodyFadeFloor;

// Which animation parts keep walking while a one shot plays on the arms.
extern s32 g_AnimSplitLowerMask;

// What is left of her walk while she is reloading.
extern f32 g_ReloadSpeed;

// Whether the body reaches for the magazine at all, and how fast it does it.
extern s32 g_ReloadAnimEnabled;
extern f32 g_ReloadAnimSpeed;

// The combat roll's push.
extern f32 g_RollImpulse;

// Build and movement: the master switch, and the body height that walks at
// exactly 1. Everything scales as a delta from the reference, so a vanilla
// roster is untouched however the rest of this moves. The mix is how much of
// the crouch discount short characters actually get -- 0 is vanilla's flat
// multipliers, 1 charges each body for the fraction of herself she folds away.
extern s32 g_BuildSpeedEnabled;
extern f32 g_BuildSpeedRef;
extern f32 g_BuildCrouchMix;

void stanceTuningReset(void);

#endif

#endif
