/**
 * @file ShooterBodyMovementComponent.h
 * @brief 슈터 아레나의 몸 이동 — 폰의 의도를 읽어 걷기 · 달리기 · 점프 · 중력으로 움직이고 막는 상자에 미끄러집니다. 플레이어와 스켈레톤이 같이 씁니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "Games/Shooter3D/ShooterBlockerComponent.h"

namespace sw
{
    /** @brief 몸 이동 한 걸음의 입력입니다. 폰 의도에서 만들고(`ShooterBodyMovementComponent::makeInput`) 네트워크로도 보낸다(클라이언트 예측의 입력). */
    struct ShooterMoveInput
    {
        float3 _move{ 0.0f, 0.0f, 0.0f }; ///< 월드 XZ 이동(길이 1 이하)
        uint8  _bSprint{ SW_FALSE };
        uint8  _bJump{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 몸 이동의 상태입니다. 같은 상태와 입력이면 서버와 클라이언트가 같은 다음 상태를 낸다(`ShooterBodyMovementComponent::simulate`). */
    struct ShooterMoveState
    {
        float3  _position{ 0.0f, 0.0f, 0.0f }; ///< 발
        float32 _verticalSpeed{ 0.0f };
        uint8   _bOnGround{ SW_TRUE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ShooterBodyMovementComponent
     * @brief 같은 오브젝트의 `PawnComponent` 의도(이동 축 · 조종 요 · `_jumpButton` · `_sprintButton`)만 읽는 운동학 몸 이동입니다. 물리 바디 없이 아레나 상자 목록으로
     *        충돌합니다(`ShooterArenaMath::resolveCircle`, 바닥 y = 0). 플레이어 · 적이 같은 이 코드로 걷는다 — 누가 조종하는지(사람 · 자동 플레이 AI · 적 AI)는 모른다.
     * @details 스스로 틱하지 않습니다 — 같은 오브젝트의 규칙 컴포넌트(`ShooterPlayerComponent` · `ShooterEnemyComponent`)가 자기 틱에서 `stepMovement` 를 불러
     *          한 프레임 안의 순서(이동 → 사격 · 휘두르기 판정)를 정합니다. 자리는 이 컴포넌트가 듭니다(발). 오브젝트 변환에 쓰는 것은 부른 쪽입니다.
     *          멈춤(`setSuspended`)이면 의도를 버리고 제자리입니다(적이 일어나는 중 · 휘두르는 중 · 움찔).
     */
    REFLECT( Category = "Shooter3D", DisplayName = "Shooter Body Movement", Tooltip = "Kinematic walk, sprint, jump and gravity from the pawn intent, sliding on the arena boxes" )
    class ShooterBodyMovementComponent : public Component
    {
    public:
        REFLECT_BODY();

        ShooterBodyMovementComponent();
        virtual ~ShooterBodyMovementComponent() override = default;

        /**
         * @brief 이번 프레임의 의도로 한 걸음 갑니다(부른 쪽의 틱 — 자기 오브젝트). @p listBox 는 막는 상자입니다.
         * @return 이 걸음에서 땅에 내려앉았으면 true 입니다(착지음).
         */
        bool stepMovement( const vector<ShooterArenaBox>& listBox, float32 deltaTime );
        /** @brief 지금 폰 의도를 걸음 입력으로 만듭니다. 멈춤이거나 폰이 없으면 빈 입력입니다. */
        ShooterMoveInput makeInput() const;
        /**
         * @brief 한 걸음의 순수 계산입니다 — 이 컴포넌트의 속도 · 중력 · 반경으로 @p state 에 @p input 을 적용한 다음 상태를 돌려줍니다(멤버는 바꾸지 않는다).
         * @details `stepMovement` 와 네트워크 예측(`ShooterNetPawnComponent` — 서버 시뮬레이션 · 클라이언트 재시뮬레이션)이 이 한 함수를 쓴다. @p outbLanded 는 이 걸음에 내려앉았으면 true.
         */
        ShooterMoveState simulate( const vector<ShooterArenaBox>& listBox, const ShooterMoveState& state, const ShooterMoveInput& input, float32 deltaTime, bool& outbLanded ) const;
        ShooterMoveState getState() const;
        /** @brief 상태를 바꿉니다(네트워크가 정한 상태). @p moveVelocity 는 지난 걸음의 수평 속도입니다. */
        void setState( const ShooterMoveState& state, const float3& moveVelocity );
        /**
         * @brief 네트워크가 이 몸을 움직이는가입니다. 켜면 `stepMovement` 는 아무것도 하지 않는다 — 고정 틱의 네트워크 걸음(`ShooterNetPawnComponent`)이 `simulate` · `setState` 로 움직인다.
         * @details 한 프레임에 두 길이 같은 몸을 움직이지 않게 하는 스위치다.
         */
        void setNetDriven( bool bNetDriven ) { _bNetDriven = bNetDriven ? SW_TRUE : SW_FALSE; }
        bool isNetDriven() const { return _bNetDriven == SW_TRUE; }
        /** @brief 그릴 자리에 더하는 오프셋입니다(예측 보정을 녹이는 시각 보정 — `PredictionSmoother`). 시뮬레이션 자리는 바뀌지 않는다. */
        void   setDisplayOffset( const float3& offset ) { _displayOffset = offset; }
        float3 getDisplayFeetPosition() const { return _position + _displayOffset; }
        /** @brief 발을 @p feet 로 옮기고 속도를 0 으로 둡니다(스폰 · 판 다시 시작). */
        void teleport( const float3& feet );
        /** @brief 멈춰 둡니다 — 의도를 버리고 제자리(수평 속도 0). */
        void setSuspended( bool bSuspended ) { _bSuspended = bSuspended ? SW_TRUE : SW_FALSE; }
        bool isSuspended() const { return _bSuspended == SW_TRUE; }

        /** @brief 발 자리입니다. */
        const float3& getFeetPosition() const { return _position; }
        /** @brief 지난 걸음의 수평 속도(월드, m/s)입니다. */
        const float3& getMoveVelocity() const { return _moveVelocity; }
        bool          isOnGround() const { return _bOnGround == SW_TRUE; }
        float32       getRadius() const { return _radius; }
        float32       getWalkSpeed() const { return _walkSpeed; }
        /** @brief 걷기 빠르기를 바꿉니다(웨이브마다 빨라지는 적). */
        void setWalkSpeed( float32 speed ) { _walkSpeed = speed; }

    private:
        PROPERTY( Category = "Movement", DisplayName = "Sprint Button", Tooltip = "Intent button that sprints while held (empty: no sprint)" )
        hashed_string _sprintButton;
        PROPERTY( Category = "Movement", DisplayName = "Jump Button", Tooltip = "Intent button that jumps when triggered on the ground (empty: no jump)" )
        hashed_string _jumpButton;
        PROPERTY( Category = "Movement", DisplayName = "Walk Speed", Min = 0.0, Units = "m/s" )
        float32 _walkSpeed;
        PROPERTY( Category = "Movement", DisplayName = "Sprint Speed", Min = 0.0, Units = "m/s" )
        float32 _sprintSpeed;
        PROPERTY( Category = "Movement", DisplayName = "Jump Speed", Min = 0.0, Units = "m/s" )
        float32 _jumpSpeed;
        PROPERTY( Category = "Movement", DisplayName = "Gravity", Min = 0.0, Units = "m/s2" )
        float32 _gravity;
        PROPERTY( Category = "Movement", DisplayName = "Radius", Tooltip = "Body radius against the blockers", Min = 0.0, Units = m )
        float32 _radius;

        float3  _position;      ///< 발
        float3  _moveVelocity;  ///< 지난 걸음의 수평 속도
        float3  _displayOffset; ///< 그릴 때만 더하는 오프셋(예측 보정 녹이기)
        float32 _verticalSpeed;
        uint8   _bOnGround  : 1;
        uint8   _bSuspended : 1;
        uint8   _bNetDriven : 1;
        uint8   _reserved   : 5;
    };
} // namespace sw
