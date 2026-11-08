#include "pch.h"

#include "Core/Math/OrientationUtil.h"

#include "Core/Math/DeterministicMathUtil.h"
#include "Core/Math/MathUtil.h"

namespace sw
{
    float3 OrientationUtil::computeEulerFromForwardUp( const float3& forward, const float3& up )
    {
        const float32 forwardLength = forward.getLength();
        if ( forwardLength < 1.0e-6f )
            return float3{ 0.0f, 0.0f, 0.0f };
        const float3  unitForward = forward * ( 1.0f / forwardLength );
        const float32 pitch       = -MathUtil::asin( MathUtil::clamp( unitForward._y, -1.0f, 1.0f ) );

        // 앞이 거의 수직이면 요를 위 방향에서 고른다 — 롤 없는 위가 그대로 @p up 이 되게.
        if ( MathUtil::abs( unitForward._y ) > 0.999f )
        {
            const float32 yaw = unitForward._y > 0.0f ? MathUtil::atan2( -up._x, -up._z ) : MathUtil::atan2( up._x, up._z );
            return float3{ pitch, yaw, 0.0f };
        }

        const float32 yaw         = MathUtil::atan2( unitForward._x, unitForward._z );
        const float32 sinPitch    = MathUtil::sin( pitch );
        const float32 cosPitch    = MathUtil::cos( pitch );
        const float32 sinYaw      = MathUtil::sin( yaw );
        const float32 cosYaw      = MathUtil::cos( yaw );
        const float3  noRollUp    = float3{ sinPitch * sinYaw, cosPitch, sinPitch * cosYaw };
        const float3  noRollRight = float3{ cosYaw, 0.0f, -sinYaw };
        // 롤 r 이면 위 = cos r · 롤없는위 − sin r · 롤없는오른쪽.
        const float32 roll = MathUtil::atan2( -up.dot( noRollRight ), up.dot( noRollUp ) );
        return float3{ pitch, yaw, roll };
    }

    float32 OrientationUtil::turnTowardAngle( float32 current, float32 target, float32 maxStep )
    {
        const float32 limit = MathUtil::max( maxStep, 0.0f );
        return MathUtil::wrapAngle( current + MathUtil::clamp( MathUtil::wrapAngle( target - current ), -limit, limit ) );
    }

    bool OrientationUtil::isInCone( const float3& origin, const float3& forward, float32 halfAngleDegree, float32 range, const float3& point )
    {
        const float3  toPoint  = point - origin;
        const float32 distance = toPoint.getLength();
        if ( distance > range )
            return false;
        const float32 forwardLength = forward.getLength();
        if ( distance <= 1.0e-6f || forwardLength <= 1.0e-6f )
            return true;
        return toPoint.dot( forward ) / ( distance * forwardLength ) >= DeterministicMathUtil::cos( MathUtil::toRadian( halfAngleDegree ) );
    }

    bool OrientationUtil::isInFlatCone( const float3& origin, const float3& forward, float32 halfAngleDegree, float32 range, const float3& point )
    {
        return isInCone( float3{ origin._x, 0.0f, origin._z }, float3{ forward._x, 0.0f, forward._z }, halfAngleDegree, range, float3{ point._x, 0.0f, point._z } );
    }

    float3 OrientationUtil::computeLookDirection( float32 yaw, float32 pitch )
    {
        const float32 cosPitch = DeterministicMathUtil::cos( pitch );
        return float3{ DeterministicMathUtil::sin( yaw ) * cosPitch, DeterministicMathUtil::sin( pitch ), DeterministicMathUtil::cos( yaw ) * cosPitch };
    }
} // namespace sw
