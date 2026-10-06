/**
 * @file PhysicsDesc.h
 * @brief 바디 · 관절 · 캐릭터를 만드는 서술자입니다. 2D · 3D 가 같은 템플릿 하나를 쓰고, 차원 묶음(`PhysicsDimension3D` · `PhysicsDimension2D`)이
 *        벡터 · 회전 · 각속도 · 셰이프 타입만 바꿉니다.
 * @details 서술자는 런타임 값입니다(데이터 파일에 쓰지 않는다). 재질 · 레이어는 이름이 아니라 씬이 아는 값으로 넘깁니다 — 재질은 이름으로
 *          씬의 재질 표에서 찾고(`PhysicsSettings`, 없으면 오류 후 기본 재질), 레이어는 번호입니다(`PhysicsSettings::findLayerIndex`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/hashed_string.h"

#include "Engine/Physics/PhysicsShape.h"
#include "Engine/Physics/PhysicsTypes.h"

namespace sw
{
    /** @brief 3D 물리의 차원 묶음입니다. */
    struct PhysicsDimension3D
    {
        using Vector    = float3;
        using Rotation  = quaternion; ///< 바디 회전
        using Angular   = float3;     ///< 각속도 · 토크(축 × 크기)
        using ShapeDesc = PhysicsShapeDesc3D;
    };
} // namespace sw

namespace sw
{
    /** @brief 2D 물리의 차원 묶음입니다. XY 평면, 회전은 Z 축 둘레의 각(라디안)입니다. */
    struct PhysicsDimension2D
    {
        using Vector    = float2;
        using Rotation  = float32;
        using Angular   = float32;
        using ShapeDesc = PhysicsShapeDesc2D;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 바디 하나를 만드는 값입니다.
     * @details 셰이프가 둘 이상이면 컴파운드 바디입니다. `_sharedShape` 가 유효하면 `_listShape` 대신 미리 지은 셰이프 묶음을 씁니다
     *          (`createShape` — 같은 모양의 파편 · 탄피를 대량으로 만들 때 셰이프를 다시 짓지 않는다).
     */
    template <typename TDimension>
    struct PhysicsBodyDesc
    {
        using Vector    = typename TDimension::Vector;
        using Rotation  = typename TDimension::Rotation;
        using Angular   = typename TDimension::Angular;
        using ShapeDesc = typename TDimension::ShapeDesc;

        vector<ShapeDesc>  _listShape;
        Vector             _position{};
        Rotation           _rotation{};
        Vector             _linearVelocity{};
        Angular            _angularVelocity{};
        uint64             _userData{ 0 };           ///< 이벤트 · 질의 결과가 돌려주는 값(엔진은 소유 오브젝트 id 를 싣는다)
        hashed_string      _material{};              ///< 셰이프가 재질을 적지 않았을 때의 재질 이름(비면 첫 재질)
        PhysicsShapeHandle _sharedShape{};           ///< 미리 지은 셰이프 묶음(`createShape`). 유효하면 `_listShape` 를 보지 않는다
        float32            _mass{ 0.0f };            ///< 0 이면 셰이프 부피 × 밀도
        float32            _linearDamping{ 0.05f };  ///< 선속도 감쇠(1/초)
        float32            _angularDamping{ 0.05f }; ///< 각속도 감쇠(1/초)
        float32            _gravityFactor{ 1.0f };   ///< 중력 배율(0 이면 떠 있다)
        PhysicsBodyType    _type{ PhysicsBodyType::Dynamic };
        uint8              _layer{ 0 };                ///< 충돌 레이어 번호(`PhysicsSettings::findLayerIndex`)
        bool               _bTrigger{ false };         ///< 감지만 하고 막지 않는다(유니티 isTrigger · Jolt sensor · Box2D sensor)
        bool               _bContinuous{ false };      ///< 빠른 바디가 얇은 바디를 지나치지 않게 연속 충돌로 잰다(Jolt LinearCast · Box2D bullet)
        bool               _bAllowSleep{ true };       ///< 멈추면 잠들어 시뮬레이션 비용을 내지 않는다
        bool               _bAllowTypeChange{ false }; ///< Static 으로 만든 바디를 나중에 Kinematic · Dynamic 으로 바꿀 수 있게 한다(Jolt 는 만들 때 정한다)
        bool               _bLockRotation{ false };    ///< 회전하지 않는다(2D 캐릭터 · 3D 는 관성을 무한으로)
    };

