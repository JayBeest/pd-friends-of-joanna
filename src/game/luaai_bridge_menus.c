/**
 * chraiLua* bridges for the menus group of the pd.* API.
 *
 * From the Perfect Dark Kai fork (be46717), where the bridges sat in
 * src/game/chraction.c. The menus group needs none: Kai's menu functions
 * (pd.menu_*, pd.menu_lore, pd.game_over) call the menu code directly, and
 * the Director dialog lives in mainmenu.c. The file is kept so every group
 * has the same layout.
 */

#include <ultra64.h>
#include "types.h"
#include "luaai_api_internal.h"
