#include "pch.h"

#include "Engine/Navigation/Recast/RecastNavMesh.h"

#include "Core/Math/MathUtil.h"
#include "Core/Memory/MemoryTag.h"

#include "Engine/Common/EngineParallel.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Navigation/Recast/DetourNavCrowd.h"
#include "Engine/Navigation/Recast/RecastAreaUtil.h"
#include "Engine/Navigation/Recast/RecastTileCache.h"
#include "Engine/Physics/PhysicsDebugDraw.h"

#include <recastnavigation/DetourAlloc.h>
#include <recastnavigation/DetourNavMesh.h>
#include <recastnavigation/DetourNavMeshBuilder.h>
#include <recastnavigation/DetourNavMeshQuery.h>
#include <recastnavigation/Recast.h>
#include <recastnavigation/RecastAlloc.h>

namespace sw
{
    SW_LOG_CALLER( "RecastNavMesh" );

    namespace
    {
        struct RecastNavMeshInternal
        {
            /** @brief 질의 객체 하나의 노드 수(경로 찾기가 한 번에 펼치는 폴리곤 상한)입니다. */
            static constexpr int32 kMaxQueryNodeCount = 4096;
            /** @brief 경로가 지나는 폴리곤의 상한입니다. */
            static constexpr int32 kMaxPathPolyCount = 512;
            /** @brief 폴리곤 참조 비트(Detour 32 비트 참조) 중 타일과 폴리곤에 줄 수 있는 비트 수입니다(나머지 10 비트는 세대입니다). */
            static constexpr uint32 kTileAndPolyBits = 22;
            /** @brief 타일 비트의 상한(나머지가 타일당 폴리곤)입니다. */
            static constexpr uint32 kMaxTileBits = 14;

            static void* allocate( size_t size, rcAllocHint hint )
            {
                (void)hint;
                return Memory::allocate( size, MemoryTag::Navigation );
            }

            static void* allocateDetour( size_t size, dtAllocHint hint )
            {
                (void)hint;
                return Memory::allocate( size, MemoryTag::Navigation );
            }

            static void free( void* pPtr )
            {
                if ( pPtr != nullptr )
                    Memory::free( pPtr );
            }

            /** @brief Recast 와 Detour 의 할당을 엔진 할당기(태그 Navigation)로 돌립니다. 처음 만들 때 한 번 호출합니다. */
            static void routeAllocatorsOnce()
            {
                static const bool s_bRouted = []()
                {
                    rcAllocSetCustom( &RecastNavMeshInternal::allocate, &RecastNavMeshInternal::free );
                    dtAllocSetCustom( &RecastNavMeshInternal::allocateDetour, &RecastNavMeshInternal::free );
                    return true;
                }();
                (void)s_bRouted;
            }

            static uint32 computeBitCount( uint32 value )
            {
                uint32 bits = 0;
                while ( ( 1u << bits ) < value )
                {
                    ++bits;
                }
                return bits;
            }

            /** @brief 링크 그림의 활 높이(두 끝 거리에 대한 비율)와 마디 수입니다. */
            static constexpr float32 kLinkArcHeightRatio  = 0.25f;
            static constexpr uint32  kLinkArcSegmentCount = 8;

            /** @brief 베이크 중간 결과를 보관하고 끝에서 해제합니다(실패로 빠져나가도 해제합니다). */
            struct BuildScratch
            {
                rcHeightfield*        _pSolid{ nullptr };
                rcCompactHeightfield* _pCompact{ nullptr };
                rcContourSet*         _pContour{ nullptr };
                rcPolyMesh*           _pPolyMesh{ nullptr };
                rcPolyMeshDetail*     _pDetail{ nullptr };

                BuildScratch()                                 = default;
                BuildScratch( const BuildScratch& )            = delete;
                BuildScratch& operator=( const BuildScratch& ) = delete;
                ~BuildScratch()
                {
                    rcFreeHeightField( _pSolid );
                    rcFreeCompactHeightfield( _pCompact );
                    rcFreeContourSet( _pContour );
                    rcFreePolyMesh( _pPolyMesh );
                    rcFreePolyMeshDetail( _pDetail );
                }
            };

            static float3 toFloat3( const float32* pValue ) { return float3{ pValue[0], pValue[1], pValue[2] }; }

            // ------------------------------------------------------------------------------
            // 타일 베이크 단계 — `RecastNavMesh::bakeTile` 이 위에서 아래로 부른다.
            // ------------------------------------------------------------------------------

            /** @brief 에이전트 설정과 타일 범위(베이크 테두리 포함)로 Recast 구성을 만듭니다. */
            static rcConfig makeConfig( const NavAgentTypeDef& agent, const AABB& tileBounds, float32 minY, float32 maxY )
            {
                rcConfig config{};
                config.cs                     = agent._cellSize;
                config.ch                     = agent._cellHeight;
                config.walkableSlopeAngle     = MathUtil::toDegree( agent._maxSlope );
                config.walkableHeight         = static_cast<int32>( MathUtil::ceil( agent._height / config.ch ) );
                config.walkableClimb          = static_cast<int32>( MathUtil::floor( agent._maxClimb / config.ch ) );
                config.walkableRadius         = static_cast<int32>( MathUtil::ceil( agent._radius / config.cs ) );
                config.maxEdgeLen             = static_cast<int32>( agent._maxEdgeLength / config.cs );
                config.maxSimplificationError = agent._maxEdgeError;
                config.minRegionArea          = static_cast<int32>( agent._minRegionSize * agent._minRegionSize );
                config.mergeRegionArea        = static_cast<int32>( agent._mergeRegionSize * agent._mergeRegionSize );
                config.maxVertsPerPoly        = DT_VERTS_PER_POLYGON;
                config.tileSize               = static_cast<int32>( agent._tileSize );
                config.borderSize             = config.walkableRadius + 3;
                config.width                  = config.tileSize + config.borderSize * 2;
                config.height                 = config.tileSize + config.borderSize * 2;
                config.detailSampleDist       = agent._detailSampleDistance < 0.9f ? 0.0f : config.cs * agent._detailSampleDistance;
                config.detailSampleMaxError   = config.ch * agent._detailSampleMaxError;

                const float32 border = static_cast<float32>( config.borderSize ) * config.cs;
                config.bmin[0]       = tileBounds._min._x - border;
                config.bmin[1]       = minY;
                config.bmin[2]       = tileBounds._min._z - border;
                config.bmax[0]       = tileBounds._max._x + border;
                config.bmax[1]       = maxY;
                config.bmax[2]       = tileBounds._max._z + border;
                return config;
            }

