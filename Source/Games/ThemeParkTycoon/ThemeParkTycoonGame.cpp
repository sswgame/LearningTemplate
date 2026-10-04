#include "pch.h"

#include "Games/ThemeParkTycoon/ThemeParkTycoonGame.h"

#include "RuntimeAPI/Export/GameModuleExports.h"

namespace sw
{
    SW_LOG_CALLER( "ThemeParkTycoonGame" );

    ThemeParkTycoonGame::ThemeParkTycoonGame()
        : _parkWorld{}
    {
    }

    ThemeParkTycoonGame::~ThemeParkTycoonGame() = default;

    bool ThemeParkTycoonGame::onInitialize()
    {
        if ( _parkWorld.loadData( "game/themepark/data/coasters.xml", "game/themepark/data/rides.xml" ) == false )
        {
            SW_LOG_WARNING( "[Park] park data could not be loaded - the park cannot open" );
            return true;
        }
        (void)_parkWorld.spawn();
        return true;
    }

    void ThemeParkTycoonGame::onShutdown()
    {
        _parkWorld.despawn();
    }

    void ThemeParkTycoonGame::onUpdate( float32 deltaTime )
    {
        GameInstanceBase::onUpdate( deltaTime );
        if ( _parkWorld.isLoaded() )
            _parkWorld.update( deltaTime );
    }

    void ThemeParkTycoonGame::onBeforeStateSerialize()
    {
        _parkWorld.despawn();
    }

    void ThemeParkTycoonGame::onAfterStateDeserialize()
    {
        (void)_parkWorld.spawn();
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::ThemeParkTycoonGame );
