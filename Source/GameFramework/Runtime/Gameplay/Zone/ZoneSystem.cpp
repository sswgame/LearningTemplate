#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Zone/ZoneSystem.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

#include "GameFramework/Runtime/Gameplay/Zone/JumpEscapeReactionComponent.h"
#include "GameFramework/Runtime/Gameplay/Zone/ScreenReactionComponent.h"
#include "GameFramework/Runtime/Gameplay/Zone/ZoneContactComponent.h"
#include "GameFramework/Runtime/Gameplay/Zone/ZoneVolumeComponent.h"

namespace sw
{
    ZoneSystem::ZoneSystem()
        : _listHandleScratch{}
    {
    }

    ZoneSystem* ZoneSystem::find( const GameObjectManager& manager )
    {
        // 이 키로는 이 타입만 붙인다(`ensureFor`).
        return static_cast<ZoneSystem*>( manager.findFrameSystem( hashed_string( kFrameSystemKey ) ) );
    }

    ZoneSystem& ZoneSystem::ensureFor( GameObjectManager& manager )
    {
        ZoneSystem* pSystem = find( manager );
        if ( pSystem != nullptr )
            return *pSystem;
        unique_ptr<ZoneSystem> system = make_unique<ZoneSystem>();
        pSystem                       = system.get();
        const bool bAdded             = manager.addFrameSystem( hashed_string( kFrameSystemKey ), std::move( system ) );
        SW_ASSERT( bAdded ); // 같은 키가 없음을 바로 위에서 보았고, 씬 데이터를 건드리지 않는다고 선언해 접근 충돌도 없다
        (void)bAdded;
        return *pSystem;
    }

    void ZoneSystem::releaseIfUnused( GameObjectManager& manager )
    {
        // 모듈을 내리면 컴포넌트가 먼저 지워진다 — 마지막 지역 컴포넌트가 떠날 때 떼므로 매니저에 이 타입이 남지 않는다(`ISceneFrameSystem` 의 약속).
        const ComponentRegistry& registry = manager.getComponentRegistry();
        const bool               bUnused  = registry.getAll<ZoneVolumeComponent>().empty() && registry.getAll<ZoneContactComponent>().empty() && registry.getAll<JumpEscapeReactionComponent>().empty() &&
                             registry.getAll<ScreenReactionComponent>().empty();
        if ( bUnused )
            manager.removeFrameSystem( hashed_string( kFrameSystemKey ) );
    }

    void ZoneSystem::runBeforeTick( GameObjectManager& manager, float32 deltaTime )
    {
        // 일은 시스템을 붙잡지 않고 매니저에서 다시 찾는다 — 큐가 돌기 전에 마지막 지역 컴포넌트가 떠나 시스템이 떨어졌을 수 있다.
        GameObjectManager* pManager = &manager;
        manager.deferPostTick( [pManager, deltaTime]()
        {
            ZoneSystem* pSystem = find( *pManager );
            if ( pSystem != nullptr )
                pSystem->runFramePass( *pManager, deltaTime );
        } );
    }

    SceneFrameAccess ZoneSystem::getFrameAccess() const
    {
        // 감사: 틱 뒤 큐에 일을 넣을 뿐이다. 큐 넣기는 틱 워커도 하는 일이지만 붙인 순서를 지키려고 게임 스레드에 둔다(레인 안전을 선언하지 않는다).
        return SceneFrameAccess{};
    }

    void ZoneSystem::runFramePass( GameObjectManager& manager, float32 deltaTime )
    {
        if ( deltaTime > 0.0f )
        {
            manager.forEachActiveRegistered<ZoneVolumeComponent>( _listHandleScratch, [&manager, deltaTime]( ZoneVolumeComponent& volume )
            { volume.updateZone( manager, deltaTime ); } );
            manager.forEachActiveRegistered<ZoneContactComponent>( _listHandleScratch, [&manager, deltaTime]( ZoneContactComponent& contact )
            { contact.updateContacts( manager, deltaTime ); } );
        }
        manager.forEachActiveRegistered<JumpEscapeReactionComponent>( _listHandleScratch, []( JumpEscapeReactionComponent& escape )
        { escape.updateJumpEscape(); } );
        // 화면 반응은 이번 프레임 볼륨 · 접촉이 건 태그를 본다(일시정지면 가중치가 그대로다).
        manager.forEachActiveRegistered<ScreenReactionComponent>( _listHandleScratch, [&manager, deltaTime]( ScreenReactionComponent& screen )
        { screen.updateScreen( manager, deltaTime ); } );
    }
} // namespace sw
