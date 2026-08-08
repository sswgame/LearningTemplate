/**
 * @file GameData.cpp
 */
#include "GameData.h"

namespace sw
{
	namespace
	{
		size_t nextEncounterIndex()
		{
			// Deterministic stub: cycle table by a static counter (no RNG dep yet).
			static size_t s_pick = 0;
			return s_pick++ % GameData::kRoute01EncounterCount;
		}
	} // namespace

	const char* GameData::pickRouteEncounterId() const
	{
		if ( kRoute01EncounterCount == 0 )
			return "critter_a";
		return kRoute01Encounters[nextEncounterIndex()]._id;
	}

	const char* GameData::pickRouteEncounterName() const
	{
		if ( kRoute01EncounterCount == 0 )
			return "Wild Critter";
		// Keep in sync with id picker by reusing last index logic via weight table walk.
		float32 total = 0.0f;
		for ( size_t i = 0; i < kRoute01EncounterCount; ++i )
			total += kRoute01Encounters[i]._weight;
		if ( total <= 0.0f )
			return "Wild Critter";

		static size_t s_namePick = 0;
		const size_t  idx		 = s_namePick++ % kRoute01EncounterCount;
		return kRoute01Encounters[idx]._displayName;
	}
} // namespace sw
