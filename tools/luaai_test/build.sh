#!/bin/sh
# Builds and runs the standalone ailist -> Lua transpiler test.
# Needs only a C compiler: no ROM, no game build.
#
# The command length table and chraiGetCommandLength() are lifted from
# src/game/chrai.c at build time, so the test walks lists with the same
# lengths the game does, including the port-only and mod opcodes. Build products go to $OUT (default: a temp dir), never
# into the tree.
set -e
cd "$(dirname "$0")"
CC="${CC:-cc}"
OUT="${OUT:-${TMPDIR:-/tmp}/luaai_test}"
LUA_DIR=../../port/lua
mkdir -p "$OUT"

sed -n '/^u16 g_CommandLengths\[\] = {/,/^};/p' ../../src/game/chrai.c \
	| sed '1d;$d' > "$OUT/cmdlengths.inc"
if [ ! -s "$OUT/cmdlengths.inc" ]; then
	echo "could not extract g_CommandLengths from src/game/chrai.c" >&2
	exit 1
fi

# The real chraiGetCommandLength() too, so the test walks with the game's
# lookup, mod opcode registry (src/game/chraicmdlen.c) included.
sed -n '/^u32 chraiGetCommandLength(u8 \*ailist, u32 aioffset)$/,/^}/p' ../../src/game/chrai.c \
	> "$OUT/cmdlenfunc.inc"
if [ ! -s "$OUT/cmdlenfunc.inc" ]; then
	echo "could not extract chraiGetCommandLength from src/game/chrai.c" >&2
	exit 1
fi

# chraicmdlen.c only needs sysLogPrintf from system.h; test.c provides it.
cat > "$OUT/system.h" <<'EOT'
#include <PR/ultratypes.h>
enum { LOG_INFO, LOG_WARNING, LOG_ERROR };
void sysLogPrintf(s32 level, const char *fmt, ...);
EOT

# The game headers shadow libc names (math.h), so the game-side units are
# built on their own include path.
GAME_INC="-D_LANGUAGE_C -I$OUT -I../../include -I../../src/include"
$CC -O2 -Wall $GAME_INC -c cmdlen.c -o "$OUT/cmdlen.o"
$CC -O2 -Wall $GAME_INC -c ../../src/game/chraicmdlen.c -o "$OUT/chraicmdlen.o"

LUA_SRC=$(ls "$LUA_DIR"/*.c | grep -Ev '/(lua|luac|onelua)\.c$')

$CC -O2 -Wall -I"$LUA_DIR" \
	test.c \
	"$OUT/cmdlen.o" \
	"$OUT/chraicmdlen.o" \
	../../src/game/luaai_transpile.c \
	$LUA_SRC \
	-lm \
	-o "$OUT/luaai_test"
echo "--- running ---"
"$OUT/luaai_test"
