#include "pch.h"

#include "Engine/Physics/Component/LaunchVelocityComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"

namespace sw
{
    SW_LOG_CALLER( "LaunchVelocity" );

    LaunchVelocityComponent::LaunchVelocityComponent()
        : _speed{ 0.0f }
        , _localDirection{ 0.0f, 0.0f, 1.0f }
        , _alignGain{ 0.0f }
        , _bLaunched{ SW_FALSE }
        , _bPending{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void LaunchVelocityComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        _bPending = _bLaunched == SW_FALSE ? SW_TRUE : SW_FALSE;
    }

    void LaunchVelocityComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        if ( _bPending == SW_FALSE )
        {
            steerTowardVelocity();
            return;
        }
        _bPending                    = SW_FALSE;
        GameObject*           pOwner = getOwner();
        RigidBodyComponent*   pBody  = pOwner != nullptr ? pOwner->getComponent<RigidBodyComponent>() : nullptr;
        const SceneComponent* pRoot  = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        const bool            bReady = pBody != nullptr && pRoot != nullptr && pBody->getBodyType() == PhysicsBodyType::Dynamic;
        if ( bReady == false )
        {
            SW_LOG_ERROR( "'%#': LaunchVelocityComponent needs a dynamic RigidBodyComponent on the same object - nothing was launched",
                          pOwner != nullptr ? pOwner->getName().c_str() : "(no owner)" );
            return;
        }
        const float32 length = _localDirection.getLength();
        if ( length <= 1e-6f )
        {
            SW_LOG_ERROR( "'%#': LaunchVelocityComponent _localDirection is zero - nothing was launched", pOwner->getName().c_str() );
            return;
        }
        const float3 worldDirection = float3::transformVector( _localDirection / length, pRoot->getWorldMatrix() );
        const float3 velocity       = worldDirection * ( _speed / MathUtil::max( worldDirection.getLength(), 1e-6f ) );
        pBody->setLinearVelocity( velocity );
        _bLaunched = SW_TRUE;
    }

    void LaunchVelocityComponent::steerTowardVelocity()
    {
        // 날린 뒤에만, 정렬을 켠 것만 한다. 속도가 거의 없으면(박히거나 멈춘 화살) 방향이 정해지지 않으므로 건드리지 않는다.
        constexpr float32 kMinAlignSpeed = 0.5f;
        if ( _bLaunched == SW_FALSE || _alignGain <= 0.0f )
            return;
        GameObject*           pOwner        = getOwner();
        RigidBodyComponent*   pBody         = pOwner != nullptr ? pOwner->getComponent<RigidBodyComponent>() : nullptr;
        const SceneComponent* pRoot         = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        const bool            bHasAwakeBody = pBody != nullptr && pRoot != nullptr && pBody->isSleeping() == false;
        if ( bHasAwakeBody == false )
            return;
        const float3  velocity = pBody->getLinearVelocity();
        const float32 speed    = velocity.getLength();
        const float32 length   = _localDirection.getLength();
        if ( speed < kMinAlignSpeed || length <= 1e-6f )
            return;
        const float3 forward = float3::transformVector( _localDirection / length, pRoot->getWorldMatrix() ).normalize();
        // 앞 방향에서 속도 방향으로 돌리는 축 × 이득. 어긋난 각이 클수록 빠르게 돌고 맞으면 0 이라 진동하지 않는다(물리가 각속도를 이어 받는다).
        pBody->setAngularVelocity( forward.cross( velocity / speed ) * _alignGain );
    }
} // namespace sw
