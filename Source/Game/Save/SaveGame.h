#pragma once
/**
 * @file SaveGame.h
 * @brief Persistent save blob (map + party + story flags). Excludes transient battle state.
 */

#include "Core/Common/CommonHeaders.h"
#include "Core/Common/Types.h"
#include "Game/Data/SpeciesData.h"

namespace sw
{
	inline constexpr size_t kMaxPartySize = 6;

	struct SaveGame
	{
		std::string				   _mapPath = "Game/Maps/town01.xml";
		int32					   _playerX = 1;
		int32					   _playerY = 1;
		std::vector<PartyMember>   _party;
		std::map<std::string, int32> _flags;

		void clearParty();
		void setPartyFrom( const std::vector<PartyMember>& party );
		void ensureStarterParty();

		int32 getFlag( const std::string& key, int32 defaultValue = 0 ) const;
		void  setFlag( const std::string& key, int32 value );

		bool saveToFile( const std::string& path ) const;
		bool loadFromFile( const std::string& path );
	};
} // namespace sw
