/**
 * @file RecastNavMesh.h
 * @brief Recast(베이크)와 Detour(타일 내비메시와 질의)로 구현한 `INavMesh` 입니다. 라이브러리 헤더는 .cpp 에서만 include 합니다.
 * @details 타일 하나를 베이크하는 순서는 Recast 표준이다. 복셀화(삼각형 영역과 경사), 낮게 걸린 장애물과 턱과 낮은 천장 걸러 내기, 압축 높이장, 몸 반지름만큼 깎기,
 *          볼록 부피 칠하기(뚫는 부피는 반지름만큼 넓힌다), 분수령 영역, 외곽선, 폴리곤(꼭짓점 6 개까지), 높이 디테일, Detour 타일 바이트(시작점이 이 타일에 포함된
 *          오프 메시 링크 포함) 순서다.
 *          영역 번호 i 는 Recast 영역 i + 1(0 은 걷지 못함)이고, 폴리곤 표시 비트는 1 << i 다. 질의 필터의 영역 비트가 그대로 표시 비트다.
 *          폴리곤 참조는 32 비트(Detour 기본)이고 엔진 쪽 64 비트에 담는다.
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
    class RecastTileCache;

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
        void               setOffMeshLinkEnabled( uint32 linkID, bool bEnabled ) override;
        bool               isOffMeshLinkEnabled( uint32 linkID ) const override;
        NavObstacleMode    getObstacleMode() const override { return _agentType._obstacleMode; }
        NavObstacleRef     addCacheObstacle( const NavCacheObstacleDesc& desc ) override;
        void               removeCacheObstacle( NavObstacleRef obstacle ) override;
        bool               updateCacheObstacles( float32 deltaSeconds ) override;

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
        /** @brief 이 스레드의 질의 객체입니다(스크래치 슬롯마다 하나이며 처음 쓸 때 만듭니다). 슬롯 밖의 스레드는 잠금 아래 하나를 나눠 씁니다. */
        dtNavMeshQuery* acquireQuery( bool& outLocked ) const;
        void            releaseQuery( bool bLocked ) const;
        void            freeQueries();
        /** @brief 타일 자리 하나(모든 층)의 오프 메시 링크 폴리곤 표시를 막힌 목록에 맞춥니다(막힘 0, 아니면 영역 비트). */
        void applyLinkFlags( int32 tileX, int32 tileZ );
        /** @brief 든 모든 타일의 링크 표시를 다시 맞춥니다(막힌 링크가 있을 때 — 링크 막기 · 장애물 캐시가 타일을 다시 지은 뒤). */
        void applyLinkFlagsToAllTiles();

        NavAgentTypeDef                 _agentType;
        NavTileGrid                     _grid;
        vector<uint32>                  _listDisabledLinkID; ///< 막힌 링크 번호(정렬)
        mutable vector<dtNavMeshQuery*> _listQuery;          ///< 스크래치 슬롯마다 하나다(한 스레드만 자기 슬롯을 만진다).
        mutable dtNavMeshQuery*         _pSharedQuery;       ///< 슬롯 밖 스레드용(`_sharedQueryMutex`)
        mutable mutex                   _sharedQueryMutex;
        dtNavMesh*                      _pNavMesh;
        unique_ptr<RecastTileCache>     _pTileCache; ///< 장애물 캐시(`NavObstacleMode::TileCache` 일 때만 있다)
        float32                         _minY;
        float32                         _maxY;
        uint32                          _revision;
    };
} // namespace sw
