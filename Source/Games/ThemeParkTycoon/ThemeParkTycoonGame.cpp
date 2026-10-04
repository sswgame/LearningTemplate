#include "pch.h"

#include "Games/ThemeParkTycoon/ThemeParkTycoonGame.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Framework/ComponentStateStore.h"

#include "Games/ThemeParkTycoon/ParkDirectorComponent.h"

#include "RuntimeAPI/Export/GameModuleExports.h"

namespace sw
{
    ThemeParkTycoonGame::ThemeParkTycoonGame() = default;

    ThemeParkTycoonGame::~ThemeParkTycoonGame() = default;

    bool ThemeParkTycoonGame::onInitialize()
    {
        // 공원 씬(시작 맵)을 연다. 에디터가 자기 시작 씬을 열면 그 요청이 뒤에 와서 이긴다.
        (void)requestFirstScene();
        return true;
    }

    void ThemeParkTycoonGame::onBeforeStateSerialize()
    {
        GameObjectManager* pManager = findActiveObjectManager();
        if ( pManager == nullptr )
            return;
        // 공원 상태(시뮬레이션 · 지은 것 · 열차)는 PROPERTY 가 아니다 — 컴포넌트 섹션에 실어 다시 만든 디렉터에 돌려준다.
        getComponentStateStore().capture<ParkDirectorComponent>( *pManager );
        // 디렉터가 세운 레일 · 차 · 손님은 시뮬레이션의 모습일 뿐이다 — 스냅샷에 실으면 복원된 것이 다시 세운 것과 겹친다.
        // 걷어 두면(삭제 대기는 스냅샷이 건너뛴다) 다시 만든 디렉터가 시작하며 세우고, 남은 디렉터는 다음 틱에 다시 세운다.
        // 순회 콜백 안에서는 오브젝트를 지울 수 없다(매니저 잠금 안) — 디렉터를 모은 뒤 걷는다.
        vector<ComponentHandle> listDirector;
        pManager->forEachComponentOfType<ParkDirectorComponent>( [&listDirector]( ParkDirectorComponent* pDirector )
        { listDirector.push_back( pDirector->getHandle() ); } );
        for ( const ComponentHandle& handle : listDirector )
        {
            ParkDirectorComponent* pDirector = static_cast<ParkDirectorComponent*>( pManager->resolveComponent( handle ) );
            if ( pDirector != nullptr )
                pDirector->despawnViews();
        }
    }

    void ThemeParkTycoonGame::onAfterStateDeserialize()
    {
        GameObjectManager* pManager = findActiveObjectManager();
        if ( pManager != nullptr )
            getComponentStateStore().restore<ParkDirectorComponent>( *pManager );
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::ThemeParkTycoonGame );
