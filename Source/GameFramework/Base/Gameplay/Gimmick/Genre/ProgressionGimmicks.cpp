#include "pch.h"

#include "GameFramework/Base/Gameplay/Gimmick/Genre/ProgressionGimmicks.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Foundation/Framework/GameEventUtil.h"
#include "GameFramework/Base/Gameplay/Gimmick/Genre/GenreGimmickUtil.h"
#include "GameFramework/Base/Gameplay/Gimmick/GimmickSensorComponent.h"
#include "GameFramework/Base/Gameplay/Interaction/InteractableComponent.h"

namespace sw
{
    AbilityGateComponent::AbilityGateComponent()
        : _requiredTags{}
        , _openRadius{ 2.0f }
        , _bStaysOpen{ true }
        , _bPlanar{ false }
        , _bOpen{ false }
        , _clock{ GenreGimmickUtil::makeClock() }
    {
    }

    void AbilityGateComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 세이브 · 핫 리로드로 열린 채 돌아왔으면 몸을 꺼 둔다.
        GameObject* pOwner = getOwner();
        if ( _bOpen && pOwner != nullptr )
            GenreGimmickUtil::setBodyActive( *pOwner, false, this );
    }

    void AbilityGateComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        const int32 stepCount = _clock.consume( deltaTime );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            stepOnce();
        }
    }

    void AbilityGateComponent::stepOnce()
    {
        GameObject*           pOwner   = getOwner();
        GameObjectManager*    pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const SceneComponent* pScene   = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pManager == nullptr || pScene == nullptr || ( _bOpen && _bStaysOpen ) || _requiredTags.getTagCount() == 0 )
            return;
        const float3        center     = pScene->getWorldPosition();
        const uint64        selfId     = pOwner->getObjectId();
        const TagContainer& required   = _requiredTags;
        const float32       radius     = _openRadius;
        const bool          bPlanar    = _bPlanar;
        bool                bQualified = false;
        pManager->forEachGameObject( [&bQualified, &required, center, selfId, radius, bPlanar]( GameObject* pObject )
        {
            const SceneComponent* pOther = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
            if ( bQualified || pOther == nullptr || pObject->getObjectId() == selfId || pObject->getTags().hasAllTags( required ) == false )
                return;
            float3 offset = pOther->getWorldPosition() - center;
            if ( bPlanar )
                offset._z = 0.0f;
            bQualified = offset.getLength() <= radius;
        } );
        if ( bQualified == _bOpen )
            return;
        _bOpen = bQualified;
        GenreGimmickUtil::setBodyActive( *pOwner, _bOpen == false, this );
    }

    GatheringNodeComponent::GatheringNodeComponent()
        : _item{ "Herb" }
        , _count{ 1 }
        , _uses{ 3 }
        , _respawnDelay{ 30.0f }
        , _usesLeft{ 3 }
        , _respawnStepsLeft{ 0 }
        , _clock{ GenreGimmickUtil::makeClock() }
    {
    }

    bool GatheringNodeComponent::gather( GameObject& who )
    {
        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr || isDepleted() )
            return false;
        --_usesLeft;
        GimmickItemEvent event;
        event._target = who.getHandle();
        event._source = pOwner->getHandle();
        event._item   = _item;
        event._count  = _count;
        GameEventUtil::send( event );
        if ( isDepleted() )
        {
            _respawnStepsLeft = _respawnDelay > 0.0f ? GenreGimmickUtil::toSteps( _respawnDelay, 1 ) : 0;
            GenreGimmickUtil::setBodyActive( *pOwner, false, this );
            GenreGimmickUtil::setInteractableEnabled( *pOwner, false );
        }
        return true;
    }

    void GatheringNodeComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        const int32 stepCount = _clock.consume( deltaTime );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            stepOnce();
        }
    }

    void GatheringNodeComponent::stepOnce()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        // 상호작용(Gather)을 끝낸 이에게 준다.
        GimmickSensorComponent* pSensor = pOwner->getComponent<GimmickSensorComponent>();
        if ( pSensor != nullptr && pSensor->consumeUses() > 0.0f )
        {
            const InteractableComponent* pInteractable = pOwner->getComponent<InteractableComponent>();
            GameObject*                  pWho          = pInteractable != nullptr ? pManager->resolveGameObject( pInteractable->getLastInteractor() ) : nullptr;
            if ( pWho != nullptr )
                (void)gather( *pWho );
        }
        if ( isDepleted() == false || _respawnStepsLeft <= 0 )
            return;
        --_respawnStepsLeft;
        if ( _respawnStepsLeft > 0 )
            return;
        _usesLeft = _uses;
        GenreGimmickUtil::setBodyActive( *pOwner, true, this );
        GenreGimmickUtil::setInteractableEnabled( *pOwner, true );
    }
} // namespace sw
