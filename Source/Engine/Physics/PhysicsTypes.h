/**
 * @file PhysicsTypes.h
 * @brief 2D 와 3D 물리가 함께 쓰는 기본 타입입니다. 바디 종류, 관절 종류, 모터, 접촉 단계를 담습니다(핸들은 `Engine/Foundation/PhysicsHandle.h`).
 * @details 백엔드(Jolt, Box2D)와 무관한 값만 둔다. 차원마다 다른 것은 벡터와 회전 타입뿐이다(`PhysicsDimension3D`, `PhysicsDimension2D`).
 *          그 위의 서술자와 결과 타입은 차원 트레이트를 받는 템플릿 하나씩이다(`PhysicsDesc.h`, `PhysicsQuery.h`, `PhysicsContact.h`).
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/Foundation/PhysicsHandle.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @brief 바디가 움직이는 방식입니다(유니티 `RigidbodyType2D`, Jolt `EMotionType`, Box2D `b2BodyType`).
     * @details Static 은 움직이지 않는다(바닥, 벽). Kinematic 은 코드가 움직이고(문, 엘리베이터, 애니메이션을 따르는 히트박스) 힘을 받지
     *          않는다. Dynamic 은 힘, 중력, 접촉으로 움직인다.
     */
    ENUM()
    enum class PhysicsBodyType : uint8
    {
        Static = 0,
        Kinematic,
        Dynamic,
    };

    /**
     * @brief 관절 종류입니다. 2D 는 Cone 을 지원하지 않습니다(평면에는 스윙이 없으므로 만들면 오류입니다).
     * @details Fixed(용접), Hinge(한 축 회전, 2D 의 revolute), Cone(스윙 원뿔과 비틀림. 래그돌의 어깨와 목, Jolt SwingTwist),
     *          Distance(두 점 사이 거리 범위), Slider(한 축 이동, 2D 의 prismatic)가 있다.
     */
    ENUM()
    enum class PhysicsJointType : uint8
    {
        Fixed = 0,
        Hinge,
        Cone,
        Distance,
        Slider,
    };

    /** @brief 관절 모터의 동작입니다. Velocity 는 목표 속도(라디안/초, 미터/초)로, Position 은 목표 각과 거리로 구동합니다. */
    ENUM()
    enum class PhysicsMotorMode : uint8
    {
        Off = 0,
        Velocity,
        Position,
    };

    /** @brief 접촉과 트리거 이벤트의 단계입니다(유니티 Enter, Stay, Exit). */
    enum class PhysicsContactPhase : uint8
    {
        Begin = 0,
        Stay,
        End,
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 소프트 바디(천 · 헤어 카드처럼 정점마다 움직이는 바디) 핸들의 태그입니다.
     * @details 다른 핸들과 달리 `Engine/Foundation/PhysicsHandle.h` 에 두지 않는다 — 컴포넌트 모델 코어가 들 일이 없다(물리 폴더 안에서만 쓴다).
     */
    struct PhysicsSoftBodyTag
    {
    };

    using PhysicsSoftBodyHandle = PhysicsHandle<PhysicsSoftBodyTag>;
} // namespace sw

namespace sw
{
    /** @brief 관절 하나의 모터입니다. Hinge(각), Slider(거리), Distance(거리)가 씁니다. */
    struct PhysicsJointMotor
    {
        float32          _target{ 0.0f };   ///< Velocity 면 목표 속도, Position 이면 목표 각(라디안)과 거리(미터)다.
        float32          _maxForce{ 0.0f }; ///< 모터가 낼 수 있는 가장 큰 힘(뉴턴)이나 토크(뉴턴미터)다. 0 이면 모터가 없는 것과 같다.
        PhysicsMotorMode _mode{ PhysicsMotorMode::Off };
    };
} // namespace sw
