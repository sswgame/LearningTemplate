#include "pch.h"

#include "GameFramework/Base/RayMath.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    bool RayMath::intersectSphere( const GameRay& ray, const float3& center, float32 radius, float32 maxDistance, float32& outDistance )
    {
        const float3  toCenter     = center - ray._origin;
        const float32 radiusSquare = radius * radius;
        if ( toCenter.getLengthSquared() <= radiusSquare )
        {
            outDistance = 0.0f;
            return true;
        }
        const float32 projection = toCenter.dot( ray._direction );
        if ( projection < 0.0f )
            return false;
        const float32 closestSquare = toCenter.getLengthSquared() - projection * projection;
        if ( closestSquare > radiusSquare )
            return false;
        const float32 distance = projection - MathUtil::sqrt( radiusSquare - closestSquare );
        if ( distance > maxDistance )
            return false;
        outDistance = distance;
        return true;
    }

    bool RayMath::intersectAabb( const GameRay& ray, const float3& boxMin, const float3& boxMax, float32 maxDistance, float32& outDistance )
    {
        const float32 arrOrigin[3]    = { ray._origin._x, ray._origin._y, ray._origin._z };
        const float32 arrDirection[3] = { ray._direction._x, ray._direction._y, ray._direction._z };
        const float32 arrMin[3]       = { boxMin._x, boxMin._y, boxMin._z };
        const float32 arrMax[3]       = { boxMax._x, boxMax._y, boxMax._z };
        float32       nearDistance    = 0.0f;
        float32       farDistance     = maxDistance;
        for ( int32 axisIndex = 0; axisIndex < 3; ++axisIndex )
        {
            if ( MathUtil::abs( arrDirection[axisIndex] ) < 1.0e-8f )
            {
                // 이 축과 나란하다 — 슬랩 밖이면 만나지 않는다.
                if ( arrOrigin[axisIndex] < arrMin[axisIndex] || arrOrigin[axisIndex] > arrMax[axisIndex] )
                    return false;
                continue;
            }
            const float32 inverse  = 1.0f / arrDirection[axisIndex];
            float32       entering = ( arrMin[axisIndex] - arrOrigin[axisIndex] ) * inverse;
            float32       leaving  = ( arrMax[axisIndex] - arrOrigin[axisIndex] ) * inverse;
            if ( entering > leaving )
            {
                const float32 swapped = entering;
                entering              = leaving;
                leaving               = swapped;
            }
            nearDistance = MathUtil::max( nearDistance, entering );
            farDistance  = MathUtil::min( farDistance, leaving );
            if ( nearDistance > farDistance )
                return false;
        }
        outDistance = nearDistance;
        return true;
    }

    bool RayMath::intersectHorizontalPlane( const GameRay& ray, float32 height, float32 maxDistance, float32& outDistance )
    {
        if ( MathUtil::abs( ray._direction._y ) < 1.0e-6f )
            return false;
        const float32 distance = ( height - ray._origin._y ) / ray._direction._y;
        if ( distance < 0.0f || distance > maxDistance )
            return false;
        outDistance = distance;
        return true;
    }

    float3 RayMath::computeLookDirection( float32 yaw, float32 pitch )
    {
        const float32 cosPitch = MathUtil::cos( pitch );
        return float3{ MathUtil::sin( yaw ) * cosPitch, MathUtil::sin( pitch ), MathUtil::cos( yaw ) * cosPitch };
    }
} // namespace sw
