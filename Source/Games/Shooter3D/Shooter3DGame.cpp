#include "pch.h"

#include "Games/Shooter3D/Shooter3DGame.h"

#include "GameFramework/Framework/GameService.h"

#include "RuntimeAPI/Export/GameModuleExports.h"

namespace sw
{
    SW_LOG_CALLER( "Shooter3DGame" );

    Shooter3DGame::Shooter3DGame()
        : _weaponCatalog{}
        , _arena{}
    {
    }

    Shooter3DGame::~Shooter3DGame() = default;

    void Shooter3DGame::configureBootstrap( BootstrapConfig& outConfig )
    {
        outConfig._packRoot = "game/shooter3d";
    }

    bool Shooter3DGame::onInitialize()
    {
        if ( _weaponCatalog.loadFromResource( "game/shooter3d/data/weapons.xml" ) == false )
        {
            SW_LOG_WARNING( "[Shooter] weapons.xml could not be loaded - the arena cannot start" );
            return true;
        }
        game::bindLocalService<WeaponCatalog>( &_weaponCatalog );
        _arena.initialize( &_weaponCatalog );
        (void)_arena.spawn();
        return true;
    }

    void Shooter3DGame::onShutdown()
    {
        _arena.despawn();
        game::unbindLocalService<WeaponCatalog>();
    }

    void Shooter3DGame::onUpdate( float32 deltaTime )
    {
        GameInstanceBase::onUpdate( deltaTime );
        if ( _weaponCatalog.getWeapons().empty() == false )
            _arena.update( deltaTime );
    }

    void Shooter3DGame::onBeforeStateSerialize()
    {
        _arena.despawn();
    }

    void Shooter3DGame::onAfterStateDeserialize()
    {
        (void)_arena.spawn();
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::Shooter3DGame );
