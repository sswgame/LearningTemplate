#include "pch.h"

#include "Games/HarvestValley/HarvestValleyGame.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Framework/ComponentStateStore.h"

#include "Games/HarvestValley/FarmDirectorComponent.h"

#include "RuntimeAPI/Export/GameModuleExports.h"

namespace sw
{
    HarvestValleyGame::HarvestValleyGame() = default;

    HarvestValleyGame::~HarvestValleyGame() = default;

    bool HarvestValleyGame::onInitialize()
    {
        // 농장 씬(시작 맵)을 연다. 에디터가 자기 시작 씬을 열면 그 요청이 뒤에 와서 이긴다.
        (void)requestFirstScene();
        return true;
    }

    void HarvestValleyGame::onBeforeStateSerialize()
    {
        GameObjectManager* pManager = findActiveObjectManager();
        if ( pManager == nullptr )
            return;
        // 농장 상태(달력 · 밭 · 인벤토리 · 농부)는 PROPERTY 가 아니다 — 컴포넌트 섹션에 실어 다시 만든 디렉터에 돌려준다.
        getComponentStateStore().capture<FarmDirectorComponent>( *pManager );
        // 디렉터가 세운 흙 · 작물은 밭 상태의 모습일 뿐이다 — 스냅샷에 실으면 복원된 것이 다시 세운 것과 겹친다.
        // 걷어 두면(삭제 대기는 스냅샷이 건너뛴다) 다시 만든 디렉터가 시작하며 세우고, 남은 디렉터는 다음 틱에 다시 세운다.
        // 순회 콜백 안에서는 오브젝트를 지울 수 없다(매니저 잠금 안) — 디렉터를 모은 뒤 걷는다.
        vector<ComponentHandle> listDirector;
        pManager->forEachComponentOfType<FarmDirectorComponent>( [&listDirector]( FarmDirectorComponent* pDirector )
        { listDirector.push_back( pDirector->getHandle() ); } );
        for ( const ComponentHandle& handle : listDirector )
        {
            FarmDirectorComponent* pDirector = static_cast<FarmDirectorComponent*>( pManager->resolveComponent( handle ) );
            if ( pDirector != nullptr )
                pDirector->despawnViews();
        }
    }

    void HarvestValleyGame::onAfterStateDeserialize()
    {
        GameObjectManager* pManager = findActiveObjectManager();
        if ( pManager != nullptr )
            getComponentStateStore().restore<FarmDirectorComponent>( *pManager );
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::HarvestValleyGame );
