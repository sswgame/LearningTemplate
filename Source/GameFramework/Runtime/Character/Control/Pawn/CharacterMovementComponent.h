/**
 * @file CharacterMovementComponent.h
 * @brief 폰 이동입니다. 의도를 같은 오브젝트의 캐릭터 컨트롤러(걷기, 달리기, 점프, 가감속, 몸 방향)로 옮깁니다. 플레이어, NPC, 자동 플레이, 재생이 모두 이것으로 걷습니다.
 */
#pragma once
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct ControlIntent;
    struct MovementConstraint;
    class CharacterControllerComponent;

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
     * @class CharacterMovementComponent
     * @brief 의도를 같은 오브젝트의 캐릭터 컨트롤러로 옮깁니다. 걷기, 달리기(버튼 `_sprintButton` 누름), 점프(버튼 `_jumpButton` 발동, 땅에서만), 가감속, 몸 방향을 다룹니다.
     *        NPC 와 플레이어의 움직임이 같은 코드로 움직이게 하는 곳입니다. 언리얼 `UCharacterMovementComponent` 를 플레이어 입력과 경로 따라가기가 함께 쓰는 것과 같습니다.
     * @details 이동 방식은 컨트롤러의 방식(`CharacterMovementMode`)을 따른다 — 언리얼 `UCharacterMovementComponent` 가 방식마다 속도를 고르는 자리다. 헤엄칠 때는 `_swimSpeed` 로, 위아래(의도의 `_moveUp`,
     *          점프 누름은 위)로도 간다. 오르기 구역(`ClimbZoneComponent`)에 겹친 채 벽 쪽으로 밀거나 위를 누르면 오르고, 점프로 벽을 차고 놓는다. 수면에서 점프하면 물 밖으로 뛰어오른다.
     *          입력은 의도뿐이고 방식 상태(`_bClimbing` · `_climbReleaseLeft`)는 상태 칸이라 되감기 · 예측이 같은 의도 열로 같은 궤적을 낸다.
     *          PrePhysics 틱에서 폰의 의도를 읽고 `setMoveVelocity` · `jump` 를 부릅니다(컨트롤러 요청은 틱 안에서 불러도 된다). 수평 속도는 목표 속도로
     *          `_acceleration`(목표가 있을 때) · `_deceleration`(멈출 때)만큼 다가갑니다. 그래서 의도가 0/1 로 끊겨도 부드럽게 움직입니다.
     *          탑승 중(`setSuspended( true )`)에는 아무것도 하지 않습니다(컨트롤러 속도도 0 으로 둔다).
     */
    REFLECT( Category = "Control", DisplayName = "Character Pawn Movement", Tooltip = "Control intent -> character controller: walk, sprint, jump, facing" )
    class SW_GF_API CharacterMovementComponent : public Component
    {
    public:
        REFLECT_BODY();

        CharacterMovementComponent();
        ~CharacterMovementComponent() override = default;

        /** @brief 버튼 이름의 인덱스를 미리 찾아 둡니다. */
        void onBeginPlay() override;
        void onTick( TickContext& context ) override;

        /** @brief 멈춰 둡니다(탑승 · 연출). 멈추는 순간 수평 속도를 0 으로 둡니다. */
        void setSuspended( bool bSuspended );
        bool isSuspended() const { return _bSuspended == SW_TRUE; }
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

    private:
        /**
         * @brief 오르기 구역에 있고 매달려야 하면 이번 프레임의 오르기 속도를 컨트롤러에 넘기고 true 를 반환합니다.
         * @details 벽 쪽으로 미는 의도 + 위아래 의도가 오르는 세기다. 땅에 선 채 내려가는 쪽이면 매달리지 않는다(걸어 나간다). 점프는 벽을 차고 놓으며(밧줄은 가는 쪽으로) `_climbReleaseSeconds` 동안 다시 잡지 않는다.
         */
        [[nodiscard]] bool updateClimb( const TickContext& context, const ControlIntent& intent, const MovementConstraint& constraint, CharacterControllerComponent& controller );
        /** @brief 몸 요를 @p targetYaw 쪽으로 `_turnRate` 만큼 돌려 트랜스폼에 씁니다. */
        void turnToward( GameObject& owner, float32 targetYaw, float32 deltaTime );

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

        PROPERTY( RuntimeState, Category = "Movement", DisplayName = "Climb Release Left", Tooltip = "Runtime" )
        float32 _climbReleaseLeft;
        PROPERTY( RuntimeState, Category = "Movement", DisplayName = "Climbing", Tooltip = "Runtime" )
        bool _bClimbing;

        float3                 _velocity;  ///< 지금 수평 속도
        float32                _facingYaw; ///< 몸의 요
        optional<int32>        _sprintIndex;
        optional<int32>        _jumpIndex;
        uint8                  _bSuspended : 1;
        [[maybe_unused]] uint8 _reserved   : 7;
    };
} // namespace sw
