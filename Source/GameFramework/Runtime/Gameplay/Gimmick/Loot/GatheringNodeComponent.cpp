#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Loot/GatheringNodeComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
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
        , _fallDuration{ 0.0f }
        , _usesTaken{ 0 }
        , _respawnStepsLeft{ 0 }
        , _fallStepsLeft{ 0 }
        , _fallYaw{ 0.0f }
        , _standingRotation{}
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
            handleDepleted( who );
        return true;
    }

    void GatheringNodeComponent::configure( const hashed_string& item, int32 count, int32 uses )
    {
        _item  = item;
        _count = count;
        _uses  = uses;
    }

    void GatheringNodeComponent::handleDepleted( const GameObject& who )
    {
        GameObject&           owner = *getOwner(); // gather 가 이미 봤다
        const SceneComponent* pRoot = owner.getPrimarySceneComponent();
        const SceneComponent* pWho  = who.getPrimarySceneComponent();
        _respawnStepsLeft           = _respawnDelay > 0.0f ? _clock.secondsToSteps( _respawnDelay, 1 ) : 0;
        GimmickUtil::setInteractableEnabled( owner, false );
        if ( _fallDuration <= 0.0f || pRoot == nullptr )
        {
            GimmickUtil::setBodyActive( owner, false, this );
            return;
        }
        const float3 away = pWho != nullptr ? pRoot->getWorldPosition() - pWho->getWorldPosition() : float3{ 0.0f, 0.0f, 1.0f };
        _fallYaw          = MathUtil::atan2( away._x, away._z );
        _standingRotation = pRoot->getLocalRotation();
        _fallStepsLeft    = _clock.secondsToSteps( _fallDuration, 1 );
    }

    void GatheringNodeComponent::applyFall( float32 amount )
    {
        SceneComponent* pRoot = getOwner() != nullptr ? getOwner()->getPrimarySceneComponent() : nullptr;
        if ( pRoot == nullptr )
            return;
        // 쓰러지는 쪽을 앞(+Z)으로 본 요에서 앞으로 눕힌다(피치) — 서 있던 회전은 그 앞에 얹는다. 오브젝트 원점이 밑동이라 축은 밑동이다.
        const quaternion standing = quaternion::makeFromYawPitchRoll( _standingRotation._y, _standingRotation._x, _standingRotation._z );
        const float3     axis{ MathUtil::cos( _fallYaw ), 0.0f, -MathUtil::sin( _fallYaw ) };
        const quaternion fallen = quaternion::concatenate( standing, quaternion::makeFromAxisAngle( axis, MathUtil::kHalfPi * amount ) ).normalize();
        pRoot->setLocalRotation( amount > 0.0f ? fallen.getEulerAngles() : _standingRotation );
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
        if ( _fallStepsLeft > 0 )
        {
            --_fallStepsLeft;
            const int32   total = _clock.secondsToSteps( _fallDuration, 1 );
            const float32 time  = 1.0f - static_cast<float32>( _fallStepsLeft ) / static_cast<float32>( total );
            applyFall( time * time );
            if ( _fallStepsLeft == 0 )
                GimmickUtil::setBodyActive( *pOwner, false, this );
            return; // 다시 자라는 시간은 다 쓰러진 뒤부터 센다
        }
        if ( isDepleted() == false || _respawnStepsLeft <= 0 )
            return;
        --_respawnStepsLeft;
        if ( _respawnStepsLeft > 0 )
            return;
        _usesTaken = 0;
        if ( _fallDuration > 0.0f )
            applyFall( 0.0f );
        GimmickUtil::setBodyActive( *pOwner, true, this );
        GimmickUtil::setInteractableEnabled( *pOwner, true );
    }
} // namespace sw
