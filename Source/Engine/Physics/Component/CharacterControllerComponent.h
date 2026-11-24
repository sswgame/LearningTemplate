/**
 * @file CharacterControllerComponent.h
 * @brief 캐릭터 컨트롤러(3D) — 캡슐 하나를 질의로 움직여 벽에 미끄러지고 · 턱을 오르고 · 가파른 비탈에 막힙니다(Jolt `CharacterVirtual`).
 */
#pragma once
#include "Core/Concurrency/ThreadSafety.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Math/Radians.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Physics/Component/PhysicsComponent.h"
#include "Engine/Physics/PhysicsField.h"
#include "Engine/Physics/PhysicsTypes.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct CharacterCommand3D;
    template <typename TCommand>
    struct PhysicsCommandPage;

    class ValidationContext;
} // namespace sw

namespace sw
{
    /** @brief 손으로 돌리는 걸음(`CharacterControllerComponent::simulateMotion`)이 실제 걸음인지 되감아 다시 돌리는 걸음인지입니다. */
    enum class CharacterMotionPass : uint8
    {
        Live = 0, ///< 실제 걸음 — 밖에서 쌓은 충격(점프 · 발사 · 루트 모션 · 멈춤)을 꺼내 쓰고 닿은 바디를 민다
        Replay    ///< 되감은 입력을 다시 돌린다 — 밖의 충격은 이미 그 입력의 실제 걸음이 썼고, 세계를 다시 밀지 않는다
    };

    /**
     * @brief 한 걸음의 이동 입력입니다 — 이동기가 의도에서 만든다. 걸음 하나가 (상태, 입력)만의 함수가 되게, 컨트롤러에 남는 요청(`setMoveVelocity` …)과 같은 값을 걸음마다 통째로 준다.
     */
    struct CharacterMotionInput
    {
        float3  _moveVelocity{};            ///< 원하는 수평 속도(`setMoveVelocity`)
        float3  _launchVelocity{};          ///< 0 이 아니면 이 걸음에 쏜다(`launch`)
        float3  _climbVelocity{};           ///< 오르기 속도(`_bClimb` 일 때, `applyClimbVelocity`)
        float32 _verticalMoveSpeed{ 0.0f }; ///< 헤엄의 위아래 속도(`setVerticalMoveSpeed`)
        float32 _jumpSpeed{ 0.0f };         ///< 0 보다 크면 이 걸음에 뛴다(`jump`)
        float32 _moveScale{ 1.0f };         ///< 이동 계수(`applyMoveScale`)
        uint8   _bClimb{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 캐릭터가 다음 걸음으로 이어 가는 운동 전부입니다 — 같은 상태 · 입력 · 세계면 같은 다음 상태가 난다(`CharacterControllerComponent::simulateMotion`).
     * @details 언리얼 `FSavedMove_Character` 의 시작 상태(자리 · 속도 · 이동 방식 · 바닥) 자리다. 디딘 바디 · 바닥 법선은 싣지 않는다 — 걸음 앞에 자리에서 다시 찾는다
     *          (`IPhysicsCharacterScene::refreshCharacterContacts`). 바디 핸들은 서버와 클라이언트에서 다르다.
     *          칸 선언(`Replicated` · 범위)이 곧 네트워크 바이트다(`ReplicatedLayout`). 예측이 서버와 같은 값에서 시작해야 하므로 실수는 양자화하지 않고, 범위는 NaN · 무한대를 거절하는 경계다.
     */
    REFLECT()
    struct SW_API CharacterMotionState
    {
        REFLECT_BODY();

        PROPERTY( Replicated, Min = -1000000, Max = 1000000 )
        float3 _position{}; ///< 발(월드)

        PROPERTY( Replicated, Min = -10000, Max = 10000 )
        float3 _walkVelocity{}; ///< 지난 걸음에 낸 걷는 속도(빙판 — 접지가 낮으면 천천히 바뀐다)

        PROPERTY( Replicated, Min = -10000, Max = 10000 )
        float3 _fieldVelocity{}; ///< 필드가 쌓은 수평 속도

        PROPERTY( Replicated, Min = -10000, Max = 10000 )
        float3 _airVelocity{}; ///< 발사 · 떠난 바닥의 수평 속도

        PROPERTY( Replicated, Min = -10000, Max = 10000 )
        float32 _verticalSpeed{ 0.0f };

        PROPERTY( Replicated, Min = 0, Max = 4 )
        uint8 _movementMode{ 0 }; ///< `CharacterMovementMode`. 범위는 cpp 의 static_assert 가 열거의 끝 값과 맞춘다

        PROPERTY( Replicated, Min = 0, Max = 1 )
        uint8 _bGrounded{ SW_FALSE };

        CharacterMovementMode getMovementMode() const { return static_cast<CharacterMovementMode>( _movementMode ); }
    };
} // namespace sw

namespace sw
{
    /**
     * @class CharacterControllerComponent
     * @brief 3D 캐릭터 컨트롤러입니다(유니티 CharacterController · 언리얼 CharacterMovement 의 충돌 부분). 이 컴포넌트의 월드 자리가 발입니다.
     * @details 시뮬레이션 바디가 아닙니다 — 고정 스텝마다 원하는 속도(`setMoveVelocity`) + 중력으로 캡슐을 쓸어 옮깁니다. 서 있으면 수직 속도를 지우고,
     *          `jump` 가 수직 속도를 줍니다. 자리는 마지막 두 스텝 사이를 보간해 씁니다. 코드가 트랜스폼을 옮기면 순간이동입니다.
     *          이동 요청은 어느 틱에서든 부를 수 있습니다(다음 물리 프레임에 든다).
     *          **손으로 돌리기**(`setManualSimulation`): 네트워크 예측처럼 자기 고정 틱으로 걷는 쪽이 `simulateMotion` 으로 걸음을 돌린다. 켜져 있는 동안 물리 고정 스텝은 이 캐릭터를
     *          옮기지 않고, 트랜스폼에는 부른 쪽이 준 그리기 자리(`setMotionDisplay`)를 쓴다. 두 길이 같은 걸음 계산(`beginStep` · `finishStep`)을 쓴다.
     */
    REFLECT( Category = "Physics", DisplayName = "Character Controller", Tooltip = "Capsule character mover: slides on walls, climbs steps, blocked by steep slopes", Meta = "Icon=Character" )
    class SW_API CharacterControllerComponent : public PhysicsComponent
    {
    public:
        REFLECT_BODY();

