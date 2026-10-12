#include "pch.h"

#include "GameFramework/Base/Gameplay/Vehicle/RiderDownWatcherComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Actor/Control/Pawn/PawnComponent.h"
#include "GameFramework/Base/Gameplay/Vehicle/MountUtil.h"

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
            pManager->executeOrDeferPostTickOnComponent<RiderDownWatcherComponent>( getHandle(), []( RiderDownWatcherComponent& component )
            { component.forceDismount(); } );
    }

    void RiderDownWatcherComponent::forceDismount()
    {
        GameObject*    pOwner = getOwner();
        PawnComponent* pPawn  = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        if ( pPawn != nullptr )
            (void)MountUtil::dismount( *pPawn, true );
    }
} // namespace sw
