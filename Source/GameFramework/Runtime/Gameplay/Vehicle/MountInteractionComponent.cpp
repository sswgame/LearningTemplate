#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Vehicle/MountInteractionComponent.h"

#include "Core/Foundation/Constants.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/VehicleSeatComponent.h"

namespace sw
{
    MountInteractionComponent::MountInteractionComponent()
        : _interactButton{}
        , _interactIndex{}
    {
        setCanEverTick( true );
    }

    void MountInteractionComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PrePhysics );
        const GameObject*    pOwner = getOwner();
        const PawnComponent* pPawn  = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        _interactIndex              = pPawn != nullptr ? pPawn->findButton( _interactButton ) : nullopt;
    }

    void MountInteractionComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject*          pOwner        = getOwner();
        const PawnComponent* pPawn         = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        GameObjectManager*   pManager      = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const bool           bNoMountInput = pPawn == nullptr || pManager == nullptr || pPawn->wasButtonTriggered( _interactIndex ) == false;
        if ( bNoMountInput )
            return;
        // 버튼이 발동한 프레임에만 미룬다(좌석 · 탑승자 계층을 바꾸는 사건) — 매 프레임 일이 아니라 시스템으로 옮기지 않는다.
        pManager->executeOrDeferPostTickOnComponent( *this, []( MountInteractionComponent& component )
        { component.toggleMount(); } );
    }

    void MountInteractionComponent::toggleMount()
    {
        GameObject*    pOwner = getOwner();
        PawnComponent* pPawn  = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        if ( pPawn == nullptr )
            return;
        const VehicleSeatComponent* pCurrentSeat = VehicleSeatComponent::findSeatOf( *pPawn );
        if ( pCurrentSeat != nullptr )
        {
            // 운전석에서 내리는 것은 탈것 쪽 버튼이다. 여기서는 승객석만 처리한다.
            if ( pCurrentSeat->isDriverSeat() == false )
                (void)VehicleSeatComponent::dismount( *pPawn, false );
            return;
        }
        VehicleSeatComponent* pSeat = VehicleSeatComponent::findNearestFreeSeat( *pPawn );
        if ( pSeat != nullptr )
            (void)VehicleSeatComponent::mount( *pPawn, *pSeat ); // 태우지 못하면 아무것도 바뀌지 않는다 — 결과는 좌석 상태로 본다
    }
} // namespace sw
