#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Vehicle/VehicleExitComponent.h"

#include "Core/Foundation/Constants.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/VehicleSeatComponent.h"

namespace sw
{
    VehicleExitComponent::VehicleExitComponent()
        : _exitButton{}
        , _exitIndex{}
    {
        setCanEverTick( true );
    }

    void VehicleExitComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PrePhysics );
        const GameObject*    pOwner = getOwner();
        const PawnComponent* pPawn  = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        _exitIndex                  = pPawn != nullptr ? pPawn->findButton( _exitButton ) : nullopt;
    }

    void VehicleExitComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject*          pOwner       = getOwner();
        const PawnComponent* pPawn        = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        GameObjectManager*   pManager     = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const bool           bNoExitInput = pPawn == nullptr || pManager == nullptr || pPawn->wasButtonTriggered( _exitIndex ) == false;
        if ( bNoExitInput )
            return;
        pManager->executeOrDeferPostTickOnComponent( *this, []( VehicleExitComponent& component )
        { component.exitDriver(); } );
    }

    void VehicleExitComponent::exitDriver()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        // 이 탈것의 운전석을 좌석 레지스트리에서 찾는다. 씬 전체를 순회하지 않는다.
        for ( VehicleSeatComponent* pSeat : pManager->getComponentRegistry().getAll<VehicleSeatComponent>() )
        {
            const bool bSkipSeat = pSeat == nullptr || pSeat->getOwner() != pOwner || pSeat->isDriverSeat() == false || pSeat->isFree();
            if ( bSkipSeat )
                continue;
            GameObject*    pRider     = pManager->resolveGameObject( pSeat->getOccupant() );
            PawnComponent* pRiderPawn = pRider != nullptr ? pRider->getComponent<PawnComponent>() : nullptr;
            if ( pRiderPawn != nullptr )
                (void)VehicleSeatComponent::dismount( *pRiderPawn, false );
            return;
        }
    }
} // namespace sw
