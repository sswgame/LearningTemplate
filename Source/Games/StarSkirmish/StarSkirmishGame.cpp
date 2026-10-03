#include "pch.h"

#include "Games/StarSkirmish/StarSkirmishGame.h"

#include "GameFramework/Base/GameService.h"

#include "RuntimeAPI/Export/GameModuleExports.h"

namespace sw
{
    SW_LOG_CALLER( "StarSkirmishGame" );

    StarSkirmishGame::StarSkirmishGame()
        : _unitCatalog{}
        , _skirmishWorld{}
    {
    }

    StarSkirmishGame::~StarSkirmishGame() = default;

    void StarSkirmishGame::configureBootstrap( BootstrapConfig& outConfig )
    {
        outConfig._packRoot = "game/starskirmish";
    }

    bool StarSkirmishGame::onInitialize()
    {
        if ( _unitCatalog.loadFromResource( "game/starskirmish/data/units.xml" ) == false )
        {
            SW_LOG_WARNING( "[Skirmish] units.xml could not be loaded - the match cannot start" );
            return true; // 모듈은 뜬다(에디터에서 데이터를 고칠 수 있게) — 판만 서지 않는다
        }
        game::bindLocalService<RtsCatalog>( &_unitCatalog );
        _skirmishWorld.initialize( &_unitCatalog );
        (void)_skirmishWorld.spawn(); // 씬 서비스가 아직 없으면 첫 update 가 다시 세운다
        return true;
    }

    void StarSkirmishGame::onShutdown()
    {
        _skirmishWorld.despawn();
        game::unbindLocalService<RtsCatalog>();
    }

    void StarSkirmishGame::onUpdate( float32 deltaTime )
    {
        GameInstanceBase::onUpdate( deltaTime );
        if ( _skirmishWorld.isInitialized() )
            _skirmishWorld.update( deltaTime );
    }

    void StarSkirmishGame::onBeforeStateSerialize()
    {
        _skirmishWorld.despawn();
    }

    void StarSkirmishGame::onAfterStateDeserialize()
    {
        (void)_skirmishWorld.spawn();
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::StarSkirmishGame );
