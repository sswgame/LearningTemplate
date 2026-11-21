#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Loot/GatheringNodeComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickUtil.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Trigger/GimmickSensorComponent.h"
#include "GameFramework/Runtime/Gameplay/Interaction/InteractableComponent.h"

namespace sw
{
    GatheringNodeComponent::GatheringNodeComponent()
        : _item{}
        , _count{ 1 }
        , _uses{ 3 }
        , _respawnDelay{ 30.0f }
        , _usesTaken{ 0 }
        , _respawnStepsLeft{ 0 }
        , _clock{}
    {
    }

    bool GatheringNodeComponent::gather( const GameObject& who )
    {
        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr || isDepleted() )
            return false;
        ++_usesTaken;
        GimmickUtil::giveItem( who, _item, _count );
        if ( isDepleted() )
        {
            _respawnStepsLeft = _respawnDelay > 0.0f ? _clock.secondsToSteps( _respawnDelay, 1 ) : 0;
            GimmickUtil::setBodyActive( *pOwner, false, this );
            GimmickUtil::setInteractableEnabled( *pOwner, false );
        }
        return true;
    }

    void GatheringNodeComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        const int32 stepCount = _clock.consume( context.getDeltaTime() );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            stepOnce();
        }
    }

    void GatheringNodeComponent::stepOnce()
    {
        GameObject*              pOwner   = getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        // 상호작용(Gather)을 끝낸 쪽에 준다.
        GimmickSensorComponent* pSensor = pOwner->getComponent<GimmickSensorComponent>();
        if ( pSensor != nullptr && pSensor->consumeUses() > 0.0f )
        {
            const InteractableComponent* pInteractable = pOwner->getComponent<InteractableComponent>();
            const GameObject*            pWho          = pInteractable != nullptr ? pManager->resolveGameObject( pInteractable->getLastInteractor() ) : nullptr;
            if ( pWho != nullptr )
                (void)gather( *pWho );
        }
        if ( isDepleted() == false || _respawnStepsLeft <= 0 )
            return;
        --_respawnStepsLeft;
        if ( _respawnStepsLeft > 0 )
            return;
        _usesTaken = 0;
        GimmickUtil::setBodyActive( *pOwner, true, this );
        GimmickUtil::setInteractableEnabled( *pOwner, true );
    }
} // namespace sw
