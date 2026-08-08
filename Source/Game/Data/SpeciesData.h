#pragma once
/**
 * @file SpeciesData.h
 * @brief Species / Move tables + party member stub
 */

#include "Core/Common/CommonHeaders.h"
#include "Core/Common/Types.h"

namespace sw
{
	struct MoveDef
	{
		const char* _id		= "tackle";
		const char* _name	= "Tackle";
		int32		_power	= 40;
		int32		_ppMax	= 35;
	};

	struct SpeciesDef
	{
		const char* _id		  = "critter_a";
		const char* _name	  = "Wild Critter";
		int32		_baseHp	  = 40;
		int32		_baseAtk  = 10;
		int32		_move0	  = 0;
		int32		_move1	  = 1;
	};

	struct PartyMember
	{
		std::string _speciesId = "critter_a";
		std::string _nickname;
		int32		_level	 = 5;
		int32		_hp		 = 40;
		int32		_hpMax	 = 40;
		int32		_pp0	 = 35;
		int32		_pp1	 = 20;
		int32		_exp	 = 0;
		int32		_expNext = 50;
	};

	struct SpeciesCatalog
	{
		static constexpr MoveDef kMoves[] = {
			{ "tackle", "Tackle", 40, 35 },
			{ "growl", "Growl", 0, 40 },
			{ "ember", "Ember", 40, 25 },
			{ "vine_whip", "Vine Whip", 45, 25 },
		};
		static constexpr size_t kMoveCount = sizeof( kMoves ) / sizeof( kMoves[0] );

		static constexpr SpeciesDef kSpecies[] = {
			{ "critter_a", "Wild Critter", 40, 10, 0, 1 },
			{ "critter_b", "Brush Hopper", 35, 12, 0, 3 },
			{ "starter_a", "Leaf Pup", 45, 11, 3, 1 },
		};
		static constexpr size_t kSpeciesCount = sizeof( kSpecies ) / sizeof( kSpecies[0] );

		static const SpeciesDef* findSpecies( const char* id );
		static const MoveDef*	 findMove( int32 index );
		static PartyMember		 makeWild( const char* speciesId, int32 level = 5 );
		static PartyMember		 makeStarter();
	};
} // namespace sw
