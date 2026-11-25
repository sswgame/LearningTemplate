/**
 * @file CharacterMovement2DComponent.h
 * @brief 2D 폰 이동입니다. 의도를 같은 오브젝트의 2D 캐릭터 컨트롤러(좌우 걷기, 점프, 가감속)로 옮깁니다.
 */
#pragma once
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct ControlIntent;
    struct MovementConstraint;
    class CharacterController2DComponent;

    /**
     * @class CharacterMovement2DComponent
     * @brief 의도의 좌우(`ControlIntent::_move` 의 X)를 2D 컨트롤러의 수평 속도로, 점프 버튼 발동을 점프로 옮깁니다. 3D 판(`CharacterMovementComponent`)과 규칙이 같고, 조종 요를 쓰지 않습니다.
     * @details 플레이어 · AI · 재생이 같은 의도로 같은 궤적을 낸다. 2D 컨트롤러(`CharacterController2DComponent`)는 물리 쪽 무버일 뿐이라 이 컴포넌트가 없으면 아무도 몰지 않았다.
     *          오르기 구역(`ClimbZoneComponent`)에 겹친 채 위아래(의도의 앞 축 + `_moveUp`)를 누르면 매달려 오르고, 옆은 걷는 속도로 움직여 사다리를 벗어나며, 점프로 놓는다.
     *          3D 처럼 벽을 보지 않는다 — 2D 사다리는 몸 뒤에 있다. 이어받는 상태(속도 · 매달림)는 상태 칸이다.
     *          유니티 2D Platformer `PlayerMovement` · Godot `CharacterBody2D` 예제 스크립트의 자리다(기억에 의존한 비교). PrePhysics 틱이다.
     */
    REFLECT( Category = "Control", DisplayName = "Character Pawn Movement 2D", Tooltip = "Control intent -> 2D character controller: walk and jump" )
    class SW_GF_API CharacterMovement2DComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PrePhysics;

        CharacterMovement2DComponent();
        ~CharacterMovement2DComponent() override = default;

        /** @brief 점프 버튼의 인덱스를 미리 찾아 둡니다. */
        void onBeginPlay() override;
        void onTick( TickContext& context ) override;

        /** @brief 수평 속도 @p current 를 의도 @p moveRight(−1..1) 쪽으로 이번 틱만큼 옮긴 값입니다. 의도가 있으면 @p acceleration, 없으면 @p deceleration 으로 다가갑니다. */
        static float32 computeVelocity( float32 current, float32 moveRight, float32 walkSpeed, float32 acceleration, float32 deceleration, float32 deltaTime );

        float32 getVelocity() const { return _velocity; }
        /** @brief 지난 틱에 오르기 구역에 매달려 있었으면 true 입니다. */
        bool isClimbing() const { return _bClimbing; }

    private:
        /** @brief 오르기 구역에 있고 매달려야 하면 이번 프레임의 오르기 속도를 컨트롤러에 넘기고 true 를 반환합니다. */
        [[nodiscard]] bool updateClimb( const TickContext& context, const ControlIntent& intent, const MovementConstraint& constraint, CharacterController2DComponent& controller );

    private:
        PROPERTY( Category = "Movement", DisplayName = "Jump Button", Tooltip = "Intent button that jumps when triggered (empty: no jump)" )
        hashed_string _jumpButton;
        PROPERTY( Category = "Movement", DisplayName = "Walk Speed", Min = 0.0, Units = "m/s" )
        float32 _walkSpeed;
        PROPERTY( Category = "Movement", DisplayName = "Acceleration", Min = 0.0, Tooltip = "How fast the speed rises toward the intent", Units = "m/s2" )
        float32 _acceleration;
        PROPERTY( Category = "Movement", DisplayName = "Deceleration", Min = 0.0, Tooltip = "How fast the speed falls when the intent stops", Units = "m/s2" )
        float32 _deceleration;
        PROPERTY( Category = "Movement", DisplayName = "Jump Speed", Min = 0.0, Units = "m/s" )
        float32 _jumpSpeed;
        PROPERTY( Category = "Movement", DisplayName = "Climb Release Time", Min = 0.0, Tooltip = "Time after a jump off before it can grab a climb zone again", Units = s )
        float32 _climbReleaseSeconds;
        PROPERTY( RuntimeState, Category = "Movement", DisplayName = "Velocity", Tooltip = "Runtime: horizontal velocity" )
        float32 _velocity; ///< 지금 수평 속도
        PROPERTY( RuntimeState, Category = "Movement", DisplayName = "Climb Release Left", Tooltip = "Runtime" )
        float32 _climbReleaseLeft;
        PROPERTY( RuntimeState, Category = "Movement", DisplayName = "Climbing", Tooltip = "Runtime" )
        bool _bClimbing;

        optional<int32> _jumpIndex;
    };
} // namespace sw
