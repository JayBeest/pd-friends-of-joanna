#include <ultra64.h>
#include "constants.h"
#include "types.h"
#include "lib/snd.h"
#include "lib/sndcue.h"

#ifndef PLATFORM_N64

/**
 * See lib/sndcue.h for what this is for.
 *
 * ON THE BOUNDARY MODES, because one obvious option is missing and its absence
 * is a finding rather than an oversight: there is no "next loop point". A
 * compact sequence loops by rewinding curLoc[track] in the byte stream
 * (n_csq.c, AL_CMIDI_LOOPEND_CODE) while lastTicks keeps accumulating, so a
 * loop is not observable from out here at all -- and loops are per track, so
 * different tracks can be in different iterations and there is no single
 * boundary even in principle. What is left is a fixed grid from sequence start:
 * a beat is one quarter and needs no meter, a bar is BeatsPerBar of them and
 * needs a guess. Beat mode is the one that cannot be wrong.
 */

s32 g_SndCueEnabled = 0;
s32 g_SndCueQuantise = SNDCUE_QUANT_BAR;
s32 g_SndCueBeatsPerBar = 4;
s32 g_SndCueSlot = 0;
s32 g_SndCueTriggerSfx = -1;
s32 g_SndCueMaskFull = 0xffff;
s32 g_SndCueMaskDucked = 0x0003;
s32 g_SndCueHoldTicks = 0;

static bool g_SndCuePending = false;
static s32 g_SndCueAtTick = 0;
static s32 g_SndCueAction = SNDCUE_ACT_NONE;
static s32 g_SndCueArg = 0;
static bool g_SndCueDucked = false;
static s32 g_SndCueHoldLeft = 0;
static s32 g_SndCueLastSfx = -1;
static s32 g_SndCuePrevTick = -1;

/**
 * The next tick the given mode will allow a change to land on.
 *
 * Returns now when it cannot work one out -- no sequence playing, or a
 * division it cannot read -- so a cue never gets stranded waiting for a
 * boundary that will not arrive.
 */
static s32 sndcueNextBoundary(s32 now)
{
	s32 tickspq;
	s32 grid;

	if (g_SndCueQuantise == SNDCUE_QUANT_NOW) {
		return now;
	}

	tickspq = snddebugGetTicksPerQuarter(g_SndCueSlot);

	if (tickspq <= 0) {
		return now;
	}

	grid = tickspq;

	if (g_SndCueQuantise == SNDCUE_QUANT_BAR) {
		s32 beats = g_SndCueBeatsPerBar > 0 ? g_SndCueBeatsPerBar : 4;
		grid = tickspq * beats;
	}

	return now + (grid - (now % grid));
}

static void sndcueSchedule(s32 action, s32 arg)
{
	s32 now = snddebugGetTicks(g_SndCueSlot);

	// One slot, so rescheduling replaces rather than queues. That is deliberate
	// -- three intensity changes stacked up across three bars because a
	// firefight was noisy is exactly the failure this is meant to avoid.
	g_SndCueAction = action;
	g_SndCueArg = arg;
	g_SndCueAtTick = sndcueNextBoundary(now);
	g_SndCuePending = true;
}

static void sndcueFire(void)
{
	if (g_SndCueAction == SNDCUE_ACT_MASK) {
		snddebugSetChanMask(g_SndCueSlot, (u16)g_SndCueArg);
		g_SndCueDucked = (g_SndCueArg != g_SndCueMaskFull);

		if (g_SndCueDucked) {
			g_SndCueHoldLeft = g_SndCueHoldTicks;
		}
	}

	g_SndCuePending = false;
	g_SndCueAction = SNDCUE_ACT_NONE;
}

void sndcueTrigger(void)
{
	sndcueSchedule(SNDCUE_ACT_MASK, g_SndCueMaskDucked);
}

/**
 * Called from sndStart for every sound the game plays, which is a lot -- every
 * footstep, casing and ricochet. The enabled check is first and is the whole
 * reason this is affordable on that path.
 */
void sndcueOnSfx(s32 soundnum)
{
	if (!g_SndCueEnabled) {
		return;
	}

	g_SndCueLastSfx = soundnum;

	if (g_SndCueTriggerSfx >= 0 && soundnum != g_SndCueTriggerSfx) {
		return;
	}

	if (g_SndCueDucked) {
		// Already down, so just hold it there longer rather than rescheduling.
		g_SndCueHoldLeft = g_SndCueHoldTicks;
		return;
	}

	sndcueTrigger();
}

void sndcueTick(void)
{
	s32 now;
	s32 delta;

	if (!g_SndCueEnabled) {
		// Drop the tick baseline while off, or the first frame after switching
		// back on reports every tick that passed in between as elapsed and
		// expires the hold instantly.
		g_SndCuePrevTick = -1;
		return;
	}

	now = snddebugGetTicks(g_SndCueSlot);
	delta = g_SndCuePrevTick < 0 ? 0 : now - g_SndCuePrevTick;
	g_SndCuePrevTick = now;

	// A sequence change restarts the tick count, so a cue waiting on a tick
	// that is now in the past would wait forever. Fire it and move on.
	if (delta < 0) {
		delta = 0;

		if (g_SndCuePending) {
			sndcueFire();
		}
	}

	if (g_SndCuePending && now >= g_SndCueAtTick) {
		sndcueFire();
	}

	// The hold runs in ticks, not frames, so it keeps its musical length when
	// the tempo changes -- and stops entirely when the music does.
	if (g_SndCueDucked && !g_SndCuePending && g_SndCueHoldTicks > 0) {
		g_SndCueHoldLeft -= delta;

		if (g_SndCueHoldLeft <= 0) {
			sndcueSchedule(SNDCUE_ACT_MASK, g_SndCueMaskFull);
		}
	}
}

void sndcueGetState(struct sndcuestate *out)
{
	out->nowtick = snddebugGetTicks(g_SndCueSlot);
	out->tickspq = snddebugGetTicksPerQuarter(g_SndCueSlot);
	out->pending = g_SndCuePending ? 1 : 0;
	out->attick = g_SndCueAtTick;
	out->action = g_SndCueAction;
	out->arg = g_SndCueArg;
	out->ducked = g_SndCueDucked ? 1 : 0;
	out->holdleft = g_SndCueHoldLeft;
	out->lastsfx = g_SndCueLastSfx;
	out->mask = (s32)snddebugGetChanMask(g_SndCueSlot);
}

#endif
