#pragma once
/**
 * @file GameData.h
 * @brief Stub game data table holder (maps, encounters, battle scene ids)
 */

#include "Core/Common/CommonHeaders.h"
#include "Game/Data/SpeciesData.h"

namespace sw
{
	struct EncounterEntry
	{
		const char* _id			 = "critter_a";
		const char* _displayName = "Wild Critter";
		float32		_weight		 = 1.0f;
	};

	struct GameData
	{
		std::string _startMap		 = "Game/Maps/town01.xml";
		std::string _battleMap		 = "Game/Maps/battle01.xml";
		std::string _battleScene	 = "Game/Maps/battle01_scene.xml";
		std::string _defaultSavePath = "Game/Save/default_slot.sav";
		float32		_encounterRate	 = 0.33f;

		/** @brief Route01 wild table stub — weights are relative, not normalized. */
		static constexpr EncounterEntry kRoute01Encounters[] = {
			{ "critter_a", "Wild Critter", 1.0f },
			{ "critter_b", "Brush Hopper", 0.5f },
		};
		static constexpr size_t kRoute01EncounterCount = sizeof( kRoute01Encounters ) / sizeof( kRoute01Encounters[0] );

		/** @brief Species id for wild encounter (use with BattleState / SpeciesCatalog). */
		const char* pickRouteEncounterId() const;
		/** @brief Display name for logging / HUD. */
		const char* pickRouteEncounterName() const;

		/** @brief Default New Game party lead. */
		static PartyMember makeStarterPartyMember() { return SpeciesCatalog::makeStarter(); }
	};
} // namespace sw
