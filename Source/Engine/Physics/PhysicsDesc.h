/**
 * @file PhysicsDesc.h
 * @brief 바디, 관절, 캐릭터를 만드는 서술자입니다. 2D 와 3D 가 같은 템플릿 하나를 쓰고, 차원 트레이트(`PhysicsDimension3D`, `PhysicsDimension2D`)가
 *        벡터, 회전, 각속도, 셰이프 타입만 바꿉니다.
 * @details 서술자는 런타임 값이다(데이터 파일에 쓰지 않는다). 재질과 레이어는 이름이 아니라 씬이 아는 값으로 넘긴다.
 *          재질은 이름으로 씬의 재질 테이블에서 찾고(`PhysicsSettings`, 없으면 오류를 남기고 기본 재질을 쓴다), 레이어는 번호다(`PhysicsSettings::findLayerIndex`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/hashed_string.h"

#include "Engine/Physics/Collision/PhysicsShape.h"
#include "Engine/Physics/PhysicsTypes.h"

namespace sw
{
    /** @brief 3D 물리의 차원 트레이트입니다. */
    struct PhysicsDimension3D
    {
        using Vector    = float3;
        using Rotation  = quaternion; ///< 바디 회전이다.
        using Angular   = float3;     ///< 각속도와 토크다(축 × 크기).
        using ShapeDesc = PhysicsShapeDesc3D;

        /** @brief 씬이 바퀴 차(`IPhysicsVehicleScene`)를 구현하는지입니다. */
        static constexpr bool kSupportsVehicle = true;
        /** @brief 씬이 소프트 바디(`IPhysicsSoftBodyScene`)를 구현하는지입니다. */
        static constexpr bool kSupportsSoftBody = true;
        /** @brief 씬이 부력(`IPhysicsBuoyancyScene`)을 구현하는지입니다. */
        static constexpr bool kSupportsBuoyancy = true;

        /** @brief 이 차원이 만들 수 있는 관절 종류인지 봅니다. 3D 는 모든 종류를 만듭니다. */
        static constexpr bool supportsJoint( PhysicsJointType ) { return true; }
    };
} // namespace sw

namespace sw
{
    /** @brief 2D 물리의 차원 트레이트입니다. XY 평면이며, 회전은 Z 축 둘레의 각(라디안)입니다. */
    struct PhysicsDimension2D
    {
        using Vector    = float2;
        using Rotation  = float32;
        using Angular   = float32;
        using ShapeDesc = PhysicsShapeDesc2D;

        /** @brief 평면 물리에는 바퀴 차가 없습니다. 씬은 `IPhysicsVehicleScene` 을 상속하지 않습니다. */
        static constexpr bool kSupportsVehicle = false;
        /** @brief 평면 물리에는 소프트 바디가 없습니다(Box2D 에 없다). 씬은 `IPhysicsSoftBodyScene` 을 상속하지 않습니다. */
        static constexpr bool kSupportsSoftBody = false;
        /** @brief 평면 물리에는 부력이 없습니다(Box2D 에 잠긴 넓이 계산이 없다). 씬은 `IPhysicsBuoyancyScene` 을 상속하지 않습니다. */
        static constexpr bool kSupportsBuoyancy = false;

