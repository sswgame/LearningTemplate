#include "pch.h"

#include "GameFramework/Utility/OrientationUtil.h"

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
} // namespace sw
