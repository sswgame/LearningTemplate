#include "pch.h"

#include "GameFramework/Base/Gameplay/Vehicle/VehicleExitComponent.h"

#include "Engine/Object/GameObject/ComponentRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Actor/Control/Pawn/PawnComponent.h"
#include "GameFramework/Base/Gameplay/Vehicle/MountUtil.h"
#include "GameFramework/Base/Gameplay/Vehicle/VehicleSeatComponent.h"

namespace sw
{
    VehicleExitComponent::VehicleExitComponent()
        : _exitButton{ "Exit" }
        , _exitIndex{ -1 }
    {
        setCanEverTick( true );
    }

    void VehicleExitComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PrePhysics );
        const GameObject*    pOwner = getOwner();
        const PawnComponent* pPawn  = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        _exitIndex                  = pPawn != nullptr ? pPawn->findButton( _exitButton ) : -1;
    }

    void VehicleExitComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject*          pOwner   = getOwner();
        const PawnComponent* pPawn    = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        GameObjectManager*   pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pPawn == nullptr || pManager == nullptr || pPawn->wasButtonTriggered( _exitIndex ) == false )
            return;
        pManager->executeOrDeferPostTick( SW_DELEGATE_METHOD( GameObjectManager::PostTickDelegate, &VehicleExitComponent::exitDriver, this ) );
    }

    void VehicleExitComponent::exitDriver()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        // 이 탈것의 운전석 — 좌석 등록부에서 이 오브젝트의 것을 찾는다(씬을 훑지 않는다).
        for ( VehicleSeatComponent* pSeat : pManager->getComponentRegistry().getAll<VehicleSeatComponent>() )
        {
            if ( pSeat == nullptr || pSeat->getOwner() != pOwner || pSeat->isDriverSeat() == false || pSeat->isFree() )
                continue;
            GameObject*    pRider     = pManager->resolveGameObject( pSeat->getOccupant() );
            PawnComponent* pRiderPawn = pRider != nullptr ? pRider->getComponent<PawnComponent>() : nullptr;
            if ( pRiderPawn != nullptr )
                (void)MountUtil::dismount( *pRiderPawn, false );
            return;
        }
    }
} // namespace sw