        /** @brief 입력 칸(`PhysicsPoseStore` 의 캐릭터 저장소)을 받습니다. */
        CharacterControllerComponent();
        /** @brief 입력 칸을 놓습니다. */
        ~CharacterControllerComponent() override;

        /** @brief 원하는 수평 이동 속도(월드, 미터/초)입니다. 다시 부를 때까지 유지합니다. Y 는 보지 않습니다(중력 · 점프가 정한다). */
        void setMoveVelocity( const float3& velocity );
        /**
         * @brief 헤엄칠 때 위(+) · 아래(-)로 가려는 속도(미터/초)입니다. 다시 부를 때까지 유지하며, 이동 방식이 `Swimming` 인 스텝만 씁니다(걷기 · 낙하의 수직은 중력 · 점프가 정한다).
         * @details 그 속도보다 느릴 때만 끌어올리거나 내린다 — 부력 · 흐름 · 뛰어오름이 더 빠르면 그대로다. 아무 스레드에서 불러도 됩니다.
         */
        void setVerticalMoveSpeed( float32 speed );
        /**
         * @brief 이번 물리 프레임은 사다리 · 벽을 이 속도(월드, 미터/초 — 수직 포함)로 오릅니다. 중력 · 물리 필드 · 공중 관성을 쓰지 않고 이동 방식이 `Climbing` 이 됩니다.
         * @details 필드(`applyPhysicsField`)처럼 프레임마다 다시 불러야 이어진다 — 부르지 않은 프레임부터 떨어진다(떠날 때 수직 속도는 0). 같은 스텝의 점프 · 발사가 이긴다.
         *          언리얼은 사다리를 `MOVE_Custom` 이동 방식으로 짠다 — 충돌 · 이동은 같은 캡슐 쓸기 한 길이다. 아무 스레드에서 불러도 됩니다.
         */
        void applyClimbVelocity( const float3& velocity );
        /** @brief 위로 @p speed(미터/초)를 줍니다. 다음 스텝에 듭니다. */
        FUNCTION( Category = "Movement", Exec )
        void jump( float32 speed );
        /**
         * @brief 애니메이션 루트 모션의 이번 프레임 이동(월드, 미터)을 더합니다. 다음 물리 프레임의 스텝들이 나눠 움직입니다(벽에 막히고 턱을 오른다).
         * @details 수평(XZ)만 씁니다 — 수직은 중력 · 점프가 정합니다. 스텝이 없는 프레임이면 다음 프레임으로 넘깁니다. 아무 스레드에서 불러도 됩니다.
         */
        void addRootMotionDisplacement( const float3& worldDisplacement );
        /**
         * @brief 발사대 · 스프링이 쏩니다 — 수직은 그 속도로 덮고, 수평은 땅에 다시 닿을 때까지 이동 속도에 더합니다. 다음 스텝에 듭니다. 아무 스레드에서 불러도 됩니다.
         */
        void launch( const float3& velocity );
        /**
         * @brief 이어받은 운동을 다음 스텝에 모두 지웁니다 — 수직 속도 · 걷는 속도 · 필드 속도 · 공중 관성, 그리고 그 전에 쌓인 점프 · 발사 · 루트 모션 대기(언리얼 CharacterMovement `StopMovementImmediately`).
         * @details 요청 속도(`setMoveVelocity`)는 조종 입력이라 남긴다. 순간이동(`teleportTo`)과 함께 부르면 "그 자리에 멈춰 놓기" 다(언리얼 `ETeleportType::ResetPhysics`). 아무 스레드에서 불러도 됩니다.
         */
        void stopMovementImmediately();
        /**
         * @brief 딛고 선 면의 속도(컨베이어 · 흐르는 물, 월드 m/s)를 이번 물리 프레임에 더합니다. 프레임마다 다시 불러야 이어집니다(여럿이면 합).
         * @details 아무 스레드에서 불러도 됩니다. 트랜스폼을 직접 옮기면 순간이동이라 벽을 지나므로, 면이 나르는 것은 이것으로 넘깁니다.
         */
        void addSurfaceVelocity( const float3& velocity );
        /**
         * @brief 이번 물리 프레임의 걷는 속도(요청 속도 · 루트 모션)에 곱할 이동 계수를 얹습니다(눈 · 모래 · 물 · 함몰 구역). 프레임마다 다시 불러야 이어지고, 여럿이면 가장 작은 값입니다.
         * @details 환경이 주는 이동 저항의 한 입력이다 — 표면(`GroundSurfaceComponent`)과 이펙트의 이동 제약(GameFramework `MovementConstraint`)이 각자 부른다.
         *          결정: 곱이 아니라 최솟값이다 — 물리 스텝이 없는 렌더 프레임이 끼면 같은 호출이 두 번 쌓이는데, 최솟값은 몇 번 불러도 같다(곱은 0.8 이 0.64 가 된다).
         *          면 속도 · 발사 · 관성은 곱하지 않는다(걷는 힘이 아니다). 아무 스레드에서 불러도 됩니다. 음수는 0 으로 둡니다.
         */
        void applyMoveScale( float32 scale );
        /** @brief 지난 물리 프레임에 걷는 속도에 곱한 계수입니다(진단 · 감시 값). */
        float32 getMoveScale() const { return _reportedMoveScale.load( std::memory_order_relaxed ); }
        /**
         * @brief 이번 게임 프레임에 받는 물리 필드(중력 배율 · 가속 · 흐름 · 저항 · 부력 · 마찰)를 얹습니다. 여러 지역이면 `PhysicsFieldSample::combine` 으로 합친다.
         * @details 물리 프레임이 시작할 때(`onPhysicsFrameBegin`) 모은 값을 그 프레임의 필드로 옮기고 비운다 — 프레임마다 다시 받아야 이어지고, 스텝이 없는 프레임이 끼어도
         *          한 프레임의 값이 두 번 쌓이지 않는다. 중립 필드면 이동은 필드가 없을 때와 같다. 아무 스레드에서 불러도 된다.
         */
        void applyPhysicsField( const PhysicsFieldSample& sample );
        /** @brief 지난 물리 프레임에 쓴 필드입니다(진단 · 감시 값). */
        const PhysicsFieldSample& getPhysicsField() const { return _frameField; }
        /** @brief 지난 스텝이 고른 이동 방식입니다(걷기 · 낙하 · 수영 · 부유 · 오르기). */
        CharacterMovementMode getMovementMode() const { return _movementMode; }
        /** @brief 마지막 스텝에 바닥을 딛고 있었는지입니다. */
        bool isGrounded() const { return _bGrounded; }
        /** @brief 마지막 스텝의 바닥 법선입니다. */
        float3 getGroundNormal() const { return _state._groundNormal; }
        /** @brief 마지막 스텝에서 실제로 난 속도입니다(막힌 몫은 빠진다 — 서 있으면 0). */
        float3 getVelocity() const { return _state._velocity; }
        /** @brief 마지막 스텝에 내려 한 속도입니다(걷기 · 필드 · 공중 관성 · 수직 — 막히기 전). 벽에 부딪히는 세기처럼 막힌 몫이 필요한 쪽이 읽는다. 게임 스레드에서 부릅니다. */
        float3 getRequestedStepVelocity() const;
        /** @brief 마지막 스텝 뒤의 발 자리입니다(보간 전). */
        float3 getSimulatedPosition() const { return _state._position; }
        /** @brief 캐릭터 핸들입니다(시작 전이면 무효). */
        PhysicsCharacterHandle getCharacterHandle() const { return _character; }
        /** @brief 마지막 스텝의 이동 결과 전부입니다(딛은 바디 · 실어 나른 속도 · 바닥 회전 포함). 이동 디버거가 읽는다. */
        const PhysicsCharacterState3D& getCharacterState() const { return _state; }
        /** @brief 조종이 요청한 수평 속도(`setMoveVelocity`)입니다 — 루트 모션 · 면 속도 · 공중 관성을 더하기 전 값이라 "요청 → 실제" 를 비교한다. 아무 스레드에서 읽어도 된다. */
        float3 getRequestedMoveVelocity() const;

