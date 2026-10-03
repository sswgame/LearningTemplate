#include "pch.h"

#include "Games/VoxelCraft/VoxelCraftGame.h"

#include "GameFramework/Base/GameService.h"

#include "RuntimeAPI/Export/GameModuleExports.h"

namespace sw
{
    SW_LOG_CALLER( "VoxelCraftGame" );

    VoxelCraftGame::VoxelCraftGame()
        : _blockCatalog{}
        , _world{}
    {
    }

    VoxelCraftGame::~VoxelCraftGame() = default;

    void VoxelCraftGame::configureBootstrap( BootstrapConfig& outConfig )
    {
        outConfig._packRoot = "game/voxelcraft";
    }

    bool VoxelCraftGame::onInitialize()
    {
        if ( _blockCatalog.loadFromResource( "game/voxelcraft/data/blocks.xml" ) == false )
        {
            SW_LOG_WARNING( "[Voxel] blocks.xml could not be loaded - the world cannot start" );
            return true;
        }
        game::bindLocalService<VoxelBlockCatalog>( &_blockCatalog );
        _world.initialize( &_blockCatalog );
        (void)_world.spawn();
        return true;
    }

    void VoxelCraftGame::onShutdown()
    {
        _world.despawn();
        game::unbindLocalService<VoxelBlockCatalog>();
    }

    void VoxelCraftGame::onUpdate( float32 deltaTime )
    {
        GameInstanceBase::onUpdate( deltaTime );
        if ( _blockCatalog.getBlocks().empty() == false )
            _world.update( deltaTime );
    }

    void VoxelCraftGame::onBeforeStateSerialize()
    {
        _world.despawn();
    }

    void VoxelCraftGame::onAfterStateDeserialize()
    {
        (void)_world.spawn();
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::VoxelCraftGame );
