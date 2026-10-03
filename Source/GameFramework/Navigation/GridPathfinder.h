/**
 * @file GridPathfinder.h
 * @brief 격자 A* — 8 방향(모서리 깎기 없음) · 칸 값 가중 · 옥타일 휴리스틱 · 탐색 예산 · 닿지 못하면 가장 가까운 곳까지 · 줄 당기기 다듬기.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class NavGrid;

    /** @brief 경로 하나를 묻는 칸입니다. */
    struct GridPathQuery
    {
        int2   _start{};
        int2   _goal{};
        uint32 _maxExpansions{ 0 }; ///< 펼칠 칸 수 상한(0 = 무제한). 넘으면 그때까지 가장 가까운 곳까지의 부분 경로
        uint8  _bAllowDiagonal{ SW_TRUE };
        uint8  _bSmooth{ SW_TRUE };        ///< 시선이 닿는 꺾임을 건너뛴다(계단 모양 → 곧은 선)
        uint8  _bAcceptPartial{ SW_TRUE }; ///< 못 닿으면 가장 가까운 곳까지라도(RTS 의 "막힌 곳 근처로")
    };

    /** @brief 경로 결과입니다. */
    enum class GridPathResult : uint8
    {
        Found = 0,   ///< 목적지까지
        Partial,     ///< 못 닿거나 예산이 다해 가장 가까운 곳까지
        NoPath,      ///< 갈 곳이 없다(부분 경로를 받지 않을 때)
        InvalidStart ///< 시작 칸이 밖이거나 막혔다
    };

    /** @brief 결과 이름입니다(로그). */
    SW_GF_API const utf8* toString( GridPathResult result );

    /**
     * @class GridPathfinder
     * @brief A* 한 벌입니다. 칸마다의 비용 · 부모 · 상태 배열을 들고 있다가 다음 물음에 다시 씁니다 — 물음마다 배열을 비우지 않고 세대 번호로 낡은 값을 가립니다.
     * @details 언리얼 `UNavigationSystemV1::FindPathSync` · 유니티 `NavMesh.CalculatePath` 의 격자판입니다. 스레드 하나에 하나씩 둡니다(배열을 고쳐 쓴다).
     *          휴리스틱은 옥타일 거리 × 격자의 가장 작은 칸 값이라 과대평가하지 않습니다(가장 싼 길을 찾는다). 가장 작은 값은 격자(주소 · 리비전)가 바뀔 때만 다시 셉니다.
     */
    class SW_GF_API GridPathfinder
    {
    public:
        GridPathfinder();

        /** @brief 경로를 찾아 @p outListCell 에 시작부터 끝까지의 칸을 채웁니다(다듬었으면 꺾이는 칸만). */
        GridPathResult findPath( const NavGrid& grid, const GridPathQuery& query, vector<int2>& outListCell );
        /** @brief 칸 경로를 칸 가운데의 월드 자리로 바꿉니다. */
        static void makeWorldPath( const NavGrid& grid, const vector<int2>& listCell, vector<float3>& outListPoint );
        /** @brief 칸 경로의 길이(월드 단위)입니다. */
        static float32 computePathLength( const NavGrid& grid, const vector<int2>& listCell );

        /** @brief 마지막 물음에서 펼친 칸 수입니다(예산 · 프로파일). */
        uint32 getLastExpansionCount() const { return _lastExpansionCount; }

    private:
        /** @brief 열린 목록 항목 — f 가 작은 것이 위(최소 힙). */
        struct OpenEntry
        {
            float32 _score;
            int32   _index;
        };

        void    prepare( const NavGrid& grid );
        void    smoothPath( const NavGrid& grid, vector<int2>& inoutListCell ) const;
        float32 computeHeuristic( const int2& from, const int2& to ) const;

        vector<float32>   _listCostSoFar;
        vector<int32>     _listParent;
        vector<uint32>    _listStamp; ///< 이번 물음에서 본 칸이면 `_stamp`, 닫혔으면 `_stamp + 1`
        vector<OpenEntry> _listOpen;
        uint32            _stamp;
        const NavGrid*    _pCachedGrid; ///< 가장 작은 칸 값을 셌던 격자 — 리비전만으로는 다른 격자를 가리지 못한다(새 격자도 같은 번호에서 시작)
        uint32            _gridRevision;
        int32             _gridCellCount;
        float32           _minCellCost;
        uint32            _lastExpansionCount;
    };
} // namespace sw
