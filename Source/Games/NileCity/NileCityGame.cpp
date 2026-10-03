#include "pch.h"

#include "Games/NileCity/NileCityGame.h"

#include "GameFramework/Framework/GameService.h"

#include "RuntimeAPI/Export/GameModuleExports.h"

namespace sw
{
    SW_LOG_CALLER( "NileCityGame" );

    NileCityGame::NileCityGame()
        : _cityCatalog{}
        , _cityWorld{}
    {
    }

    NileCityGame::~NileCityGame() = default;

    void NileCityGame::configureBootstrap( BootstrapConfig& outConfig )
    {
        outConfig._packRoot = "game/nilecity";
    }

    bool NileCityGame::onInitialize()
    {
        if ( _cityCatalog.loadFromResource( "game/nilecity/data/city.xml" ) == false )
        {
            SW_LOG_WARNING( "[Nile] city.xml could not be loaded - the city cannot be founded" );
            return true; // 모듈은 뜬다(에디터에서 데이터를 고칠 수 있게) — 도시만 서지 않는다
        }
        game::bindLocalService<CityCatalog>( &_cityCatalog );
        _cityWorld.initialize( &_cityCatalog );
        (void)_cityWorld.spawn(); // 씬 서비스가 아직 없으면 첫 update 가 다시 세운다
        return true;
    }

    void NileCityGame::onShutdown()
    {
        _cityWorld.despawn();
        game::unbindLocalService<CityCatalog>();
    }

    void NileCityGame::onUpdate( float32 deltaTime )
    {
        GameInstanceBase::onUpdate( deltaTime );
        if ( _cityWorld.isInitialized() )
            _cityWorld.update( deltaTime );
    }

    void NileCityGame::onBeforeStateSerialize()
    {
        _cityWorld.despawn();
    }

    void NileCityGame::onAfterStateDeserialize()
    {
        (void)_cityWorld.spawn();
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::NileCityGame );
