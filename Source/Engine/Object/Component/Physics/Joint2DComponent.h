/**
 * @file Joint2DComponent.h
 * @brief 2D 관절 — 이 오브젝트의 2D 강체를 부모 쪽 2D 강체(또는 월드)에 잇습니다. 3D 판(`JointComponent`)과 규칙이 같습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Physics/PhysicsComponent.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsTypes.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class RigidBody2DComponent;

    /**
     * @class Joint2DComponent
     * @brief 2D 관절입니다(유니티 Joint2D). Cone 은 없습니다(평면에는 스윙이 없다 — 고르면 만들 때 오류). Hinge 축은 늘 Z 이고 `_axis` 의 XY 는 Slider 의 축입니다.
     * @details 바디 A 는 이 오브젝트의 `RigidBody2DComponent`, 바디 B 는 부모 사슬에서 가장 가까운 `RigidBody2DComponent` 를 가진 오브젝트입니다
     *          (`_bConnectToWorld` 면 월드). 관절 자리는 이 컴포넌트의 월드 자리, 축은 이 컴포넌트의 회전으로 돌린 `_axis` 입니다.
     *          두 바디가 생긴 뒤 첫 물리 프레임에 만들고, 어느 바디든 사라지면 다시 만들 때까지 기다립니다.
     */
    REFLECT( Category = "Physics 2D", DisplayName = "Joint 2D", Tooltip = "Connects this object's 2D rigid body to the parent's 2D body (or the world)" )
    class SW_API Joint2DComponent : public PhysicsComponent
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
        void beginPhysicsFrame( ScenePhysics& physics ) override;
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
        PROPERTY( Category = "Joint", Tooltip = "The two connected bodies do not collide with each other" )
        bool _bDisableCollision;
        PROPERTY( Category = "Joint", Tooltip = "Connect to the world instead of the parent's body" )
        bool _bConnectToWorld;

        PhysicsJointHandle _joint;
        PhysicsBodyHandle  _jointBodyA; ///< 관절을 만들 때의 바디(바뀌면 다시 짓는다)
        PhysicsBodyHandle  _jointBodyB;
    };
} // namespace sw