        static constexpr float32 kFrictionResponse    = 10.0f; ///< 접지 1 일 때 걷는 속도가 요청으로 가는 빠르기(1/s) — 접지에 곱한다(빙판 0.05 면 0.5/s)
        static constexpr float32 kFieldGroundDamping  = 4.0f;  ///< 디딘 채로 필드가 쌓은 수평 속도가 줄어드는 빠르기(1/s, 접지에 곱한다)
        static constexpr float32 kSpotGroundClearance = 0.05f; ///< 빈 자리를 볼 때 발을 바닥에서 띄우는 높이(m) — 바닥과 겹치지 않게
        static constexpr int32   kSpotDirectionCount  = 8;     ///< 빈 자리를 찾을 때 고리마다 보는 방향 수
        static constexpr float32 kBodyDensity         = 1.0f;  ///< 캐릭터 몸의 밀도(물 = 1) — 유체 밀도가 이보다 크면 떠오른다

        /** @brief 스텝 기록 한 칸입니다(이동 디버거가 미끄러짐 · 벽 걸림 · 착지 판정을 전후로 본다). */
        struct StepRecord
        {
            float3                  _stepVelocity{}; ///< 이 스텝이 백엔드에 넘긴 속도(요청 + 루트 모션 + 면 + 공중 관성, 수직 포함)
            PhysicsCharacterState3D _result{};       ///< 그 결과
        };
        /** @brief 스텝 기록의 칸 수입니다(고정 용량). */
        static constexpr uint32 kStepHistoryCapacity = 8;
        /**
         * @brief 모든 캐릭터의 스텝 기록을 켜거나 끕니다(개발 명령 `movement.history on|off`). 꺼져 있으면 스텝마다 분기 하나뿐이고 칸을 만들지 않는다.
         * @details 언리얼 CharacterMovement 의 `p.VisualizeMovement` 처럼 전역 스위치다. 켜면 캐릭터마다 첫 스텝에 칸 `kStepHistoryCapacity` 개를 한 번 만든다.
         */
        static void setStepHistoryEnabled( bool bEnabled );
        static bool isStepHistoryEnabled();
        /** @brief 최근 스텝 기록(오래된 것부터)을 @p outListRecord 에 채웁니다(먼저 비운다). 기록이 꺼져 있었으면 비어 있다. 게임 스레드에서 물리 스텝 밖에 부른다. */
        void collectStepHistory( vector<StepRecord>& outListRecord ) const;
        /** @brief 상태 해시에 트랜스폼과 속도 · 접지를 접습니다. */
        void foldStateHash( StateHashBuilder& inoutBuilder ) const override;

