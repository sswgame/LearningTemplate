/**
 * @file DependencyGraph.h
 * @brief 노드 번호와 "먼저 와야 하는 노드" 간선으로 된 의존 그래프와 그 위상 정렬 · 순환 찾기입니다.
 * @details 그래프 형식(머티리얼 그래프 · 오디오 그래프 · 믹서 버스 · 기믹 회로 · 장비 조건)과 기동 순서(서브시스템 · 모듈 적재 · 모듈 다시 읽기 · 에셋 다시 읽기 연쇄)가
 *          이 한 벌을 쓴다. 전에는 형식마다 Kahn · 깊이 우선 정렬과 순환 찾기를 따로 썼다. 언리얼도 머티리얼 · 메타사운드 · 모듈이 각자 정렬을 가진다(기억에 의존) —
 *          여기서는 알고리즘을 하나로 두고, 쓰는 쪽은 **동점을 무엇으로 깨는가**만 고른다(데이터 순서 · 이름 · 뿌리에서의 깊이 우선).
 *          - 실패는 로그가 아니라 값(`DependencyOrder::_listCycle`)이다. 편집기는 노드에 붙이고, 로더는 자기 오류 이름으로 경로를 남긴다.
 *          - 결과는 간선과 고른 규칙만으로 정해진다. 같은 데이터면 같은 순서다(결정성 — 소리 난수 · 기믹 스텝 · 생성 셰이더의 해시가 여기에 묶인다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 정렬 결과입니다. 성공하면 `_listOrder`, 실패하면 `_listCycle`(과 Kahn 이면 `_listUnsorted`)이 찹니다. */
    struct DependencyOrder
    {
        vector<uint32> _listOrder;    ///< 위상 순서 — 의존이 먼저다(성공했을 때)
        vector<uint32> _listCycle;    ///< 순환 하나의 경로 `A, B, …, A` — A 가 B 를 기다리고 …(실패했을 때)
        vector<uint32> _listUnsorted; ///< 정렬하지 못한 노드 — 순환 안이나 순환에 매달린 노드(Kahn 이 실패했을 때)

        /** @brief 모든 노드(깊이 우선이면 뿌리에서 닿는 노드)를 정렬했으면 true 입니다. */
        [[nodiscard]] bool isSorted() const { return _listCycle.empty() && _listUnsorted.empty(); }
    };

    /**
     * @class DependencyGraph
     * @brief 파일 머리말 참고. 노드는 0 부터의 번호이고, 이름 · 내용은 쓰는 쪽의 목록이 같은 번호로 가진다.
     */
    class SW_API DependencyGraph final
    {
    public:
        /** @brief 노드 @p nodeCount 개, 간선 없는 그래프입니다. */
        explicit DependencyGraph( uint32 nodeCount );

        /** @brief @p node 가 @p dependency 뒤에 온다는 간선을 더합니다. 같은 간선을 두 번 더하면 두 번 셉니다(정렬 결과는 같다). 자기 자신이면 순환입니다. */
        void addDependency( uint32 node, uint32 dependency );

        /** @brief 노드 수입니다. */
        [[nodiscard]] uint32 getNodeCount() const;
        /** @brief @p node 가 기다리는 노드들입니다(더한 차례). */
        [[nodiscard]] const vector<uint32>& getDependencies( uint32 node ) const;

        /** @brief Kahn 정렬 — 준비된 노드 가운데 **번호가 가장 작은** 것을 먼저 꺼냅니다. 결과가 데이터의 차례에 묶입니다(소리 그래프 · 믹서 · 기믹 회로). */
        [[nodiscard]] DependencyOrder sortInDataOrder() const;
        /**
         * @brief Kahn 정렬 — 준비된 노드 가운데 **이름이 가장 앞인** 것을 먼저 꺼냅니다. 결과가 입력 차례와 무관합니다(서브시스템 · 모듈 적재 · `cmake/Modules/Build/ModuleManifest.cmake`).
         * @param listName 노드 이름. 노드 수와 같아야 합니다. 같은 이름이면 번호가 작은 쪽이 먼저다
         */
        [[nodiscard]] DependencyOrder sortByName( const vector<string_view>& listName ) const;
        /**
         * @brief 깊이 우선 후위 정렬 — @p listRoot 차례로, 각 노드는 간선을 더한 차례로 내려갑니다. **뿌리에서 닿는 노드만** 담습니다(머티리얼 그래프의 쓰이지 않는 노드는 펼치지 않는다).
         * @return 첫 순환을 만나면 거기서 멈추고 `_listCycle` 에 그 경로를 담습니다(`_listOrder` 는 비운다).
         */
        [[nodiscard]] DependencyOrder sortDepthFirst( const vector<uint32>& listRoot ) const;
        /** @brief @p listCandidate 안의 간선만 따라 순환 하나를 찾아 경로(`A, B, …, A`)로 돌려줍니다. 없으면 빈 목록입니다. 후보 차례 · 간선 차례대로 찾습니다. */
        [[nodiscard]] vector<uint32> findCycle( const vector<uint32>& listCandidate ) const;

    private:
        /** @brief 준비된 노드 가운데 @p listRank 가 가장 작은 것을 먼저 꺼내는 Kahn 정렬입니다. 실패하면 남은 노드에서 순환을 찾아 담습니다. */
        [[nodiscard]] DependencyOrder sortByRank( const vector<uint32>& listRank ) const;

    private:
        vector<vector<uint32>> _listDependency; ///< 노드마다 먼저 와야 하는 노드 번호
    };
} // namespace sw
