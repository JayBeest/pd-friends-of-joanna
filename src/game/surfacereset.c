#include <ultra64.h>
#include "constants.h"
#include "bss.h"
#include "lib/memp.h"
#include "data.h"
#include "types.h"

void surfaceReset(void)
{
	g_TexCacheCount = 0;

	// head and end are one slot: struct texpool unions them, and
	// the shared pool is the one that spells it head. So the end
	// store below is also what drops the previous stage's list,
	// and it has to: the port's stage-change path runs
	// mempResetPool(MEMPOOL_STAGE) beside this reset, and
	// MEMPOOL_STAGE is where every tex record on that list was
	// allocated. texFindInPool walks head for this pool, so it
	// sees NULL and misses. texInitPool writes the slot too, but
	// is never passed this pool.
	g_TexSharedPool.start = NULL;
	g_TexSharedPool.end = NULL;
	g_TexSharedPool.rightpos = NULL;
}
