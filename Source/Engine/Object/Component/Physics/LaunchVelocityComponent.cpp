#include "pch.h"

#include "Engine/Object/Component/Physics/LaunchVelocityComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

namespace sw
{
    SW_LOG_CALLER( "LaunchVelocity" );

    LaunchVelocityComponent::LaunchVelocityComponent()
        : _speed{ 0.0f }
        , _localDirection{ 0.0f, 0.0f, 1.0f }
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
            return;
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
} // namespace sw
