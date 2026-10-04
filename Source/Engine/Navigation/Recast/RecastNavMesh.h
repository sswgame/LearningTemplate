/**
 * @file RecastNavMesh.h
 * @brief Recast(베이크) · Detour(타일 내비메시 · 질의)로 구현한 `INavMesh` 입니다. 라이브러리 헤더는 .cpp 에서만 include 합니다.
 * @details 타일 하나 베이크는 Recast 표준 순서입니다 — 복셀화(삼각형 영역 · 경사) → 낮게 걸린 장애물 · 턱 · 낮은 천장 거름 → 압축 높이장 → 몸 반지름만큼 깎기
 *          → 볼록 부피 칠하기(뚫는 부피는 반지름만큼 넓힌다) → 분수령 영역 → 외곽선 → 폴리곤(꼭짓점 6 개까지) → 높이 디테일 → Detour 타일 바이트.
 *          영역 번호 i 는 Recast 영역 i + 1(0 은 걷지 못함)이고, 폴리곤 표시 비트는 1 << i 입니다 — 질의 거름의 영역 비트가 그대로 표시 비트입니다.
 *          폴리곤 참조는 32 비트(Detour 기본)이고 엔진 쪽 64 비트에 담습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"

#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/NavMeshSettings.h"

class dtNavMesh;
class dtNavMeshQuery;
class dtQueryFilter;

namespace sw
{
    /** @class RecastNavMesh @brief 파일 머리말 참고. */
    class RecastNavMesh final : public INavMesh
    {
    public:
        RecastNavMesh();
        ~RecastNavMesh() override;

        RecastNavMesh( const RecastNavMesh& )            = delete;
        RecastNavMesh& operator=( const RecastNavMesh& ) = delete;

        [[nodiscard]] bool initialize( const NavAgentTypeDef& agentType, const NavMeshSettings& areaTable, const AABB& bounds ) override;
        void               shutdown() override;

        const NavTileGrid&     getTileGrid() const override { return _grid; }
        const NavAgentTypeDef& getAgentType() const override { return _agentType; }
        void                   collectTilesOverlapping( const AABB& bounds, vector<int2>& outListTile ) const override;

        [[nodiscard]] bool bakeTile( const NavMeshGeometry& geometry, const vector<NavConvexVolume>* pExtraVolume, int32 tileX, int32 tileZ,
                                     NavTileData& outTile ) const override;
        [[nodiscard]] bool replaceTile( const NavTileData& tile ) override;
        void               collectTiles( vector<NavTileData>& outListTile ) const override;
        uint32             getLoadedTileCount() const override;
        uint32             getPolygonCount() const override;
        uint32             getRevision() const override { return _revision; }

        [[nodiscard]] bool findNearestPoint( const float3& position, const float3& searchExtent, const NavQueryFilter& filter, NavLocation& outLocation ) const override;
        NavPathStatus      findPath( const float3& start, const float3& end, const float3& searchExtent, const NavQueryFilter& filter, NavPath& outPath ) const override;
        [[nodiscard]] bool raycast( const float3& start, const float3& end, const float3& searchExtent, const NavQueryFilter& filter, NavRaycastHit& outHit ) const override;

        unique_ptr<INavCrowd> createCrowd( uint32 maxAgentCount, float32 maxAgentRadius ) override;
        void                  drawDebug( IPhysicsDebugRenderer& renderer, const float4& color ) const override;
        void                  collectDebugTriangles( vector<float3>& outListCorner, vector<uint8>& outListArea ) const override;

        /** @brief Detour 내비메시입니다(같은 백엔드의 군중만 씁니다). */
        dtNavMesh* getDetourNavMesh() const { return _pNavMesh; }
        /** @brief 엔진 거름을 Detour 거름으로 옮깁니다(군중도 같은 규칙). */
        static void applyQueryFilter( const NavQueryFilter& filter, dtQueryFilter& outFilter );

    private:
        /** @brief 이 스레드의 질의 객체입니다(스크래치 슬롯마다 하나, 처음 쓸 때 만든다). 슬롯 밖의 스레드는 잠금 아래 하나를 나눠 쓴다. */
        dtNavMeshQuery* acquireQuery( bool& outLocked ) const;
        void            releaseQuery( bool bLocked ) const;
        void            freeQueries();

        NavAgentTypeDef                 _agentType;
        NavTileGrid                     _grid;
        mutable vector<dtNavMeshQuery*> _listQuery;    ///< 스크래치 슬롯마다(한 스레드만 자기 칸을 만진다)
        mutable dtNavMeshQuery*         _pSharedQuery; ///< 슬롯 밖 스레드용(`_sharedQueryMutex`)
        mutable mutex                   _sharedQueryMutex;
        dtNavMesh*                      _pNavMesh;
        float32                         _minY;
        float32                         _maxY;
        uint32                          _revision;
    };
} // namespace sw