        /**
         * @brief 월드 배율(마지막 물리 프레임에 읽은 값)을 굽은 충돌 캡슐입니다 — 백엔드 캐릭터가 이 크기로 지어진다. 반지름은 XZ 중 큰 축, 높이는 Y 다.
         * @details 캡슐 크기로 질의 · 판정하는 코드는 속성(`getRadius` · `getHalfHeight`)이 아니라 이것을 읽는다(언리얼 `GetScaledCapsuleRadius` 와 같은 나눔).
         */
        PhysicsShapeDesc3D getScaledCapsule() const;
        /**
         * @brief 캡슐 키를 런타임에 @p meters 만큼 줄입니다(발은 그대로, 머리가 내려온다 — 지은 반 높이 칸 `_halfHeight` 는 바꾸지 않는다). 0 이면 원래 키입니다.
         * @details 언리얼 `Crouch` 가 캡슐 반 높이를 줄이고 기본값을 남기는 것과 같다 — 늪에 빠진 캐릭터 · 웅크림이 판정 캡슐을 낮춘다. 캐릭터는 다시 짓지 않고 다음 물리 프레임에 크기만 간다(`setCharacterSize`).
         *          어느 스레드에서 불러도 된다(물리 프레임 시작이 읽는다). 줄인 키는 캡슐의 원기둥 길이까지다(반구 둘은 남는다).
         */
        void    setHeightReduction( float32 meters ) { _requestedHeightReduction.store( meters, std::memory_order_relaxed ); }
        float32 getHeightReduction() const { return _requestedHeightReduction.load( std::memory_order_relaxed ); }
        float32 getRadius() const { return _radius; }
        void    setRadius( float32 radius );
        float32 getHalfHeight() const { return _halfHeight; }
        void    setHalfHeight( float32 halfHeight );
        float32 getStepHeight() const { return _stepHeight; }
        void    setStepHeight( float32 stepHeight );
        float32 getMaxSlopeAngle() const { return _maxSlopeAngle.radians(); }
        void    setMaxSlopeAngle( float32 angle );