    using PhysicsBodyDesc3D = PhysicsBodyDesc<PhysicsDimension3D>;
    using PhysicsBodyDesc2D = PhysicsBodyDesc<PhysicsDimension2D>;
} // namespace sw

namespace sw
{
    /**
     * @brief 관절 하나를 만드는 값입니다. 자리 · 축은 월드 공간입니다(만드는 순간의 두 바디 자세를 기준으로 고정된다).
     * @details 축 하나(`_axis`)가 종류마다 다른 뜻을 집니다 — Hinge 는 회전축, Slider 는 이동축, Cone 은 비틀림 축(뼈 방향).
     *          Cone 의 스윙 한계는 `_normalAxis`(비틀림 축에 수직) 둘레와 그 둘에 수직인 축 둘레로 따로 줍니다.
     *          `_bodyB` 가 무효면 월드에 붙습니다. 2D 는 `_axis` 의 XY 만 보고 Hinge 축은 늘 Z 입니다.
     */
    template <typename TDimension>
    struct PhysicsJointDesc
    {
        using Vector = typename TDimension::Vector;

        PhysicsBodyHandle _bodyA{};
        PhysicsBodyHandle _bodyB{};      ///< 무효면 월드
        Vector            _anchor{};     ///< 관절 자리(월드)
        Vector            _anchorB{};    ///< Distance 만: B 쪽 점(월드). 나머지는 `_anchor` 하나를 함께 쓴다
        Vector            _axis{};       ///< Hinge 회전축 · Slider 이동축 · Cone 비틀림 축(월드, 정규화)
        Vector            _normalAxis{}; ///< Cone 만: 비틀림 축에 수직인 기준 축(월드, 정규화)
        PhysicsJointMotor _motor{};
        float32           _minLimit{ 0.0f };         ///< Hinge 각 · Slider 거리 · Distance 길이 · Cone 비틀림 각의 아래 한계
        float32           _maxLimit{ 0.0f };         ///< 위 한계
        float32           _swingLimitNormal{ 0.0f }; ///< Cone 만: `_normalAxis` 둘레 스윙 반각(라디안)
        float32           _swingLimitPlane{ 0.0f };  ///< Cone 만: 나머지 축 둘레 스윙 반각(라디안)
        PhysicsJointType  _type{ PhysicsJointType::Fixed };
        bool              _bLimitsEnabled{ false };   ///< Hinge · Slider · Distance 의 한계를 건다(Cone 은 늘 건다)
        bool              _bDisableCollision{ true }; ///< 이어진 두 바디끼리는 부딪히지 않는다(래그돌 이웃 뼈)
    };

    using PhysicsJointDesc3D = PhysicsJointDesc<PhysicsDimension3D>;
    using PhysicsJointDesc2D = PhysicsJointDesc<PhysicsDimension2D>;
} // namespace sw

namespace sw
{
    /**
     * @brief 캐릭터 컨트롤러 하나를 만드는 값입니다. 캡슐은 위(+Y)로 섭니다. `_position` 은 캡슐 바닥(발)입니다.
     * @details 3D 는 Jolt `CharacterVirtual`(계단 오르기 · 바닥 붙기), 2D 는 Box2D 무버(`b2World_CastMover` · `b2SolvePlanes`)입니다 — 둘 다
     *          시뮬레이션 바디가 아니라 질의로 움직이는 키네마틱 컨트롤러입니다(유니티 CharacterController 와 같은 자리).
     */
    template <typename TDimension>
    struct PhysicsCharacterDesc
    {
        using Vector = typename TDimension::Vector;

