#include "pch.h"

#include "Engine/Navigation/Recast/RecastNavMesh.h"

#include "Core/Math/MathUtil.h"
#include "Core/Memory/MemoryTag.h"

#include "Engine/Common/EngineParallel.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Navigation/Recast/DetourNavCrowd.h"
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
            /** @brief 폴리곤 참조 비트(Detour 32 비트 참조) 중 타일 · 폴리곤에 줄 수 있는 몫입니다(나머지 10 비트는 세대). */
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

            /** @brief Recast · Detour 의 할당을 엔진 할당기(태그 Navigation)로 돌립니다. 처음 만들 때 한 번. */
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
                    ++bits;
                return bits;
            }

            /** @brief 엔진 영역 번호(또는 걷지 못함)의 Recast 영역입니다. */
            static uint8 toRecastArea( uint8 area )
            {
                if ( area == NavigationConstant::kNotWalkableArea || area >= NavigationConstant::kMaxAreaCount )
                    return RC_NULL_AREA;
                return static_cast<uint8>( area + 1 );
            }

            /** @brief 베이크 중간 결과를 들고 끝에서 놓습니다(실패로 빠져나가도). */
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
        };
    } // namespace
} // namespace sw

namespace sw
{
    RecastNavMesh::RecastNavMesh()
        : _agentType{}
        , _grid{}
        , _listQuery{}
        , _pSharedQuery{ nullptr }
        , _sharedQueryMutex{}
        , _pNavMesh{ nullptr }
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
        // 높이 범위는 몸 높이만큼 넉넉히 — 파괴로 생긴 조각이 원래 상자 위로 조금 솟아도 그 타일을 다시 베이크하게.
        _minY = bounds._min._y - agentType._height;
        _maxY = bounds._max._y + agentType._height;

