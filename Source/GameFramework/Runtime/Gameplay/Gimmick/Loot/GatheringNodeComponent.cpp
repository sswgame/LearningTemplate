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
        if ( GimmickUtil::giveItem( *pOwner, who, _item, _count ) == false )
            return false; // 받을 가방이 없거나 차면 채집 횟수를 쓰지 않는다
        ++_usesTaken;
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
            const GameObjectHandle       who           = pInteractable != nullptr ? pInteractable->getLastInteractor() : GameObjectHandle{};
            // 가방은 다른 오브젝트(조종자)의 상태라 틱 뒤 게임 스레드에서 준다.
            const GameObjectHandle node = pOwner->getHandle();
            pManager->executeOrDeferPostTickOnScene( [node, who]( GameObjectManager& sceneManager )
            {
                GameObject*             pNodeObject = sceneManager.resolveGameObject( node );
                GatheringNodeComponent* pNode       = pNodeObject != nullptr ? pNodeObject->getComponent<GatheringNodeComponent>() : nullptr;
                const GameObject*       pWho        = sceneManager.resolveGameObject( who );
                if ( pNode != nullptr && pWho != nullptr )
                    (void)pNode->gather( *pWho ); // 줬는지는 쓰지 않는다 — 못 주면 giveItem 이 경고를 남긴다
            } );
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
