#include "pch.h"

#include "Engine/Physics/Component/Joint2DComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Physics/Component/RigidBody2DComponent.h"
#include "Engine/Physics/Component/ScenePhysics.h"

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
        , _bJointCreateFailed{ false }
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
        // 이은 바디가 바뀌었거나(다시 지어짐) 사라졌으면 관절을 다시 짓는다. 관절이 사라지는 길은 이 컴포넌트와 이은 바디의 삭제(핸들이 바뀐다)뿐이라
        // 프레임마다 백엔드에 묻지 않고 Debug 에서만 확인한다(3D `JointComponent` 와 같은 근거).
        const bool bBodiesChanged = bodyA != _jointBodyA || bodyB != _jointBodyB;
        SW_ASSERT( _joint.isValid() == false || bBodiesChanged || pScene->isJointValid( _joint ) );
        if ( _joint.isValid() && bBodiesChanged )
            releasePhysics( physics );
        // 바디가 있는지는 핸들로 안다 — 강체 컴포넌트가 바디를 놓으면 핸들을 비운다(3D `JointComponent` 와 같은 근거).
        if ( _joint.isValid() || bodyA.isValid() == false )
            return;
        if ( _bJointCreateFailed )
        {
            const bool bSameBodies = bodyA == _jointBodyA && bodyB == _jointBodyB;
            if ( bSameBodies )
                return; // 같은 입력으로 이미 실패했다 — 설정(requestRebuild)이나 바디가 바뀔 때까지 기다린다
            _bJointCreateFailed = false;
        }
        if ( pBodyB != nullptr && bodyB.isValid() == false )
            return; // 부모 바디가 아직 없다 — 다음 프레임
        SW_ASSERT( pScene->isBodyValid( bodyA ) && ( pBodyB == nullptr || pScene->isBodyValid( bodyB ) ) );

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
        _bJointCreateFailed           = _joint.isValid() == false;
    }

    void Joint2DComponent::releasePhysics( ScenePhysics& physics )
    {
        if ( _joint.isValid() )
        {
            IPhysicsScene2D* pScene = physics.findScene2D();
            if ( pScene != nullptr )
                pScene->destroyJoint( _joint );
        }
        _joint              = PhysicsJointHandle{};
        _jointBodyA         = PhysicsBodyHandle{};
        _jointBodyB         = PhysicsBodyHandle{};
        _bJointCreateFailed = false;
    }
} // namespace sw
