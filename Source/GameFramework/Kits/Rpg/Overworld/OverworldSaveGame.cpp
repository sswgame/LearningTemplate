#include "pch.h"

#include "GameFramework/Kits/Rpg/Overworld/OverworldSaveGame.h"

#include "GameFramework/Data/GameSettings.h"
#include "GameFramework/Framework/GameService.h"

namespace sw
{
    void OverworldSaveGame::ensureStartMap()
    {
        if ( _mapPath.empty() == false )
            return;
        const GameSettings* pGameSettings = game::getService<GameSettings>();
        if ( pGameSettings != nullptr )
            _mapPath = pGameSettings->_startMap;
    }

    void OverworldSaveGame::captureFlags( const GameFlags& flags )
    {
        flags.fillEntries( _listFlag );
    }

    void OverworldSaveGame::restoreFlags( GameFlags& outFlags ) const
    {
        outFlags.restoreEntries( _listFlag );
    }

    bool OverworldSaveGame::saveToFile( string_view path ) const
    {
        // 이 타입으로 쓴다 — `SaveGame::saveToFile` 은 `SaveGame` 의 TypeInfo(프로퍼티 0)로 빈 페이로드를 쓴다.
        return SaveGameSerializer::saveGameToSlot( *this, path );
    }

    bool OverworldSaveGame::loadFromFile( string_view path )
    {
        if ( SaveGameSerializer::loadGameFromSlot( *this, path ) == false )
            return false;
        ensureStartMap();
        return true;
    }
} // namespace sw