            /**
             * @brief 타일에 닿는 삼각형을 복셀화하고 걸을 수 있는 영역을 깎아 압축 높이장(`scratch._pCompact`)을 만듭니다. 실패하면 false 입니다.
             * @details 타일에 닿는 삼각형만 번호와 영역을 모아 한 번에 복셀화한다. 경사가 걸을 수 없는 삼각형은 영역을 지운다(막기는 한다).
             */
            [[nodiscard]] static bool buildCompactHeightfield( rcContext& context, const NavMeshGeometry& geometry, const vector<uint32>& listTriangle, const rcConfig& config, BuildScratch& scratch )
            {
                const vector<float3>& listVertex = geometry.getVertices();
                const vector<uint32>& listIndex  = geometry.getIndices();
                const vector<uint8>&  listArea   = geometry.getTriangleAreas();
                static_assert( sizeof( float3 ) == sizeof( float32 ) * 3, "float3 must be three packed floats for Recast" );
                const float32* pVertex     = reinterpret_cast<const float32*>( listVertex.data() );
                const int32    vertexCount = static_cast<int32>( listVertex.size() );

                scratch._pSolid = rcAllocHeightfield();
                if ( scratch._pSolid == nullptr || rcCreateHeightfield( &context, *scratch._pSolid, config.width, config.height, config.bmin, config.bmax, config.cs, config.ch ) == false )
                    return false;

                vector<int32> listTriangleIndex( listTriangle.size() * 3 );
                vector<uint8> listTriangleArea( listTriangle.size() );
                for ( size_t slot = 0; slot < listTriangle.size(); ++slot )
                {
                    const uint32 triangleIndex      = listTriangle[slot];
                    listTriangleIndex[slot * 3 + 0] = static_cast<int32>( listIndex[triangleIndex * 3 + 0] );
                    listTriangleIndex[slot * 3 + 1] = static_cast<int32>( listIndex[triangleIndex * 3 + 1] );
                    listTriangleIndex[slot * 3 + 2] = static_cast<int32>( listIndex[triangleIndex * 3 + 2] );
                    listTriangleArea[slot]          = RecastAreaUtil::toRecastArea( listArea[triangleIndex] );
                }
                const int32 triangleCount = static_cast<int32>( listTriangle.size() );
                rcClearUnwalkableTriangles( &context, config.walkableSlopeAngle, pVertex, vertexCount, listTriangleIndex.data(), triangleCount, listTriangleArea.data() );
                if ( rcRasterizeTriangles( &context, pVertex, vertexCount, listTriangleIndex.data(), listTriangleArea.data(), triangleCount, *scratch._pSolid, config.walkableClimb ) == false )
                    return false;

                rcFilterLowHangingWalkableObstacles( &context, config.walkableClimb, *scratch._pSolid );
                rcFilterLedgeSpans( &context, config.walkableHeight, config.walkableClimb, *scratch._pSolid );
                rcFilterWalkableLowHeightSpans( &context, config.walkableHeight, *scratch._pSolid );

                scratch._pCompact = rcAllocCompactHeightfield();
                if ( scratch._pCompact == nullptr || rcBuildCompactHeightfield( &context, config.walkableHeight, config.walkableClimb, *scratch._pSolid, *scratch._pCompact ) == false )
                    return false;
                rcFreeHeightField( scratch._pSolid );
                scratch._pSolid = nullptr;
                return rcErodeWalkableArea( &context, config.walkableRadius, *scratch._pCompact );
            }

            /** @brief 볼록 부피를 칠합니다. 깎은 뒤에 칠하므로, 뚫는 부피(장애물)는 몸 반지름만큼 넓혀서 칠한다(유니티 Carve 와 같은 결과다). */
            static void markConvexVolumes( rcContext& context, const NavMeshGeometry& geometry, const vector<NavConvexVolume>* pExtraVolume, float32 agentRadius, const rcConfig& config,
                                           BuildScratch& scratch )
            {
                vector<float32>                listVolumePoint;
                vector<float32>                listOffsetPoint;
                vector<const NavConvexVolume*> listVolume;
                for ( const NavConvexVolume& volume : geometry.getConvexVolumes() )
                {
                    listVolume.push_back( &volume );
                }
                if ( pExtraVolume != nullptr )
                {
                    for ( const NavConvexVolume& volume : *pExtraVolume )
                    {
                        listVolume.push_back( &volume );
                    }
                }
                for ( const NavConvexVolume* pVolume : listVolume )
                {
                    const NavConvexVolume& volume       = *pVolume;
                    const AABB             volumeBounds = volume.computeBounds();
                    const bool             bOverlaps    = volumeBounds._min._x <= config.bmax[0] && config.bmin[0] <= volumeBounds._max._x && volumeBounds._min._z <= config.bmax[2] &&
                                           config.bmin[2] <= volumeBounds._max._z;
                    if ( bOverlaps == false )
                        continue;
                    listVolumePoint.clear();
                    for ( const float3& point : volume._listPoint )
                    {
                        listVolumePoint.push_back( point._x );
                        listVolumePoint.push_back( volume._minY );
                        listVolumePoint.push_back( point._z );
                    }
                    const uint8    recastArea  = RecastAreaUtil::toRecastArea( volume._area );
                    const int32    pointCount  = static_cast<int32>( volume._listPoint.size() );
                    const float32* pPolygon    = listVolumePoint.data();
                    int32          polygonSize = pointCount;
                    if ( recastArea == RC_NULL_AREA && agentRadius > 0.0f )
                    {
                        const int32 maxOffsetPoint = pointCount * 4 + 8;
                        listOffsetPoint.assign( static_cast<size_t>( maxOffsetPoint ) * 3, 0.0f );
                        const int32 offsetCount = rcOffsetPoly( listVolumePoint.data(), pointCount, agentRadius, listOffsetPoint.data(), maxOffsetPoint );
                        if ( offsetCount >= 3 )
                        {
                            pPolygon    = listOffsetPoint.data();
                            polygonSize = offsetCount;
                        }
                    }
                    rcMarkConvexPolyArea( &context, pPolygon, polygonSize, volume._minY, volume._maxY, recastArea, *scratch._pCompact );
                }
            }

            /** @brief 시작점이 이 타일에 포함된 오프 메시 링크만 모읍니다(Detour 는 바깥에서 시작하는 링크를 버린다). 끝은 이웃 타일이 끼워질 때 연결된다. */
            static vector<NavOffMeshLink> collectLinksInTile( const NavMeshGeometry& geometry, const AABB& tileBounds )
            {
                vector<NavOffMeshLink> listLinkInTile;
                for ( const NavOffMeshLink& link : geometry.getOffMeshLinks() )
                {
                    const bool bStartInTile = tileBounds._min._x <= link._start._x && link._start._x < tileBounds._max._x && tileBounds._min._z <= link._start._z &&
                                              link._start._z < tileBounds._max._z;
                    if ( bStartInTile )
                        listLinkInTile.push_back( link );
                }
                return listLinkInTile;
            }

