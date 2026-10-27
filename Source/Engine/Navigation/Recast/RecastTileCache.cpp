#include "pch.h"

#include "Engine/Navigation/Recast/RecastTileCache.h"

#include "Core/Math/MathUtil.h"
#include "Core/Memory/MemoryTag.h"

#include "Engine/Navigation/NavMeshSettings.h"
#include "Engine/Navigation/Recast/RecastAreaUtil.h"

#include <recastnavigation/DetourAlloc.h>
#include <recastnavigation/DetourNavMesh.h>
#include <recastnavigation/DetourNavMeshBuilder.h>
#include <recastnavigation/DetourTileCache.h>
#include <recastnavigation/DetourTileCacheBuilder.h>
#include <recastnavigation/Recast.h>
#include <cstring>

namespace sw
{
    SW_LOG_CALLER( "RecastTileCache" );

    namespace
    {
        struct RecastTileCacheInternal
        {
            /** @brief 층 묶음 바이트의 머리(`SWTC`)와 형식 버전입니다. */
            static constexpr uint32 kPackMagic   = 0x43545753u;
            static constexpr uint32 kPackVersion = 1;
            /** @brief 층에서 짓는 동안 쓰는 임시 메모리 상한(바이트)입니다. 타일 크기 256 의 층도 담는다. */
            static constexpr size_t kBuildScratchBytes = 4u * constant::kBytesPerMegabyte;

            /** @brief 바이트 끝에 값 하나를 붙입니다(리틀 엔디언 — 같은 기계의 쿠킹 · 실행이 읽는다). */
            template <typename T>
            static void writeValue( vector<uint8>& outBytes, const T& value )
            {
                const size_t offset = outBytes.size();
                outBytes.resize( offset + sizeof( T ) );
                std::memcpy( outBytes.data() + offset, &value, sizeof( T ) );
            }

            /** @brief 읽는 자리에서 값 하나를 꺼냅니다. 모자라면 false 입니다. */
            template <typename T>
            [[nodiscard]] static bool readValue( const vector<uint8>& bytes, size_t& ioOffset, T& outValue )
            {
                if ( bytes.size() < ioOffset + sizeof( T ) )
                    return false;
                std::memcpy( &outValue, bytes.data() + ioOffset, sizeof( T ) );
                ioOffset += sizeof( T );
                return true;
            }

            /** @brief 층 묶음 하나를 풀어 낸 것입니다 — 층 바이트(가리키기만 한다)와 링크입니다. */
            struct ParsedPack
            {
                vector<const uint8*>   _listLayer;
                vector<int32>          _listLayerSize;
                vector<NavOffMeshLink> _listLink;
            };

            /** @brief 층 묶음 바이트를 풉니다. 형식이 틀리면 false 입니다. */
            [[nodiscard]] static bool parsePack( const vector<uint8>& bytes, ParsedPack& outPack )
            {
                outPack = ParsedPack{};
                size_t     offset{ 0 };
                uint32     magic{ 0 };
                uint32     version{ 0 };
                uint32     layerCount{ 0 };
                const bool bHeaderRead = readValue( bytes, offset, magic ) && readValue( bytes, offset, version ) && readValue( bytes, offset, layerCount );
                if ( bHeaderRead == false )
                    return false;
                const bool bWrongHeader = magic != kPackMagic || version != kPackVersion || layerCount > RecastTileCache::kMaxLayerCount;
                if ( bWrongHeader )
                    return false;
                for ( uint32 layerIndex = 0; layerIndex < layerCount; ++layerIndex )
                {
                    uint32 layerSize{ 0 };
                    if ( readValue( bytes, offset, layerSize ) == false || bytes.size() < offset + layerSize )
                        return false;
                    outPack._listLayer.push_back( bytes.data() + offset );
                    outPack._listLayerSize.push_back( static_cast<int32>( layerSize ) );
                    offset += layerSize;
                }
                uint32 linkCount{ 0 };
                if ( readValue( bytes, offset, linkCount ) == false )
                    return false;
                for ( uint32 linkIndex = 0; linkIndex < linkCount; ++linkIndex )
                {
                    NavOffMeshLink link;
                    const bool     bRead = readValue( bytes, offset, link._start ) && readValue( bytes, offset, link._end ) && readValue( bytes, offset, link._radius ) &&
                                       readValue( bytes, offset, link._userID ) && readValue( bytes, offset, link._area ) &&
                                       readValue( bytes, offset, link._bBidirectional );
                    if ( bRead == false )
                        return false;
                    outPack._listLink.push_back( link );
                }
                return offset == bytes.size();
            }
        };

