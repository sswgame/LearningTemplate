#include "pch.h"

#include "Games/Playground/Shooter3D/ShooterBodyMovementComponent.h"

#include "Core/Delegate/Delegate.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"

#include "Games/Playground/Shooter3D/ShooterGameModeComponent.h"

namespace sw
{
    ShooterBodyMovementComponent::ShooterBodyMovementComponent()
        : _sprintButton{}
        , _jumpButton{}
        , _walkSpeed{ 5.5f }
        , _sprintSpeed{ 8.5f }
        , _jumpSpeed{ 6.0f }
        , _gravity{ 18.0f }
        , _radius{ 0.35f }
        , _position{ 0.0f, 0.0f, 0.0f }
        , _moveVelocity{ 0.0f, 0.0f, 0.0f }
        , _displayOffset{ 0.0f, 0.0f, 0.0f }
        , _verticalSpeed{ 0.0f }
        , _teleportCount{ 0 }
        , _bOnGround{ SW_TRUE }
        , _bNetDriven{ SW_FALSE }
        , _reserved{ 0 }
        , _suspendCount{ 0 }
        , _nextSuspensionID{ 1 }
        , _suspensionSubscriptions{ SW_DELEGATE_METHOD( SubscriptionList::ReleaseFunction, &ShooterBodyMovementComponent::releaseSuspension, this ) }
    {
    }

    Subscription ShooterBodyMovementComponent::suspend()
    {
        _suspendCount.fetch_add( 1, std::memory_order_relaxed );
        return _suspensionSubscriptions.issue( _nextSuspensionID++ );
    }

    void ShooterBodyMovementComponent::releaseSuspension( uint64 suspensionID )
    {
        (void)suspensionID;
        _suspendCount.fetch_sub( 1, std::memory_order_relaxed );
    }

    SpawnStateReset ShooterBodyMovementComponent::resetToSpawnState()
    {
        _moveVelocity  = float3{ 0.0f, 0.0f, 0.0f };
        _displayOffset = float3{ 0.0f, 0.0f, 0.0f };
        _verticalSpeed = 0.0f;
        _bOnGround     = SW_TRUE;
        return SpawnStateReset::Reset;
    }

    void ShooterBodyMovementComponent::copyTuning( const ShooterBodyMovementComponent& source )
    {
        _sprintButton = source._sprintButton;
        _jumpButton   = source._jumpButton;
        _walkSpeed    = source._walkSpeed;
        _sprintSpeed  = source._sprintSpeed;
        _jumpSpeed    = source._jumpSpeed;
        _gravity      = source._gravity;
        _radius       = source._radius;
    }

    bool ShooterBodyMovementComponent::stepMovement( const vector<ShooterArenaBox>& listBox, const vector<ShooterFloorStep>& listFloor, float32 deltaTime )
    {
        if ( deltaTime <= 0.0f || _bNetDriven == SW_TRUE )
            return false;
        const GameObject*      pOwner  = getOwner();
        const PawnComponent*   pPawn   = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        const ShooterMoveInput input   = pPawn != nullptr && isSuspended() == false ? makeInput( pPawn->getIntent() ) : ShooterMoveInput{};
        const float3           before  = _position;
        bool                   bLanded = false;
        const ShooterMoveState next    = simulate( listBox, listFloor, getState(), input, deltaTime, bLanded );
        setState( next, float3{ next._position._x - before._x, 0.0f, next._position._z - before._z } * ( 1.0f / deltaTime ) );
        return bLanded;
    }

    ShooterMoveInput ShooterBodyMovementComponent::makeInput( const ControlIntent& intent ) const
    {
        ShooterMoveInput     input;
        const GameObject*    pOwner = getOwner();
        const PawnComponent* pPawn  = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        if ( pPawn == nullptr )
            return input;
        // 이동 축은 조종 요 기준이다 — 월드 XZ 로 돌리고 길이는 1 에서 자른다(아날로그 스틱 · AI 경로 속도는 그 비율로 느리다).
        const float3  world = intent.computeWorldMove();
        float3        move{ world._x, 0.0f, world._z };
        const float32 length = move.getLength();
        if ( length > 1.0f )
            move = move * ( 1.0f / length );
        input._move    = move;
        input._bSprint = _sprintButton.empty() == false && intent.isDown( pPawn->findButton( _sprintButton ) ) ? SW_TRUE : SW_FALSE;
        input._bJump   = _jumpButton.empty() == false && intent.wasTriggered( pPawn->findButton( _jumpButton ) ) ? SW_TRUE : SW_FALSE;
        return input;
    }

    ShooterMoveState ShooterBodyMovementComponent::simulate( const vector<ShooterArenaBox>& listBox, const vector<ShooterFloorStep>& listFloor, const ShooterMoveState& state, const ShooterMoveInput& input, float32 deltaTime,
                                                             bool& outbLanded ) const
    {
        outbLanded             = false;
        ShooterMoveState next  = state;
        const float32    speed = input._bSprint == SW_TRUE ? _sprintSpeed : _walkSpeed;
        const float3     start = ShooterFloorStep::carryStanding( listFloor, state._position, state._bOnGround == SW_TRUE ); // 딛은 바닥이 먼저 실어 나른다
        float3           feet  = start + input._move * ( speed * deltaTime );
        if ( input._bJump == SW_TRUE && next._bOnGround == SW_TRUE )
        {
            next._verticalSpeed = _jumpSpeed;
            next._bOnGround     = SW_FALSE;
        }
        next._verticalSpeed -= _gravity * deltaTime;
        feet._y += next._verticalSpeed * deltaTime;
        const float32 ground = ShooterFloorStep::computeGround( listFloor, feet, start._y );
        if ( feet._y <= ground )
        {
            outbLanded          = next._bOnGround == SW_FALSE;
            feet._y             = ground;
            next._verticalSpeed = 0.0f;
            next._bOnGround     = SW_TRUE;
        }
        else
        {
            next._bOnGround = SW_FALSE; // 뛰었거나 바닥 끝에서 내려섰다
        }
        next._position = ShooterArenaMath::resolveCircle( listBox, feet, _radius );
        return next;
    }