        Vector  _position{};
        uint64  _userData{ 0 };
        float32 _radius{ 0.3f };
        float32 _halfHeight{ 0.6f };                        ///< 원기둥 부분의 반 높이 — 키는 2 × (반 높이 + 반지름)
        float32 _maxSlopeAngle{ MathUtil::kHalfPi * 0.5f }; ///< 걸어 오를 수 있는 가장 가파른 바닥(라디안, 기본 45 도)
        float32 _stepHeight{ 0.3f };                        ///< 걸어 오를 수 있는 가장 높은 턱(미터). 2D 는 쓰지 않는다
        float32 _mass{ 70.0f };                             ///< 밀어낼 때의 질량(3D)
        float32 _maxStrength{ 100.0f };                     ///< 동적 바디를 미는 가장 큰 힘(3D, 뉴턴)
        uint8   _layer{ 0 };
    };

    using PhysicsCharacterDesc3D = PhysicsCharacterDesc<PhysicsDimension3D>;
    using PhysicsCharacterDesc2D = PhysicsCharacterDesc<PhysicsDimension2D>;
} // namespace sw

namespace sw
{
    /** @brief 캐릭터 이동 한 번의 결과입니다. */
    template <typename TDimension>
    struct PhysicsCharacterState
    {
        using Vector = typename TDimension::Vector;

        Vector _position{};     ///< 발 자리
        Vector _velocity{};     ///< 이번 이동에서 실제로 난 속도
        Vector _groundNormal{}; ///< 서 있으면 바닥 법선
        bool   _bGrounded{ false };
    };

    using PhysicsCharacterState3D = PhysicsCharacterState<PhysicsDimension3D>;
    using PhysicsCharacterState2D = PhysicsCharacterState<PhysicsDimension2D>;
} // namespace sw

namespace sw
{
    /** @brief 바퀴 하나입니다(차체 바디 기준 — 차체의 앞 +Z · 위 +Y). 3D 만입니다. */
    struct PhysicsWheelDesc
    {
        float3  _position{}; ///< 서스펜션이 가장 짧을 때 바퀴 중심의 붙는 자리(차체 기준)
        float32 _radius{ 0.35f };
        float32 _width{ 0.25f };
        float32 _suspensionMinLength{ 0.1f };  ///< 미터
        float32 _suspensionMaxLength{ 0.35f }; ///< 미터
        float32 _suspensionFrequency{ 1.5f };  ///< 스프링 고유 진동수(Hz)
        float32 _suspensionDamping{ 0.5f };    ///< 감쇠비
        float32 _maxSteerAngle{ 0.0f };        ///< 라디안 — 0 이면 조향하지 않는 바퀴
        float32 _maxBrakeTorque{ 1500.0f };    ///< N·m
        float32 _maxHandBrakeTorque{ 0.0f };   ///< N·m — 0 이면 핸드브레이크가 걸리지 않는 바퀴
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 바퀴 차 하나입니다 — 이미 만든 동적 차체 바디에 바퀴 · 엔진 · 변속 · 디퍼렌셜을 붙입니다(Jolt `VehicleConstraint` · `WheeledVehicleController`).
     * @details 디퍼렌셜은 바퀴 둘씩(`_listDriven` 의 짝 — 0 과 1 이 한 차축)입니다. 2D 물리에는 없습니다(무효 핸들).
     */
    struct PhysicsWheeledVehicleDesc
    {
        vector<PhysicsWheelDesc> _listWheel;
        vector<int32>            _listDrivenAxle;            ///< 구동 차축의 바퀴 짝 — `[왼쪽, 오른쪽, 왼쪽, 오른쪽 …]` 자리 번호
        float32                  _engineMaxTorque{ 500.0f }; ///< N·m
        float32                  _engineMinRpm{ 1000.0f };
        float32                  _engineMaxRpm{ 6000.0f };
        float32                  _maxPitchRollAngle{ MathUtil::kPi }; ///< 뒤집히지 않게 위 방향을 묶는 원뿔 반각(π 면 끔)
    };
} // namespace sw

namespace sw
{
    /** @brief 바퀴 차의 지금 상태입니다. */
    struct PhysicsVehicleState
    {
        float32 _forwardSpeed{ 0.0f }; ///< 차체 앞 방향 속도(m/s)
        float32 _engineRpm{ 0.0f };
        int32   _gear{ 0 }; ///< 0 중립 · 음수 후진
        uint32  _groundedWheelCount{ 0 };
    };
} // namespace sw