        /** @brief 이 차원이 만들 수 있는 관절 종류인지 봅니다. 평면에는 스윙이 없어 Cone 을 만들지 않습니다. */
        static constexpr bool supportsJoint( PhysicsJointType type ) { return type != PhysicsJointType::Cone; }
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 바디 하나를 만드는 값입니다.
     * @details 셰이프가 둘 이상이면 컴파운드 바디다. `_sharedShape` 가 유효하면 `_listShape` 대신 미리 만든 셰이프를 쓴다
     *          (`createShape`. 같은 형태의 파편이나 탄피를 대량으로 만들 때 셰이프를 다시 만들지 않는다).
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
        uint64             _userData{ 0 };           ///< 이벤트와 질의 결과가 돌려주는 값이다(엔진은 소유 오브젝트 id 를 싣는다).
        hashed_string      _material{};              ///< 셰이프가 재질을 지정하지 않았을 때의 재질 이름이다(비면 첫 재질).
        PhysicsShapeHandle _sharedShape{};           ///< 미리 만든 셰이프다(`createShape`). 유효하면 `_listShape` 를 보지 않는다.
        float32            _mass{ 0.0f };            ///< 0 이면 셰이프 부피 × 밀도를 쓴다.
        float32            _linearDamping{ 0.05f };  ///< 선속도 감쇠(1/초)
        float32            _angularDamping{ 0.05f }; ///< 각속도 감쇠(1/초)
        float32            _gravityFactor{ 1.0f };   ///< 중력 배율(0 이면 떠 있다)
        PhysicsBodyType    _type{ PhysicsBodyType::Dynamic };
        uint8              _layer{ 0 };        ///< 충돌 레이어 번호다(`PhysicsSettings::findLayerIndex`).
        bool               _bTrigger{ false }; ///< 감지만 하고 막지 않는다(유니티 isTrigger, Jolt sensor, Box2D sensor).
        /**
         * @brief 막지 않고 겹침만 낸다(Box2D 센서)는 표시입니다. `_bTrigger` 와 달리 이벤트 · 질의에서 트리거로 치지 않습니다.
         * @details 막는 콜라이더도 겹침만 측정하는 2D 콜라이더(`BoxCollider2DComponent`)가 쓴다 — 막는 것과 트리거가 모두 센서여야 키네마틱끼리도 겹침이 난다.
         *          이 쌍의 이벤트는 겹침(`PhysicsContactEvent::isOverlap`)이고, 트리거를 빼는 질의(`PhysicsQueryFilter::_bIncludeTriggers`)에도 보인다.
         *          Jolt 는 센서로 만들지만 이벤트에 트리거로 알린다(3D 에서 쓰는 곳이 없다).
         */
        bool _bOverlapOnly{ false };
        bool _bContinuous{ false };      ///< 빠른 바디가 얇은 바디를 지나치지 않도록 연속 충돌로 측정한다(Jolt LinearCast, Box2D bullet).
        bool _bAllowSleep{ true };       ///< 멈추면 잠들어 시뮬레이션 비용을 내지 않는다.
        bool _bAllowTypeChange{ false }; ///< Static 으로 만든 바디를 나중에 Kinematic 이나 Dynamic 으로 바꿀 수 있게 한다(Jolt 는 만들 때 정한다).
        bool _bLockRotation{ false };    ///< 회전하지 않는다(2D 캐릭터, 3D 는 관성을 무한으로 둔다).
    };

    using PhysicsBodyDesc3D = PhysicsBodyDesc<PhysicsDimension3D>;
    using PhysicsBodyDesc2D = PhysicsBodyDesc<PhysicsDimension2D>;
} // namespace sw

namespace sw
{
    /**
     * @brief 관절 하나를 만드는 값입니다. 위치와 축은 월드 공간입니다(만드는 순간의 두 바디 자세를 기준으로 고정됩니다).
     * @details 축 하나(`_axis`)가 종류마다 다른 뜻을 가진다. Hinge 는 회전축, Slider 는 이동축, Cone 은 비틀림 축(뼈 방향)이다.
     *          Cone 의 스윙 한계는 `_normalAxis`(비틀림 축에 수직) 둘레와 그 둘에 수직인 축 둘레로 따로 준다.
     *          `_bodyB` 가 무효면 월드에 붙는다. 2D 는 `_axis` 의 XY 만 보고, Hinge 축은 항상 Z 다.
     */
    template <typename TDimension>
    struct PhysicsJointDesc
    {
        using Vector = typename TDimension::Vector;

        PhysicsBodyHandle _bodyA{};
        PhysicsBodyHandle _bodyB{};      ///< 무효면 월드에 붙는다.
        Vector            _anchor{};     ///< 관절 위치다(월드).
        Vector            _anchorB{};    ///< Distance 만 쓴다. B 쪽 점(월드)이다. 나머지는 `_anchor` 하나를 함께 쓴다.
        Vector            _axis{};       ///< Hinge 는 회전축, Slider 는 이동축, Cone 은 비틀림 축이다(월드, 정규화).
        Vector            _normalAxis{}; ///< Cone 만 쓴다. 비틀림 축에 수직인 기준 축이다(월드, 정규화).
        PhysicsJointMotor _motor{};
        float32           _minLimit{ 0.0f };         ///< Hinge 각, Slider 거리, Distance 길이, Cone 비틀림 각의 아래 한계다.
        float32           _maxLimit{ 0.0f };         ///< 위 한계다.
        float32           _swingLimitNormal{ 0.0f }; ///< Cone 만 쓴다. `_normalAxis` 둘레 스윙 반각이다(라디안).
        float32           _swingLimitPlane{ 0.0f };  ///< Cone 만 쓴다. 나머지 축 둘레 스윙 반각이다(라디안).
        PhysicsJointType  _type{ PhysicsJointType::Fixed };
        bool              _bLimitsEnabled{ false };          ///< Hinge, Slider, Distance 에서 한계를 적용한다(Cone 은 항상 적용한다).
        bool              _bCollideConnectedBodies{ false }; ///< 연결된 두 바디끼리 충돌한다(기본은 false 이며, 래그돌 이웃 뼈가 그렇다).
    };

    using PhysicsJointDesc3D = PhysicsJointDesc<PhysicsDimension3D>;
    using PhysicsJointDesc2D = PhysicsJointDesc<PhysicsDimension2D>;
} // namespace sw

