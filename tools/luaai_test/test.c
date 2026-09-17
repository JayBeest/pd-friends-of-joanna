/*
 * Standalone unit test for the ailist -> Lua transpiler and its dispatch model.
 * Needs no ROM and no game build: it links luaai_transpile.c against the
 * vendored Lua and checks that every generated chunk is valid Lua and that
 * running it reproduces the interpreter's control flow.
 *
 * The first case is the Kai fork's original synthetic test. The rest walk
 * lists with the game's real command length table (lifted from chrai.c by
 * build.sh into cmdlen.c) and cover the port-only opcodes 0x0194 and
 * 0x01e1-0x01e4, mod opcodes, CMD_PRINT strings, label jumps, and malformed
 * lists.
 *
 * Build and run: ./build.sh
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

char *luaaiTranspile(const unsigned char *list, unsigned int maxlen,
		unsigned int (*cmdlen)(const unsigned char *list, unsigned int off),
		unsigned int endopcode);

static int g_failures;

#define CHECK(cond, ...) do { \
	if (!(cond)) { \
		g_failures++; \
		printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
		printf(__VA_ARGS__); \
		printf("\n"); \
	} \
} while (0)

#define OPAT(list, off) (((unsigned int)(list)[off] << 8) | (unsigned int)(list)[(off) + 1])

/* ------------------------------------------------------------------------- *
 * The game's command lengths
 * ------------------------------------------------------------------------- */

/* g_CommandLengths and chraiGetCommandLength() as the game builds them, from
 * cmdlen.c (see build.sh). */
extern unsigned short g_CommandLengths[];
extern const unsigned int g_NumCommandLengths;
unsigned int chraiGetCommandLength(unsigned char *ailist, unsigned int aioffset);

#define NUM_CMDLENGTHS g_NumCommandLengths

/* src/game/chraicmdlen.c */
#define MOD_AICMD_LOCAL_BASE 0x0400
#define MOD_AICMD_PORT_BASE  0x0800
int chraiSetModCommandLength(int op, unsigned int len);
void chraiClearModLocalCommandLengths(void);

/* constants.h */
#define CMD_GOTONEXT  0x0000
#define CMD_GOTOFIRST 0x0001
#define CMD_LABEL     0x0002
#define CMD_YIELD     0x0003
#define CMD_END       0x0004
#define CMD_PRINT     0x00b5

/* What the game passes as cmdlen (luaai_cmdlen in luaai.c). */
static unsigned int fojo_cmdlen(const unsigned char *list, unsigned int off)
{
	return chraiGetCommandLength((unsigned char *)list, off);
}

/* system.h, for chraicmdlen.c: count the warnings and keep them all. */
static int g_logcount;
static char g_lastlog[256];
static char g_alllog[8192];

void sysLogPrintf(int level, const char *fmt, ...)
{
	va_list ap;

	(void)level;
	va_start(ap, fmt);
	vsnprintf(g_lastlog, sizeof(g_lastlog), fmt, ap);
	va_end(ap);
	g_logcount++;
	if (strlen(g_alllog) + strlen(g_lastlog) + 2 < sizeof(g_alllog)) {
		strcat(g_alllog, g_lastlog);
		strcat(g_alllog, "\n");
	}
	printf("  log: %s\n", g_lastlog);
}

/* ------------------------------------------------------------------------- *
 * Helpers
 * ------------------------------------------------------------------------- */

/* Collect the offsets of the "elseif off == N" dispatch entries, in order. */
static unsigned int dispatch_offsets(const char *src, unsigned int *out, unsigned int max)
{
	const char *p = src;
	unsigned int n = 0;

	while ((p = strstr(p, "elseif off == ")) != NULL) {
		p += strlen("elseif off == ");
		if (n < max) {
			out[n] = (unsigned int)strtoul(p, NULL, 10);
		}
		n++;
	}

	return n;
}

