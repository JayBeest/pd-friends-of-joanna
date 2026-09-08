#include <ultra64.h>
#include "constants.h"
#include "game/stancetuning.h"
#include "bss.h"
#include "data.h"
#include "types.h"

#ifndef PLATFORM_N64

f32 g_AimStanceSpeed = AIMSTANCE_SPEED;

f32 g_FlinchSpeed = FLINCH_SPEED;
s32 g_FlinchBusy = FLINCH_BUSY;
s32 g_FlinchBusyMax = FLINCH_BUSY_MAX;

f32 g_MeleeBodyReach = MELEE_BODY_REACH;
f32 g_MeleeConeCos = MELEE_CONE_COS;

f32 g_ThirdPersonCamDist = THIRDPERSON_CAMDIST;
f32 g_ThirdPersonCamClearance = THIRDPERSON_CAMCLEARANCE;
f32 g_ThirdPersonCamMinDist = THIRDPERSON_CAMMINDIST;

f32 g_BodyFadeStart = THIRDPERSON_BODYFADE_START;
f32 g_BodyFadeFloor = THIRDPERSON_BODYFADE_FLOOR;

f32 g_ReloadAnimSpeed = RELOAD_ANIMSPEED;

f32 g_RollImpulse = ROLL_IMPULSE;

s32 g_BuildSpeedEnabled = true;
f32 g_BuildSpeedRef = BUILD_SPEED_REF;
f32 g_BuildCrouchMix = BUILD_CROUCH_MIX;

/**
 * Put every knob back where it shipped.
 *
 * The defines are the defaults, so this is the whole of it - there is no second
 * table to keep in step, and adding a knob cannot forget to add its default.
 */
void stanceTuningReset(void)
{
	g_AimStanceSpeed = AIMSTANCE_SPEED;

	g_FlinchSpeed = FLINCH_SPEED;
	g_FlinchBusy = FLINCH_BUSY;
	g_FlinchBusyMax = FLINCH_BUSY_MAX;

	g_MeleeBodyReach = MELEE_BODY_REACH;
	g_MeleeConeCos = MELEE_CONE_COS;

	g_ThirdPersonCamDist = THIRDPERSON_CAMDIST;
	g_ThirdPersonCamClearance = THIRDPERSON_CAMCLEARANCE;
	g_ThirdPersonCamMinDist = THIRDPERSON_CAMMINDIST;

	g_BodyFadeStart = THIRDPERSON_BODYFADE_START;
	g_BodyFadeFloor = THIRDPERSON_BODYFADE_FLOOR;

	g_ReloadAnimSpeed = RELOAD_ANIMSPEED;
	g_RollImpulse = ROLL_IMPULSE;

	g_BuildSpeedEnabled = true;
	g_BuildSpeedRef = BUILD_SPEED_REF;
	g_BuildCrouchMix = BUILD_CROUCH_MIX;
}

#endif
