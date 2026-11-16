/**
 * @file PhysicsPoseStore.h
 * @brief 강체(3D · 2D)의 바디 핸들 · 스텝 자세 · 속도 · 명령과 캐릭터 컨트롤러(3D · 2D)의 입력 대기를 칸 SoA 로 두는 저장소입니다(`ComponentDataStore` 의 물리 사용자).
 */
#pragma once
#include "Core/Concurrency/SpinLock.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ISubsystem.h"
#include "Core/Subsystem/ServiceSlot.h"

#include "Engine/Object/Component/ComponentDataStore.h"
#include "Engine/Physics/IPhysicsBodyScene.h"
#include "Engine/Physics/PhysicsDesc.h"
#include "Engine/Physics/PhysicsField.h"
#include "Engine/Physics/PhysicsTypes.h"

namespace sw
{
    class PhysicsComponent;

    /**
     * @brief 컴포넌트가 다음 물리 프레임에 바디에 줄 명령입니다. 틱 중 어느 워커에서든 쌓고(칸의 잠금 `PhysicsBodyPage::_arrCommandLock` 아래), 물리 프레임이 게임 스레드에서 씁니다.
     * @details 물리 씬은 한 스레드에서만 만집니다(Box2D 는 동시 호출을 막는다) — 병렬 틱의 `addForce` 가 씬을 바로 부르지 않는 이유입니다.
     *          힘 · 토크는 그 프레임의 모든 스텝에, 충격량은 첫 스텝에 한 번 줍니다.
     */
    template <typename TDimension>
    struct PhysicsBodyCommand
    {
        using Vector  = typename TDimension::Vector;
        using Angular = typename TDimension::Angular;

        Vector  _force{};
        Vector  _impulse{};
        Angular _torque{};
        Vector  _linearVelocity{};
        Angular _angularVelocity{};
        bool    _bSetLinearVelocity{ false };  ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool    _bSetAngularVelocity{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool    _bWake{ false };               ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };

    /**
     * @struct PhysicsBodyPage
     * @brief 강체 칸 256 개의 바디 핸들 · 앞뒤 스텝 자세 · 속도 · 명령입니다. 값마다 배열 하나라, 스텝마다 움직인 바디를 읽는 일괄 쓰기(`ScenePhysics::readMovedBodies`)가
     *        컴포넌트를 거치지 않고 칸에 바로 쓴다(`applySteppedPose`).
     * @details 칸은 강체 컴포넌트의 수명(만들어질 때 받고 없어질 때 놓는다)이라 바디를 다시 지어도(속성 변경 · 끄고 켜기) 쌓인 명령이 남는다 — 바디 칸 번호로
     *          두면 다시 지을 때 명령을 옮겨야 한다. 바디 칸 → 강체 칸 표는 씬(`ScenePhysics::bindBody`)이 들고, 세대 확인은 이 페이지의 `_arrBody` 로 한다.
     *
     *          스레드 계약: 자세 · 속도 · 바디 핸들은 게임 스레드(물리 프레임)와 그 프레임의 일괄 읽기 워커(칸끼리 겹치지 않는다)만 쓴다. 명령은 어느 틱
     *          워커든 칸의 스핀락 아래에서 쌓는다(예전에는 강체마다 `SpinLock` + `PhysicsBodyCommand` 멤버였다).
     *          언리얼 Chaos 의 물리 스레드 결과 버퍼 · 유니티 `PhysicsWorld` 바디 배열 자리다.
     */
    /** @brief 강체 칸 페이지의 정렬(캐시 라인)입니다 — 페이지끼리 라인을 나눠 갖지 않는다(`SceneTransformPage` 와 같다). */
    inline constexpr uint32 kPhysicsBodyPageAlignment = 64;

    template <typename TDimension>
    struct alignas( kPhysicsBodyPageAlignment ) PhysicsBodyPage
    {
        using Owner    = PhysicsComponent; ///< `ComponentDataStore` 페이지 계약
        using Vector   = typename TDimension::Vector;
        using Rotation = typename TDimension::Rotation;
        using Angular  = typename TDimension::Angular;
        using State    = PhysicsBodyState<TDimension>;
        using Command  = PhysicsBodyCommand<TDimension>;

        static constexpr uint32 kSlotShift = 8;
        static constexpr uint32 kSlotCount = 1u << kSlotShift;
        static constexpr uint32 kSlotMask  = kSlotCount - 1u;