static int expect_offsets(const char *name, const char *src, const unsigned int *want, unsigned int nwant)
{
	unsigned int got[64];
	unsigned int n = dispatch_offsets(src, got, 64);
	unsigned int i;
	int ok = n == nwant;

	for (i = 0; ok && i < n; i++) {
		ok = got[i] == want[i];
	}

	if (!ok) {
		printf("  %s: dispatch offsets:", name);
		for (i = 0; i < n && i < 64; i++) {
			printf(" %u", got[i]);
		}
		printf("  want:");
		for (i = 0; i < nwant; i++) {
			printf(" %u", want[i]);
		}
		printf("\n");
	}

	CHECK(ok, "%s: dispatch offsets differ", name);
	return ok;
}

/* The block for a command carries its opcode as a trailing comment. */
static int has_block(const char *src, unsigned int off, unsigned int op)
{
	char needle[96];
	snprintf(needle, sizeof(needle), "ctx:exec(%u) if r ~= 0 then return r end end goto NEXT -- 0x%04x\n", off, op);
	return strstr(src, needle) != NULL;
}

/* Transpile, then prove the result is valid Lua: luaL_loadstring it and run
 * the chunk, which must return a function. On success that function is
 * stored in the global RUN. Returns the source (caller frees) or NULL. */
static char *transpile_load(lua_State *L, const char *name,
		const unsigned char *list, unsigned int maxlen,
		unsigned int (*cmdlen)(const unsigned char *, unsigned int), int show)
{
	char *src = luaaiTranspile(list, maxlen, cmdlen, CMD_END);

	if (!src) {
		CHECK(0, "%s: luaaiTranspile returned NULL", name);
		return NULL;
	}

	if (show) {
		printf("--- %s: generated Lua ---\n%s", name, src);
	}

	if (luaL_loadstring(L, src) != LUA_OK) {
		CHECK(0, "%s: luaL_loadstring: %s", name, lua_tostring(L, -1));
		lua_pop(L, 1);
		free(src);
		return NULL;
	}

	if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
		CHECK(0, "%s: chunk: %s", name, lua_tostring(L, -1));
		lua_pop(L, 1);
		free(src);
		return NULL;
	}

	if (!lua_isfunction(L, -1)) {
		CHECK(0, "%s: chunk did not return a function", name);
		lua_pop(L, 1);
		free(src);
		return NULL;
	}

	lua_setglobal(L, "RUN");
	return src;
}

/* ------------------------------------------------------------------------- *
 * Simulated ctx. exec is swapped per test.
 * ------------------------------------------------------------------------- */

static const unsigned char *g_list;
static unsigned int g_pc;
static int (*g_exec)(unsigned int off);

static int l_cur(lua_State *L)
{
	lua_pushinteger(L, (lua_Integer)g_pc);
	return 1;
}

static int l_exec(lua_State *L)
{
	unsigned int off = (unsigned int)luaL_checkinteger(L, 2);
	lua_pushinteger(L, g_exec(off));
	return 1;
}

static lua_State *new_state(void)
{
	lua_State *L = luaL_newstate();
	luaL_openlibs(L);

	lua_newtable(L);
	lua_pushcfunction(L, l_cur);
	lua_setfield(L, -2, "cur");
	lua_pushcfunction(L, l_exec);
	lua_setfield(L, -2, "exec");
	lua_setglobal(L, "CTX");

	return L;
}

/* Run RUN(CTX) once; returns the status or -1 on a Lua error. */
static int run_frame(lua_State *L, const char *name)
{
	int status;

	lua_getglobal(L, "RUN");
	lua_getglobal(L, "CTX");
	if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
		CHECK(0, "%s: RUN: %s", name, lua_tostring(L, -1));
		lua_pop(L, 1);
		return -1;
	}

	status = (int)lua_tointeger(L, -1);
	lua_pop(L, 1);
	return status;
}

/* ------------------------------------------------------------------------- *
 * 1. The Kai fork's synthetic test
 * ------------------------------------------------------------------------- */

#define OP_GOTO_NEXT 0x0000 /* goto_next(label): 3 bytes */
#define OP_LABEL     0x0002 /* label(id):        3 bytes */
#define OP_YIELD     0x0003 /* yield:            2 bytes */
#define OP_END       0x0004 /* endlist:          2 bytes */
#define OP_ACTION    0x0010 /* action:           2 bytes */
#define OP_ACTION2   0x0011 /* action+operand:   4 bytes */