        /** @brief 층을 그대로 싣는 압축기입니다(파일 머리말 — 코덱을 더 들이지 않는다). */
        struct RecastTileCacheStoreCompressor final : public dtTileCacheCompressor
        {
            int32 maxCompressedSize( const int32 bufferSize ) override { return bufferSize; }

            dtStatus compress( const uint8* pBuffer, const int32 bufferSize, uint8* pCompressed, const int32 maxCompressedSize, int32* pCompressedSize ) override
            {
                if ( bufferSize > maxCompressedSize )
                    return DT_FAILURE | DT_BUFFER_TOO_SMALL;
                std::memcpy( pCompressed, pBuffer, static_cast<size_t>( bufferSize ) );
                *pCompressedSize = bufferSize;
                return DT_SUCCESS;
            }

            dtStatus decompress( const uint8* pCompressed, const int32 compressedSize, uint8* pBuffer, const int32 maxBufferSize, int32* pBufferSize ) override
            {
                if ( compressedSize > maxBufferSize )
                    return DT_FAILURE | DT_BUFFER_TOO_SMALL;
                std::memcpy( pBuffer, pCompressed, static_cast<size_t>( compressedSize ) );
                *pBufferSize = compressedSize;
                return DT_SUCCESS;
            }
        };

        /** @brief 층에서 짓는 동안의 임시 메모리입니다(한 번 짓고 비운다 — Recast 샘플의 선형 할당기). 모자라면 nullptr 이고 그 타일 짓기가 실패한다. */
        struct RecastTileCacheLinearAllocator final : public dtTileCacheAlloc
        {
            vector<uint8> _buffer;
            size_t        _top{ 0 };

            RecastTileCacheLinearAllocator()
                : _buffer{}
            {
                _buffer.resize( RecastTileCacheInternal::kBuildScratchBytes ); // 중괄호로 크기를 주면 원소 하나짜리 목록이 된다
            }

            void reset() override { _top = 0; }

            void* alloc( const size_t size ) override
            {
                const size_t aligned = ( size + 15u ) & ~static_cast<size_t>( 15u );
                if ( _top + aligned > _buffer.size() )
                    return nullptr;
                void* const pResult = _buffer.data() + _top;
                _top += aligned;
                return pResult;
            }

            void free( void* pPtr ) override { (void)pPtr; } // 선형 — `reset` 이 한꺼번에 돌려준다
        };

        /** @brief 층에서 지은 폴리곤의 표시 비트와 그 타일의 오프 메시 링크를 채웁니다(정적 베이크와 같은 규칙 — `RecastAreaUtil`). */
        struct RecastTileCacheMeshProcess final : public dtTileCacheMeshProcess
        {
            const vector<NavTileData>* _pListPack{ nullptr };
            vector<float32>            _listLinkVertex;
            vector<float32>            _listLinkRadius;
            vector<uint16>             _listLinkFlag;
            vector<uint8>              _listLinkArea;
            vector<uint8>              _listLinkDirection;
            vector<uint32>             _listLinkUserID;