        PhysicsBodyHandle _arrBody[kSlotCount];             ///< 칸 주인의 바디(없으면 무효). 움직인 바디 목록의 세대 확인에 쓴다
        Vector            _arrPreviousPosition[kSlotCount]; ///< 보간의 앞 스텝 자리
        Vector            _arrCurrentPosition[kSlotCount];  ///< 마지막 스텝 자리
        Rotation          _arrPreviousRotation[kSlotCount];
        Rotation          _arrCurrentRotation[kSlotCount];
        Vector            _arrLinearVelocity[kSlotCount];  ///< 마지막 스텝 뒤 선속도
        Angular           _arrAngularVelocity[kSlotCount]; ///< 마지막 스텝 뒤 각속도
        uint64            _arrMovedStep[kSlotCount];       ///< 바디가 마지막으로 깨어 있던 스텝 번호(`ScenePhysics::getStepCount`)
        Command           _arrCommand[kSlotCount];         ///< 다음 물리 프레임 명령(`_arrCommandLock` 아래)
        SpinLock          _arrCommandLock[kSlotCount];     ///< 아무 스레드의 입력(`addForce` …) → 물리 프레임의 꺼내기를 지킨다
        PhysicsComponent* _arrOwner[kSlotCount];           ///< 칸을 쓰는 강체. 빈 칸이면 nullptr

        /** @brief 받은 칸을 기본값으로 채웁니다(`ComponentDataStore` 페이지 계약). */
        void initializeSlot( uint32 pageIndex, PhysicsComponent* pOwner )
        {
            _arrBody[pageIndex]             = PhysicsBodyHandle{};
            _arrPreviousPosition[pageIndex] = Vector{};
            _arrCurrentPosition[pageIndex]  = Vector{};
            _arrPreviousRotation[pageIndex] = Rotation{};
            _arrCurrentRotation[pageIndex]  = Rotation{};
            _arrLinearVelocity[pageIndex]   = Vector{};
            _arrAngularVelocity[pageIndex]  = Angular{};
            _arrMovedStep[pageIndex]        = 0;
            _arrCommand[pageIndex]          = Command{};
            _arrOwner[pageIndex]            = pOwner;
        }
        /** @brief 놓은 칸이 누구의 것으로도 읽히지 않게 비웁니다. */
        void releaseSlot( uint32 pageIndex )
        {
            _arrBody[pageIndex]  = PhysicsBodyHandle{};
            _arrOwner[pageIndex] = nullptr;
        }
        /** @brief 앞뒤 스텝 자세를 둘 다 @p position · @p rotation 으로 둡니다(바디를 만들거나 순간이동 — 보간을 끊는다). */
        void resetPose( uint32 pageIndex, const Vector& position, const Rotation& rotation )
        {
            _arrPreviousPosition[pageIndex] = position;
            _arrCurrentPosition[pageIndex]  = position;
            _arrPreviousRotation[pageIndex] = rotation;
            _arrCurrentRotation[pageIndex]  = rotation;
        }
        /** @brief 잠든 바디의 앞 스텝 자세를 지금 자세로 당깁니다(보간할 사이가 없다). */
        void holdPose( uint32 pageIndex )
        {
            _arrPreviousPosition[pageIndex] = _arrCurrentPosition[pageIndex];
            _arrPreviousRotation[pageIndex] = _arrCurrentRotation[pageIndex];
        }
        /** @brief 이 스텝에 깨어 있던 바디의 자세 · 속도 @p state 를 받습니다(일괄 읽기 워커 구간 — 칸끼리 겹치지 않는다). */
        void applySteppedPose( uint32 pageIndex, const State& state, uint64 stepCount )
        {
            _arrPreviousPosition[pageIndex] = _arrCurrentPosition[pageIndex];
            _arrPreviousRotation[pageIndex] = _arrCurrentRotation[pageIndex];
            _arrCurrentPosition[pageIndex]  = state._position;
            _arrCurrentRotation[pageIndex]  = state._rotation;
            _arrLinearVelocity[pageIndex]   = state._linearVelocity;
            _arrAngularVelocity[pageIndex]  = state._angularVelocity;
            _arrMovedStep[pageIndex]        = stepCount;
        }
        /** @brief 명령 칸을 잠그고 @p function( Command& ) 을 부릅니다(어느 스레드든). */
        template <typename TFunction>
        void editCommand( uint32 pageIndex, TFunction&& function )
        {
            ScopedLock<SpinLock> lock{ _arrCommandLock[pageIndex] };
            function( _arrCommand[pageIndex] );
        }
    };