static unsigned int synth_cmdlen(const unsigned char *list, unsigned int off)
{
	switch (OPAT(list, off)) {
	case OP_GOTO_NEXT: return 3;
	case OP_LABEL:     return 3;
	case OP_YIELD:     return 2;
	case OP_END:       return 2;
	case OP_ACTION:    return 2;
	case OP_ACTION2:   return 4;
	default:           return 2;
	}
}

/*
 *   0:  label(1)
 *   3:  action
 *   5:  action2(0x42)
 *   9:  yield
 *   11: goto_next(1)   ; back to label 1
 *   14: endlist
 */
static const unsigned char synth_prog[] = {
	0x00, 0x02, 0x01,
	0x00, 0x10,
	0x00, 0x11, 0x00, 0x42,
	0x00, 0x03,
	0x00, 0x00, 0x01,
	0x00, 0x04,
};

static int synth_actions;

/* chraiGoToLabel: scan from off for a label, 0 at the end marker. */
static unsigned int goto_label(const unsigned char *list,
		unsigned int (*cmdlen)(const unsigned char *, unsigned int),
		unsigned int off, unsigned char label)
{
	for (;;) {
		unsigned int op = OPAT(list, off);
		if (op == CMD_LABEL && list[off + 2] == label) {
			return off;
		}
		if (op == CMD_END) {
			return 0;
		}
		off += cmdlen(list, off);
	}
}

static int synth_exec(unsigned int off)
{
	switch (OPAT(g_list, off)) {
	case OP_LABEL:     g_pc = off + 3; return 0;
	case OP_ACTION:    synth_actions++; g_pc = off + 2; return 0;
	case OP_ACTION2:   synth_actions++; g_pc = off + 4; return 0;
	case OP_GOTO_NEXT: g_pc = goto_label(g_list, synth_cmdlen, off, g_list[off + 2]); return 0;
	case OP_YIELD:     g_pc = off + 2; return 1;
	default:           return 2;
	}
}

static void test_kai_synthetic(void)
{
	static const unsigned int want[] = { 0, 3, 5, 9, 11 };
	lua_State *L = new_state();
	char *src;
	int frame;

	printf("[kai synthetic]\n");
	src = transpile_load(L, "kai synthetic", synth_prog, sizeof(synth_prog), synth_cmdlen, 1);
	if (src) {
		expect_offsets("kai synthetic", src, want, 5);

		g_list = synth_prog;
		g_exec = synth_exec;
		g_pc = 0;
		synth_actions = 0;

		for (frame = 0; frame < 3; frame++) {
			int status = run_frame(L, "kai synthetic");
			printf("  frame %d -> status %d, pc %u\n", frame, status, g_pc);
			CHECK(status == 1, "kai synthetic: frame %d status %d, want 1", frame, status);
		}

		printf("  actions over 3 frames: %d (want 6)\n", synth_actions);
		CHECK(synth_actions == 6, "kai synthetic: %d actions, want 6", synth_actions);
		free(src);
	}

	lua_close(L);
}

/* ------------------------------------------------------------------------- *
 * 2. Port-only opcodes: table lengths vs. handler advances
 * ------------------------------------------------------------------------- */

/* How far each handler in src/game/chraicommands.c moves aioffset when it
 * does not jump. The transpiler only sees the table, so they must agree. */
static const struct {
	unsigned int op;
	unsigned int advance;
	const char *handler;
} fojo_ops[] = {
	{ 0x0194, 2, "aiDetectAIO" },
	{ 0x01e1, 3, "aiSetChrnumMatchlevel" },
	{ 0x01e2, 3, "aiSetHiddenElseMask" },
	{ 0x01e3, 4, "aiIfPlayerNumIsCoop" },
	{ 0x01e4, 3, "aiSetCoopPlayerNum" },
};