    ShooterMoveState ShooterBodyMovementComponent::getState() const
    {
        ShooterMoveState state;
        state._position      = _position;
        state._verticalSpeed = _verticalSpeed;
        state._bOnGround     = _bOnGround == SW_TRUE ? SW_TRUE : SW_FALSE;
        return state;
    }

    void ShooterBodyMovementComponent::setState( const ShooterMoveState& state, const float3& moveVelocity )
    {
        _position      = state._position;
        _verticalSpeed = state._verticalSpeed;
        _bOnGround     = state._bOnGround == SW_TRUE ? SW_TRUE : SW_FALSE;
        _moveVelocity  = moveVelocity;
    }

    void ShooterBodyMovementComponent::teleport( const AbsolutePosition& feet )
    {
        _position      = feet._value;
        _moveVelocity  = float3{ 0.0f, 0.0f, 0.0f };
        _verticalSpeed = 0.0f;
        _bOnGround     = SW_TRUE;
        ++_teleportCount;
    }
} // namespace sw

namespace sw
{
    namespace
    {
        struct ShooterBodyMovementWatchInternal
        {
            /** @brief 감시 값 `Shooter.BodySpeed` 입니다 — 대상 오브젝트의 슈터 몸 속도(수평 걸음 + 수직, m/s). 몸이 없으면 값이 없습니다. */
            [[nodiscard]] static bool readBodySpeed( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const ShooterBodyMovementComponent* pBody = query.findTargetComponent<ShooterBodyMovementComponent>( "shooter body movement", outValue );
                if ( pBody == nullptr )
                    return false;
                const float3  walk = pBody->getMoveVelocity();
                const float32 fall = pBody->getState()._verticalSpeed;
                outValue.setNumber( static_cast<float64>( float3{ walk._x, fall, walk._z }.getLength() ) );
                return true;
            }
        };
    } // namespace

    // 슈터 몸은 물리 바디 없이 운동학으로 걷는다 — 엔진 `Object.Speed`(강체 · 캐릭터 컨트롤러)가 보지 못한다(shooter3d actions · cctvmonitor).
    SW_AUTOMATION_QUERY_WATCH( shooterBodySpeed, "Shooter.BodySpeed", "Speed of the shooter body on target (walk plus vertical, m/s)", &ShooterBodyMovementWatchInternal::readBodySpeed, true, false );
} // namespace sw

namespace sw
{
    float3 ShooterFloorStep::toAbsolute( const float3& position, float32 yaw, const float3& local )
    {
        // 요 0 이 +Z, 요 90° 가 +X 다(`ControlIntent::computeWorldMove` 와 같다).
        const float32 sinYaw = MathUtil::sin( yaw );
        const float32 cosYaw = MathUtil::cos( yaw );
        return position + float3{ local._x * cosYaw + local._z * sinYaw, local._y, -local._x * sinYaw + local._z * cosYaw };
    }

    float3 ShooterFloorStep::toLocal( const float3& position, float32 yaw, const float3& absolute )
    {
        return toAbsolute( float3{}, -yaw, absolute - position );
    }

    bool ShooterFloorStep::isOver( const float3& position, float32 yaw, const float3& feet ) const
    {
        const float3 local = toLocal( position, yaw, feet );
        return _halfSize._x > 0.0f && MathUtil::abs( local._x ) <= _halfSize._x && MathUtil::abs( local._z ) <= _halfSize._z;
    }

    bool ShooterFloorStep::isStandingAfter( const float3& feet ) const
    {
        return isOver( _toPosition, _toYaw, feet ) && MathUtil::abs( feet._y - ( _toPosition._y + _halfSize._y ) ) <= kStandingTolerance;
    }

    float3 ShooterFloorStep::carry( const float3& feet ) const
    {
        return toAbsolute( _toPosition, _toYaw, toLocal( _fromPosition, _fromYaw, feet ) );
    }

    float3 ShooterFloorStep::carryStanding( const vector<ShooterFloorStep>& listFloor, const float3& feet, bool bOnGround )
    {
        if ( bOnGround == false )
            return feet;
        for ( const ShooterFloorStep& floor : listFloor )
        {
            const bool bStanding = floor.isOver( floor._fromPosition, floor._fromYaw, feet ) && MathUtil::abs( feet._y - ( floor._fromPosition._y + floor._halfSize._y ) ) <= kStandingTolerance;
            if ( bStanding )
                return floor.carry( feet );
        }
        return feet;
    }

    float32 ShooterFloorStep::computeGround( const vector<ShooterFloorStep>& listFloor, const float3& feet, float32 startHeight )
    {
        float32 ground = 0.0f;
        for ( const ShooterFloorStep& floor : listFloor )
        {
            const float32 top = floor._toPosition._y + floor._halfSize._y;
            if ( startHeight >= top - kStepHeight && floor.isOver( floor._toPosition, floor._toYaw, feet ) )
                ground = MathUtil::max( ground, top );
        }
        return ground;
    }

    uint8 ShooterFloorStep::findNumber( const vector<ShooterFloorStep>& listFloor, const float3& feet )
    {
        for ( size_t floorIndex = 0; floorIndex < listFloor.size(); ++floorIndex )
        {
            if ( listFloor[floorIndex].isStandingAfter( feet ) )
                return static_cast<uint8>( floorIndex + 1u );
        }
        return 0;
    }
} // namespace sw