            /**
             * @brief 압축 높이장에서 영역 · 윤곽 · 폴리곤 메시 · 상세 메시를 짓고 Detour 타일 바이트(`outTile._bytes`)로 만듭니다.
             * @return 짓다 실패하면 false 입니다. 윤곽이나 폴리곤이 없는 타일은 true 이며 바이트가 비어 있습니다.
             */
            [[nodiscard]] static bool buildTileData( rcContext& context, const NavMeshGeometry& geometry, const NavAgentTypeDef& agent, const rcConfig& config, const AABB& tileBounds, int32 tileX,
                                                     int32 tileZ, BuildScratch& scratch, NavTileData& outTile )
            {
                if ( rcBuildDistanceField( &context, *scratch._pCompact ) == false ||
                     rcBuildRegions( &context, *scratch._pCompact, config.borderSize, config.minRegionArea, config.mergeRegionArea ) == false )
                    return false;
                scratch._pContour = rcAllocContourSet();
                if ( scratch._pContour == nullptr || rcBuildContours( &context, *scratch._pCompact, config.maxSimplificationError, config.maxEdgeLen, *scratch._pContour ) == false )
                    return false;
                if ( scratch._pContour->nconts == 0 )
                    return true;
                scratch._pPolyMesh = rcAllocPolyMesh();
                if ( scratch._pPolyMesh == nullptr || rcBuildPolyMesh( &context, *scratch._pContour, config.maxVertsPerPoly, *scratch._pPolyMesh ) == false )
                    return false;
                scratch._pDetail = rcAllocPolyMeshDetail();
                if ( scratch._pDetail == nullptr ||
                     rcBuildPolyMeshDetail( &context, *scratch._pPolyMesh, *scratch._pCompact, config.detailSampleDist, config.detailSampleMaxError, *scratch._pDetail ) == false )
                    return false;
                rcPolyMesh& polyMesh = *scratch._pPolyMesh;
                if ( polyMesh.npolys == 0 )
                    return true;
                if ( polyMesh.nverts >= 0xffff )
                {
                    SW_LOG_ERROR( "Navmesh '%#' tile (%#, %#) has %# vertices - raise the cell size or lower the tile size", agent._name.c_str(), tileX, tileZ, polyMesh.nverts );
                    return false;
                }
                // 영역 → 표시 비트(거름의 영역 비트와 같다).
                for ( int32 polyIndex = 0; polyIndex < polyMesh.npolys; ++polyIndex )
                {
                    polyMesh.flags[polyIndex] = RecastAreaUtil::toPolyFlags( polyMesh.areas[polyIndex] );
                }

                // 오프 메시 링크 — 시작점이 이 타일에 포함된 것만 싣는다.
                vector<float32> listLinkVertex;
                vector<float32> listLinkRadius;
                vector<uint16>  listLinkFlag;
                vector<uint8>   listLinkArea;
                vector<uint8>   listLinkDirection;
                vector<uint32>  listLinkUserID;
                for ( const NavOffMeshLink& link : collectLinksInTile( geometry, tileBounds ) )
                {
                    const uint8 recastArea = RecastAreaUtil::toRecastArea( link._area );
                    for ( const float3* pPoint : { &link._start, &link._end } )
                    {
                        listLinkVertex.push_back( pPoint->_x );
                        listLinkVertex.push_back( pPoint->_y );
                        listLinkVertex.push_back( pPoint->_z );
                    }
                    listLinkRadius.push_back( link._radius );
                    listLinkArea.push_back( recastArea );
                    listLinkFlag.push_back( RecastAreaUtil::toPolyFlags( recastArea ) );
                    listLinkDirection.push_back( link._bBidirectional == SW_TRUE ? static_cast<uint8>( DT_OFFMESH_CON_BIDIR ) : static_cast<uint8>( 0 ) );
                    listLinkUserID.push_back( link._userID );
                }

                dtNavMeshCreateParams params{};
                params.verts            = polyMesh.verts;
                params.vertCount        = polyMesh.nverts;
                params.polys            = polyMesh.polys;
                params.polyAreas        = polyMesh.areas;
                params.polyFlags        = polyMesh.flags;
                params.polyCount        = polyMesh.npolys;
                params.nvp              = polyMesh.nvp;
                params.detailMeshes     = scratch._pDetail->meshes;
                params.detailVerts      = scratch._pDetail->verts;
                params.detailVertsCount = scratch._pDetail->nverts;
                params.detailTris       = scratch._pDetail->tris;
                params.detailTriCount   = scratch._pDetail->ntris;
                params.offMeshConVerts  = listLinkVertex.data();
                params.offMeshConRad    = listLinkRadius.data();
                params.offMeshConFlags  = listLinkFlag.data();
                params.offMeshConAreas  = listLinkArea.data();
                params.offMeshConDir    = listLinkDirection.data();
                params.offMeshConUserID = listLinkUserID.data();
                params.offMeshConCount  = static_cast<int32>( listLinkRadius.size() );
                params.walkableHeight   = agent._height;
                params.walkableRadius   = agent._radius;
                params.walkableClimb    = agent._maxClimb;
                params.tileX            = tileX;
                params.tileY            = tileZ;
                params.tileLayer        = 0;
                rcVcopy( params.bmin, polyMesh.bmin );
                rcVcopy( params.bmax, polyMesh.bmax );
                params.cs          = config.cs;
                params.ch          = config.ch;
                params.buildBvTree = true;
                uint8* pData       = nullptr;
                int32  dataSize    = 0;
                if ( dtCreateNavMeshData( &params, &pData, &dataSize ) == false )
                    return false;
                outTile._bytes.assign( pData, pData + dataSize );
                dtFree( pData );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    RecastNavMesh::RecastNavMesh()
        : _agentType{}
        , _grid{}
        , _listDisabledLinkID{}
        , _listQuery{}
        , _pSharedQuery{ nullptr }
        , _sharedQueryMutex{}
        , _pNavMesh{ nullptr }
        , _pTileCache{}
        , _minY{ 0.0f }
        , _maxY{ 0.0f }
        , _revision{ 0 }
    {
        RecastNavMeshInternal::routeAllocatorsOnce();
    }

    RecastNavMesh::~RecastNavMesh()
    {
        shutdown();
    }

    bool RecastNavMesh::initialize( const NavAgentTypeDef& agentType, const NavMeshSettings& areaTable, const AABB& bounds )
    {
        using Internal = RecastNavMeshInternal;
        (void)areaTable;
        shutdown();
        _agentType = agentType;
        if ( bounds.isValid() == false )
        {
            SW_LOG_ERROR( "Navmesh '%#' needs valid bounds", agentType._name.c_str() );
            return false;
        }
        const float32 tileWorldSize = static_cast<float32>( agentType._tileSize ) * agentType._cellSize;
        _grid._origin               = bounds._min;
        _grid._tileWorldSize        = tileWorldSize;
        _grid._tileCountX           = MathUtil::max( 1, static_cast<int32>( MathUtil::ceil( ( bounds._max._x - bounds._min._x ) / tileWorldSize ) ) );
        _grid._tileCountZ           = MathUtil::max( 1, static_cast<int32>( MathUtil::ceil( ( bounds._max._z - bounds._min._z ) / tileWorldSize ) ) );
        // 높이 범위는 몸 높이만큼 넉넉히 잡는다. 파괴로 생긴 조각이 원래 상자 위로 조금 솟아도 그 타일을 다시 베이크하게 하려는 것이다.
        _minY = bounds._min._y - agentType._height;
        _maxY = bounds._max._y + agentType._height;

        // 장애물 캐시는 타일 자리마다 높이 층 여럿을 내비메시 타일로 짓는다 — 자리 수가 층 상한만큼 곱해진다.
        const bool   bTileCache   = agentType._obstacleMode == NavObstacleMode::TileCache;
        const uint32 slotsPerTile = bTileCache ? RecastTileCache::kMaxLayerCount : 1u;
        const uint32 tileBits     = Internal::computeBitCount( static_cast<uint32>( _grid.getTileCount() ) * slotsPerTile );
        if ( tileBits > Internal::kMaxTileBits )
        {
            SW_LOG_ERROR( "Navmesh '%#' needs %# tiles - at most %# fit in a 32-bit polygon reference; raise the tile size", agentType._name.c_str(),
                          _grid.getTileCount(), 1u << Internal::kMaxTileBits );
            _grid = NavTileGrid{};
            return false;
        }
        dtNavMeshParams params{};
        params.orig[0]    = _grid._origin._x;
        params.orig[1]    = _grid._origin._y;
        params.orig[2]    = _grid._origin._z;
        params.tileWidth  = tileWorldSize;
        params.tileHeight = tileWorldSize;
        params.maxTiles   = static_cast<int32>( 1u << tileBits );
        params.maxPolys   = static_cast<int32>( 1u << ( Internal::kTileAndPolyBits - tileBits ) );
        _pNavMesh         = dtAllocNavMesh();
        if ( _pNavMesh == nullptr || dtStatusFailed( _pNavMesh->init( &params ) ) )
        {
            SW_LOG_ERROR( "Navmesh '%#' could not be initialized", agentType._name.c_str() );
            shutdown();
            return false;
        }
        if ( bTileCache )
        {
            _pTileCache = make_unique<RecastTileCache>();
            if ( _pTileCache->initialize( agentType, _grid, _minY, _maxY ) == false )
            {
                shutdown();
                return false;
            }
        }
        _listQuery.assign( engine::getParallelScratchSlotCount(), nullptr );
        ++_revision;
        return true;
    }

    void RecastNavMesh::freeQueries()
    {
        for ( dtNavMeshQuery*& pQuery : _listQuery )
        {
            dtFreeNavMeshQuery( pQuery );
            pQuery = nullptr;
        }
        _listQuery.clear();
        dtFreeNavMeshQuery( _pSharedQuery );
        _pSharedQuery = nullptr;
    }

    void RecastNavMesh::shutdown()
    {
        freeQueries();
        _pTileCache.reset(); // 캐시는 내비메시 타일을 짓기만 한다 — 내비메시보다 먼저 놓아도 된다
        dtFreeNavMesh( _pNavMesh );
        _pNavMesh = nullptr;
        _grid     = NavTileGrid{};
        ++_revision;
    }

    void RecastNavMesh::collectTilesOverlapping( const AABB& bounds, vector<int2>& outListTile ) const
    {
        outListTile.clear();
        const bool bNothingToQuery = _pNavMesh == nullptr || _grid._tileWorldSize <= 0.0f || bounds.isValid() == false;
        if ( bNothingToQuery )
            return;
        // 베이크 테두리(몸 반지름 + 3 셀)만큼 넓힌다. 이웃 타일의 기하가 이 타일 가장자리의 깎기를 바꾸기 때문이다.
        const float32 border = ( MathUtil::ceil( _agentType._radius / _agentType._cellSize ) + 3.0f ) * _agentType._cellSize;
        const int32   minX   = static_cast<int32>( MathUtil::floor( ( bounds._min._x - border - _grid._origin._x ) / _grid._tileWorldSize ) );
        const int32   maxX   = static_cast<int32>( MathUtil::floor( ( bounds._max._x + border - _grid._origin._x ) / _grid._tileWorldSize ) );
        const int32   minZ   = static_cast<int32>( MathUtil::floor( ( bounds._min._z - border - _grid._origin._z ) / _grid._tileWorldSize ) );
        const int32   maxZ   = static_cast<int32>( MathUtil::floor( ( bounds._max._z + border - _grid._origin._z ) / _grid._tileWorldSize ) );
        for ( int32 tileZ = MathUtil::max( 0, minZ ); tileZ <= MathUtil::min( _grid._tileCountZ - 1, maxZ ); ++tileZ )
        {
            for ( int32 tileX = MathUtil::max( 0, minX ); tileX <= MathUtil::min( _grid._tileCountX - 1, maxX ); ++tileX )
            {
                outListTile.push_back( int2{ tileX, tileZ } );
            }
        }
    }

    bool RecastNavMesh::bakeTile( const NavMeshGeometry& geometry, const vector<NavConvexVolume>* pExtraVolume, int32 tileX, int32 tileZ,
                                  NavTileData& outTile ) const
    {
        using Internal = RecastNavMeshInternal;
        outTile._bytes.clear();
        outTile._tileX         = tileX;
        outTile._tileZ         = tileZ;
        const bool bInsideGrid = 0 <= tileX && tileX < _grid._tileCountX && 0 <= tileZ && tileZ < _grid._tileCountZ;
        if ( _pNavMesh == nullptr || bInsideGrid == false )
            return false;
        // 장애물 캐시는 장애물을 층에 베이크하지 않는다(층이 정적 몫이라 장애물이 움직이면 낡는다) — 장애물은 `addCacheObstacle` 로 넣는다.
        const bool bCacheWithObstacleVolumes = _pTileCache != nullptr && pExtraVolume != nullptr && pExtraVolume->empty() == false;
        if ( bCacheWithObstacleVolumes )
        {
            SW_LOG_ERROR( "Navmesh '%#' uses the tile cache - obstacle volumes must go through addCacheObstacle, not the bake", _agentType._name.c_str() );
            return false;
        }

        const AABB     tileBounds = _grid.computeTileBounds( tileX, tileZ, _minY, _maxY );
        const rcConfig config     = Internal::makeConfig( _agentType, tileBounds, _minY, _maxY );

        vector<uint32> listTriangle;
        geometry.collectTriangles( float2{ config.bmin[0], config.bmin[2] }, float2{ config.bmax[0], config.bmax[2] }, listTriangle );
        if ( listTriangle.empty() )
            return true;

        rcContext              context( false );
        Internal::BuildScratch scratch;
        if ( Internal::buildCompactHeightfield( context, geometry, listTriangle, config, scratch ) == false )
            return false;
        Internal::markConvexVolumes( context, geometry, pExtraVolume, _agentType._radius, config, scratch );

        // 장애물 캐시 — 여기까지(복셀화 · 깎기 · 정적 부피 칠하기)가 층에 남는 정적 몫이다. 층으로 나눠 묶음으로 낸다. 폴리곤은 끼울 때 · 장애물이 바뀔 때 캐시가 짓는다.
        if ( _pTileCache != nullptr )
            return RecastTileCache::makeLayerPack( context, *scratch._pCompact, config, tileX, tileZ, Internal::collectLinksInTile( geometry, tileBounds ), outTile._bytes );

        return Internal::buildTileData( context, geometry, _agentType, config, tileBounds, tileX, tileZ, scratch, outTile );
    }

    bool RecastNavMesh::replaceTile( const NavTileData& tile )
    {
        if ( _pNavMesh == nullptr )
            return false;
        const bool bInsideGrid = 0 <= tile._tileX && tile._tileX < _grid._tileCountX && 0 <= tile._tileZ && tile._tileZ < _grid._tileCountZ;
        if ( bInsideGrid == false )
            return false;
        if ( _pTileCache != nullptr )
        {
            // 바이트는 층 묶음이다 — 캐시가 옛 층 · 타일을 빼고 새 층에서 지금 장애물로 짓는다.
            ++_revision;
            const bool bInstalled = _pTileCache->installPack( tile, *_pNavMesh );
            if ( bInstalled && _listDisabledLinkID.empty() == false )
                applyLinkFlags( tile._tileX, tile._tileZ );
            return bInstalled;
        }
        const dtTileRef oldRef = _pNavMesh->getTileRefAt( tile._tileX, tile._tileZ, 0 );
        if ( oldRef != 0 )
            _pNavMesh->removeTile( oldRef, nullptr, nullptr );
        ++_revision;
        if ( tile._bytes.empty() )
            return true;
        uint8* pData = static_cast<uint8*>( dtAlloc( tile._bytes.size(), DT_ALLOC_PERM ) );
        if ( pData == nullptr )
            return false;
        Memory::copy( pData, tile._bytes.data(), tile._bytes.size() );
        const dtStatus status = _pNavMesh->addTile( pData, static_cast<int32>( tile._bytes.size() ), DT_TILE_FREE_DATA, 0, nullptr );
        if ( dtStatusFailed( status ) )
        {
            dtFree( pData );
            SW_LOG_ERROR( "Navmesh '%#' rejected tile (%#, %#) - the bytes are not a tile of this layout", _agentType._name.c_str(), tile._tileX, tile._tileZ );
            return false;
        }
        // 새 타일은 베이크한 표시(모두 열림)로 들어온다 — 막힌 링크를 다시 막는다.
        if ( _listDisabledLinkID.empty() == false )
            applyLinkFlags( tile._tileX, tile._tileZ );
        return true;
    }

    void RecastNavMesh::applyLinkFlags( int32 tileX, int32 tileZ )
    {
        // 자리 하나에 층 여럿이 있을 수 있다(장애물 캐시). 다시 베이크는 층 0 하나뿐이다.
        const dtNavMesh* const pConst = _pNavMesh;
        const dtMeshTile*      arrTile[RecastTileCache::kMaxLayerCount * 2];
        const int32            tileCount = pConst->getTilesAt( tileX, tileZ, arrTile, static_cast<int32>( RecastTileCache::kMaxLayerCount * 2 ) );
        for ( int32 tileIndex = 0; tileIndex < tileCount; ++tileIndex )
        {
            const dtMeshTile* const pTile = arrTile[tileIndex];
            if ( pTile == nullptr || pTile->header == nullptr )
                continue;
            const dtPolyRef base = pConst->getPolyRefBase( pTile );
            for ( int32 linkIndex = 0; linkIndex < pTile->header->offMeshConCount; ++linkIndex )
            {
                const dtOffMeshConnection& link      = pTile->offMeshCons[linkIndex];
                const dtPoly&              poly      = pTile->polys[link.poly];
                const bool                 bDisabled = std::binary_search( _listDisabledLinkID.begin(), _listDisabledLinkID.end(), link.userId );
                const uint16               flags     = bDisabled ? static_cast<uint16>( 0 ) : RecastAreaUtil::toPolyFlags( poly.getArea() );
                (void)_pNavMesh->setPolyFlags( base | static_cast<dtPolyRef>( link.poly ), flags ); // 참조는 방금 이 타일에서 만든 것이라 실패하지 않는다
            }
        }
    }

    void RecastNavMesh::applyLinkFlagsToAllTiles()
    {
        const dtNavMesh* const pConst = _pNavMesh;
        for ( int32 tileIndex = 0; tileIndex < pConst->getMaxTiles(); ++tileIndex )
        {
            const dtMeshTile* pTile            = pConst->getTile( tileIndex );
            const bool        bHasOffMeshLinks = pTile != nullptr && pTile->header != nullptr && pTile->header->offMeshConCount > 0;
            if ( bHasOffMeshLinks )
                applyLinkFlags( pTile->header->x, pTile->header->y );
        }
    }

    NavObstacleRef RecastNavMesh::addCacheObstacle( const NavCacheObstacleDesc& desc )
    {
        if ( _pTileCache == nullptr )
        {
            SW_LOG_ERROR( "Navmesh '%#' does not use the tile cache (_obstacleMode) - the obstacle is not added", _agentType._name.c_str() );
            return 0;
        }
        return _pTileCache->addObstacle( desc );
    }

    void RecastNavMesh::removeCacheObstacle( NavObstacleRef obstacle )
    {
        if ( _pTileCache == nullptr )
        {
            SW_LOG_ERROR( "Navmesh '%#' does not use the tile cache (_obstacleMode) - obstacle %# cannot be removed", _agentType._name.c_str(), obstacle );
            return;
        }
        _pTileCache->removeObstacle( obstacle );
    }

    bool RecastNavMesh::updateCacheObstacles( float32 deltaSeconds )
    {
        if ( _pTileCache == nullptr || _pNavMesh == nullptr )
            return false;
        if ( _pTileCache->update( deltaSeconds, *_pNavMesh ) == false )
            return false;
        ++_revision;
        // 캐시가 다시 지은 타일은 베이크한 표시(모두 열림)로 들어온다 — 막힌 링크를 다시 막는다.
        if ( _listDisabledLinkID.empty() == false )
            applyLinkFlagsToAllTiles();
        return true;
    }

    void RecastNavMesh::setOffMeshLinkEnabled( uint32 linkID, bool bEnabled )
    {
        if ( linkID == 0 )
            return;
        const auto found    = std::lower_bound( _listDisabledLinkID.begin(), _listDisabledLinkID.end(), linkID );
        const bool bPresent = found != _listDisabledLinkID.end() && *found == linkID;
        if ( bPresent == ( bEnabled == false ) )
            return;
        if ( bEnabled )
            _listDisabledLinkID.erase( found );
        else
            _listDisabledLinkID.insert( found, linkID );
        ++_revision;
        if ( _pNavMesh == nullptr )
            return;
        applyLinkFlagsToAllTiles();
    }

    bool RecastNavMesh::isOffMeshLinkEnabled( uint32 linkID ) const
    {
        return std::binary_search( _listDisabledLinkID.begin(), _listDisabledLinkID.end(), linkID ) == false;
    }

    void RecastNavMesh::collectTiles( vector<NavTileData>& outListTile ) const
    {
        outListTile.clear();
        if ( _pNavMesh == nullptr )
            return;
        // 장애물 캐시의 쿠킹 바이트는 층 묶음이다(장애물 없이 굽은 정적 몫 — 읽은 쪽이 `replaceTile` 로 끼운다).
        if ( _pTileCache != nullptr )
        {
            _pTileCache->collectPacks( outListTile );
            return;
        }
        for ( int32 tileZ = 0; tileZ < _grid._tileCountZ; ++tileZ )
        {
            for ( int32 tileX = 0; tileX < _grid._tileCountX; ++tileX )
            {
                const dtMeshTile* pTile            = _pNavMesh->getTileAt( tileX, tileZ, 0 );
                const bool        bTileDataMissing = pTile == nullptr || pTile->header == nullptr || pTile->data == nullptr;
                if ( bTileDataMissing )
                    continue;
                NavTileData tile;
                tile._tileX = tileX;
                tile._tileZ = tileZ;
                tile._bytes.assign( pTile->data, pTile->data + pTile->dataSize );
                outListTile.push_back( std::move( tile ) );
            }
        }
    }

    uint32 RecastNavMesh::getLoadedTileCount() const
    {
        if ( _pNavMesh == nullptr )
            return 0;
        uint32                 count  = 0;
        const dtNavMesh* const pConst = _pNavMesh;
        for ( int32 tileIndex = 0; tileIndex < pConst->getMaxTiles(); ++tileIndex )
        {
            const dtMeshTile* pTile = pConst->getTile( tileIndex );
            if ( pTile != nullptr && pTile->header != nullptr )
                ++count;
        }
        return count;
    }

    uint32 RecastNavMesh::getPolygonCount() const
    {
        if ( _pNavMesh == nullptr )
            return 0;
        uint32                 count  = 0;
        const dtNavMesh* const pConst = _pNavMesh;
        for ( int32 tileIndex = 0; tileIndex < pConst->getMaxTiles(); ++tileIndex )
        {
            const dtMeshTile* pTile = pConst->getTile( tileIndex );
            if ( pTile != nullptr && pTile->header != nullptr )
                count += static_cast<uint32>( pTile->header->polyCount );
        }
        return count;
    }

    // 잠갔는지가 값(`outLocked`)으로 `releaseQuery` 에 건너가는 조건부 잠금이라 잠금 분석이 따라가지 못한다.
    SW_NO_THREAD_SAFETY_ANALYSIS dtNavMeshQuery* RecastNavMesh::acquireQuery( bool& outLocked ) const
    {
        using Internal = RecastNavMeshInternal;
        outLocked      = false;
        if ( _pNavMesh == nullptr )
            return nullptr;
        const uint32 slot = engine::getParallelScratchSlot();
        if ( slot < _listQuery.size() )
        {
            dtNavMeshQuery*& pQuery = _listQuery[slot];
            if ( pQuery == nullptr )
            {
                pQuery = dtAllocNavMeshQuery();
                if ( pQuery != nullptr && dtStatusFailed( pQuery->init( _pNavMesh, Internal::kMaxQueryNodeCount ) ) )
                {
                    dtFreeNavMeshQuery( pQuery );
                    pQuery = nullptr;
                }
            }
            return pQuery;
        }
        _sharedQueryMutex.lock();
        outLocked = true;
        if ( _pSharedQuery == nullptr )
        {
            _pSharedQuery = dtAllocNavMeshQuery();
            if ( _pSharedQuery != nullptr && dtStatusFailed( _pSharedQuery->init( _pNavMesh, Internal::kMaxQueryNodeCount ) ) )
            {
                dtFreeNavMeshQuery( _pSharedQuery );
                _pSharedQuery = nullptr;
            }
        }
        return _pSharedQuery;
    }

    SW_NO_THREAD_SAFETY_ANALYSIS void RecastNavMesh::releaseQuery( bool bLocked ) const
    {
        if ( bLocked )
            _sharedQueryMutex.unlock();
    }

    void RecastNavMesh::applyQueryFilter( const NavQueryFilter& filter, dtQueryFilter& outFilter )
    {
        for ( uint32 areaIndex = 0; areaIndex < NavigationConstant::kMaxAreaCount; ++areaIndex )
        {
            outFilter.setAreaCost( static_cast<int32>( areaIndex + 1 ), filter._arrAreaCost[areaIndex] );
        }
        outFilter.setIncludeFlags( filter._includeAreaMask );
        outFilter.setExcludeFlags( filter._excludeAreaMask );
    }

    bool RecastNavMesh::findNearestPoint( const float3& position, const float3& searchExtent, const NavQueryFilter& filter, NavLocation& outLocation ) const
    {
        outLocation             = NavLocation{};
        bool            bLocked = false;
        dtNavMeshQuery* pQuery  = acquireQuery( bLocked );
        if ( pQuery == nullptr )
        {
            releaseQuery( bLocked );
            return false;
        }
        dtQueryFilter detourFilter;
        applyQueryFilter( filter, detourFilter );
        dtPolyRef      polyRef = 0;
        float32        arrNearest[3]{};
        const dtStatus status = pQuery->findNearestPoly( &position._x, &searchExtent._x, &detourFilter, &polyRef, arrNearest );
        releaseQuery( bLocked );
        if ( dtStatusFailed( status ) || polyRef == 0 )
            return false;
        // Detour 는 경계 상자만 질의 상자에 걸친 폴리곤도 고르고 그 폴리곤 위 가장 가까운 점을 돌려준다 — 점이 상자 밖일 수 있다(돌린 장애물 구멍 옆 삼각형).
        // 계약(상자 안의 점)을 여기서 지킨다. 높이는 경사면 투영이 상자 밖으로 나갈 수 있어 가로(XZ)만 본다.
        if ( MathUtil::abs( arrNearest[0] - position._x ) > searchExtent._x || MathUtil::abs( arrNearest[2] - position._z ) > searchExtent._z )
            return false;
        outLocation._position = RecastNavMeshInternal::toFloat3( arrNearest );
        outLocation._poly     = static_cast<NavPolyRef>( polyRef );
        return true;
    }

    NavPathStatus RecastNavMesh::findPath( const float3& start, const float3& end, const float3& searchExtent, const NavQueryFilter& filter, NavPath& outPath ) const
    {
        using Internal = RecastNavMeshInternal;
        outPath._listPoint.clear();
        outPath._listLinkCrossing.clear();
        outPath._status         = NavPathStatus::Failed;
        outPath._polyCount      = 0;
        bool            bLocked = false;
        dtNavMeshQuery* pQuery  = acquireQuery( bLocked );
        if ( pQuery == nullptr )
        {
            releaseQuery( bLocked );
            return NavPathStatus::Failed;
        }
        dtQueryFilter detourFilter;
        applyQueryFilter( filter, detourFilter );
        dtPolyRef startRef = 0;
        dtPolyRef endRef   = 0;
        float32   arrStart[3]{};
        float32   arrEnd[3]{};
        (void)pQuery->findNearestPoly( &start._x, &searchExtent._x, &detourFilter, &startRef, arrStart ); // 결과는 ref 로 본다 — 못 찾으면 0 이다
        (void)pQuery->findNearestPoly( &end._x, &searchExtent._x, &detourFilter, &endRef, arrEnd );       // 결과는 ref 로 본다 — 못 찾으면 0 이다
        if ( startRef == 0 || endRef == 0 )
        {
            releaseQuery( bLocked );
            return NavPathStatus::Failed;
        }
        dtPolyRef      arrPoly[Internal::kMaxPathPolyCount]{};
        int32          polyCount = 0;
        const dtStatus status    = pQuery->findPath( startRef, endRef, arrStart, arrEnd, &detourFilter, arrPoly, &polyCount, Internal::kMaxPathPolyCount );
        if ( dtStatusFailed( status ) || polyCount == 0 )
        {
            releaseQuery( bLocked );
            return NavPathStatus::Failed;
        }
        // 끝 폴리곤에 닿지 못했으면(막혔거나 노드 상한에 걸렸으면) 닿은 마지막 폴리곤에서 끝에 가장 가까운 점까지만 간다.
        const bool bPartial = dtStatusDetail( status, DT_PARTIAL_RESULT ) || arrPoly[polyCount - 1] != endRef;
        float32    arrGoal[3]{ arrEnd[0], arrEnd[1], arrEnd[2] };
        if ( bPartial )
            (void)pQuery->closestPointOnPoly( arrPoly[polyCount - 1], arrEnd, arrGoal, nullptr );

        constexpr int32 kMaxStraight = static_cast<int32>( NavigationConstant::kMaxPathPointCount );
        float32         arrStraight[kMaxStraight * 3]{};
        uint8           arrStraightFlag[kMaxStraight]{};
        dtPolyRef       arrStraightPoly[kMaxStraight]{};
        int32           straightCount  = 0;
        const dtStatus  straightStatus = pQuery->findStraightPath( arrStart, arrGoal, arrPoly, polyCount, arrStraight, arrStraightFlag, arrStraightPoly,
                                                                   &straightCount, kMaxStraight, 0 );
        releaseQuery( bLocked );
        if ( dtStatusFailed( straightStatus ) || straightCount == 0 )
            return NavPathStatus::Failed;
        outPath._listPoint.reserve( static_cast<size_t>( straightCount ) );
        for ( int32 pointIndex = 0; pointIndex < straightCount; ++pointIndex )
        {
            outPath._listPoint.push_back( Internal::toFloat3( &arrStraight[pointIndex * 3] ) );
            // 링크에 오르는 점이다 — 그 점의 폴리곤이 링크 폴리곤이다. 링크 번호는 그 링크의 사용자 번호다.
            if ( ( arrStraightFlag[pointIndex] & DT_STRAIGHTPATH_OFFMESH_CONNECTION ) == 0 )
                continue;
            const dtOffMeshConnection* pLink = _pNavMesh->getOffMeshConnectionByRef( arrStraightPoly[pointIndex] );
            outPath._listLinkCrossing.push_back( NavPathLinkCrossing{ static_cast<uint32>( pointIndex ), pLink != nullptr ? pLink->userId : 0u } );
        }
        outPath._polyCount    = static_cast<uint32>( polyCount );
        const bool bTruncated = dtStatusDetail( straightStatus, DT_BUFFER_TOO_SMALL );
        outPath._status       = ( bPartial || bTruncated ) ? NavPathStatus::Partial : NavPathStatus::Complete;
        return outPath._status;
    }

    bool RecastNavMesh::raycast( const float3& start, const float3& end, const float3& searchExtent, const NavQueryFilter& filter, NavRaycastHit& outHit ) const
    {
        outHit                  = NavRaycastHit{};
        bool            bLocked = false;
        dtNavMeshQuery* pQuery  = acquireQuery( bLocked );
        if ( pQuery == nullptr )
        {
            releaseQuery( bLocked );
            return false;
        }
        dtQueryFilter detourFilter;
        applyQueryFilter( filter, detourFilter );
        dtPolyRef startRef = 0;
        float32   arrStart[3]{};
        (void)pQuery->findNearestPoly( &start._x, &searchExtent._x, &detourFilter, &startRef, arrStart ); // 결과는 ref 로 본다 — 못 찾으면 0 이다
        if ( startRef == 0 )
        {
            releaseQuery( bLocked );
            return false;
        }
        float32        hitFraction = 0.0f;
        float32        arrNormal[3]{};
        dtPolyRef      arrVisited[RecastNavMeshInternal::kMaxPathPolyCount]{};
        int32          visitedCount = 0;
        const dtStatus status       = pQuery->raycast( startRef, arrStart, &end._x, &detourFilter, &hitFraction, arrNormal, arrVisited, &visitedCount,
                                                       RecastNavMeshInternal::kMaxPathPolyCount );
        releaseQuery( bLocked );
        if ( dtStatusFailed( status ) )
            return false;
        const float3 from = RecastNavMeshInternal::toFloat3( arrStart );
        if ( hitFraction > 1.0f )
        {
            outHit._position = end;
            outHit._fraction = 1.0f;
            return true;
        }
        outHit._bHit     = true;
        outHit._fraction = hitFraction;
        outHit._position = from + ( end - from ) * hitFraction;
        outHit._normal   = RecastNavMeshInternal::toFloat3( arrNormal );
        return true;
    }

    unique_ptr<INavCrowd> RecastNavMesh::createCrowd( uint32 maxAgentCount, float32 maxAgentRadius )
    {
        if ( _pNavMesh == nullptr )
            return nullptr;
        unique_ptr<DetourNavCrowd> pCrowd = make_unique<DetourNavCrowd>();
        if ( pCrowd->initialize( *this, maxAgentCount, maxAgentRadius ) == false )
            return nullptr;
        return pCrowd;
    }

    void RecastNavMesh::drawDebug( IPhysicsDebugRenderer& renderer, const float4& color ) const
    {
        if ( _pNavMesh == nullptr )
            return;
        // 폴리곤 테두리는 걸을 면의 바깥 경계를 진하게, 안쪽 이음을 옅게 그린다. 바닥과 겹쳐 깜빡이지 않게 조금 띄운다.
        const float4           innerColor{ color._x, color._y, color._z, color._w * 0.35f };
        const float3           lift{ 0.0f, 0.05f, 0.0f };
        const dtNavMesh* const pConst = _pNavMesh;
        for ( int32 tileIndex = 0; tileIndex < pConst->getMaxTiles(); ++tileIndex )
        {
            const dtMeshTile* pTile = pConst->getTile( tileIndex );
            if ( pTile == nullptr || pTile->header == nullptr )
                continue;
            for ( int32 polyIndex = 0; polyIndex < pTile->header->polyCount; ++polyIndex )
            {
                const dtPoly& poly = pTile->polys[polyIndex];
                if ( poly.getType() == DT_POLYTYPE_OFFMESH_CONNECTION )
                    continue;
                for ( int32 edgeIndex = 0; edgeIndex < poly.vertCount; ++edgeIndex )
                {
                    const int32  nextIndex = ( edgeIndex + 1 ) % poly.vertCount;
                    const float3 from      = RecastNavMeshInternal::toFloat3( &pTile->verts[poly.verts[edgeIndex] * 3] ) + lift;
                    const float3 to        = RecastNavMeshInternal::toFloat3( &pTile->verts[poly.verts[nextIndex] * 3] ) + lift;
                    const bool   bBoundary = poly.neis[edgeIndex] == 0;
                    renderer.drawLine( from, to, bBoundary ? color : innerColor );
                }
            }
            // 오프 메시 링크는 두 끝을 연결하는 활로 그린다. 막힌 링크(표시 0)는 빨강이다.
            const float4 blockedColor{ 1.0f, 0.15f, 0.1f, color._w };
            for ( int32 linkIndex = 0; linkIndex < pTile->header->offMeshConCount; ++linkIndex )
            {
                const dtOffMeshConnection& link   = pTile->offMeshCons[linkIndex];
                const float3               start  = RecastNavMeshInternal::toFloat3( &link.pos[0] ) + lift;
                const float3               end    = RecastNavMeshInternal::toFloat3( &link.pos[3] ) + lift;
                const float32              height = ( end - start ).getLength() * RecastNavMeshInternal::kLinkArcHeightRatio;
                const bool                 bOpen  = pTile->polys[link.poly].flags != 0;
                float3                     from   = start;
                for ( uint32 segment = 1; segment <= RecastNavMeshInternal::kLinkArcSegmentCount; ++segment )
                {
                    const float32 ratio = static_cast<float32>( segment ) / static_cast<float32>( RecastNavMeshInternal::kLinkArcSegmentCount );
                    const float3  to    = start + ( end - start ) * ratio + float3{ 0.0f, height * 4.0f * ratio * ( 1.0f - ratio ), 0.0f };
                    renderer.drawLine( from, to, bOpen ? color : blockedColor );
                    from = to;
                }
            }
        }
    }
} // namespace sw

namespace sw
{
    void RecastNavMesh::collectDebugTriangles( vector<float3>& outListCorner, vector<uint8>& outListArea ) const
    {
        outListCorner.clear();
        outListArea.clear();
        if ( _pNavMesh == nullptr )
            return;
        const dtNavMesh* const pConst = _pNavMesh;
        for ( int32 tileIndex = 0; tileIndex < pConst->getMaxTiles(); ++tileIndex )
        {
            const dtMeshTile* pTile = pConst->getTile( tileIndex );
            if ( pTile == nullptr || pTile->header == nullptr )
                continue;
            for ( int32 polyIndex = 0; polyIndex < pTile->header->polyCount; ++polyIndex )
            {
                const dtPoly& poly = pTile->polys[polyIndex];
                if ( poly.getType() == DT_POLYTYPE_OFFMESH_CONNECTION || pTile->detailMeshes == nullptr )
                    continue;
                // 디테일 삼각형이다. 꼭짓점 번호가 폴리곤 꼭짓점 수보다 작으면 폴리곤 꼭짓점이고, 아니면 디테일 꼭짓점이다.
                const dtPolyDetail& detail = pTile->detailMeshes[polyIndex];
                const uint8         area   = poly.getArea();
                const uint8         engine = ( 1 <= area && area <= NavigationConstant::kMaxAreaCount ) ? static_cast<uint8>( area - 1 ) : 0;
                for ( uint32 triangleIndex = 0; triangleIndex < detail.triCount; ++triangleIndex )
                {
                    const uint8* pTriangle = &pTile->detailTris[( detail.triBase + triangleIndex ) * 4];
                    for ( uint32 corner = 0; corner < 3; ++corner )
                    {
                        const uint8    vertex = pTriangle[corner];
                        const float32* pPoint = vertex < poly.vertCount ? &pTile->verts[poly.verts[vertex] * 3]
                                                                        : &pTile->detailVerts[( detail.vertBase + vertex - poly.vertCount ) * 3];
                        outListCorner.push_back( RecastNavMeshInternal::toFloat3( pPoint ) );
                    }
                    outListArea.push_back( engine );
                }
            }
        }
    }
} // namespace sw

namespace sw
{
    const utf8* NavMeshBackend::getBackendName()
    {
        return "recast";
    }

    uint32 NavMeshBackend::getBackendFormatVersion()
    {
        // Detour 타일 바이트의 버전(DT_NAVMESH_VERSION)이다. 라이브러리가 그 버전을 올리면 쿠킹본이 낡는다.
        return static_cast<uint32>( DT_NAVMESH_VERSION );
    }

    unique_ptr<INavMesh> NavMeshBackend::createNavMesh()
    {
        return make_unique<RecastNavMesh>();
    }
} // namespace sw
