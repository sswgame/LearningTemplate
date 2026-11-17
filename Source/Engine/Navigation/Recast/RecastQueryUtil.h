/**
 * @file RecastQueryUtil.h
 * @brief Detour 질의 객체로 점을 내비메시에 붙이는 규칙입니다. `RecastNavMesh` 의 질의와 `DetourNavCrowd` 의 목적지 · 위치 붙이기가 같은 규칙을 씁니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Navigation/NavigationTypes.h"

#include <recastnavigation/DetourNavMeshQuery.h>

// Detour 는 64 비트 폴리 참조로 짓는다(`ThirdParty/recastnavigation/vcpkg-port` 의 `RECASTNAVIGATION_DT_POLYREF64`). 32 비트는 타일 · 폴리 22 비트라
// 큰 월드(16K 타일 넘게)가 서지 않았다(감사 S-6). 옛 설치가 남아 좁은 참조로 지어지면 여기서 컴파일이 멈춘다.
static_assert( sizeof( dtPolyRef ) == sizeof( sw::NavPolyRef ), "Detour must be built with DT_POLYREF64 (vcpkg overlay port ThirdParty/recastnavigation/vcpkg-port)" );

namespace sw
{
    /** @brief 점을 질의 상자 안의 폴리곤에 붙이는 규칙입니다. */
    struct RecastQueryUtil
    {
        /** @brief Detour 참조를 엔진 형식으로 바꿉니다. 폭이 같다(위 `static_assert`). */
        static NavPolyRef toNavPolyRef( dtPolyRef polyRef ) { return static_cast<NavPolyRef>( polyRef ); }
        /** @brief 엔진 형식을 Detour 참조로 바꿉니다. */
        static dtPolyRef toDetourPolyRef( NavPolyRef polyRef ) { return static_cast<dtPolyRef>( polyRef ); }

        /**
         * @brief 질의 상자 안의 가장 가까운 폴리곤을 찾습니다. 못 찾거나 붙은 점이 상자 밖이면 ref 0 으로 답합니다.
         * @details Detour 는 경계 상자만 질의 상자에 걸친 폴리곤도 고르고 그 폴리곤 위 가장 가까운 점을 돌려준다 — 점이 상자 밖일 수 있다(돌린 장애물 구멍 옆 삼각형).
         *          계약(상자 안의 점)을 한 곳에서 지킨다. 높이는 경사면 투영이 상자 밖으로 나갈 수 있어 가로(XZ)만 본다.
         * @param pPosition 질의 점(x, y, z). @param pHalfExtent 질의 상자의 반 크기(x, y, z). @param pOutNearest 붙은 점(x, y, z)이며 ref 가 0 이면 값이 정해지지 않는다.
         */
        static dtPolyRef findPolyInBox( const dtNavMeshQuery* pQuery, const float32* pPosition, const float32* pHalfExtent, const dtQueryFilter* pFilter, float32* pOutNearest )
        {
            dtPolyRef polyRef = 0;
            // 결과는 ref 로 본다 — 못 찾으면 0 이다
            (void)pQuery->findNearestPoly( pPosition, pHalfExtent, pFilter, &polyRef, pOutNearest );
            if ( polyRef == 0 )
                return 0;
            if ( MathUtil::abs( pOutNearest[0] - pPosition[0] ) > pHalfExtent[0] || MathUtil::abs( pOutNearest[2] - pPosition[2] ) > pHalfExtent[2] )
                return 0;
            return polyRef;
        }
    };
} // namespace sw
