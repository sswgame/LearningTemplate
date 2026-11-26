/**
 * @file GridPathfinder.h
 * @brief 격자 A* 입니다. 8 방향(모서리 깎기 없음), 셀 값 가중, 옥타일 휴리스틱, 탐색 예산을 지원합니다.
 * @details 닿지 못하면 가장 가까운 곳까지 가는 경로를 내고, 줄 당기기(string pulling)로 경로를 다듬습니다.
 *          요청 줄의 풀이(`GridPathJob` · `GridPathSolver`)도 둡니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Grid/GridTopology.h"

namespace sw
{
    class NavGrid;

    /** @brief 경로 하나를 묻는 셀입니다. */
    struct GridPathQuery
    {
        int2   _start{};
        int2   _goal{};
        uint32 _maxExpansions{ 0 }; ///< 펼칠 셀 수 상한(0 = 무제한). 넘으면 그때까지 가장 가까운 곳까지의 부분 경로
        uint8  _bAllowDiagonal{ SW_TRUE };
        uint8  _bSmooth{ SW_TRUE };        ///< 시선이 닿는 꺾임을 건너뛴다(계단 모양 → 곧은 선)
        uint8  _bAcceptPartial{ SW_TRUE }; ///< 못 닿으면 가장 가까운 곳까지라도(RTS 의 "막힌 곳 근처로")
    };
} // namespace sw

namespace sw
{
    /** @brief 경로 결과입니다. */
    ENUM()
    enum class GridPathResult : uint8
    {
        Found = 0,   ///< 목적지까지
        Partial,     ///< 못 닿거나 예산이 다해 가장 가까운 곳까지
        NoPath,      ///< 갈 곳이 없다(부분 경로를 받지 않을 때)
        InvalidStart ///< 시작 셀이 밖이거나 막혔다
    };

    /**
     * @class GridPathfinder
     * @brief A* 탐색기 하나입니다. 셀마다의 비용, 부모, 상태 배열을 보관했다가 다음 질의에 다시 씁니다. 질의마다 배열을 비우지 않고 세대 번호로 낡은 값을 가립니다.
     * @details 언리얼 `UNavigationSystemV1::FindPathSync` · 유니티 `NavMesh.CalculatePath` 의 격자판입니다. 스레드 하나에 하나씩 둡니다(배열을 고쳐 쓴다).
     *          휴리스틱은 옥타일 거리 × 격자의 가장 작은 셀 값이라 과대평가하지 않습니다(가장 싼 길을 찾는다). 가장 작은 값은 격자(주소 · 리비전)가 바뀔 때만 다시 셉니다.
     */
    class SW_GF_API GridPathfinder
    {
    public:
        GridPathfinder();

        /** @brief 경로를 찾아 @p outListCell 에 시작부터 끝까지의 셀을 채웁니다(다듬었으면 꺾이는 셀만). */
        GridPathResult findPath( const NavGrid& grid, const GridPathQuery& query, vector<int2>& outListCell );
        /** @brief 셀 경로를 셀 가운데의 월드 위치로 바꿉니다. */
        static void makeWorldPath( const NavGrid& grid, const vector<int2>& listCell, vector<float3>& outListPoint );
        /** @brief 셀 경로의 길이(월드 단위)입니다. */
        static float32 computePathLength( const NavGrid& grid, const vector<int2>& listCell );

        /** @brief 마지막 물음에서 펼친 셀 수입니다(예산 · 프로파일). */
        uint32 getLastExpansionCount() const { return _lastExpansionCount; }

    private:
        /** @brief 열린 목록 항목입니다. f(`_cost`)가 작은 것이 위에 옵니다(최소 힙). */
        using OpenEntry = GridOpenEntry<float32>;

        void    prepare( const NavGrid& grid );
        void    smoothPath( const NavGrid& grid, vector<int2>& inoutListCell ) const;
        float32 computeHeuristic( const int2& from, const int2& to ) const;

    private:
        vector<float32>   _listCostSoFar;
        vector<int32>     _listParent;
        vector<uint32>    _listStamp; ///< 이번 물음에서 본 셀이면 `_stamp`, 닫혔으면 `_stamp + 1`
        vector<OpenEntry> _listOpen;
        uint32            _stamp;
        const NavGrid*    _pCachedGrid; ///< 가장 작은 셀 값을 셌던 격자. 새 격자도 같은 번호에서 시작하므로 리비전만으로는 다른 격자를 구별하지 못한다
        uint32            _gridRevision;
        int32             _gridCellCount;
        float32           _minCellCost;
        uint32            _lastExpansionCount;
    };

    class NavGrid;

    /** @brief 격자 경로 요청 하나 — 질의(시작 · 목표 월드 자리)와 결과(`NavAgent::planPath` 의 월드 경로)입니다. */
    struct GridPathJob
    {
        float3         _from{};                           ///< 시작(월드) — 요청한 때의 자리
        float3         _target{};                         ///< 목표(월드)
        vector<float3> _listPoint{};                      ///< 결과 — 행위자가 따라갈 경로(`NavAgent::followPath`)
        GridPathResult _result{ GridPathResult::NoPath }; ///< 결과 — `Found` · `Partial` 이면 따라간다
    };

    /**
     * @class GridPathSolver
     * @brief 격자 A* 경로 요청의 풀이입니다 — 엔진 요청 줄 `PathRequestQueue<GridPathSolver>` 의 변형 부분. 격자를 빌리므로 격자보다 먼저 사라진다.
     * @details 대상 수만큼 같은 걸음에 경로를 다시 구하는 곳(RTS 유닛 — 다가가기 · 쫓기)이 요청을 줄에 세우고, 걸음 끝에 한 번 워커로 풀어(`launch` → `complete`) 요청 순서대로
     *          결과를 적용한다. 구간마다 탐색기(`GridPathfinder` — 배열을 고쳐 쓰므로 스레드마다 하나)를 하나씩 둔다. 질의는 서로 독립이고 탐색기의 상태는 결과에 들지 않아
     *          구간 수 · 나눈 모양과 상관없이 같은 결과다(결정적 고정 걸음 시뮬레이션이 그대로 쓴다).
     *          한 번 묻는 경로(`NavAgent::moveTo` — `NavGridMover`, 일정의 화면 안 경로)는 동기로 둔다 — 그 자리에서 답이 필요하고 대상 수에 비례해 매 걸음 돌지 않는다.
     */
    class SW_GF_API GridPathSolver
    {
    public:
        using Job = GridPathJob;

        explicit GridPathSolver( const NavGrid& grid );

        /** @brief 구간마다 탐색기를 마련합니다(게임 스레드, 넘기기 직전). 탐색기는 다음 묶음에 다시 쓴다(배열을 새로 잡지 않는다). */
        void prepareBatch( vector<GridPathJob>& inoutListJob, uint32 chunkCount );
        /** @brief 경로 하나를 그 구간의 탐색기로 구합니다(워커). 격자는 읽기만 한다 — 격자를 바꾸는 쪽은 수확(`complete`) 뒤에만 바꾼다. */
        void solve( GridPathJob& inoutJob, uint32 chunkIndex );

    private:
        const NavGrid*         _pGrid;
        vector<GridPathfinder> _listPathfinder;   ///< 구간 하나에 하나(`prepareBatch` 가 구간 수만큼 늘린다)
        GridPathfinder*        _pChunkPathfinder; ///< `_listPathfinder` 의 첫 칸 — 워커는 컨테이너를 만지지 않고 이 포인터로 자기 구간 칸만 쓴다(`prepareBatch` 가 잡는다)
    };
} // namespace sw
