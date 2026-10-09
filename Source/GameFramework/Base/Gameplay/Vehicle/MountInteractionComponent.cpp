#include "pch.h"

#include "GameFramework/Base/Gameplay/Vehicle/MountInteractionComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Actor/Control/Pawn/PawnComponent.h"
#include "GameFramework/Base/Gameplay/Vehicle/MountUtil.h"
#include "GameFramework/Base/Gameplay/Vehicle/VehicleSeatComponent.h"

namespace sw
{
    MountInteractionComponent::MountInteractionComponent()
        : _interactButton{ "Interact" }
        , _interactIndex{ -1 }
    {
        setCanEverTick( true );
    }

    void MountInteractionComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PrePhysics );
        const GameObject*    pOwner = getOwner();
        const PawnComponent* pPawn  = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        _interactIndex              = pPawn != nullptr ? pPawn->findButton( _interactButton ) : -1;
    }

    void MountInteractionComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject*          pOwner   = getOwner();
        const PawnComponent* pPawn    = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        GameObjectManager*   pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pPawn == nullptr || pManager == nullptr || pPawn->wasButtonTriggered( _interactIndex ) == false )
            return;
        pManager->executeOrDeferPostTick( SW_DELEGATE_METHOD( GameObjectManager::PostTickDelegate, &MountInteractionComponent::toggleMount, this ) );
    }

    void MountInteractionComponent::toggleMount()
    {
        GameObject*    pOwner = getOwner();
        PawnComponent* pPawn  = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        if ( pPawn == nullptr )
            return;
        const VehicleSeatComponent* pCurrentSeat = MountUtil::findSeatOf( *pPawn );
        if ( pCurrentSeat != nullptr )
        {
            // 운전석에서 내리는 것은 탈것 쪽 버튼이다 — 여기서는 승객석만.
            if ( pCurrentSeat->isDriverSeat() == false )
                (void)MountUtil::dismount( *pPawn, false );
            return;
        }
        VehicleSeatComponent* pSeat = MountUtil::findNearestFreeSeat( *pPawn );
        if ( pSeat != nullptr )
            (void)MountUtil::mount( *pPawn, *pSeat );
    }
} // namespace sw
