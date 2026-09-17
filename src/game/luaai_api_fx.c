/**
 * pd.* API, fx group.
 *
 * From the Perfect Dark Kai fork (be46717), where the whole API was one file,
 * src/game/luaai_api.c. The fx lane fills this in; the core registers it
 * through luaApiRegisterFx.
 */

#include <ultra64.h>
#include "types.h"
#include "luaai_api_internal.h"

void luaApiRegisterFx(lua_State *L)
{
	(void)L;
}

/* Core hooks (luaai_api_internal.h). The fx lane replaces these. */

void luaApiResetFx(void)
{
}

Gfx *luaApiDrawImage(Gfx *gdl, s32 handle, s32 cx, s32 cy, s32 w, s32 h, f32 angle, u32 color)
{
	(void)handle;
	(void)cx;
	(void)cy;
	(void)w;
	(void)h;
	(void)angle;
	(void)color;
	return gdl;
}

const char *luaApiOverlayText(const char *text)
{
	return text;
}