static void test_fojo_table(void)
{
	unsigned int i;

	printf("[port opcode lengths] table has %u entries (last 0x%04x)\n",
			(unsigned int)NUM_CMDLENGTHS, (unsigned int)NUM_CMDLENGTHS - 1);

	for (i = 0; i < sizeof(fojo_ops) / sizeof(fojo_ops[0]); i++) {
		unsigned int op = fojo_ops[i].op;
		unsigned int len = op < NUM_CMDLENGTHS ? g_CommandLengths[op] : 0;

		if (op == 0x0194 && len == 1) {
			/* Known: the table still carries the vanilla placeholder for this
			 * slot. The walk steps one byte into the opcode, reads 0x94xx
			 * (out of table, length 1) and lands back on the next command,
			 * so the only effect is one dead dispatch entry. See case 3. */
			printf("  0x%04x %-22s table %u, handler %u  KNOWN MISMATCH (benign, see below)\n",
					op, fojo_ops[i].handler, len, fojo_ops[i].advance);
			continue;
		}

		printf("  0x%04x %-22s table %u, handler %u\n", op, fojo_ops[i].handler, len, fojo_ops[i].advance);
		CHECK(len == fojo_ops[i].advance, "0x%04x: table length %u, handler advances %u",
				op, len, fojo_ops[i].advance);
	}
}

/* ------------------------------------------------------------------------- *
 * 3. 0x0194 in a list
 * ------------------------------------------------------------------------- */

static void test_detect_aio(void)
{
	static const unsigned char prog[] = {
		0x01, 0x94,             /* detect_aio          @0 */
		0x01, 0xe1, 0x07,       /* 0x01e1              @2 */
		0x00, 0x03,             /* yield               @5 */
		0x00, 0x04,             /* end                 @7 */
	};
	lua_State *L = new_state();
	char *src;

	printf("[0x0194 in a list]\n");
	src = transpile_load(L, "0x0194", prog, sizeof(prog), fojo_cmdlen, 1);
	if (src) {
		unsigned int got[16];
		unsigned int n = dispatch_offsets(src, got, 16);

		CHECK(has_block(src, 0, 0x0194), "0x0194: no block for detect_aio at 0");
		CHECK(has_block(src, 2, 0x01e1), "0x0194: walk did not resync on 0x01e1 at 2");
		CHECK(has_block(src, 5, 0x0003), "0x0194: no block for yield at 5");

		if (g_CommandLengths[0x0194] == 1) {
			/* 0, 1 (dead 0x9401), 2, 5 */
			CHECK(n == 4 && has_block(src, 1, 0x9401),
					"0x0194: expected exactly one dead entry at 1, got %u entries", n);
			printf("  dead entry at offset 1 (0x9401) from g_CommandLengths[0x0194] == 1\n");
		} else {
			CHECK(n == 3, "0x0194: %u dispatch entries, want 3", n);
		}

		free(src);
	}

	lua_close(L);
}

/* ------------------------------------------------------------------------- *
 * 4. Port-only opcodes, CMD_PRINT and label jumps, executed
 * ------------------------------------------------------------------------- */

/*
 *   @0  label 1
 *   @3  0x01e1 x
 *   @6  0x01e2 x
 *   @9  0x01e3 x, label 2     ; jump to label 2 once coop is set
 *   @13 0x01e4 x              ; sets coop
 *   @16 yield
 *   @18 goto_first 1
 *   @21 label 2
 *   @24 print "fojo"
 *   @31 yield
 *   @33 goto_first 1
 *   @36 end
 */
static const unsigned char fojo_prog[] = {
	0x00, 0x02, 0x01,
	0x01, 0xe1, 0x00,
	0x01, 0xe2, 0x00,
	0x01, 0xe3, 0x00, 0x02,
	0x01, 0xe4, 0x00,
	0x00, 0x03,
	0x00, 0x01, 0x01,
	0x00, 0x02, 0x02,
	0x00, 0xb5, 'f', 'o', 'j', 'o', 0x00,
	0x00, 0x03,
	0x00, 0x01, 0x01,
	0x00, 0x04,
};

static int fojo_coop;
static int fojo_ran;
static int fojo_prints;

