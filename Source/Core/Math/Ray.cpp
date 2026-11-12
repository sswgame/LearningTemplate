#include "pch.h"

#include "Core/Math/Ray.h"

#include "Core/Math/DeterministicMathUtil.h"
#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct RayInternal
        {
            /** @brief 삼각형 무게중심 경계의 여유입니다 — 공유 모서리를 정확히 지나는 광선이 양쪽 다 빗나가지 않게 합니다. */
            static constexpr float32 kBarycentricSlack = 1.0e-5f;
            /** @brief 이보다 작은 행렬식은 광선이 삼각형 면과 나란한 것으로 봅니다. */
            static constexpr float32 kParallelDeterminant = 1.0e-12f;
        };
    } // namespace

    bool Ray::intersectSphere( const Ray& ray, const float3& center, float32 radius, float32 maxDistance, float32& outDistance )
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

    bool Ray::intersectCapsule( const Ray& ray, const float3& segmentStart, const float3& segmentEnd, float32 radius, float32 maxDistance,
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
                const bool    bBetterHit = distance >= 0.0f && bOnSegment && distance <= best;
                if ( bBetterHit )
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

    bool Ray::intersectAABB( const Ray& ray, const float3& boxMin, const float3& boxMax, float32 maxDistance, float32& outDistance )
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

    bool Ray::intersectTriangle( const Ray& ray, const float3& a, const float3& b, const float3& c, float32 maxDistance, RayTriangleHit& outHit )
    {
        const float3  edge1       = b - a;
        const float3  edge2       = c - a;
        const float3  pVector     = ray._direction.cross( edge2 );
        const float32 determinant = edge1.dot( pVector );
        if ( MathUtil::abs( determinant ) < RayInternal::kParallelDeterminant )
            return false; // 광선이 삼각형 면과 나란하다
        const float32 inverseDeterminant = 1.0f / determinant;
        const float3  tVector            = ray._origin - a;
        float32       u                  = tVector.dot( pVector ) * inverseDeterminant;
        if ( u < -RayInternal::kBarycentricSlack || u > 1.0f + RayInternal::kBarycentricSlack )
            return false;
        const float3 qVector = tVector.cross( edge1 );
        float32      v       = ray._direction.dot( qVector ) * inverseDeterminant;
        if ( v < -RayInternal::kBarycentricSlack || u + v > 1.0f + RayInternal::kBarycentricSlack )
            return false;
        const float32 distance = edge2.dot( qVector ) * inverseDeterminant;
        if ( distance < 0.0f || distance > maxDistance )
            return false;
        // 여유 안의 무게중심 값은 경계로 묶는다 — 보간한 UV · 위치가 삼각형 밖으로 나가지 않게.
        u                  = MathUtil::saturate( u );
        v                  = MathUtil::clamp( v, 0.0f, 1.0f - u );
        outHit._distance   = distance;
        outHit._baryU      = u;
        outHit._baryV      = v;
        outHit._bFrontFace = ray._direction.dot( edge1.cross( edge2 ) ) < 0.0f ? SW_TRUE : SW_FALSE;
        return true;
    }

    bool Ray::intersectHorizontalPlane( const Ray& ray, float32 height, float32 maxDistance, float32& outDistance )
    {
        if ( MathUtil::abs( ray._direction._y ) < 1.0e-6f )
            return false;
        const float32 distance = ( height - ray._origin._y ) / ray._direction._y;
        if ( distance < 0.0f || distance > maxDistance )
            return false;
        outDistance = distance;
        return true;
    }
} // namespace sw
