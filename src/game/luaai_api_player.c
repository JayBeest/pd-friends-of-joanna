/**
 * pd.* API, player group.
 *
 * From the Perfect Dark Kai fork (be46717), where the whole API was one file,
 * src/game/luaai_api.c. The player lane fills this in; the core registers it
 * through luaApiRegisterPlayer.
 */

#include <ultra64.h>
#include "types.h"
#include "luaai_api_internal.h"

void luaApiRegisterPlayer(lua_State *L)
{
	(void)L;
}