    using PhysicsBodyPage3D = PhysicsBodyPage<PhysicsDimension3D>;
    using PhysicsBodyPage2D = PhysicsBodyPage<PhysicsDimension2D>;

    /**
     * @struct CharacterCommand3D
     * @brief 3D 캐릭터 컨트롤러가 다음 물리 프레임에 받을 입력입니다 — 이동 요청과 아직 스텝에 넘기지 않은 대기(점프 · 루트 모션 · 면 속도 · 이동 계수 · 발사 · 필드).
     * @details 아무 틱 워커든 칸의 잠금(`PhysicsCommandPage::_arrCommandLock`) 아래에서 쌓고, 물리 프레임(`beginPhysicsFrame` · `prePhysicsStep`)이 꺼낸다.
     */
    struct CharacterCommand3D
    {
        float3             _moveVelocity{};           ///< 요청된 수평 속도 — 다시 부를 때까지 유지한다
        float3             _pendingRootMotion{};      ///< 아직 스텝에 넘기지 않은 루트 모션 이동
        float3             _pendingSurfaceVelocity{}; ///< 이번 프레임에 쌓인 면 속도
        float3             _pendingLaunch{};          ///< 0 이 아니면 다음 스텝에 쏜다
        PhysicsFieldSample _pendingField{};           ///< 이번 게임 프레임에 모인 물리 필드
        float32            _pendingMoveScale{ 1.0f }; ///< 이번 프레임에 받은 이동 계수의 최솟값
        float32            _pendingJumpSpeed{ 0.0f }; ///< 0 이 아니면 다음 스텝에 준다
    };

    /** @struct CharacterCommand2D @brief 2D 캐릭터 컨트롤러가 다음 물리 프레임에 받을 입력입니다(이동 요청 · 점프 대기 — `CharacterCommand3D` 와 같은 계약). */
    struct CharacterCommand2D
    {
        float32 _moveVelocity{ 0.0f };     ///< 요청된 수평 속도 — 다시 부를 때까지 유지한다
        float32 _pendingJumpSpeed{ 0.0f }; ///< 0 이 아니면 다음 스텝에 준다
    };

    /** @struct VehicleDriverInput @brief 바퀴 차(`WheeledVehicleComponent`)가 물리 프레임 시작에 백엔드로 넘길 운전 입력입니다 — 다시 부를 때까지 유지한다. */
    struct VehicleDriverInput
    {
        float32 _forward{ 0.0f };   ///< −1..1(음수 후진)
        float32 _right{ 0.0f };     ///< −1..1
        float32 _brake{ 0.0f };     ///< 0..1
        float32 _handBrake{ 0.0f }; ///< 0..1
    };

    /**
     * @struct PhysicsCommandPage
     * @brief 물리 컴포넌트 칸 256 개의 입력(@p TCommand)과 그 잠금입니다(`PhysicsBodyPage` 의 명령 칸과 같은 틀 — 예전에는 컴포넌트마다 `SpinLock` + 대기 멤버였다).
     * @details 칸은 컴포넌트의 수명이라 백엔드 객체를 다시 지어도(속성 변경 · 끄고 켜기) 쌓인 입력이 남는다. 언리얼 Mass 프래그먼트 · 유니티 Entities 의
     *          입력 컴포넌트 배열 자리다 — 프레임마다 쌓이는 값만 종류별 배열로 옮기고 상태(스텝 결과 · 보간)는 컴포넌트에 둔다.
     *          입력 종류: 캐릭터 컨트롤러 3D · 2D(`CharacterCommand3D` · `CharacterCommand2D`), 바퀴 차(`VehicleDriverInput`).
     */
    template <typename TCommand>
    struct alignas( kPhysicsBodyPageAlignment ) PhysicsCommandPage
    {
        using Owner   = PhysicsComponent; ///< `ComponentDataStore` 페이지 계약
        using Command = TCommand;

        static constexpr uint32 kSlotShift = 8;
        static constexpr uint32 kSlotCount = 1u << kSlotShift;
        static constexpr uint32 kSlotMask  = kSlotCount - 1u;

        Command           _arrCommand[kSlotCount];     ///< 다음 물리 프레임 입력(`_arrCommandLock` 아래)
        SpinLock          _arrCommandLock[kSlotCount]; ///< 아무 스레드의 입력(`setMoveVelocity` · `jump` …) → 물리 프레임의 꺼내기를 지킨다
        PhysicsComponent* _arrOwner[kSlotCount];       ///< 칸을 쓰는 컴포넌트. 빈 칸이면 nullptr