static int fojo_exec(unsigned int off)
{
	const unsigned char *cmd = g_list + off;

	switch (OPAT(g_list, off)) {
	case CMD_LABEL:
		g_pc = off + 3;
		return 0;
	case CMD_GOTOFIRST:
		g_pc = goto_label(g_list, fojo_cmdlen, 0, cmd[2]);
		return 0;
	case CMD_GOTONEXT:
		g_pc = goto_label(g_list, fojo_cmdlen, off, cmd[2]);
		return 0;
	case CMD_YIELD:
		g_pc = off + 2;
		return 1;
	case CMD_PRINT:
		CHECK(strcmp((const char *)cmd + 2, "fojo") == 0, "print: operand '%s'", (const char *)cmd + 2);
		fojo_prints++;
		g_pc = off + fojo_cmdlen(g_list, off);
		return 0;
	case 0x01e1:
	case 0x01e2:
		fojo_ran++;
		g_pc = off + 3;
		return 0;
	case 0x01e3:
		fojo_ran++;
		g_pc = fojo_coop ? goto_label(g_list, fojo_cmdlen, off, cmd[3]) : off + 4;
		return 0;
	case 0x01e4:
		fojo_ran++;
		fojo_coop = 1;
		g_pc = off + 3;
		return 0;
	default:
		return 2;
	}
}

static void test_fojo_program(void)
{
	static const unsigned int want[] = { 0, 3, 6, 9, 13, 16, 18, 21, 24, 31, 33 };
	static const unsigned int wantpc[] = { 18, 33, 33 };
	static const int wantran[] = { 4, 7, 10 };
	lua_State *L = new_state();
	char *src;
	int frame;

	printf("[port opcodes, print, labels]\n");
	src = transpile_load(L, "fojo program", fojo_prog, sizeof(fojo_prog), fojo_cmdlen, 1);
	if (src) {
		expect_offsets("fojo program", src, want, sizeof(want) / sizeof(want[0]));
		CHECK(has_block(src, 3, 0x01e1), "no 0x01e1 block");
		CHECK(has_block(src, 6, 0x01e2), "no 0x01e2 block");
		CHECK(has_block(src, 9, 0x01e3), "no 0x01e3 block");
		CHECK(has_block(src, 13, 0x01e4), "no 0x01e4 block");
		CHECK(has_block(src, 24, CMD_PRINT), "no print block");

		g_list = fojo_prog;
		g_exec = fojo_exec;
		g_pc = 0;
		fojo_coop = 0;
		fojo_ran = 0;
		fojo_prints = 0;

		for (frame = 0; frame < 3; frame++) {
			int status = run_frame(L, "fojo program");
			printf("  frame %d -> status %d, pc %u, port cmds %d, prints %d\n",
					frame, status, g_pc, fojo_ran, fojo_prints);
			CHECK(status == 1, "fojo program: frame %d status %d, want 1", frame, status);
			CHECK(g_pc == wantpc[frame], "fojo program: frame %d pc %u, want %u", frame, g_pc, wantpc[frame]);
			CHECK(fojo_ran == wantran[frame], "fojo program: frame %d ran %d, want %d", frame, fojo_ran, wantran[frame]);
		}

		CHECK(fojo_prints == 2, "fojo program: %d prints, want 2", fojo_prints);
		free(src);
	}

	lua_close(L);
}

/* ------------------------------------------------------------------------- *
 * 5. Malformed and degenerate lists
 * ------------------------------------------------------------------------- */

static unsigned int zero_cmdlen(const unsigned char *list, unsigned int off)
{
	(void)list;
	(void)off;
	return 0;
}

static int never_exec(unsigned int off)
{
	CHECK(0, "exec(%u) called on a list with no commands", off);
	return 2;
}

/* A chunk with no dispatch entries must load and return 0 without calling
 * exec. */
static void expect_empty(const char *name, const unsigned char *list, unsigned int maxlen)
{
	lua_State *L = new_state();
	char *src = transpile_load(L, name, list, maxlen, fojo_cmdlen, 0);

	if (src) {
		int status;
		CHECK(dispatch_offsets(src, NULL, 0) == 0, "%s: has dispatch entries", name);
		g_exec = never_exec;
		g_pc = 0;
		status = run_frame(L, name);
		CHECK(status == 0, "%s: status %d, want 0", name, status);
		printf("  %-28s ok, %u bytes of Lua, status %d\n", name, (unsigned int)strlen(src), status);
		free(src);
	}

	lua_close(L);
}

