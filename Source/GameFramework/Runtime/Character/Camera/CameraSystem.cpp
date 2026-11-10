#include "pch.h"

#include "GameFramework/Runtime/Character/Camera/CameraSystem.h"

#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

#include "GameFramework/Runtime/Character/Camera/CameraDirectorComponent.h"
#include "GameFramework/Runtime/Character/Camera/CameraManagerComponent.h"

namespace sw
{
    CameraSystem::CameraSystem()
        : _listHandleScratch{}
    {
    }

    CameraSystem* CameraSystem::find( const GameObjectManager& manager )
    {
        // 이 키로는 이 타입만 붙인다(`ensureFor`).
        return static_cast<CameraSystem*>( manager.findFrameSystem( hashed_string( kFrameSystemKey ) ) );
    }

    CameraSystem& CameraSystem::ensureFor( GameObjectManager& manager )
    {
        CameraSystem* pSystem = find( manager );
        if ( pSystem != nullptr )
            return *pSystem;
        unique_ptr<CameraSystem> system = make_unique<CameraSystem>();
        pSystem                         = system.get();
        const bool bAdded               = manager.addFrameSystem( hashed_string( kFrameSystemKey ), std::move( system ) );
        SW_ASSERT( bAdded ); // 같은 키가 없음을 바로 위에서 보았고, 씬 데이터를 건드리지 않는다고 선언해 접근 충돌도 없다
        (void)bAdded;
        return *pSystem;
    }

    void CameraSystem::releaseIfUnused( GameObjectManager& manager )
    {
        // 모듈을 내리면 컴포넌트가 먼저 지워진다 — 마지막 카메라 컴포넌트가 떠날 때 떼므로 매니저에 이 타입이 남지 않는다(`ISceneFrameSystem` 의 약속).
        const ComponentRegistry& registry = manager.getComponentRegistry();
        if ( registry.getAll<CameraDirectorComponent>().empty() && registry.getAll<CameraManagerComponent>().empty() )
            manager.removeFrameSystem( hashed_string( kFrameSystemKey ) );
    }

    void CameraSystem::runMain( GameObjectManager& manager, float32 /*deltaTime*/ )
    {
        // 일은 시스템을 붙잡지 않고 매니저에서 다시 찾는다 — 큐가 돌기 전에 마지막 카메라 컴포넌트가 떠나 시스템이 떨어졌을 수 있다.
        // 첫 일은 `ApplyPrePhysics` 의 비우기에서 돌고, 거기서 넣은 일은 다음 비우기(`ApplyPostPhysics`)의 맨 앞에 선다 — 늦은 틱이 끝나고 그 틱이 미룬 일보다 앞이다.
        GameObjectManager* pManager = &manager;
        manager.deferPostTick( [pManager]()
        {
            pManager->deferPostTick( [pManager]()
            {
                CameraSystem* pSystem = find( *pManager );
                if ( pSystem != nullptr )
                    pSystem->runFramePass( *pManager );
            } );
        } );
    }

    SceneFrameAccess CameraSystem::getFrameAccess() const
    {
        // 감사: 틱 뒤 큐에 일을 넣을 뿐이다. 붙인 순서를 지키려고 게임 스레드에 둔다(레인 안전을 선언하지 않는다).
        return SceneFrameAccess{};
    }

    void CameraSystem::runFramePass( GameObjectManager& manager )
    {
        // 디렉터가 먼저다 — 매니저는 디렉터가 움직인 타깃 카메라를 같은 프레임에 읽는다.
        manager.forEachActiveRegistered<CameraDirectorComponent>( _listHandleScratch, []( CameraDirectorComponent& director )
        { director.resolvePendingCamera(); } );
        manager.forEachActiveRegistered<CameraManagerComponent>( _listHandleScratch, []( CameraManagerComponent& cameraManager )
        { cameraManager.resolvePendingCamera(); } );
    }
} // namespace sw
