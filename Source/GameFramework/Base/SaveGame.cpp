#include "pch.h"

#include "GameFramework/Base/SaveGame.h"

#include "Core/Log/Logger.h"

namespace sw
{
	SW_LOG_CALLER( "SaveGame" );

	bool SaveGame::saveToFile( string_view path ) const
	{
		return SaveGameSerializer::saveGameToSlot( *this, path );
	}

	bool SaveGame::loadFromFile( string_view path )
	{
		return SaveGameSerializer::loadGameFromSlot( *this, path );
	}
} // namespace sw