        /**
         * @brief 이 캐릭터의 걸음을 물리 고정 스텝 대신 부른 쪽이 `simulateMotion` 으로 돌립니다(유니티 `Physics.simulationMode = Script`, Godot 의 손 `move_and_slide` 자리). 게임 스레드, 틱 밖에서 부릅니다.
         * @details 끄면 다음 물리 프레임부터 지금 상태에서 고정 스텝이 잇는다. 바뀔 때 쓰지 않은 한 번짜리 요청(점프 · 발사 · 루트 모션 …)은 버리고 이동 요청만 남긴다.
         */
        void setManualSimulation( bool bManual );
        bool isManualSimulation() const { return _bManualSimulation == SW_TRUE; }
        /** @brief 지금 운동 상태입니다. 캐릭터가 아직 없으면(시작 전 · 꺼짐) 자리는 트랜스폼의 것입니다. 자기 오브젝트의 틱이나 틱 밖 게임 스레드에서 부릅니다. */
        CharacterMotionState getMotionState() const;
        /**
         * @brief 운동 상태를 되돌립니다 — 자리를 옮기고 닿은 것 · 바닥을 그 자리에서 다시 찾습니다. 게임 스레드, 틱 밖에서 부릅니다.
         * @details 캐릭터가 아직 없으면 트랜스폼을 순간이동해 다음 물리 프레임이 그 자리에 짓습니다.
         */
        void setMotionState( const CharacterMotionState& state );
        /**
         * @brief @p state 에서 @p input 으로 한 걸음(@p deltaTime) 갑니다 — 끝 상태가 이 캐릭터의 상태가 됩니다. 손으로 돌리는 캐릭터만 부릅니다(게임 스레드, 틱 · 물리 스텝 밖).
         * @details 걸음 앞에 늘 @p state 로 되돌리므로(`setMotionState`) 같은 (상태, 입력, 세계)면 이어 돌린 것과 되감아 다시 돌린 것이 비트까지 같다. 지역 필드 · 표면 계수 · 바닥은 **지금** 세계의
         *          것이다 — 언리얼이 저장한 이동을 지금 세계에서 다시 돌리는 것과 같다(세계가 달랐으면 서버 상태가 다시 맞춘다).
         *          캐릭터가 아직 없으면(첫 물리 프레임 전 · 꺼짐) @p state 를 그대로 돌려준다.
         */
        CharacterMotionState simulateMotion( const CharacterMotionState& state, const CharacterMotionInput& input, float32 deltaTime, CharacterMotionPass pass );
        /** @brief 이동 입력을 다음 물리 프레임의 요청으로 겁니다(`setMoveVelocity` · `jump` … 를 한 번에). 고정 스텝으로 도는 캐릭터의 이동기가 부릅니다(아무 스레드). */
        void applyMotionInput( const CharacterMotionInput& input );
        /** @brief 손으로 돌리는 캐릭터를 그릴 자리입니다 — 지난 걸음 자리 @p previousPosition 과 지금 자리 사이 @p alpha, 거기에 @p offset(예측 보정 녹이기)을 더합니다. 게임 스레드. */
        void setMotionDisplay( const float3& previousPosition, float32 alpha, const float3& offset );
        /**
         * @brief 이 캡슐을 발 자리 @p feet 에 세우면 무엇과도 겹치지 않는지 봅니다(트리거 · 제 속 바디는 빼고 본다). 물리 씬이 없으면 빈 자리로 봅니다. 게임 스레드, 틱 밖에서 부릅니다.
         * @details 내릴 자리(`VehicleSeatComponent`) · 세울 자리(서버의 폰)가 함께 쓴다 — 언리얼 `UWorld::EncroachingBlockingGeometry` 자리다.
         */
        [[nodiscard]] bool isSpotFree( const float3& feet ) const;
        /** @brief @p feet 가 비었으면 그 자리, 아니면 @p searchRadius 안의 가까운 고리부터 본 빈 자리입니다(언리얼 `FindTeleportSpot`). 없으면 비어 있습니다. */
        [[nodiscard]] optional<float3> findFreeSpot( const float3& feet, float32 searchRadius ) const;
        /**
         * @brief 몸의 치수 · 이동 튜닝(반지름 · 키 · 턱 · 비탈 · 중력 · 질량 · 충돌 프로파일)을 @p source 에서 옮깁니다.
         * @details 같은 걸음을 내야 하는 다른 몸(서버가 접속마다 세운 폰)이 데이터를 두 벌 두지 않게 한다. 다음 물리 프레임에 캐릭터를 다시 짓는다.
         */
        void copyTuning( const CharacterControllerComponent& source );

