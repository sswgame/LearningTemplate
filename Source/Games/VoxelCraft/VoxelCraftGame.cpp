#include "pch.h"

#include "Games/VoxelCraft/VoxelCraftGame.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Framework/ComponentStateStore.h"
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
        GameObjectManager* pManager = findActiveObjectManager();
        if ( pManager == nullptr )
            return;
        // 블록(부수고 놓은 것) · 플레이어의 몸 자리 · 핫바는 PROPERTY 가 아니다 — 컴포넌트 섹션에 실어 다시 만든 컴포넌트에 돌려준다.
        getComponentStateStore().capture<VoxelDirectorComponent>( *pManager );
        getComponentStateStore().capture<VoxelPlayerComponent>( *pManager );
        // 청크 오브젝트는 월드의 모습일 뿐이다 — 스냅샷에 실으면 복원된 것이 다시 세운 것과 겹친다.
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

    void VoxelCraftGame::onAfterStateDeserialize()
    {
        GameObjectManager* pManager = findActiveObjectManager();
        if ( pManager == nullptr )
            return;
        getComponentStateStore().restore<VoxelDirectorComponent>( *pManager );
        getComponentStateStore().restore<VoxelPlayerComponent>( *pManager );
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::VoxelCraftGame );
