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
} // namespace sw
