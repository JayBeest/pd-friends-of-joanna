#ifndef _IN_LIB_SNDCUE_H
#define _IN_LIB_SNDCUE_H

#include <ultra64.h>
#include "types.h"

#ifndef PLATFORM_N64

/**
 * sndcue: game events in, musical changes out, landing on a boundary.
 *
 * The whole point is that a change asked for mid-firefight does not happen
 * mid-firefight -- it waits for a musical position so it does not sound like a
 * mistake. Nothing here calls into a script: a scripting layer decides what and
 * when, this decides exactly when and does it, so a script erroring cannot take
 * the music down with it.
 *
 * All of it is behind PLATFORM_N64 for now and hangs off sndTick, so the
 * granularity of the check is a game frame while the granularity of the
 * boundary is a sequence tick. At a two-second bar the difference is inaudible.
 */

// Where a cue is allowed to land.
#define SNDCUE_QUANT_NOW  0 // no waiting. the control condition -- without it
                            // you cannot tell the quantising is doing anything
#define SNDCUE_QUANT_BEAT 1 // next quarter note. needs no meter at all
#define SNDCUE_QUANT_BAR  2 // next Cue.BeatsPerBar quarters from sequence start

// What a cue does when it lands.
#define SNDCUE_ACT_NONE 0
#define SNDCUE_ACT_MASK 1 // set the sequence player's channel mask

extern s32 g_SndCueEnabled;
extern s32 g_SndCueQuantise;
extern s32 g_SndCueBeatsPerBar;
extern s32 g_SndCueSlot;
extern s32 g_SndCueTriggerSfx; // -1 = any sound at all
extern s32 g_SndCueMaskFull;
extern s32 g_SndCueMaskDucked;
extern s32 g_SndCueHoldTicks;  // 0 = stay ducked until something else says so

void sndcueOnSfx(s32 soundnum);
void sndcueTick(void);
void sndcueTrigger(void); // what the panel's Fire button calls

// For the panel. Everything here is read-only.
struct sndcuestate {
	s32 nowtick;
	s32 tickspq;
	s32 pending;    // is a cue waiting
	s32 attick;     // where it will land
	s32 action;
	s32 arg;
	s32 ducked;
	s32 holdleft;
	s32 lastsfx;    // last sound seen, so the trigger id can be learned
	s32 mask;       // the slot's live channel mask
};

void sndcueGetState(struct sndcuestate *out);

#endif

#endif
