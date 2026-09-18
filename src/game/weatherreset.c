#include <ultra64.h>
#include "constants.h"
#include "game/chaosstate.h"
#include "game/weather.h"
#include "bss.h"
#include "lib/memp.h"
#include "system.h"
#include "data.h"
#include "types.h"

s32 g_WeatherActive = false;
u32 var800623f4 = 0x00000000;
u32 var800623f8 = 0x00000000;
u32 var800623fc = 0x00000000;

#ifndef PLATFORM_N64

static void weatherResetRooms(void)
{
	// The rooms this stage actually has. g_Vars.roomcount says how many the
	// level file declared and g_NumRoomsAllocated how many g_Rooms was built
	// for: the same fact twice, and a stage that reaches here with the two
	// disagreeing - a Windows player's crash of 2026-09-12 in Dab's Mod, an
	// access violation in the walk below - is walking off the end of the
	// array or through a null pointer. Neither is this pass's to fix, so it
	// says so and marks what is there.
	const s32 roomcount = g_Vars.roomcount < g_NumRoomsAllocated ? g_Vars.roomcount : g_NumRoomsAllocated;

	if (g_Rooms == NULL || roomcount != g_Vars.roomcount) {
		sysLogPrintf(LOG_WARNING, "weather: stage 0x%02x has %d rooms but g_Rooms holds %d (%p); leaving the rest alone",
				g_Vars.stagenum, g_Vars.roomcount, g_NumRoomsAllocated, g_Rooms);
	}

	if (g_Rooms == NULL) {
		return;
	}

	if (g_CurWeatherConfig->flags & WEATHERFLAG_INCLUDE) {
		// weather is present only in skiprooms, so mark every room as weatherproof
		for (s32 i = 0; i < roomcount; ++i) {
			g_Rooms[i].extra_flags |= ROOMFLAG_EX_WEATHERPROOF;
		}
		// then unmark skiprooms
		for (s32 i = 0; i < WEATHERCFG_MAX_SKIPROOMS && g_CurWeatherConfig->skiprooms[i]; ++i) {
			const RoomNum room = g_CurWeatherConfig->skiprooms[i];
			if (room >= 0 && room < roomcount) {
				g_Rooms[room].extra_flags &= ~ROOMFLAG_EX_WEATHERPROOF;
			}
		}
	} else {
		// weather is present in all rooms except skiprooms
		for (s32 i = 0; i < WEATHERCFG_MAX_SKIPROOMS && g_CurWeatherConfig->skiprooms[i]; ++i) {
			const RoomNum room = g_CurWeatherConfig->skiprooms[i];
			if (room >= 0 && room < roomcount) {
				g_Rooms[room].extra_flags |= ROOMFLAG_EX_WEATHERPROOF;
			}
		}
	}
}

#endif

#ifndef PLATFORM_N64
// Chaos weather (pd.weather, Kai fork): force rain/snow on ANY stage. type 0 =
// off (weatherStop), 1 = rain, 2 = snow; intensity 0..3. Stages without
// configured weather never allocate g_WeatherData, so this replicates
// weatherReset's init on demand (MEMPOOL_STAGE — freed with the stage). No
// weatherproof room flags are set up for unconfigured stages, so it "rains"
// indoors too — which is the chaos-mode joke, not a bug.
//
// MEMPOOL_STAGE is a bump allocator with no free, and scripts toggle weather
// repeatedly within a stage, so turning it off parks the block in
// g_ChaosWeatherKeep for the next "on" instead of orphaning ~15.7KB each cycle.
// Kai kept it in g_WeatherData with g_WeatherActive cleared, but weatherRender
// is gated on g_WeatherData alone, and the next "on" never set g_WeatherActive
// again; parking it outside g_WeatherData avoids both. The park is keyed on
// g_ChaosLoadSerial so a block from an earlier stage's pool is never reused.
static struct weatherdata *g_ChaosWeatherKeep = NULL;
static s32 g_ChaosWeatherKeepSerial = -1;

