/**
 * @file CharacterMovementComponent.h
 * @brief 폰 이동입니다. 의도를 같은 오브젝트의 캐릭터 컨트롤러(걷기, 달리기, 점프, 가감속, 몸 방향)로 옮깁니다. 플레이어, NPC, 자동 플레이, 재생이 모두 이것으로 걷습니다.
 */
#pragma once
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneTeleportCursor.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct ControlIntent;
    struct MovementConstraint;

    /** @brief 몸이 어디를 보는가입니다. */
    ENUM()
    enum class PawnFacingMode : uint8
    {
        ControlYaw = 0, ///< 조종 요(1인칭과 슈터의 옆걸음)
        MoveDirection,  ///< 움직이는 쪽으로 돈다(3인칭 액션 · NPC)
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 폰 이동이 다음 걸음으로 이어 가는 것 전부입니다 — 컨트롤러의 운동과 이동기의 가감속 · 몸 요 · 매달림. 같은 (상태, 의도, 세계)면 같은 다음 상태다(`CharacterMovementComponent::simulate`).
     * @details 언리얼 `FSavedMove_Character` · 유니티 Netcode 의 예측 고스트 칸 자리다. 칸 선언이 네트워크 바이트이고(`ReplicatedLayout`), 예측이 서버와 같은 값에서 시작해야 하므로 양자화하지 않는다.
     */
    REFLECT()
    struct SW_GF_API CharacterMoveState
    {
        REFLECT_BODY();

        PROPERTY( Replicated )
        CharacterMotionState _motion{};

        PROPERTY( Replicated, Min = -10000, Max = 10000 )
        float3 _velocity{}; ///< 이동기의 수평 속도(가감속)

        PROPERTY( Replicated, Min = 0, Max = 1000 )
        float32 _climbReleaseLeft{ 0.0f };

        PROPERTY( Replicated, Min = -3.2, Max = 3.2 )
        float32 _facingYaw{ 0.0f }; ///< 몸의 요([-π, π] 로 감은 값)

        PROPERTY( Replicated, Min = 0, Max = 1 )
        uint8 _bClimbing{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class CharacterMovementComponent
     * @brief 의도를 같은 오브젝트의 캐릭터 컨트롤러로 옮깁니다. 걷기, 달리기(버튼 `_sprintButton` 누름), 점프(버튼 `_jumpButton` 발동, 땅에서만), 가감속, 몸 방향을 다룹니다.
     *        NPC 와 플레이어의 움직임이 같은 코드로 움직이게 하는 곳입니다. 언리얼 `UCharacterMovementComponent` 를 플레이어 입력과 경로 따라가기가 함께 쓰는 것과 같습니다.
     * @details 이동 방식은 컨트롤러의 방식(`CharacterMovementMode`)을 따른다 — 언리얼 `UCharacterMovementComponent` 가 방식마다 속도를 고르는 자리다. 헤엄칠 때는 `_swimSpeed` 로, 위아래(의도의 `_moveUp`,
     *          점프 누름은 위)로도 간다. 오르기 구역(`ClimbZoneComponent`)에 겹친 채 벽 쪽으로 밀거나 위를 누르면 오르고, 점프로 벽을 차고 놓는다. 수면에서 점프하면 물 밖으로 뛰어오른다.
     *          입력은 의도뿐이고 방식 상태(`_bClimbing` · `_climbReleaseLeft`)는 상태 칸이라 되감기 · 예측이 같은 의도 열로 같은 궤적을 낸다.
     *          PrePhysics 틱에서 폰의 의도를 읽고 `setMoveVelocity` · `jump` 를 부릅니다(컨트롤러 요청은 틱 안에서 불러도 된다). 수평 속도는 목표 속도로
     *          `_acceleration`(목표가 있을 때) · `_deceleration`(멈출 때)만큼 다가갑니다. 그래서 의도가 0/1 로 끊겨도 부드럽게 움직입니다.
     *          컨트롤러가 멈춰 있으면(`CharacterControllerComponent::suspend` — 탑승 · 래그돌) 걷지 않습니다(이어 오던 속도와 컨트롤러 요청을 0 으로 둔다).
     *          **네트워크 예측**(`setNetDriven`): 켜면 틱이 아무것도 하지 않고, 고정 넷 틱의 걸음(`simulate`)이 컨트롤러까지 움직인다 — 서버 시뮬레이션 · 클라이언트 예측 · 되감아 다시 돌리기가
     *          이 한 함수다(`ShooterBodyMovementComponent::simulate` 와 같은 계약, `ClientPrediction` 이 쓴다). 틱과 걸음이 의도를 이동 입력으로 바꾸는 계산(`advance`)도 하나다.
     */
    REFLECT( Category = "Control", DisplayName = "Character Pawn Movement", Tooltip = "Control intent -> character controller: walk, sprint, jump, facing" )
    class SW_GF_API CharacterMovementComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PrePhysics;

        CharacterMovementComponent();
        ~CharacterMovementComponent() override = default;

        /** @brief 버튼 이름의 인덱스를 미리 찾아 둡니다. */
        void onBeginPlay() override;
        void onTick( TickContext& context ) override;

        /** @brief 지난 틱에 오르기 구역에 매달려 있었으면 true 입니다. */
        bool isClimbing() const { return _bClimbing; }
        /** @brief 지금 수평 속도입니다(애니메이션 · 감시 값이 읽는다). */
        const float3& getHorizontalVelocity() const { return _velocity; }
        /** @brief 몸의 요입니다(라디안). */
        float32 getFacingYaw() const { return _facingYaw; }
        /** @brief 몸의 요를 바로 둡니다(텔레포트 도착). 다음 틱부터 그 요에서 돕니다. 게임 스레드(틱 밖)에서 부릅니다. */
        void setFacingYaw( float32 facingYaw );

        float32        getWalkSpeed() const { return _walkSpeed; }
        void           setWalkSpeed( float32 speed ) { _walkSpeed = speed; }
        float32        getSprintSpeed() const { return _sprintSpeed; }
        void           setSprintSpeed( float32 speed ) { _sprintSpeed = speed; }
        void           setAcceleration( float32 acceleration ) { _acceleration = acceleration; }
        void           setDeceleration( float32 deceleration ) { _deceleration = deceleration; }
        PawnFacingMode getFacingMode() const { return _facingMode; }
        void           setFacingMode( PawnFacingMode facingMode ) { _facingMode = facingMode; }

        /** @brief 고정 넷 틱이 이 몸을 움직이는가입니다. 컨트롤러도 함께 손으로 돌린다(`CharacterControllerComponent::setManualSimulation`). 게임 스레드, 틱 밖에서 부릅니다. */
        void setNetDriven( bool bNetDriven );
        bool isNetDriven() const { return _bNetDriven == SW_TRUE; }
        /** @brief 지금 이동 상태입니다. */
        CharacterMoveState getState() const;
        /** @brief 이동 상태를 바꿉니다(서버가 정한 상태 · 재조정 결과) — 컨트롤러의 자리와 몸 요까지. 게임 스레드, 틱 밖에서 부릅니다. */
        void setState( const CharacterMoveState& state );
        /**
         * @brief @p state 에서 의도 @p intent 로 한 걸음(@p deltaTime)의 다음 상태를 냅니다. 네트가 움직이는 몸만 부릅니다(게임 스레드, 틱 · 물리 스텝 밖).
         * @details 컨트롤러는 끝 상태에 남고 이 컴포넌트의 멤버는 바꾸지 않는다 — 실제 걸음이든 다시 돌린 걸음이든 끝에 `setState` 로 한 번 정한다.
         *          이동 제약(이펙트) · 오르기 구역 · 물리 필드는 지금 값이다(`CharacterControllerComponent::simulateMotion` 과 같은 결정).
         */
        CharacterMoveState simulate( const CharacterMoveState& state, const ControlIntent& intent, float32 deltaTime, CharacterMotionPass pass );
        /** @brief 이동 튜닝(빠르기 · 가감속 · 점프 · 오르기 · 조향 · 버튼)을 @p source 에서 옮깁니다 — 서버가 접속마다 세운 폰이 그 플레이어의 몸과 같은 걸음을 내게 한다. 버튼 번호도 다시 찾는다. */
        void copyTuning( const CharacterMovementComponent& source );

    private:
        /** @brief 의도 한 걸음을 이동 입력으로 바꾸며 @p inoutState 의 가감속 · 매달림 · 몸 요를 나아갑니다. 컨트롤러의 운동(`_motion`)은 읽기만 합니다. 멤버는 바꾸지 않습니다. */
        CharacterMotionInput advance( CharacterMoveState& inoutState, const ControlIntent& intent, const MovementConstraint& constraint, const CharacterControllerComponent& controller,
                                      float32 deltaTime ) const;
        /**
         * @brief 오르기 구역에 있고 매달려야 하면 이번 걸음의 오르기 입력을 @p outInput 에 채우고 true 를 반환합니다.
         * @details 벽 쪽으로 미는 의도 + 위아래 의도가 오르는 세기다. 땅에 선 채 내려가는 쪽이면 매달리지 않는다(걸어 나간다). 점프는 벽을 차고 놓으며(밧줄은 가는 쪽으로) `_climbReleaseSeconds` 동안 다시 잡지 않는다.
         */
        [[nodiscard]] bool advanceClimb( CharacterMoveState& inoutState, const ControlIntent& intent, const MovementConstraint& constraint, float32 deltaTime, CharacterMotionInput& outInput ) const;
        /** @brief 몸 요 @p facingYaw 를 @p targetYaw 쪽으로 `_turnRate` 만큼 돌린 값입니다. 거의 같으면 그대로입니다. */
        float32 turnToward( float32 facingYaw, float32 targetYaw, float32 deltaTime ) const;
        /** @brief 몸 요를 바꾸고 바뀌었으면 트랜스폼에 씁니다. */
        void applyFacingYaw( float32 facingYaw );
        /** @brief 버튼 이름의 번호를 폰 스키마에서 찾아 둡니다. */
        void resolveButtons();

    private:
        PROPERTY( Category = "Movement", DisplayName = "Sprint Button", Tooltip = "Intent button that sprints while held (empty: no sprint)" )
        hashed_string _sprintButton;
        PROPERTY( Category = "Movement", DisplayName = "Jump Button", Tooltip = "Intent button that jumps when triggered (empty: no jump)" )
        hashed_string _jumpButton;
        PROPERTY( Category = "Movement", DisplayName = "Walk Speed", Min = 0.0, Units = "m/s" )
        float32 _walkSpeed;
        PROPERTY( Category = "Movement", DisplayName = "Sprint Speed", Min = 0.0, Units = "m/s" )
        float32 _sprintSpeed;
        PROPERTY( Category = "Movement", DisplayName = "Acceleration", Min = 0.0, Tooltip = "How fast the speed rises toward the intent", Units = "m/s2" )
        float32 _acceleration;
        PROPERTY( Category = "Movement", DisplayName = "Deceleration", Min = 0.0, Tooltip = "How fast the speed falls when the intent stops", Units = "m/s2" )
        float32 _deceleration;
        PROPERTY( Category = "Movement", DisplayName = "Jump Speed", Min = 0.0, Units = "m/s" )
        float32 _jumpSpeed;
        PROPERTY( Category = "Movement", DisplayName = "Swim Speed", Min = 0.0, Tooltip = "Speed in a fluid, sideways and up or down", Units = "m/s" )
        float32 _swimSpeed;
        PROPERTY( Category = "Movement", DisplayName = "Climb Push Off Speed", Min = 0.0, Tooltip = "Horizontal speed of a jump off a ladder, rope or climbable wall", Units = "m/s" )
        float32 _climbPushOffSpeed;
        PROPERTY( Category = "Movement", DisplayName = "Climb Release Time", Min = 0.0, Tooltip = "Time after a jump off before it can grab a climb zone again", Units = s )
        float32 _climbReleaseSeconds;
        PROPERTY( Category = "Movement", DisplayName = "Turn Rate", Min = 0.0, Tooltip = "How fast the body turns to its facing", Units = "rad/s" )
        float32 _turnRate;
        PROPERTY( Category = "Movement", DisplayName = "Facing", Tooltip = "Body faces the control yaw (shooter) or the move direction (third person, NPC)" )
        PawnFacingMode _facingMode;

        PROPERTY( RuntimeState, Category = "Movement", DisplayName = "Velocity", Tooltip = "Runtime: horizontal velocity" )
        float3 _velocity; ///< 지금 수평 속도 — 상태 칸이라 스냅샷에서 돌아와도 같은 가감속을 잇는다
        PROPERTY( RuntimeState, Category = "Movement", DisplayName = "Climb Release Left", Tooltip = "Runtime" )
        float32 _climbReleaseLeft;
        PROPERTY( RuntimeState, Category = "Movement", DisplayName = "Climbing", Tooltip = "Runtime" )
        bool _bClimbing;

        SceneTeleportCursor    _teleport;  ///< 운동을 지우는 순간이동이면 걷는 속도 · 매달림도 지운다
        float32                _facingYaw; ///< 몸의 요
        optional<int32>        _sprintIndex;
        optional<int32>        _jumpIndex;
        uint8                  _bNetDriven : 1; ///< 고정 넷 틱이 움직인다(`setNetDriven`)
        [[maybe_unused]] uint8 _reserved   : 7;
    };
} // namespace sw
