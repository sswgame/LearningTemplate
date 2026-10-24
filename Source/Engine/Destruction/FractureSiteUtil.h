/**
 * @file FractureSiteUtil.h
 * @brief 파쇄 시드 점(Voronoi 사이트)을 놓는 두 파일(`MeshFracture` 3D, `PolygonFracture` 2D)이 함께 쓰는 상수와 점 더하기입니다.
 * @details 점 종류(`float3` · `float2`)와 안쪽 판정만 다르고 나머지는 같다 — 이미 놓인 점과 겹치면 버리고, 안이 아니면 버리고, 아니면 더한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

namespace sw
{
    /** @struct FractureSiteUtil @brief 시드 점 놓기의 공용 상수와 점 더하기. 파일 머리말 참고. */
    struct FractureSiteUtil
    {
        /** @brief 점 하나를 놓을 때 시도하는 횟수 상한의 배율입니다(목표 점 수 × 이 값). 안쪽 판정에 실패하는 점이 많은 모양에서도 끝나게 한다. */
        static constexpr uint32 kAttemptFactor = 200;
        /** @brief 이 거리(제곱) 안의 점은 이미 있는 점과 같은 점으로 봅니다. */
        static constexpr float32 kMergeDistanceSquared = 1.0e-10f;

        /**
         * @brief @p point 가 이미 놓인 점과 겹치지 않고 @p isInside 가 참이면 @p inoutListSite 에 더합니다. 겹치거나 밖이면 아무것도 하지 않습니다.
         * @tparam TPoint `getDistanceSquared( a, b )` 를 가진 점 종류(`float3` · `float2`)
         * @param isInside `bool( const TPoint& )` 로 부를 수 있는 안쪽 판정
         */
        template <typename TPoint, typename TIsInside>
        static void addIfInside( vector<TPoint>& inoutListSite, const TPoint& point, const TIsInside& isInside )
        {
            for ( const TPoint& existing : inoutListSite )
            {
                if ( TPoint::getDistanceSquared( existing, point ) < kMergeDistanceSquared )
                    return;
            }
            if ( isInside( point ) )
                inoutListSite.push_back( point );
        }
    };
} // namespace sw
