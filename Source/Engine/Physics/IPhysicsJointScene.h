/**
 * @file IPhysicsJointScene.h
 * @brief 물리 씬의 관절 쪽 인터페이스입니다(`IPhysicsJointScene3D`, `IPhysicsJointScene2D`).
 * @details 언리얼 Chaos 의 구속(`FConstraintInstance`), 유니티의 `Joint` 에 해당한다. 핸들 · 스레드 · 단위 규칙은 `IPhysicsScene.h` 머리말과 같다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Physics/PhysicsDesc.h"
#include "Engine/Physics/PhysicsTypes.h"

namespace sw
{
    /** @brief 바디 둘을 잇는 관절을 다룹니다. */
    template <typename TDimension>
    class IPhysicsJointScene
    {
    public:
        using JointDesc = PhysicsJointDesc<TDimension>;
        using Rotation  = typename TDimension::Rotation;

        virtual ~IPhysicsJointScene() = default;

        /**
         * @brief 이 차원이 @p type 관절을 만들 수 있는지 봅니다(차원 트레이트가 정한다. 2D 는 Cone 을 만들지 않는다).
         * @details `createJoint` 의 계약이다. 구현은 이 질의가 참인 종류를 모두 만들어야 하고, 거짓인 종류만 거절할 수 있다.
         */
        static constexpr bool supportsJoint( PhysicsJointType type ) { return TDimension::supportsJoint( type ); }
        /** @brief 관절을 만듭니다. 바디가 무효이거나 `supportsJoint` 가 거짓인 종류면 오류를 남기고 무효 핸들입니다. */
        virtual PhysicsJointHandle createJoint( const JointDesc& desc )           = 0;
        virtual void               destroyJoint( PhysicsJointHandle joint )       = 0;
        virtual bool               isJointValid( PhysicsJointHandle joint ) const = 0;
        /** @brief 모터를 바꿉니다(Hinge, Slider, Distance). */
        virtual void setJointMotor( PhysicsJointHandle joint, const PhysicsJointMotor& motor ) = 0;
        /** @brief Hinge 면 현재 각(라디안), Slider 면 현재 거리, Distance 면 현재 길이를 반환합니다. 나머지는 0 입니다. */
        virtual float32 getJointPosition( PhysicsJointHandle joint ) const = 0;
        /**
         * @brief Cone 관절의 스윙 · 비틀림 모터로 두 바디의 상대 자세를 목표(@p targetRotationA 에 대한 @p targetRotationB, 둘 다 월드)로 끕니다 — 파워드 래그돌.
         * @details 언리얼 Physical Animation 의 각 구동 · 유니티 ConfigurableJoint 의 targetRotation 자리다. 상대 자세만 쓰므로 몸 전체의 자리는 중력 · 충돌이 정한다.
         *          @p maxTorque(뉴턴미터)가 0 이하면 모터를 끈다(축 늘어진 래그돌). Cone 이 아닌 관절이면 오류를 남기고 아무것도 하지 않는다(2D 에는 Cone 이 없다).
         */
        virtual void driveJointToRotation( PhysicsJointHandle joint, const Rotation& targetRotationA, const Rotation& targetRotationB, float32 maxTorque ) = 0;
    };

    using IPhysicsJointScene3D = IPhysicsJointScene<PhysicsDimension3D>;
    using IPhysicsJointScene2D = IPhysicsJointScene<PhysicsDimension2D>;
} // namespace sw