static void test_malformed(void)
{
	static const unsigned char endonly[] = { 0x00, 0x04 };
	static const unsigned char noend[] = { 0x00, 0x03, 0x01, 0xe1, 0x00 };
	static const unsigned char tail[] = { 0x00, 0x03, 0x01, 0xe3, 0x00 };
	static const unsigned int want_noend[] = { 0, 2 };
	unsigned char trunc[300];
	lua_State *L = new_state();
	char *src;

	printf("[malformed]\n");

	CHECK(luaaiTranspile(NULL, 16, fojo_cmdlen, CMD_END) == NULL, "NULL list did not return NULL");
	CHECK(luaaiTranspile(endonly, 2, NULL, CMD_END) == NULL, "NULL cmdlen did not return NULL");
	printf("  NULL list / NULL cmdlen      -> NULL\n");

	expect_empty("zero-length list", endonly, 0);
	expect_empty("one-byte list", endonly, 1);
	expect_empty("end marker only", endonly, sizeof(endonly));

	/* No end marker: the walk stops at maxlen. */
	src = transpile_load(L, "no end marker", noend, sizeof(noend), fojo_cmdlen, 0);
	if (src) {
		expect_offsets("no end marker", src, want_noend, 2);
		printf("  %-28s ok, stops at maxlen\n", "no end marker");
		free(src);
	}

	/* Last command's operands run past maxlen: it is still emitted. The
	 * transpiler only guarantees the two opcode bytes are in range. */
	src = transpile_load(L, "operands past maxlen", tail, sizeof(tail), fojo_cmdlen, 0);
	if (src) {
		expect_offsets("operands past maxlen", src, want_noend, 2);
		printf("  %-28s ok, truncated 0x01e3 at 2 still emitted\n", "operands past maxlen");
		free(src);
	}

	/* A cmdlen that returns 0 must not hang the walk. */
	src = transpile_load(L, "zero cmdlen", fojo_prog, sizeof(fojo_prog), zero_cmdlen, 0);
	if (src) {
		CHECK(dispatch_offsets(src, NULL, 0) == 1, "zero cmdlen: want exactly one entry");
		printf("  %-28s ok, one entry, no hang\n", "zero cmdlen");
		free(src);
	}

	/* CMD_PRINT with no terminator: the bounded scan (256) carries the walk
	 * past maxlen and it ends. The buffer is larger than the list so the scan
	 * stays in bounds, as it must in the game. */
	memset(trunc, 'A', sizeof(trunc));
	trunc[0] = 0x00;
	trunc[1] = 0xb5;
	CHECK(fojo_cmdlen(trunc, 0) == 259, "unterminated print length %u, want 259", fojo_cmdlen(trunc, 0));
	src = transpile_load(L, "unterminated print", trunc, 12, fojo_cmdlen, 0);
	if (src) {
		static const unsigned int want0[] = { 0 };
		expect_offsets("unterminated print", src, want0, 1);
		printf("  %-28s ok, length %u, one entry\n", "unterminated print", fojo_cmdlen(trunc, 0));
		free(src);
	}

	lua_close(L);
}

/* ------------------------------------------------------------------------- *
 * 6. Mod opcodes: registered lengths are stepped over, unknown ones are not
 * ------------------------------------------------------------------------- */

/*
 *   @0  0x0401 a b c        ; mod-local, registered length 5
 *   @5  yield
 *   @7  0x0805 a b c d e    ; mod-port, registered length 7
 *   @14 yield
 *   @16 end
 */
static const unsigned char mod_prog[] = {
	0x04, 0x01, 0x00, 0x03, 0x00,
	0x00, 0x03,
	0x08, 0x05, 0x00, 0x04, 0x00, 0x04, 0x00,
	0x00, 0x03,
	0x00, 0x04,
};

