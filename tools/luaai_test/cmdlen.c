/*
 * The game's command length lookup, built for the test: g_CommandLengths and
 * chraiGetCommandLength() are lifted verbatim from src/game/chrai.c by
 * build.sh, and mod opcodes go to the real src/game/chraicmdlen.c.
 */
#include <PR/ultratypes.h>
#include "game/chraicmdlen.h"

/* versions.h values; the PC port builds ntsc-final. */
#define VERSION_NTSC_1_0   1
#define VERSION_NTSC_FINAL 2
#define VERSION            VERSION_NTSC_FINAL

/* constants.h */
#define ARRAYCOUNT(a) (s32)(sizeof(a) / sizeof(a[0]))
#define CMD_PRINT     0x00b5

u16 g_CommandLengths[] = {
#include "cmdlengths.inc"
};

const u32 g_NumCommandLengths = ARRAYCOUNT(g_CommandLengths);

u32 chraiGetCommandLength(u8 *ailist, u32 aioffset);

#include "cmdlenfunc.inc"
