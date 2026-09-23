#include "pch.h"

#include "Engine/Physics/CCD.h"

#include "Core/Math/Math.h"

#include "Engine/Physics/AABB.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 축 하나의 슬랩으로 [tNear, tFar] 구간을 좁힙니다. 이 축에서 이미 빗나갔으면 false 입니다.
         * @details 슬랩 검사는 축마다 똑같습니다. 예전에는 이 22줄이 **여섯 벌**(두 함수 × 세 축)
         *          있었고, 한 축의 부호나 첨자를 잘못 적어도 나머지 다섯과 비교해 보지 않는 한
         *          보이지 않았습니다. 증상은 "특정 방향에서만 안 맞는다" 라서 가장 찾기 어렵습니다.
         * @param origin 이동 시작점의 이 축 좌표
         * @param delta 이 축의 변위
         * @param slabMin 슬랩(확장된 대상 상자)의 이 축 최소값
         * @param slabMax 슬랩의 이 축 최대값
         * @param negativeNormal 이 축의 최소면 법선
         * @param positiveNormal 이 축의 최대면 법선
         */
        bool clipSlab( float32 origin, float32 delta, float32 slabMin, float32 slabMax,
                       const float3& negativeNormal, const float3& positiveNormal,
                       float32& inoutNear, float32& inoutFar, float3& inoutNearNormal )
        {
            // 이 축으로 움직이지 않으면 시작 좌표가 슬랩 안에 있는지만 본다. 나누면 무한대가 된다.
            if ( MathUtil::abs( delta ) < MathUtil::Epsilon )
                return slabMin <= origin && origin <= slabMax;

            const float32 invDelta = 1.0f / delta;
            float32       tEnter   = ( slabMin - origin ) * invDelta;
            float32       tExit    = ( slabMax - origin ) * invDelta;

            // 음의 방향으로 가면 두 면의 순서가 뒤집힌다. 법선도 같이 뒤집는다.
            const bool bReversed = tEnter > tExit;
            if ( bReversed )
                std::swap( tEnter, tExit );

            if ( tEnter > inoutNear )
            {
                inoutNear       = tEnter;
                inoutNearNormal = bReversed ? positiveNormal : negativeNormal;
            }
            inoutFar = MathUtil::min( inoutFar, tExit );

            return inoutNear <= inoutFar && inoutFar >= 0.0f;
        }

        /** @brief 세 축을 모두 잘라 진입 시각과 그 면의 법선을 구합니다. 빗나가면 false 입니다. */
        bool clipAllSlabs( const float3& origin, const float3& displacement, const AABB& slabBox,
                           float32& outNear, float3& outNearNormal )
        {
            outNear         = -MathUtil::MaxFloat;
            float32 farTime = MathUtil::MaxFloat;
            outNearNormal   = float3{ 0.0f, 0.0f, 0.0f };

            if ( clipSlab( origin._x, displacement._x, slabBox._min._x, slabBox._max._x,
                           float3{ -1.0f, 0.0f, 0.0f }, float3{ 1.0f, 0.0f, 0.0f },
                           outNear, farTime, outNearNormal ) == false )
                return false;

            if ( clipSlab( origin._y, displacement._y, slabBox._min._y, slabBox._max._y,
                           float3{ 0.0f, -1.0f, 0.0f }, float3{ 0.0f, 1.0f, 0.0f },
                           outNear, farTime, outNearNormal ) == false )
                return false;

            if ( clipSlab( origin._z, displacement._z, slabBox._min._z, slabBox._max._z,
                           float3{ 0.0f, 0.0f, -1.0f }, float3{ 0.0f, 0.0f, 1.0f },
                           outNear, farTime, outNearNormal ) == false )
                return false;

            return true;
        }

        /** @brief 상자를 각 축으로 @p halfExtents 만큼 부풀립니다(민코프스키 합). */
        AABB expandBox( const AABB& box, const float3& halfExtents )
        {
            return AABB{
                float3{box._min._x - halfExtents._x, box._min._y - halfExtents._y, box._min._z - halfExtents._z},
                float3{box._max._x + halfExtents._x, box._max._y + halfExtents._y, box._max._z + halfExtents._z}
            };
        }
    } // namespace
} // namespace sw