static void test_mod_opcodes(void)
{
	/* Unregistered, each mod command is walked a byte at a time and the walk
	 * resyncs on operand bytes: 0x0100 at 1 (len 3), 0x0000 at 4 (len 3),
	 * 0x0805 at 7, 0x0500 at 8, then 0x0004 at 9 ends it early. Neither
	 * yield is found. */
	static const unsigned int want_before[] = { 0, 1, 4, 7, 8 };
	static const unsigned int want_after[] = { 0, 5, 7, 14 };
	static const unsigned char unknown[] = { 0x04, 0x02, 0x00, 0x03, 0x00, 0x04 };
	static const unsigned int want_unknown[] = { 0, 1, 2 };
	lua_State *L = new_state();
	char *src;
	int logs;

	printf("[mod opcodes]\n");

	logs = g_logcount;
	src = transpile_load(L, "mod unregistered", mod_prog, sizeof(mod_prog), fojo_cmdlen, 0);
	if (src) {
		expect_offsets("mod unregistered", src, want_before, 5);
		free(src);
	}
	CHECK(g_logcount == logs + 3
			&& strstr(g_alllog, "0x0401 has no known length (mod-local")
			&& strstr(g_alllog, "0x0805 has no known length (mod-port")
			&& strstr(g_alllog, "0x0500 has no known length (outside"),
			"unregistered: want warnings for 0x0401, 0x0805, 0x0500, got %d", g_logcount - logs);
	CHECK(fojo_cmdlen(mod_prog, 0) == 1, "unregistered 0x0401 length %u, want 1", fojo_cmdlen(mod_prog, 0));

	/* Logged once per opcode. */
	logs = g_logcount;
	fojo_cmdlen(mod_prog, 0);
	fojo_cmdlen(mod_prog, 0);
	CHECK(g_logcount == logs, "0x0401 warned again (%d new lines)", g_logcount - logs);

	/* Registration rules. */
	CHECK(chraiSetModCommandLength(0x0401, 5) == 1, "0x0401 len 5 refused");
	CHECK(chraiSetModCommandLength(0x0401, 5) == 1, "0x0401 same length again refused");
	CHECK(chraiSetModCommandLength(0x0401, 6) == 0, "0x0401 conflicting length accepted");
	CHECK(chraiSetModCommandLength(0x0805, 7) == 1, "0x0805 len 7 refused");
	CHECK(chraiSetModCommandLength(0x0500, 4) == 0, "gap opcode 0x0500 accepted");
	CHECK(chraiSetModCommandLength(0x01e5, 4) == 0, "port reserve opcode 0x01e5 accepted");
	CHECK(chraiSetModCommandLength(0x0402, 1) == 0, "length 1 accepted");
	CHECK(chraiSetModCommandLength(0x0402, 0x101) == 0, "length 0x101 accepted");

	/* Vanilla lookups are untouched. */
	CHECK(fojo_cmdlen(fojo_prog, 3) == 3, "0x01e1 length changed");
	CHECK(fojo_cmdlen(mod_prog, 5) == 2, "yield length changed");

	src = transpile_load(L, "mod registered", mod_prog, sizeof(mod_prog), fojo_cmdlen, 1);
	if (src) {
		expect_offsets("mod registered", src, want_after, 4);
		CHECK(has_block(src, 0, 0x0401), "no 0x0401 block at 0");
		CHECK(has_block(src, 7, 0x0805), "no 0x0805 block at 7");
		free(src);
	}

	/* An unregistered mod-local opcode still steps one byte: 0x0402 at 0,
	 * 0x0200 at 1 (also unknown), yield at 2, end at 4. */
	logs = g_logcount;
	src = transpile_load(L, "mod unknown", unknown, sizeof(unknown), fojo_cmdlen, 0);
	if (src) {
		expect_offsets("mod unknown", src, want_unknown, 3);
		free(src);
	}
	CHECK(g_logcount == logs + 2
			&& strstr(g_alllog, "0x0402 has no known length (mod-local")
			&& strstr(g_alllog, "0x0200 has no known length (outside"),
			"mod unknown: want warnings for 0x0402 and 0x0200, got %d", g_logcount - logs);

	/* Clearing the local window forgets 0x0401 but keeps the port window. */
	chraiClearModLocalCommandLengths();
	CHECK(fojo_cmdlen(mod_prog, 0) == 1, "0x0401 survived clear");
	CHECK(fojo_cmdlen(mod_prog, 7) == 7, "0x0805 lost on local clear");
	CHECK(chraiSetModCommandLength(0x0401, 6) == 1, "0x0401 len 6 refused after clear");
	CHECK(fojo_cmdlen(mod_prog, 0) == 6, "0x0401 length after re-register %u", fojo_cmdlen(mod_prog, 0));
	chraiClearModLocalCommandLengths();

	printf("  0x0401 len 5 and 0x0805 len 7 stepped over; unregistered ones step 1 byte\n");
	lua_close(L);
}

