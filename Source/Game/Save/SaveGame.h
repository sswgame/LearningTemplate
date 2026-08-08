#pragma once
/**
 * @file SaveGame.h
 * @brief Minimal save blob stub (map + player tile)
 */

#include "Core/Common/CommonHeaders.h"
#include "Core/Common/Types.h"

namespace sw
{
	struct SaveGame
	{
		std::string _mapPath = "Game/Maps/town01.xml";
		int32		_playerX = 1;
		int32		_playerY = 1;

		bool saveToFile( const std::string& path ) const;
		bool loadFromFile( const std::string& path );
	};
} // namespace sw