namespace sw
{
    /**
     * @brief 캐릭터 컨트롤러 하나를 만드는 값입니다. 캡슐은 위(+Y)로 섭니다. `_position` 은 캡슐 바닥(발)입니다.
     * @details 3D 는 Jolt `CharacterVirtual`(계단 오르기, 바닥 붙기)을, 2D 는 Box2D 무버(`b2World_CastMover`, `b2SolvePlanes`)를 쓴다.
     *          둘 다 시뮬레이션 바디가 아니라 질의로 움직이는 키네마틱 컨트롤러이며, 유니티의 CharacterController 에 해당한다.
     */
    template <typename TDimension>
    struct PhysicsCharacterDesc
    {
        using Vector = typename TDimension::Vector;

        Vector  _position{};
        uint64  _userData{ 0 };
        float32 _radius{ 0.3f };
        float32 _halfHeight{ 0.6f };                        ///< 원기둥 부분의 반 높이다. 키는 2 × (반 높이 + 반지름)이다.
        float32 _maxSlopeAngle{ MathUtil::kHalfPi * 0.5f }; ///< 걸어 오를 수 있는 가장 가파른 바닥이다(라디안, 기본 45 도).
        float32 _stepHeight{ 0.3f };                        ///< 걸어 오를 수 있는 가장 높은 턱이다(미터). 2D 는 쓰지 않는다.
        float32 _mass{ 70.0f };                             ///< 밀어낼 때의 질량이다(3D).
        float32 _maxStrength{ 100.0f };                     ///< 동적 바디를 미는 가장 큰 힘이다(3D, 뉴턴).
        uint8   _layer{ 0 };
        /**
         * @brief 캐릭터를 따라다니는 속 바디(키네마틱, 같은 캡슐)를 둔다(3D, Jolt `mInnerBodyShape`). 천 · 헤어 같은 소프트 바디와 센서가 캐릭터를 몸으로 느낀다.
         * @details 끄면(기본) 캐릭터는 이동 질의로만 세계와 닿아 소프트 바디가 몸을 뚫고 지나간다. 켜면 속 바디가 다른 움직이는 바디를 더 세게 밀 수 있어 쓰는 게임이 고른다.
         */
        bool _bInnerBody{ false };
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

        Vector            _position{};     ///< 발 위치다.
        Vector            _velocity{};     ///< 이번 이동에서 실제로 난 속도다. 움직이는 바닥이 실어 나른 몫(`_groundBody` 의 속도)은 뺀 바닥 기준 속도다.
        Vector            _groundNormal{}; ///< 서 있으면 바닥 법선이다.
        PhysicsBodyHandle _groundBody{};   ///< 딛고 선 바디다. 서 있지 않으면 비어 있다. 이 바디가 움직이면 `moveCharacter` 가 그 속도를 실어 준다.
        /// 이번 이동에서 바닥이 실어 나른 속도(`_velocity` 에서 뺀 몫)다. 바닥을 떠나는 스텝에 컴포넌트가 이것을 관성으로 잇는다.
        Vector  _carriedVelocity{};
        float32 _groundYawRate{ 0.0f }; ///< 디딘 바닥이 위 축(3D Y, 2D Z) 둘레로 도는 각속도(rad/s)다. 3D 컴포넌트가 캐릭터 방향을 따라 돌린다.
        bool    _bGrounded{ false };
    };

    using PhysicsCharacterState3D = PhysicsCharacterState<PhysicsDimension3D>;
    using PhysicsCharacterState2D = PhysicsCharacterState<PhysicsDimension2D>;
} // namespace sw

