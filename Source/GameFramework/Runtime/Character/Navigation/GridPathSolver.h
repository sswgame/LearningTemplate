/**
 * @file GridPathSolver.h
 * @brief 격자 A* 경로 요청의 일(`GridPathJob`)과 풀이(`GridPathSolver`)입니다 — 엔진 요청 줄 `PathRequestQueue<GridPathSolver>` 의 변형 부분.
 * @details 대상 수만큼 같은 걸음에 경로를 다시 구하는 곳(RTS 유닛 — 다가가기 · 쫓기)이 요청을 줄에 세우고, 걸음 끝에 한 번 워커로 풀어(`launch` → `complete`) 요청 순서대로
 *          결과를 적용한다. 구간마다 탐색기(`GridPathfinder` — 배열을 고쳐 쓰므로 스레드마다 하나)를 하나씩 둔다. 질의는 서로 독립이고 탐색기의 상태는 결과에 들지 않아
 *          구간 수 · 나눈 모양과 상관없이 같은 결과다(결정적 고정 걸음 시뮬레이션이 그대로 쓴다).
 *          한 번 묻는 경로(`NavAgent::moveTo` — `NavGridMover`, 일정의 화면 안 경로)는 동기로 둔다 — 그 자리에서 답이 필요하고 대상 수에 비례해 매 걸음 돌지 않는다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Character/Navigation/GridPathfinder.h"

namespace sw
{
    class NavGrid;

    /** @brief 격자 경로 요청 하나 — 질의(시작 · 목표 월드 자리)와 결과(`NavAgent::planPath` 의 월드 경로)입니다. */
    struct GridPathJob
    {
        float3         _from{};                           ///< 시작(월드) — 요청한 때의 자리
        float3         _target{};                         ///< 목표(월드)
        vector<float3> _listPoint{};                      ///< 결과 — 행위자가 따라갈 경로(`NavAgent::followPath`)
        GridPathResult _result{ GridPathResult::NoPath }; ///< 결과 — `Found` · `Partial` 이면 따라간다
    };

    /** @class GridPathSolver @brief `PathRequestQueue` 의 격자 풀이 — 파일 머리말 참고. 격자를 빌리므로 격자보다 먼저 사라진다. */
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
