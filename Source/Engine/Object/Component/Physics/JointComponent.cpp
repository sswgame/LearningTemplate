#include "pch.h"

#include "Engine/Object/Component/Physics/JointComponent.h"

#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/Component/Physics/ScenePhysics.h"
#include "Engine/Object/GameObject/GameObject.h"

namespace sw
{
    JointComponent::JointComponent()
        : PhysicsComponent{ PhysicsComponentPhase::Joint }
        , _jointType{ PhysicsJointType::Hinge }
        , _axis{ 0.0f, 1.0f, 0.0f }
        , _normalAxis{ 1.0f, 0.0f, 0.0f }
        , _minLimit{ 0.0f }
        , _maxLimit{ 0.0f }
        , _swingLimitNormal{ 0.5f }
        , _swingLimitPlane{ 0.5f }
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

    float32 JointComponent::getJointPosition() const
    {
        const IPhysicsScene3D* pScene = getScenePhysics() != nullptr ? getScenePhysics()->findScene3D() : nullptr;
        return pScene != nullptr ? pScene->getJointPosition( _joint ) : 0.0f;
    }

    void JointComponent::setMotor( PhysicsMotorMode mode, float32 target, float32 maxForce )
    {
        _motorMode              = mode;
        _motorTarget            = target;
        _motorMaxForce          = maxForce;
        IPhysicsScene3D* pScene = getScenePhysics() != nullptr ? getScenePhysics()->findScene3D() : nullptr;
        if ( pScene != nullptr && _joint.isValid() )
            pScene->setJointMotor( _joint, PhysicsJointMotor{ target, maxForce, mode } );
    }

    void JointComponent::setJointType( PhysicsJointType type )
    {
        _jointType = type;
        requestRebuild();
    }

    void JointComponent::setLimits( bool bEnabled, float32 minLimit, float32 maxLimit )
    {
        _bLimitsEnabled = bEnabled;
        _minLimit       = minLimit;
        _maxLimit       = maxLimit;
        requestRebuild();
    }

    void JointComponent::setSwingLimits( float32 swingLimitNormal, float32 swingLimitPlane )
    {
        _swingLimitNormal = swingLimitNormal;
        _swingLimitPlane  = swingLimitPlane;
        requestRebuild();
    }

    void JointComponent::setConnectToWorld( bool bConnectToWorld )
    {
        _bConnectToWorld = bConnectToWorld;
        requestRebuild();
    }

    const RigidBodyComponent* JointComponent::findConnectedBody() const
    {
        if ( _bConnectToWorld )
            return nullptr;
        for ( const GameObject* pObject = getOwner() != nullptr ? getOwner()->getParent() : nullptr; pObject != nullptr; pObject = pObject->getParent() )
        {
            const RigidBodyComponent* pBody = pObject->getComponent<RigidBodyComponent>();
            if ( pBody != nullptr )
                return pBody;
        }
        return nullptr;
    }

    void JointComponent::beginPhysicsFrame( ScenePhysics& physics )
    {
        if ( isSimulated() == false )
        {
            releasePhysics( physics );
            return;
        }
        IPhysicsScene3D* pScene = physics.getScene3D();
        if ( pScene == nullptr )
            return;
        if ( consumeRebuild() )
            releasePhysics( physics );

        const RigidBodyComponent* pBodyA = getOwner()->getComponent<RigidBodyComponent>();
        const RigidBodyComponent* pBodyB = findConnectedBody();
        const PhysicsBodyHandle   bodyA  = pBodyA != nullptr ? pBodyA->getBodyHandle() : PhysicsBodyHandle{};
        const PhysicsBodyHandle   bodyB  = pBodyB != nullptr ? pBodyB->getBodyHandle() : PhysicsBodyHandle{};
        // 이은 바디가 바뀌었거나(다시 지어짐) 사라졌으면 관절을 다시 짓는다. 관절은 이 컴포넌트와, 이은 바디를 지울 때의 백엔드만 지우고 바디는 그
        // 강체 컴포넌트만 지우며 그때 핸들이 바뀐다 — 핸들 비교가 그 경우를 다 잡으므로 프레임마다 백엔드에 묻지 않고 Debug 에서만 확인한다.
        const bool bBodiesChanged = bodyA != _jointBodyA || bodyB != _jointBodyB;
        SW_ASSERT( _joint.isValid() == false || bBodiesChanged || pScene->isJointValid( _joint ) );
        if ( _joint.isValid() && bBodiesChanged )
            releasePhysics( physics );
        // 바디가 있는지는 핸들로 안다 — 강체 컴포넌트가 바디를 놓으면 핸들을 비우므로(바디 단계가 관절 단계보다 먼저 돈다) 백엔드에 묻지 않는다.
        if ( _joint.isValid() || bodyA.isValid() == false )
            return;
        if ( pBodyB != nullptr && bodyB.isValid() == false )
            return; // 부모 바디가 아직 없다 — 다음 프레임
        SW_ASSERT( pScene->isBodyValid( bodyA ) && ( pBodyB == nullptr || pScene->isBodyValid( bodyB ) ) );

        float3     position{};
        quaternion rotation{};
        float3     scale{};
        PhysicsComponentUtil::readWorldPose( *this, position, rotation, scale );
        PhysicsJointDesc3D desc;
        desc._bodyA   = bodyA;
        desc._bodyB   = bodyB;
        desc._anchor  = position;
        desc._anchorB = position;
        if ( _jointType == PhysicsJointType::Distance && pBodyB != nullptr )
            desc._anchorB = pBodyB->getWorldPosition();
        desc._axis                    = float3::transform( _axis, rotation ).normalize();
        desc._normalAxis              = float3::transform( _normalAxis, rotation ).normalize();
        desc._motor                   = PhysicsJointMotor{ _motorTarget, _motorMaxForce, _motorMode };
        desc._minLimit                = _minLimit;
        desc._maxLimit                = _maxLimit;
        desc._swingLimitNormal        = _swingLimitNormal;
        desc._swingLimitPlane         = _swingLimitPlane;
        desc._type                    = _jointType;
        desc._bLimitsEnabled          = _bLimitsEnabled;
        desc._bCollideConnectedBodies = _bCollideConnectedBodies;
        _joint                        = pScene->createJoint( desc );
        _jointBodyA                   = bodyA;
        _jointBodyB                   = bodyB;
    }

    void JointComponent::releasePhysics( ScenePhysics& physics )
    {
        if ( _joint.isValid() )
        {
            IPhysicsScene3D* pScene = physics.findScene3D();
            if ( pScene != nullptr )
                pScene->destroyJoint( _joint );
        }
        _joint      = PhysicsJointHandle{};
        _jointBodyA = PhysicsBodyHandle{};
        _jointBodyB = PhysicsBodyHandle{};
    }
} // namespace sw