    protected:
        /** @brief `stopMovementImmediately` 입니다(`TeleportType::ResetPhysics`). */
        void resetMotion() override { stopMovementImmediately(); }
        /** @brief 캐릭터는 바닥 없이 떨어진다. */
        bool needsStreamingFloor() const override { return true; }
        /** @brief 발이 원점인 캡슐 하나입니다. */
        void collectStreamingFloorShapes( const float3& scale, vector<PhysicsShapeDesc3D>& outListShape ) const override;
        /** @brief 쓰는 고정 스텝 그룹마다 메서드를 등록합니다(`registerFixedStep`). */
        void onRegister( GameObjectManager& manager ) override;
        void onPhysicsFrameBegin( const FixedStepContext& context );
        /** @brief 이번 스텝의 속도를 정해 씬에 이동을 넣습니다(`ScenePhysics::queueCharacterMove` — 씬이 모든 캐릭터를 한 번에 옮긴다, 감사 S-8). */
        void onPrePhysicsStep( const FixedStepContext& context );
        /** @brief 옮긴 결과(`getCharacterState`)를 읽어 수직 속도 · 공중 관성 · 이동 방식 · 바닥 회전을 잇습니다. */
        void onPostPhysicsStep( const FixedStepContext& context );
        void onPhysicsFrameEnd( const FixedStepContext& context );
        void releasePhysics( ScenePhysics& physics ) override;

