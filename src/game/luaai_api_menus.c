/**
 * pd.* API, menus group.
 *
 * From the Perfect Dark Kai fork (be46717), where the whole API was one file,
 * src/game/luaai_api.c. The menus lane fills this in; the core registers it
 * through luaApiRegisterMenus.
 */

#include <ultra64.h>
#include "types.h"
#include "luaai_api_internal.h"

void luaApiRegisterMenus(lua_State *L)
{
	(void)L;
}

/* Core hooks. The menus lane replaces these. */

void luaApiResetMenus(void)
{
}

/* Declared in game/luaai.h; luaApiResetFrame calls it. The menus lane
 * replaces this with the Director dialog's rebuild. */
void luaDirectorRebuild(void)
{
}