            void process( dtNavMeshCreateParams* pParams, uint8* pPolyAreas, uint16* pPolyFlags ) override
            {
                for ( int32 polyIndex = 0; polyIndex < pParams->polyCount; ++polyIndex )
                {
                    pPolyFlags[polyIndex] = RecastAreaUtil::toPolyFlags( pPolyAreas[polyIndex] );
                }
                _listLinkVertex.clear();
                _listLinkRadius.clear();
                _listLinkFlag.clear();
                _listLinkArea.clear();
                _listLinkDirection.clear();
                _listLinkUserID.clear();
                // 링크는 그 타일의 첫 층에만 싣는다 — 층마다 실으면 같은 링크가 겹친다. 끝은 이웃 타일이 끼워질 때 연결된다.
                const NavTileData* pPack = nullptr;
                for ( const NavTileData& pack : *_pListPack )
                {
                    if ( pack._tileX == pParams->tileX && pack._tileZ == pParams->tileY )
                        pPack = &pack;
                }
                RecastTileCacheInternal::ParsedPack parsed;
                const bool                          bLinksHere = pParams->tileLayer == 0 && pPack != nullptr && RecastTileCacheInternal::parsePack( pPack->_bytes, parsed );
                if ( bLinksHere )
                {
                    for ( const NavOffMeshLink& link : parsed._listLink )
                    {
                        const uint8 recastArea = RecastAreaUtil::toRecastArea( link._area );
                        for ( const float3* pPoint : { &link._start, &link._end } )
                        {
                            _listLinkVertex.insert( _listLinkVertex.end(), { pPoint->_x, pPoint->_y, pPoint->_z } );
                        }
                        _listLinkRadius.push_back( link._radius );
                        _listLinkArea.push_back( recastArea );
                        _listLinkFlag.push_back( RecastAreaUtil::toPolyFlags( recastArea ) );
                        _listLinkDirection.push_back( link._bBidirectional == SW_TRUE ? static_cast<uint8>( DT_OFFMESH_CON_BIDIR ) : static_cast<uint8>( 0 ) );
                        _listLinkUserID.push_back( link._userID );
                    }
                }
                pParams->offMeshConVerts  = _listLinkVertex.data();
                pParams->offMeshConRad    = _listLinkRadius.data();
                pParams->offMeshConFlags  = _listLinkFlag.data();
                pParams->offMeshConAreas  = _listLinkArea.data();
                pParams->offMeshConDir    = _listLinkDirection.data();
                pParams->offMeshConUserID = _listLinkUserID.data();
                pParams->offMeshConCount  = static_cast<int32>( _listLinkRadius.size() );
            }
        };
    } // namespace

    /** @brief 캐시가 빌려 쓰는 라이브러리 쪽 부품입니다(캐시보다 오래 살아야 한다 — 캐시를 먼저 놓는다). */
    struct RecastTileCache::Parts
    {
        RecastTileCacheLinearAllocator _allocator;
        RecastTileCacheStoreCompressor _compressor;
        RecastTileCacheMeshProcess     _meshProcess;
        uint32                         _pendingRequestCount{ 0 }; ///< 다음 `update` 가 처리할 장애물 요청 수
        bool                           _bUpToDate{ true };        ///< 지난 `update` 가 할 일을 다 끝냈다
    };
} // namespace sw

namespace sw
{
    RecastTileCache::RecastTileCache()
        : _pParts{ make_unique<Parts>() }
        , _pTileCache{ nullptr }
        , _listPack{}
    {
        _pParts->_meshProcess._pListPack = &_listPack;
    }

    RecastTileCache::~RecastTileCache()
    {
        shutdown();
    }

    bool RecastTileCache::initialize( const NavAgentTypeDef& agentType, const NavTileGrid& grid, float32 minY, float32 maxY )
    {
        shutdown();
        dtTileCacheParams params{};
        params.orig[0]                = grid._origin._x;
        params.orig[1]                = minY;
        params.orig[2]                = grid._origin._z;
        params.cs                     = agentType._cellSize;
        params.ch                     = agentType._cellHeight;
        params.width                  = static_cast<int32>( agentType._tileSize );
        params.height                 = static_cast<int32>( agentType._tileSize );
        params.walkableHeight         = agentType._height;
        params.walkableRadius         = agentType._radius;
        params.walkableClimb          = agentType._maxClimb;
        params.maxSimplificationError = agentType._maxEdgeError;
        params.maxTiles               = grid.getTileCount() * static_cast<int32>( kMaxLayerCount );
        params.maxObstacles           = static_cast<int32>( agentType._maxCacheObstacleCount );
        (void)maxY; // 층은 자기 높이 범위를 들고 있다 — 캐시는 원점의 높이만 안다
        _pTileCache = dtAllocTileCache();
        if ( _pTileCache == nullptr ||
             dtStatusFailed( _pTileCache->init( &params, &_pParts->_allocator, &_pParts->_compressor, &_pParts->_meshProcess ) ) )
        {
            SW_LOG_ERROR( "Navmesh '%#' tile cache could not be initialized (%# tiles x %# layers, %# obstacles)", agentType._name.c_str(), grid.getTileCount(),
                          kMaxLayerCount, agentType._maxCacheObstacleCount );
            shutdown();
            return false;
        }
        return true;
    }