namespace sw
{
    /** @brief 바퀴 하나입니다(차체 바디 기준이며 차체의 앞은 +Z, 위는 +Y). 3D 만 지원합니다. */
    struct PhysicsWheelDesc
    {
        float3  _position{}; ///< 서스펜션이 가장 짧을 때 바퀴 중심이 붙는 위치다(차체 기준).
        float32 _radius{ 0.35f };
        float32 _width{ 0.25f };
        float32 _suspensionMinLength{ 0.1f };  ///< 단위는 미터다.
        float32 _suspensionMaxLength{ 0.35f }; ///< 단위는 미터다.
        float32 _suspensionFrequency{ 1.5f };  ///< 스프링 고유 진동수(Hz)
        float32 _suspensionDamping{ 0.5f };    ///< 감쇠비다.
        float32 _maxSteerAngle{ 0.0f };        ///< 라디안이다. 0 이면 조향하지 않는 바퀴다.
        float32 _maxBrakeTorque{ 1500.0f };    ///< N·m
        float32 _maxHandBrakeTorque{ 0.0f };   ///< N·m 이다. 0 이면 핸드브레이크가 걸리지 않는 바퀴다.
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 바퀴 차 하나입니다. 이미 만든 동적 차체 바디에 바퀴, 엔진, 변속, 디퍼렌셜을 붙입니다(Jolt `VehicleConstraint`, `WheeledVehicleController`).
     * @details 디퍼렌셜은 바퀴 둘씩(`_listDriven` 의 짝, 0 과 1 이 한 차축) 묶는다. 2D 물리에는 없다(무효 핸들).
     */
    struct PhysicsWheeledVehicleDesc
    {
        vector<PhysicsWheelDesc> _listWheel;
        vector<int32>            _listDrivenAxle;            ///< 구동 차축의 바퀴 쌍이다. `[왼쪽, 오른쪽, 왼쪽, 오른쪽 ...]` 의 인덱스를 담는다.
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
        int32   _gear{ 0 }; ///< 0 은 중립이고, 음수는 후진이다.
        uint32  _groundedWheelCount{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 스킨 구속 정점 하나가 따르는 뼈 하나와 그 가중치입니다. */
    struct PhysicsSoftBodySkinWeight
    {
        uint32  _jointIndex{ 0 }; ///< `PhysicsSoftBodyDesc::_listInverseBind` 의 자리
        float32 _weight{ 0.0f };  ///< 0..1. 한 정점의 가중치 합은 1 이어야 한다(만들 때 확인한다)
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 스킨 구속 정점 하나입니다(헤어 카드 · 옷 — Jolt `SoftBodySharedSettings::Skinned`).
     * @details 매 스텝 그 정점의 스킨 위치(뼈 가중 합)에서 `_maxDistance` 안으로 끌어당긴다. 0 이면 스킨 위치에 붙는다(고정 정점과 같지만 여러 뼈를 따른다).
     */
    struct PhysicsSoftBodySkinnedVertex
    {
        static constexpr uint32 kMaxWeightCount = 4; ///< 정점 하나의 뼈 수 상한(Jolt `Skinned::cMaxSkinWeights`)

        uint32                    _vertex{ 0 };
        PhysicsSoftBodySkinWeight _arrWeight[kMaxWeightCount]{};
        uint32                    _weightCount{ 0 };
        float32                   _maxDistance{ 0.0f }; ///< 스킨 위치에서 벗어날 수 있는 거리(m)
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 소프트 바디 하나입니다(천 · 헤어 카드). 정점과 삼각형으로 주고, 변마다 늘어남 · 비틀림 · 굽힘 구속을 만듭니다(Jolt `SoftBodySharedSettings`). 3D 만 지원합니다.
     * @details 고정 정점(`_listPinnedVertex`)은 질량이 없어 시뮬레이션이 움직이지 않고, `setSoftBodyPinnedTransform` 이 준 자세를 따른다(유니티 Cloth 의
     *          max distance 0 정점 · 언리얼 Chaos Cloth 의 고정 가중치에 해당한다). 컴플라이언스는 강성의 역수(m/N)이고 0 이면 늘어나지 않는다.
     */
    struct PhysicsSoftBodyDesc
    {
        vector<float3> _listVertex;        ///< 정점(바디 기준 자세의 로컬)
        vector<uint32> _listTriangleIndex; ///< 삼각형마다 정점 인덱스 셋
        vector<uint32> _listPinnedVertex;  ///< 움직이지 않고 자세를 따르는 정점의 인덱스
        /// @brief 뼈를 따르는 정점(`setSoftBodySkinningPose` 가 프레임마다 뼈 자세를 준다). 고정 정점과 겹치면 안 된다.
        vector<PhysicsSoftBodySkinnedVertex> _listSkinnedVertex;
        /// @brief 뼈마다 바인드 자세의 역행렬(서술자 로컬 → 뼈 로컬). `_listSkinnedVertex` 가 있으면 비어 있으면 안 된다.
        vector<float4x4> _listInverseBind;
        float3           _position{}; ///< 처음 자세(월드)
        quaternion       _rotation{};
        float32          _vertexMass{ 0.05f };       ///< 고정되지 않은 정점 하나의 질량(kg)
        float32          _stretchCompliance{ 0.0f }; ///< 변 늘어남(0 이면 늘어나지 않는다)
        float32          _shearCompliance{ 0.0f };   ///< 사각형 대각선 비틀림
        float32          _bendCompliance{ 1.0e-3f }; ///< 이웃 삼각형 사이 굽힘 — 공유 변 맞은편 두 정점 사이 거리 구속(음수면 굽힘 구속을 만들지 않는다)
        float32          _linearDamping{ 0.1f };
        float32          _friction{ 0.2f };
        float32          _gravityFactor{ 1.0f };
        float32          _vertexRadius{ 0.01f }; ///< 정점의 충돌 반지름(m). 다른 바디 표면에서 이만큼 떨어진다
        uint32           _iterationCount{ 5 };   ///< 스텝마다 구속을 푸는 횟수
        uint8            _layer{ 0 };
    };
} // namespace sw