namespace sw
{
    bool CCD::sweepAabb( const AABB& movingBox, const float3& displacement, const AABB& targetBox, SweepHit& outHit )
    {
        // **빗나가면 outHit 은 비어 있다.** 예전에는 이 함수만 비우지 않아서, 결과 구조체를
        // 재사용하는 쪽이 false 를 받고도 이전 호출의 `_bHit` 을 그대로 읽을 수 있었다
        // (형제 함수 `sweepSphere` 는 처음부터 비우고 있었다. 둘이 다른 약속을 하고 있었다).
        outHit = SweepHit{};

        const float3 movingHalfExtents = movingBox.getExtents();
        const float3 movingCenter      = movingBox.getCenter();
        const AABB   expanded          = expandBox( targetBox, movingHalfExtents );

        if ( expanded.contains( movingCenter ) )
        {
            outHit._bHit     = true;
            outHit._time     = 0.0f;
            outHit._hitPoint = movingCenter;
            // 이미 겹친 상태에서는 진입면이 없다. 밀어내는 방향으로 위를 준다.
            outHit._hitNormal = float3{ 0.0f, 1.0f, 0.0f };
            return true;
        }

        float32 tNear{ 0.0f };
        float3  nearNormal{};
        if ( clipAllSlabs( movingCenter, displacement, expanded, tNear, nearNormal ) == false )
            return false;

        if ( tNear < 0.0f || tNear > 1.0f )
            return false;

        outHit._bHit      = true;
        outHit._time      = tNear;
        outHit._hitPoint  = movingCenter + displacement * tNear;
        outHit._hitNormal = nearNormal;
        return true;
    }

    bool CCD::sweepSphere( const float3& startCenter, float32 radius, const float3& displacement, const AABB& targetBox, SweepHit& outHit )
    {
        outHit = SweepHit{};

        // 1) 처음부터 겹쳐 있는지 본다(t = 0)
        const float3  initialClosest  = startCenter.clamped( targetBox._min, targetBox._max );
        const float3  toCenterInitial = startCenter - initialClosest;
        const float32 distSqInitial   = toCenterInitial.getLengthSquared();
        if ( distSqInitial <= radius * radius )
        {
            outHit._bHit      = true;
            outHit._time      = 0.0f;
            outHit._hitPoint  = initialClosest;
            const float32 len = MathUtil::sqrt( distSqInitial );
            outHit._hitNormal = len > MathUtil::Epsilon ? float3{ toCenterInitial._x / len, toCenterInitial._y / len, toCenterInitial._z / len } : float3{ 0.0f, 1.0f, 0.0f };
            return true;
        }

        // 2) 변위가 0 에 가까우면 더 쓸어 볼 필요가 없다
        const float32 dispLenSq = displacement.getLengthSquared();
        if ( dispLenSq < 0.000001f )
            return false;

        // 3) 반지름만큼 부풀린 AABB(TargetBox + Radius)에 대해 슬랩으로 쓸어 본다
        const AABB expandedBox = expandBox( targetBox, float3{ radius, radius, radius } );

        float32 tNear{ 0.0f };
        float3  nearNormal{};
        if ( clipAllSlabs( startCenter, displacement, expandedBox, tNear, nearNormal ) == false )
            return false;

        if ( tNear < 0.0f || tNear > 1.0f )
            return false;

        // 4) 충돌 시점의 구 중심과 대상 상자의 최근접점을 확인한다
        const float3  sphereCenterAtHit = startCenter + displacement * tNear;
        const float3  closestOnBox      = sphereCenterAtHit.clamped( targetBox._min, targetBox._max );
        const float3  toCenter          = sphereCenterAtHit - closestOnBox;
        const float32 distSqHit         = toCenter.getLengthSquared();

        if ( distSqHit <= ( radius * radius + 0.01f ) )
        {
            outHit._bHit      = true;
            outHit._time      = tNear;
            outHit._hitPoint  = closestOnBox;
            const float32 len = MathUtil::sqrt( distSqHit );
            outHit._hitNormal = len > MathUtil::Epsilon ? float3{ toCenter._x / len, toCenter._y / len, toCenter._z / len } : nearNormal;
            return true;
        }

        // 모서리 · 꼭짓점 영역은 광선-구 교차로 다시 보정한다
        const float3  rayToClosest      = closestOnBox - startCenter;
        const float32 dotVal            = rayToClosest.dot( displacement );
        const float32 proj              = dotVal / dispLenSq;
        const float32 clampedProj       = MathUtil::saturate( proj );
        const float3  closestPointOnRay = startCenter + displacement * clampedProj;

        const float3  diff       = closestPointOnRay - closestOnBox;
        const float32 edgeDistSq = diff.getLengthSquared();
        if ( edgeDistSq <= radius * radius && clampedProj <= 1.0f )
        {
            outHit._bHit      = true;
            outHit._time      = clampedProj;
            outHit._hitPoint  = closestOnBox;
            const float32 len = MathUtil::sqrt( edgeDistSq );
            outHit._hitNormal = len > MathUtil::Epsilon ? float3{ diff._x / len, diff._y / len, diff._z / len } : nearNormal;
            return true;
        }

        return false;
    }
} // namespace sw
