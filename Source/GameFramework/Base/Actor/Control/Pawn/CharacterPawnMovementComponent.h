/**
 * @file CharacterPawnMovementComponent.h
 * @brief 폰 이동 — 의도 → 같은 오브젝트의 캐릭터 컨트롤러(걷기 · 달리기 · 점프 · 가감속 · 몸 방향). 플레이어 · NPC · 자동 플레이 · 재생이 모두 이것으로 걷습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 몸이 어디를 보는가입니다. */
    ENUM()
    enum class PawnFacingMode : uint8
    {
        ControlYaw = 0, ///< 조종 요(1인칭 · 슈터 — 옆걸음)
        MoveDirection,  ///< 움직이는 쪽으로 돈다(3인칭 액션 · NPC)
    };
} // namespace sw

namespace sw
{
    /**
     * @class CharacterPawnMovementComponent
     * @brief 의도 → 같은 오브젝트의 캐릭터 컨트롤러입니다. 걷기 · 달리기(버튼 `_sprintButton` 누름) · 점프(버튼 `_jumpButton` 발동, 땅에서만) · 가감속 · 몸 방향.
     *        "NPC 와 플레이어의 움직임 차이가 없다" 의 자리입니다(언리얼 `UCharacterMovementComponent` 를 플레이어 입력과 경로 따라가기가 함께 쓰는 것과 같다).
     * @details PrePhysics 틱에서 폰의 의도를 읽고 `setMoveVelocity` · `jump` 를 부릅니다(컨트롤러 요청은 틱 안에서 불러도 된다). 수평 속도는 목표 속도로
     *          `_acceleration`(목표가 있을 때) · `_deceleration`(멈출 때)만큼 다가갑니다 — 의도가 0/1 로 끊겨도 미끄럽게.
     *          탑승 중(`setSuspended( true )`)에는 아무것도 하지 않습니다(컨트롤러 속도도 0 으로 둔다).
     */
    REFLECT( Category = "Control", DisplayName = "Character Pawn Movement", Tooltip = "Control intent -> character controller: walk, sprint, jump, facing" )
    class SW_GF_API CharacterPawnMovementComponent : public Component
    {
    public:
        REFLECT_BODY();

        CharacterPawnMovementComponent();
        ~CharacterPawnMovementComponent() override = default;

        /** @brief 버튼 이름의 자리를 풀어 둡니다. */
        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

        /** @brief 멈춰 둡니다(탑승 · 연출). 멈추는 순간 수평 속도를 0 으로 둡니다. */
        void setSuspended( bool bSuspended );
        bool isSuspended() const { return _bSuspended == SW_TRUE; }
        /** @brief 지금 수평 속도입니다(애니메이션 · 탐침이 읽는다). */
        const float3& getHorizontalVelocity() const { return _velocity; }
        /** @brief 몸의 요입니다(라디안). */
        float32 getFacingYaw() const { return _facingYaw; }

        float32        getWalkSpeed() const { return _walkSpeed; }
        void           setWalkSpeed( float32 speed ) { _walkSpeed = speed; }
        float32        getSprintSpeed() const { return _sprintSpeed; }
        void           setSprintSpeed( float32 speed ) { _sprintSpeed = speed; }
        void           setAcceleration( float32 acceleration ) { _acceleration = acceleration; }
        void           setDeceleration( float32 deceleration ) { _deceleration = deceleration; }
        PawnFacingMode getFacingMode() const { return _facingMode; }
        void           setFacingMode( PawnFacingMode facingMode ) { _facingMode = facingMode; }

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
        PROPERTY( Category = "Movement", DisplayName = "Turn Rate", Min = 0.0, Tooltip = "How fast the body turns to its facing", Units = "rad/s" )
        float32 _turnRate;
        PROPERTY( Category = "Movement", DisplayName = "Facing", Tooltip = "Body faces the control yaw (shooter) or the move direction (third person, NPC)" )
        PawnFacingMode _facingMode;

        float3                 _velocity;  ///< 지금 수평 속도
        float32                _facingYaw; ///< 몸의 요
        int32                  _sprintIndex;
        int32                  _jumpIndex;
        uint8                  _bSuspended : 1;
        [[maybe_unused]] uint8 _reserved   : 7;
    };
} // namespace sw