        const uint32 tileBits = Internal::computeBitCount( static_cast<uint32>( _grid.getTileCount() ) );
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
        dtFreeNavMesh( _pNavMesh );
        _pNavMesh = nullptr;
        _grid     = NavTileGrid{};
        ++_revision;
    }

    void RecastNavMesh::collectTilesOverlapping( const AABB& bounds, vector<int2>& outListTile ) const
    {
        outListTile.clear();
        if ( _pNavMesh == nullptr || _grid._tileWorldSize <= 0.0f || bounds.isValid() == false )
            return;
        // 베이크 테두리(몸 반지름 + 3 칸)만큼 넓힌다 — 이웃 타일의 기하가 이 타일 가장자리의 깎기를 바꾼다.
        const float32 border = ( MathUtil::ceil( _agentType._radius / _agentType._cellSize ) + 3.0f ) * _agentType._cellSize;
        const int32   minX   = static_cast<int32>( MathUtil::floor( ( bounds._min._x - border - _grid._origin._x ) / _grid._tileWorldSize ) );
        const int32   maxX   = static_cast<int32>( MathUtil::floor( ( bounds._max._x + border - _grid._origin._x ) / _grid._tileWorldSize ) );
        const int32   minZ   = static_cast<int32>( MathUtil::floor( ( bounds._min._z - border - _grid._origin._z ) / _grid._tileWorldSize ) );
        const int32   maxZ   = static_cast<int32>( MathUtil::floor( ( bounds._max._z + border - _grid._origin._z ) / _grid._tileWorldSize ) );
        for ( int32 tileZ = MathUtil::max( 0, minZ ); tileZ <= MathUtil::min( _grid._tileCountZ - 1, maxZ ); ++tileZ )
        {
            for ( int32 tileX = MathUtil::max( 0, minX ); tileX <= MathUtil::min( _grid._tileCountX - 1, maxX ); ++tileX )
                outListTile.push_back( int2{ tileX, tileZ } );
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

        const NavAgentTypeDef& agent = _agentType;
        rcConfig               config{};
        config.cs                     = agent._cellSize;
        config.ch                     = agent._cellHeight;
        config.walkableSlopeAngle     = agent._maxSlope;
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

        const AABB    tileBounds = _grid.computeTileBounds( tileX, tileZ, _minY, _maxY );
        const float32 border     = static_cast<float32>( config.borderSize ) * config.cs;
        config.bmin[0]           = tileBounds._min._x - border;
        config.bmin[1]           = _minY;
        config.bmin[2]           = tileBounds._min._z - border;
        config.bmax[0]           = tileBounds._max._x + border;
        config.bmax[1]           = _maxY;
        config.bmax[2]           = tileBounds._max._z + border;

        vector<uint32> listTriangle;
        geometry.collectTriangles( float2{ config.bmin[0], config.bmin[2] }, float2{ config.bmax[0], config.bmax[2] }, listTriangle );
        if ( listTriangle.empty() )
            return true;

        rcContext              context( false );
        Internal::BuildScratch scratch;
        const vector<float3>&  listVertex = geometry.getVertices();
        const vector<uint32>&  listIndex  = geometry.getIndices();
        const vector<uint8>&   listArea   = geometry.getTriangleAreas();
        static_assert( sizeof( float3 ) == sizeof( float32 ) * 3, "float3 must be three packed floats for Recast" );
        const float32* pVertex     = reinterpret_cast<const float32*>( listVertex.data() );
        const int32    vertexCount = static_cast<int32>( listVertex.size() );

        scratch._pSolid = rcAllocHeightfield();
        if ( scratch._pSolid == nullptr ||
             rcCreateHeightfield( &context, *scratch._pSolid, config.width, config.height, config.bmin, config.bmax, config.cs, config.ch ) == false )
            return false;

        // 타일에 닿는 삼각형만 번호 · 영역을 모아 한 번에 복셀화한다. 경사가 걸을 수 없는 삼각형은 영역을 지운다(막기는 한다).
        vector<int32> listTriangleIndex( listTriangle.size() * 3 );
        vector<uint8> listTriangleArea( listTriangle.size() );
        for ( size_t slot = 0; slot < listTriangle.size(); ++slot )
        {
            const uint32 triangleIndex      = listTriangle[slot];
            listTriangleIndex[slot * 3 + 0] = static_cast<int32>( listIndex[triangleIndex * 3 + 0] );
            listTriangleIndex[slot * 3 + 1] = static_cast<int32>( listIndex[triangleIndex * 3 + 1] );
            listTriangleIndex[slot * 3 + 2] = static_cast<int32>( listIndex[triangleIndex * 3 + 2] );
            listTriangleArea[slot]          = Internal::toRecastArea( listArea[triangleIndex] );
        }
        const int32 triangleCount = static_cast<int32>( listTriangle.size() );
        rcClearUnwalkableTriangles( &context, config.walkableSlopeAngle, pVertex, vertexCount, listTriangleIndex.data(), triangleCount, listTriangleArea.data() );
        if ( rcRasterizeTriangles( &context, pVertex, vertexCount, listTriangleIndex.data(), listTriangleArea.data(), triangleCount, *scratch._pSolid,
                                   config.walkableClimb ) == false )
            return false;

        rcFilterLowHangingWalkableObstacles( &context, config.walkableClimb, *scratch._pSolid );
        rcFilterLedgeSpans( &context, config.walkableHeight, config.walkableClimb, *scratch._pSolid );
        rcFilterWalkableLowHeightSpans( &context, config.walkableHeight, *scratch._pSolid );

        scratch._pCompact = rcAllocCompactHeightfield();
        if ( scratch._pCompact == nullptr ||
             rcBuildCompactHeightfield( &context, config.walkableHeight, config.walkableClimb, *scratch._pSolid, *scratch._pCompact ) == false )
            return false;
        rcFreeHeightField( scratch._pSolid );
        scratch._pSolid = nullptr;
        if ( rcErodeWalkableArea( &context, config.walkableRadius, *scratch._pCompact ) == false )
            return false;

        // 볼록 부피 — 깎은 뒤에 칠하므로 뚫는 부피(장애물)는 몸 반지름만큼 넓혀 칠한다(유니티 Carve 와 같은 결과).
        vector<float32>                listVolumePoint;
        vector<float32>                listOffsetPoint;
        vector<const NavConvexVolume*> listVolume;
        for ( const NavConvexVolume& volume : geometry.getConvexVolumes() )
            listVolume.push_back( &volume );
        if ( pExtraVolume != nullptr )
        {
            for ( const NavConvexVolume& volume : *pExtraVolume )
                listVolume.push_back( &volume );
        }
        for ( const NavConvexVolume* pVolume : listVolume )
        {
            const NavConvexVolume& volume       = *pVolume;
            const AABB             volumeBounds = volume.computeBounds();
            const bool             bOverlaps    = volumeBounds._min._x <= config.bmax[0] && config.bmin[0] <= volumeBounds._max._x &&
                                   volumeBounds._min._z <= config.bmax[2] && config.bmin[2] <= volumeBounds._max._z;
            if ( bOverlaps == false )
                continue;
            listVolumePoint.clear();
            for ( const float3& point : volume._listPoint )
            {
                listVolumePoint.push_back( point._x );
                listVolumePoint.push_back( volume._minY );
                listVolumePoint.push_back( point._z );
            }
            const uint8    recastArea  = Internal::toRecastArea( volume._area );
            const int32    pointCount  = static_cast<int32>( volume._listPoint.size() );
            const float32* pPolygon    = listVolumePoint.data();
            int32          polygonSize = pointCount;
            if ( recastArea == RC_NULL_AREA && agent._radius > 0.0f )
            {
                const int32 maxOffsetPoint = pointCount * 4 + 8;
                listOffsetPoint.assign( static_cast<size_t>( maxOffsetPoint ) * 3, 0.0f );
                const int32 offsetCount = rcOffsetPoly( listVolumePoint.data(), pointCount, agent._radius, listOffsetPoint.data(), maxOffsetPoint );
                if ( offsetCount >= 3 )
                {
                    pPolygon    = listOffsetPoint.data();
                    polygonSize = offsetCount;
                }
            }
            rcMarkConvexPolyArea( &context, pPolygon, polygonSize, volume._minY, volume._maxY, recastArea, *scratch._pCompact );
        }

        if ( rcBuildDistanceField( &context, *scratch._pCompact ) == false ||
             rcBuildRegions( &context, *scratch._pCompact, config.borderSize, config.minRegionArea, config.mergeRegionArea ) == false )
            return false;
        scratch._pContour = rcAllocContourSet();
        if ( scratch._pContour == nullptr ||
             rcBuildContours( &context, *scratch._pCompact, config.maxSimplificationError, config.maxEdgeLen, *scratch._pContour ) == false )
            return false;
        if ( scratch._pContour->nconts == 0 )
            return true;
        scratch._pPolyMesh = rcAllocPolyMesh();
        if ( scratch._pPolyMesh == nullptr || rcBuildPolyMesh( &context, *scratch._pContour, config.maxVertsPerPoly, *scratch._pPolyMesh ) == false )
            return false;
        scratch._pDetail = rcAllocPolyMeshDetail();
        if ( scratch._pDetail == nullptr || rcBuildPolyMeshDetail( &context, *scratch._pPolyMesh, *scratch._pCompact, config.detailSampleDist,
                                                                   config.detailSampleMaxError, *scratch._pDetail ) == false )
            return false;
        rcPolyMesh& polyMesh = *scratch._pPolyMesh;
        if ( polyMesh.npolys == 0 )
            return true;
        if ( polyMesh.nverts >= 0xffff )
        {
            SW_LOG_ERROR( "Navmesh '%#' tile (%#, %#) has %# vertices - raise the cell size or lower the tile size", agent._name.c_str(), tileX, tileZ,
                          polyMesh.nverts );
            return false;
        }
        // 영역 → 표시 비트(거름의 영역 비트와 같다).
        for ( int32 polyIndex = 0; polyIndex < polyMesh.npolys; ++polyIndex )
        {
            const uint8 recastArea    = polyMesh.areas[polyIndex];
            const bool  bEngineArea   = 1 <= recastArea && recastArea <= NavigationConstant::kMaxAreaCount;
            polyMesh.flags[polyIndex] = bEngineArea ? static_cast<uint16>( 1u << ( recastArea - 1 ) ) : 0;
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

    bool RecastNavMesh::replaceTile( const NavTileData& tile )
    {
        if ( _pNavMesh == nullptr )
            return false;
        const bool bInsideGrid = 0 <= tile._tileX && tile._tileX < _grid._tileCountX && 0 <= tile._tileZ && tile._tileZ < _grid._tileCountZ;
        if ( bInsideGrid == false )
            return false;
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
        return true;
    }

    void RecastNavMesh::collectTiles( vector<NavTileData>& outListTile ) const
    {
        outListTile.clear();
        if ( _pNavMesh == nullptr )
            return;
        for ( int32 tileZ = 0; tileZ < _grid._tileCountZ; ++tileZ )
        {
            for ( int32 tileX = 0; tileX < _grid._tileCountX; ++tileX )
            {
                const dtMeshTile* pTile = _pNavMesh->getTileAt( tileX, tileZ, 0 );
                if ( pTile == nullptr || pTile->header == nullptr || pTile->data == nullptr )
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

    dtNavMeshQuery* RecastNavMesh::acquireQuery( bool& outLocked ) const
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

    void RecastNavMesh::releaseQuery( bool bLocked ) const
    {
        if ( bLocked )
            _sharedQueryMutex.unlock();
    }

    void RecastNavMesh::applyQueryFilter( const NavQueryFilter& filter, dtQueryFilter& outFilter )
    {
        for ( uint32 areaIndex = 0; areaIndex < NavigationConstant::kMaxAreaCount; ++areaIndex )
            outFilter.setAreaCost( static_cast<int32>( areaIndex + 1 ), filter._arrAreaCost[areaIndex] );
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
        outLocation._position = RecastNavMeshInternal::toFloat3( arrNearest );
        outLocation._poly     = static_cast<NavPolyRef>( polyRef );
        return true;
    }

    NavPathStatus RecastNavMesh::findPath( const float3& start, const float3& end, const float3& searchExtent, const NavQueryFilter& filter, NavPath& outPath ) const
    {
        using Internal = RecastNavMeshInternal;
        outPath._listPoint.clear();
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
        (void)pQuery->findNearestPoly( &start._x, &searchExtent._x, &detourFilter, &startRef, arrStart );
        (void)pQuery->findNearestPoly( &end._x, &searchExtent._x, &detourFilter, &endRef, arrEnd );
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
        // 끝 폴리곤에 닿지 못했으면(막힘 · 노드 상한) 닿은 마지막 폴리곤에서 끝에 가장 가까운 점까지만 간다.
        const bool bPartial = dtStatusDetail( status, DT_PARTIAL_RESULT ) || arrPoly[polyCount - 1] != endRef;
        float32    arrGoal[3]{ arrEnd[0], arrEnd[1], arrEnd[2] };
        if ( bPartial )
            (void)pQuery->closestPointOnPoly( arrPoly[polyCount - 1], arrEnd, arrGoal, nullptr );

        constexpr int32 kMaxStraight = static_cast<int32>( NavigationConstant::kMaxPathPointCount );
        float32         arrStraight[kMaxStraight * 3]{};
        int32           straightCount = 0;
        const dtStatus  straightStatus =
            pQuery->findStraightPath( arrStart, arrGoal, arrPoly, polyCount, arrStraight, nullptr, nullptr, &straightCount, kMaxStraight, 0 );
        releaseQuery( bLocked );
        if ( dtStatusFailed( straightStatus ) || straightCount == 0 )
            return NavPathStatus::Failed;
        outPath._listPoint.reserve( static_cast<size_t>( straightCount ) );
        for ( int32 pointIndex = 0; pointIndex < straightCount; ++pointIndex )
            outPath._listPoint.push_back( Internal::toFloat3( &arrStraight[pointIndex * 3] ) );
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
        (void)pQuery->findNearestPoly( &start._x, &searchExtent._x, &detourFilter, &startRef, arrStart );
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
        // 폴리곤 테두리 — 걸을 면의 바깥 경계는 진하게, 안쪽 이음은 옅게. 바닥과 겹쳐 깜빡이지 않게 조금 띄운다.
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
        // Detour 타일 바이트의 판(DT_NAVMESH_VERSION) — 라이브러리가 그 판을 올리면 쿠킹본이 낡는다.
        return static_cast<uint32>( DT_NAVMESH_VERSION );
    }

    unique_ptr<INavMesh> NavMeshBackend::createNavMesh()
    {
        return make_unique<RecastNavMesh>();
    }
} // namespace sw
