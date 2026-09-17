// Command lengths for mod aicmd opcodes. Kept free of game headers so
// tools/luaai_test can compile it as is.
#ifndef PLATFORM_N64
#include <PR/ultratypes.h>
#include "system.h"
#include "game/chraicmdlen.h"

// 0 means unregistered: no real command is shorter than its 2 opcode bytes.
static u16 g_ModLocalCommandLengths[MOD_AICMD_LOCAL_COUNT];
static u16 g_ModPortCommandLengths[MOD_AICMD_PORT_COUNT];

// One bit per 16-bit opcode, set once its missing length has been logged.
static u8 g_ModCommandLengthWarned[0x10000 / 8];
static s32 g_ModCommandLengthWarnCount;

#define MOD_CMDLEN_MAX_WARNINGS 64

static u16 *chraiModCommandLengthSlot(s32 op)
{
	if (op >= MOD_AICMD_LOCAL_BASE && op < MOD_AICMD_LOCAL_BASE + MOD_AICMD_LOCAL_COUNT) {
		return &g_ModLocalCommandLengths[op - MOD_AICMD_LOCAL_BASE];
	}

	if (op >= MOD_AICMD_PORT_BASE && op < MOD_AICMD_PORT_BASE + MOD_AICMD_PORT_COUNT) {
		return &g_ModPortCommandLengths[op - MOD_AICMD_PORT_BASE];
	}

	return NULL;
}

s32 chraiSetModCommandLength(s32 op, u32 len)
{
	u16 *slot = chraiModCommandLengthSlot(op);

	if (!slot) {
		sysLogPrintf(LOG_WARNING, "chrai: opcode 0x%04x is outside the mod aicmd windows, length not registered", op);
		return 0;
	}

	if (len < 2 || len > MOD_AICMD_MAX_LENGTH) {
		sysLogPrintf(LOG_WARNING, "chrai: opcode 0x%04x length %u out of range 2-%u", op, len, MOD_AICMD_MAX_LENGTH);
		return 0;
	}

	if (*slot != 0 && *slot != len) {
		sysLogPrintf(LOG_WARNING, "chrai: opcode 0x%04x already has length %u, refusing %u", op, *slot, len);
		return 0;
	}

	*slot = (u16)len;
	return 1;
}

void chraiClearModLocalCommandLengths(void)
{
	s32 i;

	for (i = 0; i < MOD_AICMD_LOCAL_COUNT; i++) {
		g_ModLocalCommandLengths[i] = 0;
	}
}

void chraiResetModCommandLengthWarnings(void)
{
	s32 i;

	for (i = 0; i < (s32)sizeof(g_ModCommandLengthWarned); i++) {
		g_ModCommandLengthWarned[i] = 0;
	}

	g_ModCommandLengthWarnCount = 0;
}

u32 chraiGetModCommandLength(s32 op)
{
	u16 *slot = chraiModCommandLengthSlot(op);

	if (slot && *slot) {
		return *slot;
	}

	// Unknown: step one byte, as before, but say so. A walker stepping a
	// command one byte at a time resyncs on garbage, so this is the only sign.
	op &= 0xffff;

	if (!(g_ModCommandLengthWarned[op >> 3] & (1 << (op & 7)))) {
		g_ModCommandLengthWarned[op >> 3] |= 1 << (op & 7);

		if (g_ModCommandLengthWarnCount < MOD_CMDLEN_MAX_WARNINGS) {
			sysLogPrintf(LOG_WARNING, "chrai: opcode 0x%04x has no known length (%s), stepping 1 byte",
					op,
					op >= MOD_AICMD_LOCAL_BASE && op < MOD_AICMD_LOCAL_BASE + MOD_AICMD_LOCAL_COUNT ? "mod-local, unregistered"
					: op >= MOD_AICMD_PORT_BASE && op < MOD_AICMD_PORT_BASE + MOD_AICMD_PORT_COUNT ? "mod-port, unregistered"
					: "outside every window");
		} else if (g_ModCommandLengthWarnCount == MOD_CMDLEN_MAX_WARNINGS) {
			sysLogPrintf(LOG_WARNING, "chrai: further unknown opcode lengths not logged");
		}

		g_ModCommandLengthWarnCount++;
	}

	return 1;
}
#endif
