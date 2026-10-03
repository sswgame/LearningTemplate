#include "pch.h"

#include "Games/HarvestValley/HarvestValleyGame.h"

#include "GameFramework/Framework/GameService.h"

#include "RuntimeAPI/Export/GameModuleExports.h"

namespace sw
{
    SW_LOG_CALLER( "HarvestValleyGame" );

    HarvestValleyGame::HarvestValleyGame()
        : _cropCatalog{}
        , _farmWorld{}
    {
    }

    HarvestValleyGame::~HarvestValleyGame() = default;

    void HarvestValleyGame::configureBootstrap( BootstrapConfig& outConfig )
    {
        outConfig._packRoot = "game/harvestvalley";
    }

    bool HarvestValleyGame::onInitialize()
    {
        if ( _cropCatalog.loadFromResource( "game/harvestvalley/data/crops.xml" ) == false )
        {
            SW_LOG_WARNING( "[Farm] crops.xml could not be loaded - the farm cannot start" );
            return true; // 모듈은 뜬다(에디터에서 데이터를 고칠 수 있게) — 농장만 서지 않는다
        }
        game::bindLocalService<CropCatalog>( &_cropCatalog );
        _farmWorld.initialize( &_cropCatalog );
        (void)_farmWorld.spawn(); // 씬 서비스가 아직 없으면 첫 update 가 다시 세운다
        return true;
    }

    void HarvestValleyGame::onShutdown()
    {
        _farmWorld.despawn();
        game::unbindLocalService<CropCatalog>();
    }

    void HarvestValleyGame::onUpdate( float32 deltaTime )
    {
        GameInstanceBase::onUpdate( deltaTime );
        if ( _cropCatalog.getCrops().empty() == false )
            _farmWorld.update( deltaTime );
    }

    void HarvestValleyGame::onBeforeStateSerialize()
    {
        _farmWorld.despawn();
    }

    void HarvestValleyGame::onAfterStateDeserialize()
    {
        (void)_farmWorld.spawn();
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::HarvestValleyGame );
