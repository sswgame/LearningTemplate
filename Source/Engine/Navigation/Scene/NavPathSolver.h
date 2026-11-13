/**
 * @file NavPathSolver.h
 * @brief 비동기 내비메시 경로 요청(`SceneNavigation::requestPath`)의 일(`NavPathJob`)과 그 풀이(`NavPathSolver`)입니다 — `PathRequestQueue<NavPathSolver>` 의 변형 부분.
 * @details 풀이는 게임 스레드에서 에이전트 종류를 내비메시 · 찾는 범위 · 거름으로 풀어 일에 적고(`prepareBatch`), 워커는 그 내비메시에 질의만 합니다(`solve` — 스레드마다 질의 객체).
 *          일은 거름을 **사본**으로 들어, 워커가 도는 동안 게임 스레드가 종류의 거름을 바꿔도(`setAgentAreaCost`) 이 질의는 넘길 때의 값으로 끝난다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Navigation/NavigationTypes.h"

namespace sw
{
    class INavMeshQuery;
    class SceneNavigation;

    /** @brief 경로 요청 하나 — 질의(종류 · 시작 · 끝 · 거름)와 결과(`_path`)입니다. 요청자는 질의 칸만 채우고, 풀이가 나머지를 채웁니다. */
    struct NavPathJob
    {
        hashed_string        _agentType{};            ///< 빈 이름은 기본 종류(`NavMeshSettings::findAgentType`)
        float3               _start{};                ///< 시작(월드)
        float3               _end{};                  ///< 끝(월드)
        NavQueryFilter       _filter{};               ///< `_bCustomFilter` 면 요청자의 거름, 아니면 풀이가 넘길 때 종류의 거름을 베껴 둔다
        float3               _searchExtent{};         ///< 풀이가 채운다 — 종류의 몸 크기로 정한 붙이기 범위
        const INavMeshQuery* _pNavMesh{ nullptr };    ///< 풀이가 채운다 — 아직 마련하지 않은 종류면 nullptr(결과는 `Failed`)
        NavPath              _path{};                 ///< 결과
        bool                 _bCustomFilter{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };

    /** @class NavPathSolver @brief `PathRequestQueue` 의 풀이 — 파일 머리말 참고. `SceneNavigation` 의 값 멤버이고 그 내비메시 표를 읽습니다. */
    class NavPathSolver
    {
    public:
        using Job = NavPathJob;

        explicit NavPathSolver( const SceneNavigation& navigation )
            : _pNavigation{ &navigation }
        {
        }

        /** @brief 일마다 종류의 내비메시 · 범위 · 거름을 적습니다(게임 스레드, 넘기기 직전 — 그 사이 타일 교체가 없다). 구간 스크래치는 쓰지 않습니다(질의 객체는 내비메시가 스레드마다 둔다). */
        void prepareBatch( vector<NavPathJob>& inoutListJob, uint32 chunkCount ) const;
        /** @brief 경로 하나를 구합니다(워커). 내비메시가 없으면 `Failed` 입니다. */
        void solve( NavPathJob& inoutJob, uint32 chunkIndex ) const;

    private:
        const SceneNavigation* _pNavigation;
    };
} // namespace sw