s32 weatherChaosSet(s32 type, s32 intensity)
{
	if (intensity < 0) intensity = 0;
	if (intensity > 3) intensity = 3;
	// snow only implements intensities 0/1 in weatherSetIntensity (the N64
	// game never ran snow harder); 2/3 fall through its switch leaving the
	// particle target (unkd4) at 0 = invisible weather. 1 is already the
	// 500-particle maximum, so clamp rather than extend the table.
	if (type == 2 && intensity > 1) {
		intensity = 1;
	}

	if (type <= 0) {
		if (g_WeatherData) {
			// weatherStop() stops the audio handles and NULLs g_WeatherData.
			g_ChaosWeatherKeep = g_WeatherData;
			g_ChaosWeatherKeepSerial = g_ChaosLoadSerial;
			weatherStop();
		}
		g_WeatherActive = false;
		return 1;
	}

	if (!g_WeatherData && g_ChaosWeatherKeep && g_ChaosWeatherKeepSerial == g_ChaosLoadSerial) {
		g_WeatherData = g_ChaosWeatherKeep;
		g_WeatherData->audiohandles[0] = 0;
		g_WeatherData->audiohandles[1] = 0;
		g_WeatherData->audiohandles[2] = 0;
		g_WeatherData->audiohandles[3] = 0;
	}

	g_ChaosWeatherKeep = NULL;

	if (!g_WeatherData) {
		struct weatherdata *data = mempAlloc(sizeof(struct weatherdata), MEMPOOL_STAGE);

		if (!data) {
			return 0;
		}

		// weatherAllocateParticles reads g_CurWeatherConfig, which
		// weatherReset has already pointed at this stage's row (or the
		// default one).
		data->particledata[0] = weatherAllocateParticles();

		if (!data->particledata[0]) {
			// an exhausted stage pool has to be caught here
			return 0;
		}

		g_WeatherData = data;
		g_WeatherData->type = -1;
		g_WeatherData->windanglerad = 0;
		g_WeatherData->unk0c = 0;
		g_WeatherData->unk10 = 1;
		g_WeatherData->windspeed = 15;
		g_WeatherData->audiohandles[0] = 0;
		g_WeatherData->audiohandles[1] = 0;
		g_WeatherData->audiohandles[2] = 0;
		g_WeatherData->audiohandles[3] = 0;
		g_WeatherData->unk44 = 0;
		g_WeatherData->unk94 = -1;
		g_WeatherData->unk48 = 1;
		g_WeatherData->unk4c = 0;
		g_WeatherData->unk50 = 0;
		g_WeatherData->unk54 = 0;
		g_WeatherData->unk58[0].unk00 = 0;
		g_WeatherData->unk58[1].unk00 = 0;
		g_WeatherData->unk58[2].unk00 = 0;
		g_WeatherData->unk58[3].unk00 = 1;
		g_WeatherData->unk58[0].unk04 = 1;
		g_WeatherData->unk58[0].unk08 = 0;
		g_WeatherData->unk58[1].unk08 = 0;
		g_WeatherData->unk58[2].unk08 = 0;
		g_WeatherData->unk58[3].unk08 = 0;
		g_WeatherData->unkb8 = 150;
		g_WeatherData->unkc0 = 0;
		g_WeatherData->unkc4 = 0;
		g_WeatherData->unkc8 = 15;
		g_WeatherData->unk88 = 1;
		g_WeatherData->unk90 = 0;
		g_WeatherData->intensity = 0;
		g_WeatherData->unkd0 = 0;
		g_WeatherData->unkd4 = 0;
	}

	g_WeatherActive = true;

	if (type == 1) {
		weatherConfigureRain((u32)intensity);
	} else {
		weatherConfigureSnow((u32)intensity);
	}

	return 1;
}
#endif

void weatherReset(void)
{
	g_WeatherActive = false;
	g_WeatherData = NULL;

#ifndef PLATFORM_N64
	// find the weather config for this stage, if any
	g_CurWeatherConfig = &g_DefaultWeatherConfig;
	for (s32 i = 0; i < ARRAYCOUNT(g_WeatherConfig) && g_WeatherConfig[i].stagenum; ++i) {
		if (g_WeatherConfig[i].stagenum == g_Stages[g_StageIndex].id) {
			g_CurWeatherConfig = &g_WeatherConfig[i];
			break;
		}
	}

	if (g_CurWeatherConfig != &g_DefaultWeatherConfig)
#else
	if ((g_StageIndex == STAGEINDEX_CHICAGO
				|| g_StageIndex == STAGEINDEX_AIRBASE
				|| g_StageIndex == STAGEINDEX_G5BUILDING
				|| g_StageIndex == STAGEINDEX_CRASHSITE)
			&& PLAYERCOUNT() < 2)
#endif
	{
		g_WeatherData = mempAlloc(sizeof(struct weatherdata), MEMPOOL_STAGE);
		g_WeatherData->particledata[0] = weatherAllocateParticles();
		g_WeatherData->type = -1;
		g_WeatherData->windanglerad = 0;
		g_WeatherData->unk0c = 0;
		g_WeatherData->unk10 = 1;

#ifdef PLATFORM_N64
		if (g_StageIndex == STAGEINDEX_CHICAGO || g_StageIndex == STAGEINDEX_G5BUILDING) {
			g_WeatherData->windspeed = 20;
		} else if (g_StageIndex == STAGEINDEX_CRASHSITE) {
			g_WeatherData->windspeed = 10;
		} else {
			g_WeatherData->windspeed = 5;
		}
#else
		g_WeatherData->windspeed = g_CurWeatherConfig->windspeed;
		// set up the weatherproof flags in all rooms
		if (g_Vars.stagenum != STAGE_TITLE) {
			weatherResetRooms();
		}
#endif

		g_WeatherData->audiohandles[0] = 0;
		g_WeatherData->audiohandles[1] = 0;
		g_WeatherData->audiohandles[2] = 0;
		g_WeatherData->audiohandles[3] = 0;
		g_WeatherData->unk44 = 0;
		g_WeatherData->unk94 = -1;
		g_WeatherData->unk48 = 1;
		g_WeatherData->unk4c = 0;
		g_WeatherData->unk50 = 0;
		g_WeatherData->unk54 = 0;
		g_WeatherData->unk58[0].unk00 = 0;
		g_WeatherData->unk58[1].unk00 = 0;
		g_WeatherData->unk58[2].unk00 = 0;
		g_WeatherData->unk58[3].unk00 = 1;
		g_WeatherData->unk58[0].unk04 = 1;
		g_WeatherData->unk58[0].unk08 = 10;

		if (g_WeatherData->unk58[0].unk08 < 0) {
			g_WeatherData->unk58[0].unk08 = -g_WeatherData->unk58[0].unk08;
		}

		g_WeatherData->unk58[0].unk08 = 0;
		g_WeatherData->unk58[1].unk08 = 0;
		g_WeatherData->unk58[2].unk08 = 0;
		g_WeatherData->unk58[3].unk08 = 0;
		g_WeatherData->unkb8 = 150;
		g_WeatherData->unkc0 = 0;
		g_WeatherData->unkc4 = 0;
		g_WeatherData->unkc8 = 15;
		g_WeatherData->unk88 = 1;
		g_WeatherData->unk90 = 0;
		g_WeatherData->intensity = 0;
		g_WeatherData->unkd0 = 0;
		g_WeatherData->unkd4 = 0;

		g_WeatherActive = true;
	}
}
