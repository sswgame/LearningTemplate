/**
 * @file IPhysicsScene.h
 * @brief 물리 씬 인터페이스입니다. 월드(고정 스텝, 중력, 레이어, 접촉 이벤트)와 관절을 다루고, 역할별 인터페이스(바디 · 캐릭터 · 질의 · 바퀴 차 · 소프트 바디 · 부력)를 모두 상속합니다
 *        (`IPhysicsScene3D`, `IPhysicsScene2D`).
 * @details 엔진 코드는 이 인터페이스들만 본다. 구현(Jolt, Box2D)은 백엔드 폴더(`Physics/Jolt`, `Physics/Box2D`) 안에 있고, 라이브러리 헤더는 거기서만
 *          include 한다. 백엔드를 바꾸려면 이 인터페이스를 구현한 씬 하나를 만들고 `PhysicsBackend` 를 정의하는 .cpp 를 그 백엔드 것으로 바꾼다
 *          (`PhysicsSystem` 은 백엔드 이름을 모른다 — `NavMeshBackend` 와 같은 모양).
 *          Godot 의 PhysicsServer3D 에 해당한다.
 *
 *          **역할 나눔.** 씬 하나가 역할을 함께 구현하지만, 한 역할만 쓰는 코드는 그 역할의 인터페이스만 받는다(따로 받는 곳이 없는 관절은 이 인터페이스에 둔다). 언리얼 Chaos 가 월드(`FPhysScene`),
 *          바디(`FPhysicsInterface`), 구속(`FConstraintInstance`), 캐릭터 이동을 나누고, 유니티가 `Physics` 질의와 `Rigidbody` · `Joint` ·
 *          `CharacterController` 를 나누는 것과 같다.
 *
 *          | 역할 | 인터페이스 | 쓰는 곳 |
 *          |---|---|---|
 *          | 바디 · 셰이프 | `IPhysicsBodyScene` | 강체 컴포넌트, 파괴, 래그돌 빌더 |
 *          | 캐릭터 | `IPhysicsCharacterScene` | 캐릭터 컨트롤러 |
 *          | 질의 | `IPhysicsQueryScene` | 맞힘 판정, 소리 가림, 발 IK, 탈것 |
 *          | 바퀴 차(3D 만) | `IPhysicsVehicleScene` | 바퀴 차 컴포넌트 |
 *          | 소프트 바디(3D 만) | `IPhysicsSoftBodyScene` | 천 컴포넌트 |
 *          | 부력(3D 만) | `IPhysicsBuoyancyScene` | 부력 부피 컴포넌트 |
 *          | 월드 · 관절 | `IPhysicsScene` | `ScenePhysics`(스텝, 이벤트), 디버그 그리기, 관절 컴포넌트, 래그돌 빌더 |
 *
 *          **스레드.** 씬 하나는 한 스레드(게임 스레드)에서 다룬다. `step` 안에서만 백엔드가 작업을 나눠 돌린다. step 중에 다른 스레드가 바디를
 *          만들거나 지우면 안 된다. 스텝 밖에서 워커가 질의만 함께 할 때는 `ScenePhysics::openReadScope` 의 질의 창을 쓰고, 닫아 받은 증표(`WorkerReadFence`)가 스텝을 연다.
 *
 *          **핸들.** 무효 핸들이나 지워진 핸들을 받은 함수는 아무것도 하지 않고 false, 0, 기본값을 반환한다(세대가 다르면 지워진 핸들이다).
 *
 *          **단위.** 미터, 킬로그램, 초, 라디안을 쓴다. 3D 는 +Y 가 위이고, 2D 는 XY 평면에서 각이 Z 축 둘레(반시계가 +)다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Physics/Collision/PhysicsContact.h"
#include "Engine/Physics/IPhysicsBodyScene.h"
#include "Engine/Physics/IPhysicsBuoyancyScene.h"
#include "Engine/Physics/IPhysicsCharacterScene.h"
#include "Engine/Physics/IPhysicsQueryScene.h"
#include "Engine/Physics/IPhysicsSceneFwd.h"
#include "Engine/Physics/IPhysicsSoftBodyScene.h"
#include "Engine/Physics/IPhysicsVehicleScene.h"
#include "Engine/Physics/PhysicsDesc.h"
#include "Engine/Physics/PhysicsTypes.h"

namespace sw
{
    class CollisionLayers;
    class IPhysicsDebugRenderer;

    /** @brief 물리 장면 구현이 요구하는 정렬(Jolt 장면의 SIMD 수학 칸 — 16 바이트)입니다. 기반 `IPhysicsScene` 이 선언한다(아래 주석). */
    inline constexpr size_t kPhysicsSceneAlignment = 16;

    /**
     * @brief 물리 씬 하나입니다. 차원 트레이트(`PhysicsDimension3D`, `PhysicsDimension2D`)만 다르고 함수는 같습니다.
     * @details 바퀴 차 · 소프트 바디는 차원 트레이트의 `kSupportsVehicle` · `kSupportsSoftBody` 가 참일 때만 상속한다(2D 는 빈 `PhysicsNoVehicleScene` · `PhysicsNoSoftBodyScene`).
     *          역할 인터페이스의 타입 이름(`Vector`, `CastHit` …)은 여러 바탕 클래스에 있어 그대로면 모호하므로 여기서 다시 선언해 가린다.
     *          `alignas( 16 )`: 구현(Jolt 장면 — SIMD 수학 칸)이 16 바이트 정렬이다. 기반 포인터(`unique_ptr<IPhysicsScene3D>`)로 지우므로 기반이 같은 정렬을 선언해야
     *          할당 · 해제가 같은 기준(`kUsesAlignedAllocation`)을 본다 — wasm32 는 기본 new 정렬이 16 보다 작아 그렇지 않으면 정렬 할당 블록을 일반 해제로 풀었다.
     */
    template <typename TDimension>
    class alignas( kPhysicsSceneAlignment ) IPhysicsScene : public IPhysicsBodyScene<TDimension>,
                                                            public IPhysicsCharacterScene<TDimension>,
                                                            public IPhysicsQueryScene<TDimension>,
                                                            public std::conditional_t<TDimension::kSupportsVehicle, IPhysicsVehicleScene, PhysicsNoVehicleScene>,
                                                            public std::conditional_t<TDimension::kSupportsSoftBody, IPhysicsSoftBodyScene, PhysicsNoSoftBodyScene>,
                                                            public std::conditional_t<TDimension::kSupportsBuoyancy, IPhysicsBuoyancyScene, PhysicsNoBuoyancyScene>
    {
    public:
        using Vector         = typename TDimension::Vector;
        using Rotation       = typename TDimension::Rotation;
        using Angular        = typename TDimension::Angular;
        using ShapeDesc      = typename TDimension::ShapeDesc;
        using BodyDesc       = PhysicsBodyDesc<TDimension>;
        using JointDesc      = PhysicsJointDesc<TDimension>;
        using CharacterDesc  = PhysicsCharacterDesc<TDimension>;
        using CharacterState = PhysicsCharacterState<TDimension>;
        using CastHit        = PhysicsCastHit<TDimension>;
        using ContactEvent   = PhysicsContactEvent<TDimension>;

        ~IPhysicsScene() override = default;

        // --- 시뮬레이션 ---------------------------------------------------------------------------------------------------

        /**
         * @brief 고정 스텝 하나를 진행합니다. 접촉 이벤트는 이 스텝의 것으로 바뀝니다(`getContactEvents`).
         * @details 가변 프레임 시간은 호출하는 쪽이 `FixedStepTimer` 로 고정 스텝 수로 바꾼다. 같은 스텝 크기와 같은 입력이면 같은 결과가 나온다.
         */
        virtual void step( float32 fixedDeltaTime ) = 0;
        /** @brief 마지막 `step` 이 낸 접촉과 트리거 이벤트를 반환합니다(결정적 순서, `PhysicsContactTracker`). 다음 `step` 까지 그대로입니다. */
        virtual const vector<ContactEvent>& getContactEvents() const = 0;
        /**
         * @brief 사용자 값 @p userData 를 실은 바디가 낀 쌍이 유지 이벤트를 낼지 정합니다. 처음에는 아무도 듣지 않아 유지가 나오지 않습니다(시작 · 끝은 늘 나온다).
         * @details 언리얼 `bNotifyRigidBodyCollision` · Godot `contact_monitor` 처럼 켠 쪽만 알린다 — 듣지 않는 쌍은 유지 값(충격량 어림)도 구하지 않는다(감사 S-1).
         *          바디가 아니라 사용자 값에 걸어 같은 값으로 다시 지은 바디도 따라간다. step 사이에 부릅니다.
         */
        virtual void   setContactStayListener( uint64 userData, bool bListen ) = 0;
        virtual void   setGravity( const Vector& gravity )                     = 0;
        virtual Vector getGravity() const                                      = 0;
        /** @brief 레이어끼리의 충돌 행렬을 바꿉니다(`PhysicsSettings::makeCollisionLayers`). 이미 있는 바디에도 다음 스텝부터 적용됩니다. */
        virtual void setLayerCollision( const CollisionLayers& layers ) = 0;
        /** @brief 지금 쓰는 레이어 충돌 행렬입니다(`setLayerCollision` 이 마지막으로 준 것, 처음에는 설정 표의 것). */
        virtual const CollisionLayers& getLayerCollision() const = 0;
        /** @brief 바디마다 셰이프를 선으로 냅니다(`IPhysicsDebugRenderer`). */
        virtual void drawDebug( IPhysicsDebugRenderer& renderer ) const = 0;

        // --- 관절 — 언리얼 Chaos 의 구속(`FConstraintInstance`), 유니티의 `Joint` 자리 ----------------------------------------

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
} // namespace sw

