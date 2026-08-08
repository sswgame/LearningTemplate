/**
 * @file GameData.cpp
 */
#include "GameData.h"

namespace sw
{
	const char* GameData::pickRouteEncounterName() const
	{
		float32 total = 0.0f;
		for ( size_t i = 0; i < kRoute01EncounterCount; ++i )
			total += kRoute01Encounters[i]._weight;
		if ( total <= 0.0f )
			return "Wild Critter";

		// Deterministic stub: cycle table by a static counter (no RNG dep yet).
		static size_t s_pick = 0;
		const size_t  idx	 = s_pick++ % kRoute01EncounterCount;
		return kRoute01Encounters[idx]._displayName;
	}
} // namespace sw
