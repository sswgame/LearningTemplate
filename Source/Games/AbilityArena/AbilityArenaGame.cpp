#include "pch.h"

#include "Games/AbilityArena/AbilityArenaGame.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Framework/ComponentStateStore.h"
#include "GameFramework/Framework/GameService.h"

#include "Games/AbilityArena/ArenaAbilities.h"
#include "Games/AbilityArena/ArenaDirectorComponent.h"

#include "RuntimeAPI/Export/GameModuleExports.h"

namespace sw
{
    SW_LOG_CALLER( "AbilityArenaGame" );

    AbilityArenaGame::AbilityArenaGame()
        : _abilityCatalog{}
    {
    }

    AbilityArenaGame::~AbilityArenaGame() = default;

    bool AbilityArenaGame::onInitialize()
    {
        // 클래스를 먼저 등록하고 데이터를 읽는다 — 읽을 때 모르는 클래스 이름은 경고한다.
        ArenaAbilities::registerClasses( _abilityCatalog );
        if ( _abilityCatalog.loadFromResource( "game/abilityarena/data/abilities.xml" ) == false )
        {
            SW_LOG_WARNING( "[Arena] abilities.xml could not be loaded - the arena cannot start" );
            return true; // 모듈은 뜬다(에디터에서 데이터를 고칠 수 있게) — 디렉터만 서지 않는다
        }
        // 어빌리티 시스템이 찾는 자리. 모듈이 다시 올라오면 새 인스턴스가 다시 건다.
        game::bindLocalService<AbilityCatalog>( &_abilityCatalog );
        // 아레나 씬(시작 맵)을 연다. 에디터가 자기 시작 씬을 열면 그 요청이 뒤에 와서 이긴다.
        (void)requestFirstScene();
        return true;
    }

    void AbilityArenaGame::onShutdown()
    {
        game::unbindLocalService<AbilityCatalog>();
    }

    void AbilityArenaGame::onBeforeStateSerialize()
    {
        GameObjectManager* pManager = findActiveObjectManager();
        if ( pManager == nullptr )
            return;
        // 판의 진행(웨이브 · 처치 수)는 PROPERTY 가 아니다 — 컴포넌트 섹션에 실어 다시 만든 디렉터에 돌려준다.
        getComponentStateStore().capture<ArenaDirectorComponent>( *pManager );
        // 디렉터가 세운 유닛 · 투사체는 판의 모습일 뿐이다 — 스냅샷에 실으면 복원된 것이 다시 세운 것과 겹친다.
        // 걷어 두면(삭제 대기는 스냅샷이 건너뛴다) 다시 만든 디렉터가 시작하며 세우고, 남은 디렉터는 다음 틱에 다시 세운다.
        // 순회 콜백 안에서는 오브젝트를 지울 수 없다(매니저 잠금 안) — 디렉터를 모은 뒤 걷는다.
        vector<ComponentHandle> listDirector;
        pManager->forEachComponentOfType<ArenaDirectorComponent>( [&listDirector]( ArenaDirectorComponent* pDirector )
        { listDirector.push_back( pDirector->getHandle() ); } );
        for ( const ComponentHandle& handle : listDirector )
        {
            ArenaDirectorComponent* pDirector = static_cast<ArenaDirectorComponent*>( pManager->resolveComponent( handle ) );
            if ( pDirector != nullptr )
                pDirector->despawnRuntime();
        }
    }

    void AbilityArenaGame::onAfterStateDeserialize()
    {
        GameObjectManager* pManager = findActiveObjectManager();
        if ( pManager != nullptr )
            getComponentStateStore().restore<ArenaDirectorComponent>( *pManager );
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::AbilityArenaGame );