        /** @brief 받은 칸을 기본값으로 채웁니다(`ComponentDataStore` 페이지 계약). */
        void initializeSlot( uint32 pageIndex, PhysicsComponent* pOwner )
        {
            _arrCommand[pageIndex] = Command{};
            _arrOwner[pageIndex]   = pOwner;
        }
        /** @brief 놓은 칸이 누구의 것으로도 읽히지 않게 비웁니다. */
        void releaseSlot( uint32 pageIndex ) { _arrOwner[pageIndex] = nullptr; }
        /** @brief 입력 칸을 잠그고 @p function( Command& ) 을 부릅니다(어느 스레드든 — 읽기도 이 길로 한다). */
        template <typename TFunction>
        void editCommand( uint32 pageIndex, TFunction&& function )
        {
            ScopedLock<SpinLock> lock{ _arrCommandLock[pageIndex] };
            function( _arrCommand[pageIndex] );
        }
    };

    using CharacterCommandPage3D = PhysicsCommandPage<CharacterCommand3D>;
    using CharacterCommandPage2D = PhysicsCommandPage<CharacterCommand2D>;
    using VehicleInputPage       = PhysicsCommandPage<VehicleDriverInput>;

    /**
     * @class PhysicsPoseStore
     * @brief 강체 3D · 2D 와 캐릭터 컨트롤러 3D · 2D · 바퀴 차 입력의 칸 저장소를 든 서비스입니다(`SceneTransformStorage` 와 같은 틀 · 같은 수명 — 서비스 범위라 모든 컴포넌트보다 오래 산다).
     * @details 페이지 4096 × 칸 256 = 종류마다 약 100만 개(오브젝트 id 표의 끝과 같다).
     */
    class SW_API PhysicsPoseStore final : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "PhysicsPoseStore";

        /** @brief 차원마다 페이지 표의 칸 수입니다(위 설명). */
        static constexpr uint32 kMaxPageCount = 4096;
        template <typename TDimension>
        using Store = ComponentDataStore<PhysicsBodyPage<TDimension>, kMaxPageCount>;
        template <typename TCommand>
        using CommandStore = ComponentDataStore<PhysicsCommandPage<TCommand>, kMaxPageCount>;

        /** @brief 칸이 없음을 나타냅니다. */
        static constexpr uint32 kInvalidSlot = Store<PhysicsDimension3D>::kInvalidSlot;

        PhysicsPoseStore();
        ~PhysicsPoseStore() override;

        PhysicsPoseStore( const PhysicsPoseStore& )            = delete;
        PhysicsPoseStore& operator=( const PhysicsPoseStore& ) = delete;

        // ISubsystem — 세울 것이 없다(처음 받는 칸부터 페이지를 만든다).
        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /** @brief 차원 @p TDimension 의 칸 저장소입니다. */
        template <typename TDimension>
        Store<TDimension>& getStore()
        {
            if constexpr ( std::is_same_v<TDimension, PhysicsDimension3D> )
                return _store3D;
            else
                return _store2D;
        }
        template <typename TDimension>
        const Store<TDimension>& getStore() const
        {
            if constexpr ( std::is_same_v<TDimension, PhysicsDimension3D> )
                return _store3D;
            else
                return _store2D;
        }

        /** @brief 입력 @p TCommand(`CharacterCommand3D` · `CharacterCommand2D` · `VehicleDriverInput`)의 칸 저장소입니다. */
        template <typename TCommand>
        CommandStore<TCommand>& getCommandStore()
        {
            if constexpr ( std::is_same_v<TCommand, CharacterCommand3D> )
                return _commandStore3D;
            else if constexpr ( std::is_same_v<TCommand, CharacterCommand2D> )
                return _commandStore2D;
            else
            {
                static_assert( std::is_same_v<TCommand, VehicleDriverInput>, "PhysicsPoseStore has no command store for this input type" );
                return _vehicleInputStore;
            }
        }

    private:
        Store<PhysicsDimension3D>        _store3D;
        Store<PhysicsDimension2D>        _store2D;
        CommandStore<CharacterCommand3D> _commandStore3D;
        CommandStore<CharacterCommand2D> _commandStore2D;
        CommandStore<VehicleDriverInput> _vehicleInputStore;

        SW_SERVICE_SLOT( PhysicsPoseStore, Required );
    };
} // namespace sw