    void RecastTileCache::shutdown()
    {
        dtFreeTileCache( _pTileCache );
        _pTileCache = nullptr;
        _listPack.clear();
        _pParts->_pendingRequestCount = 0;
        _pParts->_bUpToDate           = true;
    }

    bool RecastTileCache::makeLayerPack( rcContext& context, rcCompactHeightfield& compact, const rcConfig& config, int32 tileX, int32 tileZ,
                                         const vector<NavOffMeshLink>& listLink, vector<uint8>& outBytes )
    {
        using Internal = RecastTileCacheInternal;
        outBytes.clear();
        rcHeightfieldLayerSet* pLayerSet = rcAllocHeightfieldLayerSet();
        if ( pLayerSet == nullptr )
            return false;
        if ( rcBuildHeightfieldLayers( &context, compact, config.borderSize, config.walkableHeight, *pLayerSet ) == false )
        {
            rcFreeHeightfieldLayerSet( pLayerSet );
            return false;
        }
        const int32 layerCount = MathUtil::min( pLayerSet->nlayers, static_cast<int32>( kMaxLayerCount ) );
        if ( pLayerSet->nlayers > static_cast<int32>( kMaxLayerCount ) )
            SW_LOG_WARNING( "Nav tile (%#, %#) has %# height layers - only the lowest %# are kept", tileX, tileZ, pLayerSet->nlayers, kMaxLayerCount );
        if ( layerCount == 0 )
        {
            rcFreeHeightfieldLayerSet( pLayerSet );
            return true;
        }

        RecastTileCacheStoreCompressor compressor;
        Internal::writeValue( outBytes, Internal::kPackMagic );
        Internal::writeValue( outBytes, Internal::kPackVersion );
        Internal::writeValue( outBytes, static_cast<uint32>( layerCount ) );
        bool bBuilt = true;
        for ( int32 layerIndex = 0; layerIndex < layerCount && bBuilt; ++layerIndex )
        {
            const rcHeightfieldLayer& layer = pLayerSet->layers[layerIndex];
            dtTileCacheLayerHeader    header{};
            header.magic   = DT_TILECACHE_MAGIC;
            header.version = DT_TILECACHE_VERSION;
            header.tx      = tileX;
            header.ty      = tileZ;
            header.tlayer  = layerIndex;
            rcVcopy( header.bmin, layer.bmin );
            rcVcopy( header.bmax, layer.bmax );
            header.width  = static_cast<uint8>( layer.width );
            header.height = static_cast<uint8>( layer.height );
            header.minx   = static_cast<uint8>( layer.minx );
            header.maxx   = static_cast<uint8>( layer.maxx );
            header.miny   = static_cast<uint8>( layer.miny );
            header.maxy   = static_cast<uint8>( layer.maxy );
            header.hmin   = static_cast<uint16>( layer.hmin );
            header.hmax   = static_cast<uint16>( layer.hmax );
            uint8* pData{ nullptr };
            int32  dataSize{ 0 };
            if ( dtStatusFailed( dtBuildTileCacheLayer( &compressor, &header, layer.heights, layer.areas, layer.cons, &pData, &dataSize ) ) )
            {
                bBuilt = false;
                break;
            }
            Internal::writeValue( outBytes, static_cast<uint32>( dataSize ) );
            outBytes.insert( outBytes.end(), pData, pData + dataSize );
            dtFree( pData );
        }
        rcFreeHeightfieldLayerSet( pLayerSet );
        if ( bBuilt == false )
        {
            outBytes.clear();
            return false;
        }
        Internal::writeValue( outBytes, static_cast<uint32>( listLink.size() ) );
        for ( const NavOffMeshLink& link : listLink )
        {
            Internal::writeValue( outBytes, link._start );
            Internal::writeValue( outBytes, link._end );
            Internal::writeValue( outBytes, link._radius );
            Internal::writeValue( outBytes, link._userID );
            Internal::writeValue( outBytes, link._area );
            Internal::writeValue( outBytes, link._bBidirectional );
        }
        return true;
    }