    private:
        /** @brief `_layer` 가 물리 레이어 표에 있는지 봅니다(`PhysicsNameValidation`). */
        void validateLayer( ValidationContext& context ) const;
        /** @brief 입력 칸을 잠그고 @p function( CharacterCommand3D& ) 을 부릅니다(아무 스레드). */
        template <typename TFunction>
        void editCommand( TFunction&& function ) const;
        /**
         * @brief 한 걸음의 속도를 정합니다 — 요청 @p command 와 이번 프레임의 면 속도 · 루트 모션 · 필드 · 오르기(`_frame*`), 이동 계수 @p moveScale 을 이어받은 운동에 얹는다. 백엔드에 넘길 속도를 돌려준다.
         * @details 물리 고정 스텝(`onPrePhysicsStep`)과 손으로 돌리는 걸음(`simulateMotion`)이 함께 쓴다.
         */
        float3 beginStep( const CharacterCommand3D& command, float32 moveScale, float32 gravityY, float32 fixedDeltaTime );
        /** @brief 옮긴 결과(`_state`)에서 이동 방식 · 수직 속도 · 공중 관성 · 바닥 회전을 잇습니다. */
        void finishStep( float32 fixedDeltaTime );

    private:
        PROPERTY( Category = "Character", Min = 0.01, Tooltip = "Capsule radius", Units = m, Meta = "Shape=Capsule, ShapeRange=_halfHeight, ShapeOrigin=Base, ShapeScale=Physics" )
        float32 _radius;
        PROPERTY( Category = "Character", Min = 0.0, Tooltip = "Half the cylinder part; height = 2 * (half height + radius)", Units = m )
        float32 _halfHeight;
        PROPERTY( Category = "Character", Min = 0.0, Tooltip = "Highest step the character walks up", Units = m )
        float32 _stepHeight;
        PROPERTY( Category = "Character", Min = 0.0, Max = 89.95, Tooltip = "Steepest walkable floor" )
        Radians _maxSlopeAngle;
        PROPERTY( Category = "Character", Tooltip = "Multiplier on scene gravity (0 floats)" )
        float32 _gravityScale;
        PROPERTY( Category = "Character", Min = 0.0, Tooltip = "Mass used to push dynamic bodies", Units = kg )
        float32 _mass;
        PROPERTY( DefaultName, Category = "Character", DisplayName = "Collision Profile", Tooltip = "Collision profile name from the physics settings (object channel and responses)", Validate = validateLayer, Meta = "NameSource=CollisionProfile" )
        hashed_string _layer{ "Character" };
        /// 기본 켬(감사 S-7) — 트리거 · 천이 캐릭터를 느낀다. 캐릭터끼리 막는 것은 씬의 캐릭터 대 캐릭터 충돌이 한다(속 바디와 상관없다).
        PROPERTY( Category = "Character", DisplayName = "Inner Body", Tooltip = "Keep a kinematic capsule body on the character so triggers, cloth and sensors feel it (it also pushes dynamic bodies harder)" )
        bool _bInnerBody;
        // 이어받는 운동 상태 — 상태 칸이라 스냅샷 복원 · 상태 해시가 싣는다(복원한 장면이 새로 지은 캐릭터가 같은 궤적을 잇는다). 물리 스텝만 쓴다(`onPrePhysicsStep` · `onPostPhysicsStep`).
        PROPERTY( RuntimeState, Category = "Character", Tooltip = "Runtime: walking velocity of the last step" )
        float3 _walkVelocity; ///< 지난 스텝에 실제로 낸 걷는 속도 — 접지가 낮으면(빙판) 요청 속도로 천천히 간다
        PROPERTY( RuntimeState, Category = "Character", Tooltip = "Runtime: horizontal velocity gathered from fields" )
        float3 _fieldVelocity; ///< 필드의 가속 · 흐름이 쌓은 수평 속도(당김 · 해류) — 디디면 접지만큼 줄어든다
        PROPERTY( RuntimeState, Category = "Character", Tooltip = "Runtime: horizontal velocity kept in the air (launch, leaving a moving floor)" )
        float3 _airVelocity; ///< 발사 · 떠난 바닥의 수평 속도 — 땅에 다시 닿으면 0
        PROPERTY( RuntimeState, Category = "Character", Tooltip = "Runtime: vertical speed" )
        float32 _verticalSpeed;
        PROPERTY( RuntimeState, Category = "Character", Tooltip = "Runtime: movement mode of the last step" )
        CharacterMovementMode _movementMode;
        PROPERTY( RuntimeState, Category = "Character", Tooltip = "Runtime: stood on a floor after the last step" )
        bool _bGrounded; ///< 마지막 스텝 뒤 바닥을 딛고 있었다(`_state._bGrounded` — 복원한 뒤 첫 스텝 앞에도 이동기가 읽는다)

