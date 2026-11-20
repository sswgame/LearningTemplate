/**
 * @file NavClusterGraph.h
 * @brief 내비메시의 대략 그래프입니다 — 타일 안에서 이어진 폴리곤 덩어리(클러스터) 하나가 노드, 타일 경계 · 오프 메시 링크로 이어진 두 클러스터가 간선입니다.
 * @details 스트리밍 월드의 먼 경로(계층 경로)가 씁니다. 쿠킹이 모든 타일을 끼운 내비메시에서 짓고(`INavMeshTileStore::collectClusterGraph`) 쿠킹본 항목에 싣습니다
 *          (`NavMeshAssetEntry::_clusterGraph`). 런타임은 타일이 오르지 않은 구역도 이 그래프로 지나갈 클러스터 열을 정하고, 오른 구간만 Detour 로 세밀하게 걷습니다
 *          (`NavStreamRoute`). 언리얼 Recast 의 클러스터 링크(`dtNavMesh` cluster · `FindHierarchicalPath`) · World Partition 내비 청크의 자리입니다.
 *
 *          **모양.** 클러스터는 (타일 Z, 타일 X) 순으로 놓이고 간선은 출발 클러스터 순의 묶음(CSR)입니다. 간선은 방향이 있습니다(한 방향 링크).
 *          비용은 두 중심 사이 거리입니다 — 영역 비용 · 막힌 링크는 보지 않는다(세밀 경로가 본다. 막혀 있으면 다가가며 다시 계획한다).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

namespace sw
{
    class ByteReader;
    class ByteWriter;

    /** @brief 클러스터 하나 — 타일 하나 안에서 이어진 폴리곤 덩어리입니다. `_center` 는 그 덩어리의 폴리곤 중심 하나(내비메시 위 점)입니다. */
    struct NavCluster
    {
        float3 _center{};
        int32  _tileX{ 0 };
        int32  _tileZ{ 0 };
        uint32 _firstEdge{ 0 }; ///< `NavClusterGraph::getEdges` 에서 이 클러스터 간선의 첫 자리
        uint32 _edgeCount{ 0 };
    };

    /** @brief 클러스터에서 나가는 간선 하나입니다. */
    struct NavClusterEdge
    {
        uint32  _target{ 0 };
        float32 _cost{ 0.0f };
    };

    /** @brief 짓는 쪽이 넘기는 연결 하나(출발 → 도착 클러스터 자리)입니다. 같은 쌍이 여럿이어도 된다 — 짓기가 하나로 줄인다. */
    struct NavClusterLink
    {
        uint32 _from{ 0 };
        uint32 _to{ 0 };
    };

    /** @class NavClusterGraph @brief 파일 머리말 참고. */
    class SW_API NavClusterGraph
    {
    public:
        static constexpr uint32 kNoCluster = constant::invalid_index::kUint32;

        void clear();
        bool isEmpty() const { return _listCluster.empty(); }

        /**
         * @brief 클러스터(간선 칸은 무시한다)와 연결로 그래프를 짓습니다. 클러스터는 (타일 Z, 타일 X) 순이어야 하고, 연결의 자리는 클러스터 수보다 작아야 합니다.
         * @return 순서 · 자리가 어긋나면 오류를 남기고 비운 채 false 입니다.
         */
        [[nodiscard]] bool build( vector<NavCluster> listCluster, const vector<NavClusterLink>& listLink );

        /** @brief 타일 (@p tileX, @p tileZ) 의 클러스터 가운데 중심이 @p position 에 가장 가까운 것입니다. 그 타일에 클러스터가 없으면 `kNoCluster` 입니다. */
        uint32 findCluster( int32 tileX, int32 tileZ, const float3& position ) const;
        /**
         * @brief @p start 에서 @p goal 까지 지나는 클러스터 열(두 끝 포함)을 A* 로 찾습니다. 휴리스틱은 중심 사이 거리라 비용(같은 거리)을 넘지 않는다.
         * @return 이어지지 않았거나 자리가 틀리면 false 이고 @p outListRoute 는 빈다.
         */
        [[nodiscard]] bool findRoute( uint32 start, uint32 goal, vector<uint32>& outListRoute ) const;

        const vector<NavCluster>&     getClusterList() const { return _listCluster; }
        const vector<NavClusterEdge>& getEdges() const { return _listEdge; }

        /** @brief 쿠킹본 머리에 씁니다(클러스터 수 · 클러스터 · 간선 수 · 간선). */
        void write( ByteWriter& writer ) const;
        /** @brief `write` 가 쓴 것을 읽습니다. 순서 · 자리 · 유한한 값이 아니면 비우고 false 입니다. */
        [[nodiscard]] bool read( ByteReader& reader );

    private:
        /** @brief 읽거나 지은 표가 규칙(순서 · 자리 · 유한한 값)에 맞으면 true 입니다. */
        bool isWellFormed() const;

    private:
        vector<NavCluster>     _listCluster;
        vector<NavClusterEdge> _listEdge;
    };
} // namespace sw
