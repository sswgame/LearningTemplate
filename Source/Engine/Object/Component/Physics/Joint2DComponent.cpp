#include "pch.h"

#include "Engine/Object/Component/Physics/Joint2DComponent.h"

#include "Engine/Object/Component/Physics/RigidBody2DComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/ScenePhysics.h"

namespace sw
{
    Joint2DComponent::Joint2DComponent()
        : PhysicsComponent{ PhysicsComponentPhase::Joint }
        , _jointType{ PhysicsJointType::Hinge }
        , _axis{ 1.0f, 0.0f }
        , _minLimit{ 0.0f }
        , _maxLimit{ 0.0f }
        , _motorMode{ PhysicsMotorMode::Off }
        , _motorTarget{ 0.0f }
        , _motorMaxForce{ 0.0f }
        , _bLimitsEnabled{ false }
        , _bCollideConnectedBodies{ false }
        , _bConnectToWorld{ false }
        , _joint{}
        , _jointBodyA{}
        , _jointBodyB{}
    {
    }

    float32 Joint2DComponent::getJointPosition() const
    {
        const IPhysicsScene2D* pScene = getScenePhysics() != nullptr ? getScenePhysics()->findScene2D() : nullptr;
        return pScene != nullptr ? pScene->getJointPosition( _joint ) : 0.0f;
    }

    void Joint2DComponent::setMotor( PhysicsMotorMode mode, float32 target, float32 maxForce )
    {
        _motorMode              = mode;
        _motorTarget            = target;
        _motorMaxForce          = maxForce;
        IPhysicsScene2D* pScene = getScenePhysics() != nullptr ? getScenePhysics()->findScene2D() : nullptr;
        if ( pScene != nullptr && _joint.isValid() )
            pScene->setJointMotor( _joint, PhysicsJointMotor{ target, maxForce, mode } );
    }

    void Joint2DComponent::setJointType( PhysicsJointType type )
    {
        _jointType = type;
        requestRebuild();
    }

    void Joint2DComponent::setLimits( bool bEnabled, float32 minLimit, float32 maxLimit )
    {
        _bLimitsEnabled = bEnabled;
        _minLimit       = minLimit;
        _maxLimit       = maxLimit;
        requestRebuild();
    }

    void Joint2DComponent::setConnectToWorld( bool bConnectToWorld )
    {
        _bConnectToWorld = bConnectToWorld;
        requestRebuild();
    }

    const RigidBody2DComponent* Joint2DComponent::findConnectedBody() const
    {
        if ( _bConnectToWorld )
            return nullptr;
        for ( const GameObject* pObject = getOwner() != nullptr ? getOwner()->getParent() : nullptr; pObject != nullptr; pObject = pObject->getParent() )
        {
            const RigidBody2DComponent* pBody = pObject->getComponent<RigidBody2DComponent>();
            if ( pBody != nullptr )
                return pBody;
        }
        return nullptr;
    }

    void Joint2DComponent::beginPhysicsFrame( ScenePhysics& physics )
    {
        if ( isSimulated() == false )
        {
            releasePhysics( physics );
            return;
        }
        IPhysicsScene2D* pScene = physics.getScene2D();
        if ( pScene == nullptr )
            return;
        if ( consumeRebuild() )
            releasePhysics( physics );

        const RigidBody2DComponent* pBodyA = getOwner()->getComponent<RigidBody2DComponent>();
        const RigidBody2DComponent* pBodyB = findConnectedBody();
        const PhysicsBodyHandle     bodyA  = pBodyA != nullptr ? pBodyA->getBodyHandle() : PhysicsBodyHandle{};
        const PhysicsBodyHandle     bodyB  = pBodyB != nullptr ? pBodyB->getBodyHandle() : PhysicsBodyHandle{};
        // 이은 바디가 바뀌었거나(다시 지어짐) 사라졌으면 관절을 다시 짓는다.
        const bool bBodiesChanged = bodyA != _jointBodyA || bodyB != _jointBodyB || pScene->isJointValid( _joint ) == false;
        if ( _joint.isValid() && bBodiesChanged )
            releasePhysics( physics );
        if ( _joint.isValid() || pScene->isBodyValid( bodyA ) == false )
            return;
        if ( pBodyB != nullptr && pScene->isBodyValid( bodyB ) == false )
            return; // 부모 바디가 아직 없다 — 다음 프레임

        float3     position{};
        quaternion rotation{};
        float3     scale{};
        PhysicsComponentUtil::readWorldPose( *this, position, rotation, scale );
        PhysicsJointDesc2D desc;
        desc._bodyA   = bodyA;
        desc._bodyB   = bodyB;
        desc._anchor  = float2{ position._x, position._y };
        desc._anchorB = desc._anchor;
        if ( _jointType == PhysicsJointType::Distance && pBodyB != nullptr )
            desc._anchorB = float2{ pBodyB->getWorldPosition()._x, pBodyB->getWorldPosition()._y };
        const float3 axis             = float3::transform( float3{ _axis._x, _axis._y, 0.0f }, rotation );
        desc._axis                    = float2{ axis._x, axis._y }.normalize();
        desc._motor                   = PhysicsJointMotor{ _motorTarget, _motorMaxForce, _motorMode };
        desc._minLimit                = _minLimit;
        desc._maxLimit                = _maxLimit;
        desc._type                    = _jointType;
        desc._bLimitsEnabled          = _bLimitsEnabled;
        desc._bCollideConnectedBodies = _bCollideConnectedBodies;
        _joint                        = pScene->createJoint( desc );
        _jointBodyA                   = bodyA;
        _jointBodyB                   = bodyB;
    }

    void Joint2DComponent::releasePhysics( ScenePhysics& physics )
    {
        if ( _joint.isValid() )
        {
            IPhysicsScene2D* pScene = physics.findScene2D();
            if ( pScene != nullptr )
                pScene->destroyJoint( _joint );
        }
        _joint      = PhysicsJointHandle{};
        _jointBodyA = PhysicsBodyHandle{};
        _jointBodyB = PhysicsBodyHandle{};
    }
} // namespace sw
