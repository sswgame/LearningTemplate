#include "pch.h"

#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/Tick/FixedStepContext.h"
#include "Engine/Physics/Component/PhysicsComponent.h"
#include "Engine/Physics/Component/RigidBody2DComponent.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsTypes.h"
#include "Engine/Physics/Scene/ScenePhysics.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class RigidBody2DComponent;

    /**
     * @class Joint2DComponent
     * @brief 2D 관절입니다(유니티 Joint2D). Cone 은 없습니다(평면에는 스윙이 없다 — 고르면 만들 때 오류 한 번, 설정이 바뀔 때까지 다시 만들지 않는다). Hinge 축은 늘 Z 이고 `_axis` 의 XY 는 Slider 의 축입니다.
     * @details 바디 A 는 이 오브젝트의 `RigidBody2DComponent`, 바디 B 는 부모 사슬에서 가장 가까운 `RigidBody2DComponent` 를 가진 오브젝트입니다
     *          (`_bConnectToWorld` 면 월드). 관절 자리는 이 컴포넌트의 월드 자리, 축은 이 컴포넌트의 회전으로 돌린 `_axis` 입니다.
     *          두 바디가 생긴 뒤 첫 물리 프레임에 만들고, 어느 바디든 사라지면 다시 만들 때까지 기다립니다.
     */
    REFLECT( Category = "Physics 2D", DisplayName = "Joint 2D", Tooltip = "Connects this object's 2D rigid body to the parent's 2D body (or the world)" )
    class Joint2DComponent : public PhysicsComponent
    {
    public:
        REFLECT_BODY();

        Joint2DComponent();
        ~Joint2DComponent() override = default;

        /** @brief 관절 핸들입니다(아직 없으면 무효). */
        PhysicsJointHandle getJointHandle() const { return _joint; }
        /** @brief Hinge 면 지금 각, Slider 면 지금 거리, Distance 면 지금 길이입니다. */
        float32 getJointPosition() const;
        /** @brief 모터를 바꿉니다(Hinge · Slider · Distance). 관절이 있으면 바로 듭니다. */
        void setMotor( PhysicsMotorMode mode, float32 target, float32 maxForce );

        PhysicsJointType getJointType() const { return _jointType; }
        void             setJointType( PhysicsJointType type );
        /** @brief 한계를 바꿉니다(다음 물리 프레임에 관절을 다시 짓는다). */
        void setLimits( bool bEnabled, float32 minLimit, float32 maxLimit );
        void setConnectToWorld( bool bConnectToWorld );

    protected:
        /** @brief 쓰는 고정 스텝 그룹마다 메서드를 등록합니다(`registerFixedStep`). */
        void onRegister( GameObjectManager& manager ) override;
        void onPhysicsFrameBegin( const FixedStepContext& context );
        void releasePhysics( ScenePhysics& physics ) override;

    private:
        /** @brief 바디 B — 부모 사슬에서 가장 가까운 강체입니다. 없으면 nullptr(월드). */
        const RigidBody2DComponent* findConnectedBody() const;

        PROPERTY( Category = "Joint", Tooltip = "Fixed, Hinge, Distance or Slider (Cone is 3D only)" )
        PhysicsJointType _jointType;
        PROPERTY( Category = "Joint", Tooltip = "Slider axis in this component's space (XY)" )
        float2 _axis;
        PROPERTY( Category = "Joint", Tooltip = "Hinge angle / slider distance / distance length / cone twist lower limit" )
        float32 _minLimit;
        PROPERTY( Category = "Joint", Tooltip = "Upper limit" )
        float32 _maxLimit;
        PROPERTY( Category = "Motor", Tooltip = "Off, Velocity or Position" )
        PhysicsMotorMode _motorMode;
        PROPERTY( Category = "Motor", Tooltip = "Target velocity or position" )
        float32 _motorTarget;
        PROPERTY( Category = "Motor", Min = 0.0, Tooltip = "Largest force / torque the motor applies" )
        float32 _motorMaxForce;
        PROPERTY( Category = "Joint", Tooltip = "Hinge / slider / distance limits are applied" )
        bool _bLimitsEnabled;
        PROPERTY( Category = "Joint", Tooltip = "The two connected bodies collide with each other" )
        bool _bCollideConnectedBodies;
        PROPERTY( Category = "Joint", Tooltip = "Connect to the world instead of the parent's body" )
        bool _bConnectToWorld;
        /// 이 바디 쌍으로 만들기가 실패했다(Cone 등) — 바디 · 설정이 바뀔 때까지 다시 시도하지 않는다(실패는 프레임마다 오류 로그를 남긴다)
        SW_TRANSIENT bool _bJointCreateFailed;

        SW_TRANSIENT PhysicsJointHandle _joint;
        SW_TRANSIENT PhysicsBodyHandle  _jointBodyA; ///< 관절을 만들 때의 바디(바뀌면 다시 짓는다)
        SW_TRANSIENT PhysicsBodyHandle  _jointBodyB;
    };

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

    void Joint2DComponent::onRegister( GameObjectManager& manager )
    {
        PhysicsComponent::onRegister( manager );
        registerFixedStep<&Joint2DComponent::onPhysicsFrameBegin>( manager, FixedStepGroup::FrameBegin );
    }

    void Joint2DComponent::onPhysicsFrameBegin( const FixedStepContext& context )
    {
        ScenePhysics& physics = context.getPhysics();
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
        if ( PhysicsComponent::readWorldPose( *this, position, rotation, scale ) == false )
            return; // 배율 0 — 축을 정할 수 없다(`readWorldPose` 가 알렸다). 배율이 돌아오면 만든다
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

#include "Joint2DComponent.cpp.gen.inl"
