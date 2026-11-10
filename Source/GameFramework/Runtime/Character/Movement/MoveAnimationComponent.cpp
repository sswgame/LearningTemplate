#include "pch.h"

#include "GameFramework/Runtime/Character/Movement/MoveAnimationComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Visual/Component/MeshComponent.h"

namespace sw
{
    MoveAnimationComponent::MoveAnimationComponent()
        : _speedParameter{}
        , _smoothingSeconds{ 0.15f }
        , _faceMinSpeed{ 0.2f }
        , _turnRateDegrees{ 540.0f }
        , _teleportDistance{ 2.0f }
        , _lastPosition{ 0.0f, 0.0f, 0.0f }
        , _speed{ 0.0f }
        , _bHasLast{ SW_FALSE }
    {
        setCanEverTick( true );
    }

    void MoveAnimationComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 몸을 옮기는 쪽(디렉터 PrePhysics · 뷰 PostUpdate)이 이번 프레임 자리를 쓴 뒤에 읽는다.
        setTickGroup( TickGroup::PostUpdate );
        _bHasLast = SW_FALSE;
        _speed    = 0.0f;
    }

    void MoveAnimationComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        GameObject*    pOwner = getOwner();
        MeshComponent* pBody  = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( pBody == nullptr )
            return;
        const float3  position = pBody->getWorldPosition();
        const float3  step{ position._x - _lastPosition._x, 0.0f, position._z - _lastPosition._z };
        const float32 stepDistance = step.getLength();
        const bool    bCounts      = _bHasLast == SW_TRUE && stepDistance <= _teleportDistance;
        _lastPosition              = position;
        _bHasLast                  = SW_TRUE;
        if ( bCounts == false )
            return;
        _speed = smoothSpeed( _speed, stepDistance, context.getDeltaTime(), _smoothingSeconds );

        SkeletalAnimatorComponent* pAnimator = pOwner->getComponent<SkeletalAnimatorComponent>();
        if ( pAnimator != nullptr )
            pAnimator->getParameters().setFloat( _speedParameter, _speed );
        if ( context.getDeltaTime() > 0.0f && stepDistance / context.getDeltaTime() > _faceMinSpeed )
        {
            const float3  rotation  = pBody->getLocalRotation();
            const float32 targetYaw = MathUtil::atan2( step._x, step._z );
            const float32 maxStep   = _turnRateDegrees * MathUtil::kDegreeToRadian * context.getDeltaTime();
            pBody->setLocalRotation( float3{ rotation._x, turnTowards( rotation._y, targetYaw, maxStep ), rotation._z } );
        }
    }

    float32 MoveAnimationComponent::smoothSpeed( float32 previousSpeed, float32 stepDistance, float32 deltaTime, float32 smoothingSeconds )
    {
        if ( deltaTime <= 0.0f )
            return previousSpeed;
        const float32 stepSpeed = stepDistance / deltaTime;
        if ( smoothingSeconds <= 0.0f )
            return stepSpeed;
        const float32 blend = 1.0f - MathUtil::exp( -deltaTime / smoothingSeconds );
        return previousSpeed + ( stepSpeed - previousSpeed ) * blend;
    }

    float32 MoveAnimationComponent::turnTowards( float32 yaw, float32 targetYaw, float32 maxStep )
    {
        return yaw + MathUtil::clamp( MathUtil::wrapAngle( targetYaw - yaw ), -maxStep, maxStep );
    }
} // namespace sw
