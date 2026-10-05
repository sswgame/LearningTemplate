/**
 * @file JointComponent.h
 * @brief 3D 관절 — 이 오브젝트의 강체를 부모 쪽 강체(또는 월드)에 잇습니다. 자리 · 축은 이 컴포넌트의 월드 자세에서 나옵니다.
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
    class RigidBodyComponent;

    /**
     * @class JointComponent
     * @brief 3D 관절입니다(유니티 Joint · 언리얼 PhysicsConstraintComponent). 두 바디를 데이터로 고릅니다 — 다른 오브젝트를 이름으로 가리키지 않습니다.
     * @details 바디 A 는 이 오브젝트의 `RigidBodyComponent`, 바디 B 는 부모 사슬에서 가장 가까운 `RigidBodyComponent` 를 가진 오브젝트입니다
     *          (`_bConnectToWorld` 면 월드). 관절 자리는 이 컴포넌트의 월드 자리, 축은 이 컴포넌트의 회전으로 돌린 `_axis` · `_normalAxis` 입니다.
     *          두 바디가 생긴 뒤 첫 물리 프레임에 만들고, 어느 바디든 사라지면 다시 만들 때까지 기다립니다.
     */
    REFLECT( Category = "Physics", DisplayName = "Joint", Tooltip = "Connects this object's rigid body to the parent's body (or the world)" )
    class SW_API JointComponent : public PhysicsComponent
    {
    public:
        REFLECT_BODY();

        JointComponent();
        ~JointComponent() override = default;

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
        /** @brief Cone 의 스윙 한계를 바꿉니다. */
        void setSwingLimits( float32 swingLimitNormal, float32 swingLimitPlane );
        void setConnectToWorld( bool bConnectToWorld );

    protected:
        void beginPhysicsFrame( ScenePhysics& physics ) override;
        void releasePhysics( ScenePhysics& physics ) override;

    private:
        /** @brief 바디 B — 부모 사슬에서 가장 가까운 강체입니다. 없으면 nullptr(월드). */
        const RigidBodyComponent* findConnectedBody() const;

        PROPERTY( Category = "Joint", Tooltip = "Fixed, Hinge, Cone (swing-twist), Distance or Slider" )
        PhysicsJointType _jointType;
        PROPERTY( Category = "Joint", Tooltip = "Hinge axis / slider axis / twist axis, in this component's space" )
        float3 _axis;
        PROPERTY( Category = "Joint", Tooltip = "Cone: reference axis perpendicular to the twist axis, in this component's space" )
        float3 _normalAxis;
        PROPERTY( Category = "Joint", Tooltip = "Hinge angle / slider distance / distance length / cone twist lower limit" )
        float32 _minLimit;
        PROPERTY( Category = "Joint", Tooltip = "Upper limit" )
        float32 _maxLimit;
        PROPERTY( Category = "Joint", Min = 0.0, Tooltip = "Cone: swing half angle around the normal axis", Units = rad )
        float32 _swingLimitNormal;
        PROPERTY( Category = "Joint", Min = 0.0, Tooltip = "Cone: swing half angle around the third axis", Units = rad )
        float32 _swingLimitPlane;
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
