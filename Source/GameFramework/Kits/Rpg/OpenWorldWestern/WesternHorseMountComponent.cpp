#include "pch.h"

#include "GameFramework/Kits/Rpg/OpenWorldWestern/WesternHorseMountComponent.h"

#include "Engine/Object/GameObject/ComponentRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Control/PawnComponent.h"
#include "GameFramework/Base/Vehicle/MountMovementComponent.h"
#include "GameFramework/Base/Vehicle/MountUtil.h"
#include "GameFramework/Base/Vehicle/VehicleSeatComponent.h"

namespace sw
{
    WesternHorseMountComponent::WesternHorseMountComponent()
        : _gallopAbility{}
        , _resumeGallopRatio{ 0.25f }
        , _pHorse{ nullptr }
    {
        setCanEverTick( true );
    }

    void WesternHorseMountComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 걸음새는 PrePhysics 의 탈것 이동이 고른다 — 그 뒤에 스태미나를 쓴다.
        setTickGroup( TickGroup::PostPhysics );
    }

    void WesternHorseMountComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject*             pOwner    = getOwner();
        MountMovementComponent* pMovement = pOwner != nullptr ? pOwner->getComponent<MountMovementComponent>() : nullptr;
        if ( _pHorse == nullptr || pMovement == nullptr )
            return;
        _pHorse->setRidden( isRidden() );
        // 유대 능력이 없는 말은 질주하지 않는다.
        const bool bCanGallop = _gallopAbility.empty() || _pHorse->hasAbility( _gallopAbility );
        pMovement->setMaxGait( bCanGallop ? MountGait::Gallop : MountGait::Canter );
        if ( pMovement->getGait() == MountGait::Gallop )
        {
            if ( _pHorse->gallop( deltaTime ) == false )
                pMovement->setGallopAllowed( false );
        }
        else if ( pMovement->isGallopAllowed() == false && _pHorse->getStamina().getRatio() >= _resumeGallopRatio )
        {
            pMovement->setGallopAllowed( true );
        }
    }

    WesternHorseReaction WesternHorseMountComponent::frighten( float32 amount )
    {
        if ( _pHorse == nullptr )
            return WesternHorseReaction::Calm;
        const WesternHorseReaction reaction = _pHorse->frighten( amount );
        GameObject*                pOwner   = getOwner();
        GameObjectManager*         pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( reaction == WesternHorseReaction::Bucked && pManager != nullptr )
            pManager->executeOrDeferPostTick( SW_DELEGATE_METHOD( GameObjectManager::PostTickDelegate, &WesternHorseMountComponent::throwRiders, this ) );
        return reaction;
    }

    void WesternHorseMountComponent::throwRiders()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        // 좌석 등록부에서 이 말의 것만 — 내리기는 등록부를 바꾸지 않지만, 내린 탑승자를 먼저 모은 뒤 내린다.
        vector<GameObjectHandle> listRider;
        for ( const VehicleSeatComponent* pSeat : pManager->getComponentRegistry().getAll<VehicleSeatComponent>() )
        {
            if ( pSeat != nullptr && pSeat->getOwner() == pOwner && pSeat->isFree() == false )
                listRider.push_back( pSeat->getOccupant() );
        }
        for ( const GameObjectHandle& rider : listRider )
        {
            GameObject*    pRider     = pManager->resolveGameObject( rider );
            PawnComponent* pRiderPawn = pRider != nullptr ? pRider->getComponent<PawnComponent>() : nullptr;
            if ( pRiderPawn != nullptr )
                (void)MountUtil::dismount( *pRiderPawn, true );
        }
    }

    bool WesternHorseMountComponent::isRidden() const
    {
        const GameObject*        pOwner   = getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return false;
        for ( const VehicleSeatComponent* pSeat : pManager->getComponentRegistry().getAll<VehicleSeatComponent>() )
        {
            if ( pSeat != nullptr && pSeat->getOwner() == pOwner && pSeat->isDriverSeat() && pSeat->isFree() == false )
                return true;
        }
        return false;
    }
} // namespace sw
