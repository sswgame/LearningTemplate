#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Vehicle/VehicleExitComponent.h"

#include "Core/Foundation/Constants.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Service/SceneNetLink.h"

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
        const GameObject*    pOwner = getOwner();
        const PawnComponent* pPawn  = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        _exitIndex                  = pPawn != nullptr ? pPawn->findButton( _exitButton ) : nullopt;
    }

    void VehicleExitComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        GameObject*          pOwner       = getOwner();
        const PawnComponent* pPawn        = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        const bool           bNoExitInput = pPawn == nullptr || pPawn->wasButtonTriggered( _exitIndex ) == false;
        if ( bNoExitInput )
            return;
        context.deferPostTickOnComponent( *this, []( VehicleExitComponent& component )
        { component.exitDriver(); } );
    }

    void VehicleExitComponent::exitDriver()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager == nullptr || pManager->getSubsystem<SceneNetLink>().hasAuthority() == false )
            return; // 접속한 클라이언트에서는 서버의 탈것이 내린다
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
