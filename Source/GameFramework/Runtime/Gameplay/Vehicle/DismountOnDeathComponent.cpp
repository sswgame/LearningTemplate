#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Vehicle/DismountOnDeathComponent.h"

#include "Engine/Character/Socket/SocketBindingComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"

#include "GameFramework/Runtime/Character/Control/ControlSystem.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/VehicleSeatComponent.h"

namespace sw
{
    DismountOnDeathComponent::DismountOnDeathComponent()
        : _healthListenerPort{}
        , _movementSuspension{}
        , _bFallen{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void DismountOnDeathComponent::onHealthChanged( const HealthChangedEvent& event )
    {
        const GameObject*        pOwner   = getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager != nullptr && event._kind == HealthChangeKind::Reset && _bFallen == SW_TRUE )
            pManager->executeOrDeferPostTickOnComponent( *this, []( DismountOnDeathComponent& component )
            { component.resumeAfterFall(); } );
        if ( pManager != nullptr && event._kind == HealthChangeKind::Died )
            pManager->executeOrDeferPostTickOnComponent( *this, []( DismountOnDeathComponent& component )
            { component.forceDismount(); } );
    }

    void DismountOnDeathComponent::onUnregister( GameObjectManager& manager )
    {
        // 조종 시스템이 없으면 씬을 비우는 중이다. 좌석도 곧 사라지므로 할 일이 없다.
        const GameObject* pOwner = getOwner();
        if ( pOwner != nullptr && ControlSystem::find( manager ) != nullptr )
            VehicleSeatComponent::releaseGoneOccupant( manager, pOwner->getHandle() );
        Component::onUnregister( manager );
    }

    void DismountOnDeathComponent::forceDismount()
    {
        GameObject*    pOwner = getOwner();
        PawnComponent* pPawn  = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        if ( pPawn != nullptr )
            (void)VehicleSeatComponent::dismount( *pPawn, true );
    }

    void DismountOnDeathComponent::holdFall( Subscription&& suspension )
    {
        _bFallen            = SW_TRUE;
        _movementSuspension = std::move( suspension );
    }

    void DismountOnDeathComponent::resumeAfterFall()
    {
        if ( _bFallen == SW_FALSE || getOwner() == nullptr )
            return;
        _bFallen = SW_FALSE;
        if ( SocketBindingComponent* const pBinding = getOwner()->getComponent<SocketBindingComponent>() )
            pBinding->endPhysics();
        _movementSuspension.reset();
    }

    void DismountOnDeathComponent::HealthListenerPort::onHealthChanged( const HealthChangedEvent& event )
    {
        getPortOwner<DismountOnDeathComponent>( *this, offsetof( DismountOnDeathComponent, _healthListenerPort ) ).onHealthChanged( event );
    }
} // namespace sw
