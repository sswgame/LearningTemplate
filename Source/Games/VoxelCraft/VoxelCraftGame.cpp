#include "pch.h"

#include "Games/VoxelCraft/VoxelCraftGame.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Framework/GameService.h"

#include "Games/VoxelCraft/VoxelDirectorComponent.h"
#include "Games/VoxelCraft/VoxelPlayerComponent.h"

#include "RuntimeAPI/Export/GameModuleExports.h"

namespace sw
{
    SW_LOG_CALLER( "VoxelCraftGame" );

    VoxelCraftGame::VoxelCraftGame()
        : _blockCatalog{}
    {
        // 상태 스냅샷에 오르는 컴포넌트 — 저장 전에 상태를 싣고 세운 것을 걷으며, 복원 뒤 돌려준다.
        registerDirector<VoxelDirectorComponent>();
        registerStatefulComponent<VoxelPlayerComponent>();
    }

    VoxelCraftGame::~VoxelCraftGame() = default;

    bool VoxelCraftGame::onInitialize()
    {
        if ( _blockCatalog.loadFromResource( "game/voxelcraft/data/blocks.xml" ) == false )
        {
            SW_LOG_WARNING( "[Voxel] blocks.xml could not be loaded - the world cannot start" );
            return true;
        }
        // 디렉터 · 플레이어가 찾는 자리. 모듈이 다시 올라오면 새 인스턴스가 다시 건다.
        game::bindLocalService<VoxelBlockCatalog>( &_blockCatalog );
        // 섬 씬(시작 맵)을 연다. 에디터가 자기 시작 씬을 열면 그 요청이 뒤에 와서 이긴다.
        (void)requestFirstScene();
        return true;
    }

    void VoxelCraftGame::onShutdown()
    {
        game::unbindLocalService<VoxelBlockCatalog>();
    }

} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::VoxelCraftGame );
