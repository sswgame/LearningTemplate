#include "pch.h"

#include "GameFramework/Base/Foundation/Utility/Math/RayMath.h"

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

    bool RayMath::intersectCapsule( const GameRay& ray, const float3& segmentStart, const float3& segmentEnd, float32 radius, float32 maxDistance,
                                    float32& outDistance )
    {
        const float3  axis         = segmentEnd - segmentStart;
        const float32 axisLength   = axis.getLength();
        const float32 radiusSquare = radius * radius;
        if ( axisLength < 1.0e-6f )
            return intersectSphere( ray, segmentStart, radius, maxDistance, outDistance );
        const float3  unitAxis   = axis * ( 1.0f / axisLength );
        const float3  fromStart  = ray._origin - segmentStart;
        const float32 originAxis = MathUtil::clamp( fromStart.dot( unitAxis ), 0.0f, axisLength );
        if ( ( fromStart - unitAxis * originAxis ).getLengthSquared() <= radiusSquare )
        {
            outDistance = 0.0f;
            return true;
        }
        // 옆면 — 축에 수직인 성분만 남겨 원 판정을 푼다. 만난 높이가 선분 안일 때만 옆면이다.
        float32       best          = maxDistance;
        bool          bHit          = false;
        const float3  directionPerp = ray._direction - unitAxis * ray._direction.dot( unitAxis );
        const float3  originPerp    = fromStart - unitAxis * fromStart.dot( unitAxis );
        const float32 quadA         = directionPerp.dot( directionPerp );
        if ( quadA > 1.0e-8f )
        {
            const float32 quadB        = 2.0f * originPerp.dot( directionPerp );
            const float32 quadC        = originPerp.dot( originPerp ) - radiusSquare;
            const float32 discriminant = quadB * quadB - 4.0f * quadA * quadC;
            if ( discriminant >= 0.0f )
            {
                const float32 distance   = ( -quadB - MathUtil::sqrt( discriminant ) ) / ( 2.0f * quadA );
                const float32 hitOnAxis  = ( fromStart + ray._direction * distance ).dot( unitAxis );
                const bool    bOnSegment = 0.0f <= hitOnAxis && hitOnAxis <= axisLength;
                if ( distance >= 0.0f && bOnSegment && distance <= best )
                {
                    best = distance;
                    bHit = true;
                }
            }
        }
        // 양 끝 반구 — 옆면보다 가까우면 그것이다.
        float32 capDistance = 0.0f;
        if ( intersectSphere( ray, segmentStart, radius, best, capDistance ) && capDistance <= best )
        {
            best = capDistance;
            bHit = true;
        }
        if ( intersectSphere( ray, segmentEnd, radius, best, capDistance ) && capDistance <= best )
        {
            best = capDistance;
            bHit = true;
        }
        if ( bHit )
            outDistance = best;
        return bHit;
    }

    bool RayMath::intersectAABB( const GameRay& ray, const float3& boxMin, const float3& boxMax, float32 maxDistance, float32& outDistance )
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

    bool RayMath::isInCone( const float3& origin, const float3& forward, float32 halfAngleDegree, float32 range, const float3& point )
    {
        const float3  toPoint  = point - origin;
        const float32 distance = toPoint.getLength();
        if ( distance > range )
            return false;
        const float32 forwardLength = forward.getLength();
        if ( distance <= 1.0e-6f || forwardLength <= 1.0e-6f )
            return true;
        return toPoint.dot( forward ) / ( distance * forwardLength ) >= MathUtil::cos( MathUtil::toRadian( halfAngleDegree ) );
    }

    bool RayMath::isInFlatCone( const float3& origin, const float3& forward, float32 halfAngleDegree, float32 range, const float3& point )
    {
        return isInCone( float3{ origin._x, 0.0f, origin._z }, float3{ forward._x, 0.0f, forward._z }, halfAngleDegree, range, float3{ point._x, 0.0f, point._z } );
    }

    float3 RayMath::computeLookDirection( float32 yaw, float32 pitch )
    {
        const float32 cosPitch = MathUtil::cos( pitch );
        return float3{ MathUtil::sin( yaw ) * cosPitch, MathUtil::sin( pitch ), MathUtil::cos( yaw ) * cosPitch };
    }
} // namespace sw
