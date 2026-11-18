#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Vehicle/VehicleSeatSystem.h"

#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

#include "GameFramework/Runtime/Foundation/Framework/FrameSystemUtil.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/VehicleSeatComponent.h"

namespace sw
{
    VehicleSeatSystem::VehicleSeatSystem()
        : _listHandleScratch{}
    {
    }

    VehicleSeatSystem* VehicleSeatSystem::find( const GameObjectManager& manager )
    {
        return FrameSystemUtil::find<VehicleSeatSystem>( manager );
    }

    VehicleSeatSystem& VehicleSeatSystem::getOrCreate( GameObjectManager& manager )
    {
        return FrameSystemUtil::getOrCreate<VehicleSeatSystem>( manager );
    }

    void VehicleSeatSystem::releaseIfUnused( GameObjectManager& manager )
    {
        // 모듈을 내리면 컴포넌트가 먼저 지워진다 — 마지막 좌석이 떠날 때 떼므로 매니저에 이 타입이 남지 않는다(`ISceneFrameSystem` 의 약속).
        if ( manager.getComponentRegistry().getAll<VehicleSeatComponent>().empty() )
            FrameSystemUtil::remove<VehicleSeatSystem>( manager );
    }

    void VehicleSeatSystem::runMain( GameObjectManager& manager, float32 /*deltaTime*/ )
    {
        // 일은 시스템을 붙잡지 않고 매니저에서 다시 찾는다 — 큐가 돌기 전에 마지막 좌석이 떠나 시스템이 떨어졌을 수 있다.
        // 이 일은 `ApplyPrePhysics` 비우기의 맨 앞에서 돈다 — 지난 프레임 `PostPhysics` 틱이 잰 값을 이번 프레임 카메라 계산(`ApplyPostPhysics`) 앞에 건다.
        GameObjectManager* pManager = &manager;
        manager.deferPostTick( [pManager]()
        {
            VehicleSeatSystem* pSystem = find( *pManager );
            if ( pSystem != nullptr )
                pSystem->runFramePass( *pManager );
        } );
    }

    SceneFrameAccess VehicleSeatSystem::getFrameAccess() const
    {
        // 감사: 틱 뒤 큐에 일을 넣을 뿐이다. 붙인 순서를 지키려고 게임 스레드에 둔다(레인 안전을 선언하지 않는다).
        return SceneFrameAccess{};
    }

    void VehicleSeatSystem::runFramePass( GameObjectManager& manager )
    {
        manager.forEachActiveRegistered<VehicleSeatComponent>( _listHandleScratch, [&manager]( VehicleSeatComponent& seat )
        { seat.applyPendingRide( manager ); } );
    }
} // namespace sw
