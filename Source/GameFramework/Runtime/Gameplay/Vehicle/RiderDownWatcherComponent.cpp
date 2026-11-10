#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Vehicle/RiderDownWatcherComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"

#include "GameFramework/Runtime/Character/Control/ControlSystem.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/VehicleSeatComponent.h"

namespace sw
{
    RiderDownWatcherComponent::RiderDownWatcherComponent() = default;

    void RiderDownWatcherComponent::onHealthChanged( const HealthChangedEvent& event )
    {
        if ( event._kind != HealthChangeKind::Died )
            return;
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager != nullptr )
            pManager->executeOrDeferPostTickOnComponent( *this, []( RiderDownWatcherComponent& component )
            { component.forceDismount(); } );
    }

    void RiderDownWatcherComponent::onUnregister( GameObjectManager& manager )
    {
        // 조종 시스템이 없으면 씬을 비우는 중이다. 좌석도 곧 사라지므로 할 일이 없다.
        const GameObject* pOwner = getOwner();
        if ( pOwner != nullptr && ControlSystem::find( manager ) != nullptr )
            VehicleSeatComponent::releaseGoneOccupant( manager, pOwner->getHandle() );
        HealthListenerComponent::onUnregister( manager );
    }

    void RiderDownWatcherComponent::forceDismount()
    {
        GameObject*    pOwner = getOwner();
        PawnComponent* pPawn  = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        if ( pPawn != nullptr )
            (void)VehicleSeatComponent::dismount( *pPawn, true );
    }
} // namespace sw