    bool RecastTileCache::installPack( const NavTileData& pack, dtNavMesh& navMesh )
    {
        using Internal = RecastTileCacheInternal;
        if ( _pTileCache == nullptr )
            return false;
        Internal::ParsedPack parsed;
        if ( pack._bytes.empty() == false && Internal::parsePack( pack._bytes, parsed ) == false )
        {
            SW_LOG_ERROR( "Nav tile (%#, %#) bytes are not a tile-cache layer pack - the cooked navmesh does not match the obstacle mode", pack._tileX, pack._tileZ );
            return false;
        }

        // 옛것을 뺀다 — 압축 층, 그 자리의 모든 층의 내비메시 타일, 묶음.
        dtCompressedTileRef arrOldLayer[kMaxLayerCount * 2];
        const int32         oldLayerCount = _pTileCache->getTilesAt( pack._tileX, pack._tileZ, arrOldLayer, static_cast<int32>( kMaxLayerCount * 2 ) );
        for ( int32 index = 0; index < oldLayerCount; ++index )
        {
            (void)_pTileCache->removeTile( arrOldLayer[index], nullptr, nullptr ); // 캐시가 들고 있던 데이터를 캐시가 놓는다
        }
        const dtNavMesh* const pConstNavMesh = &navMesh;
        const dtMeshTile*      arrOldTile[kMaxLayerCount * 2];
        const int32            oldTileCount = pConstNavMesh->getTilesAt( pack._tileX, pack._tileZ, arrOldTile, static_cast<int32>( kMaxLayerCount * 2 ) );
        for ( int32 index = 0; index < oldTileCount; ++index )
        {
            (void)navMesh.removeTile( pConstNavMesh->getTileRef( arrOldTile[index] ), nullptr, nullptr ); // 방금 찾은 타일의 참조라 실패하지 않는다
        }
        for ( size_t index = 0; index < _listPack.size(); ++index )
        {
            if ( _listPack[index]._tileX != pack._tileX || _listPack[index]._tileZ != pack._tileZ )
                continue;
            _listPack[index] = std::move( _listPack.back() );
            _listPack.pop_back();
            break;
        }
        if ( pack._bytes.empty() )
            return true;

        // 새 층을 캐시에 넣는다(캐시가 복사본을 갖고 놓는다). 묶음은 메시 처리기가 링크를 찾도록 남긴다 — 짓기 전에 넣는다.
        _listPack.push_back( pack );
        for ( size_t layerIndex = 0; layerIndex < parsed._listLayer.size(); ++layerIndex )
        {
            const int32  layerSize = parsed._listLayerSize[layerIndex];
            uint8* const pData     = static_cast<uint8*>( dtAlloc( static_cast<size_t>( layerSize ), DT_ALLOC_PERM ) );
            if ( pData == nullptr )
                return false;
            std::memcpy( pData, parsed._listLayer[layerIndex], static_cast<size_t>( layerSize ) );
            if ( dtStatusFailed( _pTileCache->addTile( pData, layerSize, DT_COMPRESSEDTILE_FREE_DATA, nullptr ) ) )
            {
                dtFree( pData );
                SW_LOG_ERROR( "Nav tile (%#, %#) layer %# was rejected by the tile cache (full or not this layout)", pack._tileX, pack._tileZ, layerIndex );
                return false;
            }
        }
        // 지금 든 장애물로 이 타일의 층마다 내비메시 타일을 짓는다.
        return dtStatusSucceed( _pTileCache->buildNavMeshTilesAt( pack._tileX, pack._tileZ, &navMesh ) );
    }

    void RecastTileCache::collectPacks( vector<NavTileData>& outListPack ) const
    {
        outListPack = _listPack;
    }

