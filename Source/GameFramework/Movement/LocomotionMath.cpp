#include "pch.h"

#include "GameFramework/Movement/LocomotionMath.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    float2 LocomotionMath::computeLocalVelocity( const float3& velocity, float32 yaw )
    {
        const float32 sinYaw = MathUtil::sin( yaw );
        const float32 cosYaw = MathUtil::cos( yaw );
        // 앞 = (sin, 0, cos), 오른쪽 = (cos, 0, -sin) — 왼손 좌표, +Y 위.
        return float2{ velocity._x * sinYaw + velocity._z * cosYaw, velocity._x * cosYaw - velocity._z * sinYaw };
    }

    LocomotionDirection LocomotionMath::classify( const float3& velocity, float32 yaw, bool bOnGround, float32 idleSpeed, float32 sideBias )
    {
        if ( bOnGround == false )
            return LocomotionDirection::Airborne;
        const float32 speed = float3{ velocity._x, 0.0f, velocity._z }.getLength();
        if ( speed < idleSpeed )
            return LocomotionDirection::Idle;
        const float2  local   = computeLocalVelocity( velocity, yaw );
        const float32 forward = local._x;
        const float32 right   = local._y;
        if ( MathUtil::abs( right ) > MathUtil::abs( forward ) * sideBias )
            return right > 0.0f ? LocomotionDirection::Right : LocomotionDirection::Left;
        return forward < 0.0f ? LocomotionDirection::Backward : LocomotionDirection::Forward;
    }

    LocomotionDirection LocomotionMath::classifyFrom( LocomotionDirection current, const float3& velocity, float32 yaw, bool bOnGround, float32 idleSpeed, float32 sideBias )
    {
        const bool    bSideNow = current == LocomotionDirection::Left || current == LocomotionDirection::Right;
        const float32 bias     = bSideNow ? 1.0f / MathUtil::max( sideBias, 1.0f ) : sideBias;
        return classify( velocity, yaw, bOnGround, idleSpeed, bias );
    }
} // namespace sw

namespace sw
{
    LocomotionDirectionFilter::LocomotionDirectionFilter( float32 minHoldSeconds )
        : _minHoldSeconds{ minHoldSeconds }
        , _pendingSeconds{ 0.0f }
        , _direction{ LocomotionDirection::Idle }
        , _pending{ LocomotionDirection::Idle }
    {
    }

    LocomotionDirection LocomotionDirectionFilter::update( LocomotionDirection candidate, float32 deltaSeconds )
    {
        // 공중에 뜨고 내리는 것은 바로 — 점프 클립이 늦으면 그것이 튄다.
        const bool bAirChange = candidate == LocomotionDirection::Airborne || _direction == LocomotionDirection::Airborne;
        if ( candidate == _direction || bAirChange )
        {
            _direction      = candidate;
            _pending        = candidate;
            _pendingSeconds = 0.0f;
            return _direction;
        }
        if ( candidate != _pending )
        {
            _pending        = candidate;
            _pendingSeconds = 0.0f;
        }
        _pendingSeconds += MathUtil::max( deltaSeconds, 0.0f );
        if ( _pendingSeconds >= _minHoldSeconds )
        {
            _direction      = candidate;
            _pendingSeconds = 0.0f;
        }
        return _direction;
    }

    void LocomotionDirectionFilter::reset()
    {
        _direction      = LocomotionDirection::Idle;
        _pending        = LocomotionDirection::Idle;
        _pendingSeconds = 0.0f;
    }
} // namespace sw
