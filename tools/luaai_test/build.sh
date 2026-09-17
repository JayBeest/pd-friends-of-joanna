#!/bin/sh
# Builds and runs the standalone ailist -> Lua transpiler test.
# Needs only a C compiler: no ROM, no game build.
#
# The command length table is lifted from src/game/chrai.c at build time, so
# the test walks lists with the same lengths the game does, including the
# port-only opcodes. Build products go to $OUT (default: a temp dir), never
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

LUA_SRC=$(ls "$LUA_DIR"/*.c | grep -Ev '/(lua|luac|onelua)\.c$')

$CC -O2 -Wall -I"$LUA_DIR" -I"$OUT" \
	test.c \
	../../src/game/luaai_transpile.c \
	$LUA_SRC \
	-lm \
	-o "$OUT/luaai_test"
echo "--- running ---"
"$OUT/luaai_test"
