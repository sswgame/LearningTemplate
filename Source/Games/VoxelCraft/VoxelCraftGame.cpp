#include "pch.h"

#include "Games/VoxelCraft/VoxelCraftGame.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Framework/GameService.h"

#include "Games/VoxelCraft/VoxelDirectorComponent.h"

#include "RuntimeAPI/Export/GameModuleExports.h"

namespace sw
{
    SW_LOG_CALLER( "VoxelCraftGame" );

    VoxelCraftGame::VoxelCraftGame()
        : _blockCatalog{}
    {
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

    void VoxelCraftGame::onBeforeStateSerialize()
    {
        // 청크 오브젝트는 월드의 모습일 뿐이다 — 스냅샷에 실으면 복원된 것이 다시 세운 것과 겹친다.
        SceneManager*      pSceneManager = game::getService<SceneManager>();
        Scene*             pScene        = pSceneManager != nullptr ? pSceneManager->getActiveScene() : nullptr;
        GameObjectManager* pManager      = pScene != nullptr ? pScene->getObjectManager() : nullptr;
        if ( pManager == nullptr )
            return;
        // 순회 콜백 안에서는 오브젝트를 지울 수 없다(매니저 잠금 안) — 디렉터를 모은 뒤 걷는다.
        vector<ComponentHandle> listDirector;
        pManager->forEachComponentOfType<VoxelDirectorComponent>( [&listDirector]( VoxelDirectorComponent* pDirector )
        { listDirector.push_back( pDirector->getHandle() ); } );
        for ( const ComponentHandle& handle : listDirector )
        {
            VoxelDirectorComponent* pDirector = static_cast<VoxelDirectorComponent*>( pManager->resolveComponent( handle ) );
            if ( pDirector != nullptr )
                pDirector->despawnRuntime();
        }
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::VoxelCraftGame );