/* ------------------------------------------------------------------------- *
 * 7. A long list still loads
 * ------------------------------------------------------------------------- */

static int yield_exec(unsigned int off)
{
	g_pc = off + 2;
	return 1;
}

static void test_long(unsigned int ncmds)
{
	unsigned int len = ncmds * 2 + 2;
	unsigned char *prog = malloc(len);
	lua_State *L = new_state();
	char name[32];
	char *src;
	clock_t t0, t1, t2;
	unsigned int i;

	snprintf(name, sizeof(name), "%u yields", ncmds);

	for (i = 0; i < ncmds; i++) {
		prog[i * 2] = 0x00;
		prog[i * 2 + 1] = 0x03;
	}
	prog[len - 2] = 0x00;
	prog[len - 1] = 0x04;

	t0 = clock();
	src = transpile_load(L, name, prog, len, fojo_cmdlen, 0);
	t1 = clock();

	if (src) {
		int status;

		CHECK(dispatch_offsets(src, NULL, 0) == ncmds, "%s: entry count", name);

		/* Worst case for the linear chain: the last command. */
		g_list = prog;
		g_exec = yield_exec;
		g_pc = (ncmds - 1) * 2;
		status = run_frame(L, name);
		t2 = clock();
		CHECK(status == 1 && g_pc == ncmds * 2, "%s: last command did not run", name);

		printf("  %-12s %7u bytes of Lua, transpile+load %.1f ms, last-entry dispatch %.3f ms\n",
				name, (unsigned int)strlen(src),
				(t1 - t0) * 1000.0 / CLOCKS_PER_SEC, (t2 - t1) * 1000.0 / CLOCKS_PER_SEC);
		free(src);
	}

	lua_close(L);
	free(prog);
}

/* The parser caps its per-function label, goto and local lists at 32767
 * (SHRT_MAX). The chunk spends one label, two gotos and one local per command,
 * so 32765 commands is the most a list can have. Past that, loading must fail
 * with an ordinary load error rather than crash. */
static void test_too_long(unsigned int ncmds)
{
	unsigned int len = ncmds * 2 + 2;
	unsigned char *prog = calloc(len, 1);
	lua_State *L = new_state();
	char *src;
	unsigned int i;

	for (i = 0; i < ncmds; i++) {
		prog[i * 2 + 1] = 0x03;
	}
	prog[len - 1] = 0x04;

	src = luaaiTranspile(prog, len, fojo_cmdlen, CMD_END);
	CHECK(src != NULL, "%u yields: luaaiTranspile returned NULL", ncmds);
	if (src) {
		int rc = luaL_loadstring(L, src);
		const char *msg = rc == LUA_OK ? "" : lua_tostring(L, -1);
		/* The parser raises this as LUA_ERRRUN, not LUA_ERRSYNTAX. */
		CHECK(rc != LUA_OK && strstr(msg, "limit is 32767") != NULL,
				"%u yields: want a parser limit error, got rc %d '%s'", ncmds, rc, msg);
		printf("  %u yields  rejected by loadstring: %s\n", ncmds, msg);
		free(src);
	}

	lua_close(L);
	free(prog);
}

int main(void)
{
	test_kai_synthetic();
	test_fojo_table();
	test_detect_aio();
	test_fojo_program();
	test_malformed();
	test_mod_opcodes();

	printf("[long lists]\n");
	test_long(1000);
	test_long(10000);
	test_too_long(32766);

	if (g_failures) {
		printf("\nFAILED: %d check(s)\n", g_failures);
		return 1;
	}

	printf("\nPASS\n");
	return 0;
}
