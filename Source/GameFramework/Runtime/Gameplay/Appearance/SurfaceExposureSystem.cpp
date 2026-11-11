#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Appearance/SurfaceExposureSystem.h"

#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

#include "GameFramework/Runtime/Gameplay/Appearance/SurfaceExposureComponent.h"

namespace sw
{
    SurfaceExposureSystem::SurfaceExposureSystem()
        : _listHandleScratch{}
    {
    }

    SurfaceExposureSystem* SurfaceExposureSystem::find( const GameObjectManager& manager )
    {
        // 이 키로는 이 타입만 붙인다(`findOrCreate`).
        return static_cast<SurfaceExposureSystem*>( manager.findFrameSystem( hashed_string( kFrameSystemKey ) ) );
    }

    SurfaceExposureSystem& SurfaceExposureSystem::findOrCreate( GameObjectManager& manager )
    {
        SurfaceExposureSystem* pSystem = find( manager );
        if ( pSystem != nullptr )
            return *pSystem;
        unique_ptr<SurfaceExposureSystem> system = make_unique<SurfaceExposureSystem>();
        pSystem                                  = system.get();
        const bool bAdded                        = manager.addFrameSystem( hashed_string( kFrameSystemKey ), std::move( system ) );
        SW_ASSERT( bAdded ); // 같은 키가 없음을 바로 위에서 보았고, 씬 데이터를 건드리지 않는다고 선언해 접근 충돌도 없다
        (void)bAdded;
        return *pSystem;
    }

    void SurfaceExposureSystem::releaseIfUnused( GameObjectManager& manager )
    {
        // 모듈을 내리면 컴포넌트가 먼저 지워진다 — 마지막 노출 컴포넌트가 떠날 때 떼므로 매니저에 이 타입이 남지 않는다(`ISceneFrameSystem` 의 약속).
        if ( manager.getComponentRegistry().getAll<SurfaceExposureComponent>().empty() )
            manager.removeFrameSystem( hashed_string( kFrameSystemKey ) );
    }

    void SurfaceExposureSystem::runMain( GameObjectManager& manager, float32 /*deltaTime*/ )
    {
        // 일은 시스템을 붙잡지 않고 매니저에서 다시 찾는다 — 큐가 돌기 전에 마지막 노출 컴포넌트가 떠나 시스템이 떨어졌을 수 있다.
        // 시간은 컴포넌트 틱이 쌓은 것을 쓴다 — 틱이 돌지 않은(편집 모드 · 꺼진) 컴포넌트는 바뀌지 않는다.
        GameObjectManager* pManager = &manager;
        manager.deferPostTick( [pManager]()
        {
            SurfaceExposureSystem* pSystem = find( *pManager );
            if ( pSystem != nullptr )
                pSystem->runFramePass( *pManager );
        } );
    }

    SceneFrameAccess SurfaceExposureSystem::getFrameAccess() const
    {
        // 감사: 틱 뒤 큐에 일을 넣을 뿐이다. 붙인 순서를 지키려고 게임 스레드에 둔다(레인 안전을 선언하지 않는다).
        return SceneFrameAccess{};
    }

    void SurfaceExposureSystem::runFramePass( GameObjectManager& manager )
    {
        manager.forEachActiveRegistered<SurfaceExposureComponent>( _listHandleScratch, [&manager]( SurfaceExposureComponent& exposure )
        { exposure.applyExposure( manager ); } );
    }
} // namespace sw
