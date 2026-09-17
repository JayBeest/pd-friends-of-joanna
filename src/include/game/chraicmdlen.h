#ifndef _IN_GAME_CHRAICMDLEN_H
#define _IN_GAME_CHRAICMDLEN_H
#include <PR/ultratypes.h>

#ifndef PLATFORM_N64
/*
 * Command lengths for mod aicmd opcodes, which live outside g_CommandLengths.
 * See aicmd-namespacing-plan.md for the windows.
 *
 * chraiGetCommandLength() consults this only after the vanilla table misses,
 * so vanilla lookups are unchanged. Every list walker (label search,
 * chraiGetAilistLength, the Lua transpiler) goes through it.
 *
 * Mod-local window: what a mod's setup bytes carry before remap. Local
 * opcodes mean different verbs in different mods, so this table describes one
 * mod at a time: the loader clears it and fills it for the mod whose lists it
 * is about to walk.
 *
 * Mod-port window: engine-assigned, globally unique opcodes after remap.
 * Registered once at startup and never cleared.
 *
 * Fillers (not built yet): the modconfig AiCommands loader and
 * pd.register_aicmd. Both call chraiSetModCommandLength().
 */
#define MOD_AICMD_LOCAL_BASE  0x0400
#define MOD_AICMD_LOCAL_COUNT 0x0100 // 0x0400-0x04ff
#define MOD_AICMD_PORT_BASE   0x0800
#define MOD_AICMD_PORT_COUNT  0x0800 // 0x0800-0x0fff, bounded for now

// Longest command a mod may declare, opcode bytes included.
#define MOD_AICMD_MAX_LENGTH  0x0100

// Register the total length (opcode bytes included) of a mod opcode in either
// window. Returns 0 and logs if the opcode is outside both windows, the length
// is out of range, or a different length is already registered for it.
s32 chraiSetModCommandLength(s32 op, u32 len);

// Forget every mod-local length, before loading the next mod's declarations.
void chraiClearModLocalCommandLengths(void);

// Length of an opcode beyond g_CommandLengths. An opcode with no registered
// length keeps the old behaviour, length 1, and is logged once.
u32 chraiGetModCommandLength(s32 op);
#endif

#endif