namespace sw
{
    struct PhysicsSettings;

    /**
     * @struct PhysicsBackend
     * @brief 쓰는 물리 백엔드를 고르는 곳입니다. 3D 는 Jolt(`Physics/Jolt/JoltPhysicsBackend.cpp`), 2D 는 Box2D(`Physics/Box2D/Box2DPhysicsScene.cpp`)가 정의합니다.
     * @details `PhysicsSystem` 만 부른다. 바꾸려면 이 선언을 정의하는 .cpp 를 바꾼다 — 언리얼이 Chaos 를 `FPhysicsInterface` 뒤에, Godot 가 서버를 `PhysicsServer3D` 뒤에 두는 자리.
     */
    struct SW_API PhysicsBackend
    {
        /**
         * @brief 3D 백엔드의 전역 상태(할당자, 로그 · 단언, 셰이프 타입 등록, 잡 시스템)를 올립니다. 헤더와 라이브러리의 빌드 설정이 다르면 오류를 남기고 false 입니다.
         */
        [[nodiscard]] static bool initialize3D();
        /** @brief 3D 백엔드의 전역 상태를 내립니다. 모든 3D 씬이 먼저 사라져 있어야 합니다. */
        static void shutdown3D();
        /** @brief 설정(중력, 레이어 테이블, 재질)으로 3D 씬 하나를 만듭니다. 올라와 있지 않으면 nullptr 입니다. */
        static unique_ptr<IPhysicsScene3D> createScene3D( const PhysicsSettings& settings );
        /** @brief 설정(중력, 레이어 테이블, 재질, 서브 스텝)으로 2D 씬 하나를 만듭니다. 2D 백엔드는 전역 상태가 없다(월드마다 따로 있다). */
        static unique_ptr<IPhysicsScene2D> createScene2D( const PhysicsSettings& settings );
    };
} // namespace sw