    NavObstacleRef RecastTileCache::addObstacle( const NavCacheObstacleDesc& desc )
    {
        if ( _pTileCache == nullptr )
            return 0;
        // Detour 가 쓸 모양을 먼저 채워 그 경계 상자(Detour 식 그대로)가 걸치는 타일 층을 센다.
        dtTileCacheObstacle probe{};
        if ( desc._bCylinder )
        {
            // Detour 원기둥의 자리는 바닥 가운데다.
            probe.type            = DT_OBSTACLE_CYLINDER;
            probe.cylinder.pos[0] = desc._center._x;
            probe.cylinder.pos[1] = desc._center._y - desc._halfExtents._y;
            probe.cylinder.pos[2] = desc._center._z;
            probe.cylinder.radius = desc._halfExtents._x;
            probe.cylinder.height = desc._halfExtents._y * 2.0f;
        }
        else
        {
            probe.type                       = DT_OBSTACLE_ORIENTED_BOX;
            probe.orientedBox.center[0]      = desc._center._x;
            probe.orientedBox.center[1]      = desc._center._y;
            probe.orientedBox.center[2]      = desc._center._z;
            probe.orientedBox.halfExtents[0] = desc._halfExtents._x;
            probe.orientedBox.halfExtents[1] = desc._halfExtents._y;
            probe.orientedBox.halfExtents[2] = desc._halfExtents._z;
        }
        // Detour 는 장애물 하나가 고칠 타일 층을 DT_MAX_TOUCHED_TILES 개에서 말없이 자른다 — 넘으면 일부 타일에만 구멍이 난다. 자르지 않고 거절한다.
        float32 arrBoundsMin[3]{};
        float32 arrBoundsMax[3]{};
        _pTileCache->getObstacleBounds( &probe, arrBoundsMin, arrBoundsMax );
        dtCompressedTileRef arrTouched[DT_MAX_TOUCHED_TILES + 1]{};
        int32               touchedCount{ 0 };
        _pTileCache->queryTiles( arrBoundsMin, arrBoundsMax, arrTouched, &touchedCount, DT_MAX_TOUCHED_TILES + 1 );
        if ( touchedCount > DT_MAX_TOUCHED_TILES )
        {
            SW_LOG_ERROR( "Nav obstacle bounds (%# x %# m) touch more than %# tile layers - split the obstacle or raise _tileSize", arrBoundsMax[0] - arrBoundsMin[0],
                          arrBoundsMax[2] - arrBoundsMin[2], DT_MAX_TOUCHED_TILES );
            return 0;
        }
        dtObstacleRef  ref{ 0 };
        const dtStatus status = desc._bCylinder ? _pTileCache->addObstacle( probe.cylinder.pos, probe.cylinder.radius, probe.cylinder.height, &ref )
                                                : _pTileCache->addBoxObstacle( probe.orientedBox.center, probe.orientedBox.halfExtents, desc._yaw, &ref );
        if ( dtStatusFailed( status ) )
        {
            SW_LOG_ERROR( "Nav obstacle could not enter the tile cache (cache full or too many requests this frame) - raise _maxCacheObstacleCount" );
            return 0;
        }
        ++_pParts->_pendingRequestCount;
        return static_cast<NavObstacleRef>( ref );
    }

    void RecastTileCache::removeObstacle( NavObstacleRef obstacle )
    {
        if ( _pTileCache == nullptr || obstacle == 0 )
            return;
        if ( dtStatusFailed( _pTileCache->removeObstacle( static_cast<dtObstacleRef>( obstacle ) ) ) )
        {
            SW_LOG_ERROR( "Nav obstacle %# could not leave the tile cache (too many requests this frame)", obstacle );
            return;
        }
        ++_pParts->_pendingRequestCount;
    }

    bool RecastTileCache::update( float32 deltaSeconds, dtNavMesh& navMesh )
    {
        if ( _pTileCache == nullptr )
            return false;
        const bool bHadWork = _pParts->_pendingRequestCount > 0 || _pParts->_bUpToDate == false;
        if ( bHadWork == false )
            return false;
        bool           bUpToDate = false;
        const dtStatus status    = _pTileCache->update( deltaSeconds, &navMesh, &bUpToDate );
        if ( dtStatusFailed( status ) )
            SW_LOG_ERROR( "Nav tile cache update failed (status 0x%#)", static_cast<uint32>( status ) );
        _pParts->_pendingRequestCount = 0;
        _pParts->_bUpToDate           = bUpToDate;
        return true;
    }

    uint32 RecastTileCache::getObstacleCount() const
    {
        if ( _pTileCache == nullptr )
            return 0;
        uint32 count{ 0 };
        for ( int32 index = 0; index < _pTileCache->getObstacleCount(); ++index )
        {
            const dtTileCacheObstacle* pObstacle = _pTileCache->getObstacle( index );
            const bool                 bLive     = pObstacle != nullptr && pObstacle->state != DT_OBSTACLE_EMPTY && pObstacle->state != DT_OBSTACLE_REMOVING;
            if ( bLive )
                ++count;
        }
        return count;
    }
} // namespace sw