        PhysicsCharacterHandle                  _character;
        PhysicsCharacterState3D                 _state;
        PhysicsCommandPage<CharacterCommand3D>* _pCommandPage;                 ///< 입력 칸이 든 페이지 — 이동 요청 · 대기(`PhysicsPoseStore::getCommandStore`)
        uint32                                  _commandSlot;                  ///< 입력 칸 번호(컴포넌트 수명 동안 그대로)
        ThreadRole                              _physicsStep;                  ///< 물리 스텝 역할 — 이번 프레임 속도 · 공중 속도는 onPrePhysicsStep 만 만진다(잠금이 아니라 분석용 능력)
        float3 _frameRootMotionVelocity         SW_GUARDED_BY( _physicsStep ); ///< 이번 물리 프레임의 스텝마다 더할 루트 모션 속도
        float3 _frameSurfaceVelocity            SW_GUARDED_BY( _physicsStep ); ///< 이번 물리 프레임의 면 속도
        float32 _frameMoveScale                 SW_GUARDED_BY( _physicsStep ); ///< 이번 물리 프레임의 이동 계수
        atomic<float32>                         _reportedMoveScale;            ///< 진단 · 감시 값이 읽는 사본(`getMoveScale`) — 물리 스텝이 쓰고 게임 스레드가 읽는다
        PhysicsFieldSample                      _frameField;                   ///< 이번 물리 프레임의 필드(`onPhysicsFrameBegin` 이 옮긴다)
        float3                                  _frameClimbVelocity;           ///< 이번 물리 프레임의 오르기 속도(`_bFrameClimb` 일 때)
        float3 _queuedStepVelocity              SW_GUARDED_BY( _physicsStep ); ///< 이번 스텝에 넣은 이동 속도(수직 포함 — 스텝 기록용)
        float32 _queuedStepSeconds              SW_GUARDED_BY( _physicsStep ); ///< 이번 스텝에 넣은 이동의 길이(초). 0 이면 넣지 않았다 — `onPostPhysicsStep` 이 읽고 비운다
        float3                                  _previousPosition;
        float3                                  _worldScale;               ///< 트랜스폼의 월드 배율(변경 번호가 바뀔 때만 다시 읽는다)
        quaternion                              _worldRotation;            ///< 트랜스폼의 월드 회전(바닥이 돈 각을 얹어 쓴다). 변경 번호가 같으면 그대로다
        float32                                 _pendingGroundYaw;         ///< 마지막 쓰기 뒤 디딘 바닥이 위 축 둘레로 돈 각(rad) — 다음 트랜스폼 쓰기에 얹는다
        atomic<float32>                         _requestedHeightReduction; ///< 줄일 키(m, `setHeightReduction`) — 아무 스레드가 쓰고 물리 프레임 시작이 읽는다
        float32                                 _appliedHeightReduction;   ///< 백엔드 캡슐을 지은 줄인 키(m) — `getScaledCapsule` 이 뺀다
        vector<StepRecord>                      _listStepHistory;          ///< 스텝 기록 고리(`setStepHistoryEnabled` 가 켜졌을 때 처음 칸을 만든다 — 꺼져 있으면 비어 있다)
        uint32                                  _stepHistoryCount;         ///< 기록한 스텝의 누계(고리의 다음 칸은 `% kStepHistoryCapacity`)
        float3                                  _motionDisplayOffset;      ///< 손으로 돌리는 캐릭터의 그리기 자리에 더한다(`setMotionDisplay`)
        float32                                 _motionDisplayAlpha;       ///< 손으로 돌리는 캐릭터의 그리기 보간 비
        uint8                                   _bFrameClimb       : 1;    ///< 이번 물리 프레임에 오르기 요청이 있다
        uint8                                   _bStepClimb        : 1;    ///< 이번 스텝을 오르기로 움직였다(점프 · 발사가 없었다)
        uint8                                   _bManualSimulation : 1;    ///< 물리 고정 스텝 대신 `simulateMotion` 이 걸음을 돌린다
        [[maybe_unused]] uint8                  _reserved          : 5;
    };
} // namespace sw
