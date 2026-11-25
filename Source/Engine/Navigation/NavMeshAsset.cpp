#include "pch.h"

#include "Engine/Navigation/NavMeshAsset.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Serialization/ByteStream.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Content/AssetIO.h"
#include "Engine/Foundation/EngineParallel.h"
#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Navigation/NavMeshSettings.h"

namespace sw
{
    namespace
    {
        struct NavMeshAssetInternal
        {
            /** @brief 씬 저작본(`.scene.xml`)과 쿠킹본(`.scene.bin`) 접미사입니다. 파일이 하나도 없던 맨 `.scene` 은 별칭이라 두지 않습니다. */
            static constexpr string_view kArrSceneSuffix[] = { ".scene.xml", ".scene.bin" };
            /** @brief 머리의 타일 한 줄 바이트 수입니다(x · z · 내용 안 시작 · 길이). 깨진 타일 수를 할당 전에 남은 바이트로 거른다. */
            static constexpr size_t kMinTileBytes = sizeof( int32 ) * 2 + sizeof( uint64 ) * 3;
            /** @brief 고정 머리입니다(매직 · 판 · 머리 길이). 머리만 읽는 로더가 먼저 이만큼 읽는다. */
            static constexpr size_t kFixedHeadBytes = sizeof( uint32 ) * 2 + sizeof( uint64 );
            /** @brief 머리 길이 칸의 자리입니다(쓴 뒤 채운다). */
            static constexpr size_t kHeadSizeOffset = sizeof( uint32 ) * 2;
            /** @brief 항목 하나의 최소 바이트입니다(종류 이름 길이 · 두 해시 · 경계 · 타일 수 · 클러스터 수 · 간선 수). */
            static constexpr size_t kMinEntryBytes = sizeof( uint32 ) + sizeof( uint64 ) * 2 + sizeof( AABB ) + sizeof( uint32 ) * 3;
            /** @brief 타일 베이크 한 덩어리입니다. 워커는 자기 구간의 슬롯만 씁니다. */
            /** @brief 타일 하나의 굽기 결과 칸입니다(`BakeJob` 이 타일마다 하나 쓴다). */
            struct TileBake
            {
                NavTileData _tile;
                uint8       _bSucceeded{ SW_FALSE };
            };

            struct BakeJob
            {
                const INavMeshTileStore*       _pNavMesh{ nullptr };
                const NavMeshGeometry*         _pGeometry{ nullptr };
                const vector<NavConvexVolume>* _pExtraVolume{ nullptr };
                int32                          _tileCountX{ 0 };

                /** @brief 타일 @p tileIndex 를 구워 제 칸 @p outBake 에 씁니다(`runParallelInto` 본문 — 타일 저장소 · 지오메트리는 읽기만 한다). */
                void operator()( uint32 tileIndex, TileBake& outBake ) const
                {
                    const int32 tileX   = static_cast<int32>( tileIndex ) % _tileCountX;
                    const int32 tileZ   = static_cast<int32>( tileIndex ) / _tileCountX;
                    outBake._bSucceeded = _pNavMesh->bakeTile( *_pGeometry, _pExtraVolume, tileX, tileZ, outBake._tile ) ? SW_TRUE : SW_FALSE;
                }
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    NavMeshAsset::NavMeshAsset()
        : _listEntry{}
        , _bHeadOnly{ SW_FALSE }
    {
    }

    string NavMeshAsset::makeCookedPath( string_view scenePath )
    {
        for ( const string_view suffix : NavMeshAssetInternal::kArrSceneSuffix )
        {
            if ( StringUtil::endsWith( scenePath, suffix, true ) )
            {
                string path{ scenePath.substr( 0, scenePath.size() - suffix.size() ) };
                path.append( kExtension.data(), kExtension.size() );
                return StringUtil::toLower( path.c_str() );
            }
        }
        return string{};
    }

    void NavMeshAsset::writeBytes( vector<uint8>& outBytes ) const
    {
        outBytes.clear();
        // 머리만 읽은 쿠킹본은 타일 바이트가 없다 — 쓰면 모든 타일이 "걸을 곳 없음" 이 된다.
        if ( _bHeadOnly == SW_TRUE )
        {
            SW_LOG_ERROR_ID( "NavMeshAsset.HeadOnlyNotWritten", "A navmesh read head-only (tile bytes left in the cooked file) cannot be written" );
            return;
        }
        ByteWriter writer{ outBytes };
        kFormat.write( outBytes );
        writer.writePod( uint64{ 0 } ); // 머리 길이 — 머리를 다 쓴 뒤 채운다
        writer.writeString( NavMeshBackend::getBackendName() );
        writer.writePod( NavMeshBackend::getBackendFormatVersion() );
        writer.writePod( static_cast<uint32>( _listEntry.size() ) );
        uint64 contentOffset = 0;
        for ( const NavMeshAssetEntry& entry : _listEntry )
        {
            writer.writeString( entry._agentType.view() );
            writer.writePod( entry._settingsHash );
            writer.writePod( entry._inputHash );
            writer.writePod( entry._bounds );
            writer.writePod( static_cast<uint32>( entry._listTile.size() ) );
            for ( size_t tileIndex = 0; tileIndex < entry._listTile.size(); ++tileIndex )
            {
                const NavTileData& tile = entry._listTile[tileIndex];
                writer.writePod( tile._tileX );
                writer.writePod( tile._tileZ );
                writer.writePod( contentOffset );
                writer.writePod( static_cast<uint64>( tile._bytes.size() ) );
                writer.writePod( tileIndex < entry._listTileInputHash.size() ? entry._listTileInputHash[tileIndex] : uint64{ 0 } ); // 0 — 모른다
                contentOffset += tile._bytes.size();
            }
            entry._clusterGraph.write( writer );
        }
        writer.writePodAt( NavMeshAssetInternal::kHeadSizeOffset, static_cast<uint64>( writer.getByteCount() ) );
        // 머리 뒤에 타일 바이트를 머리의 순서대로 빈틈없이 잇는다.
        for ( const NavMeshAssetEntry& entry : _listEntry )
        {
            for ( const NavTileData& tile : entry._listTile )
            {
                writer.writeBytes( tile._bytes.data(), tile._bytes.size() );
            }
        }
    }

    bool NavMeshAsset::readBytes( const uint8* pData, size_t size, string_view sourceLabel )
    {
        _listEntry.clear();
        _bHeadOnly = SW_FALSE;
        ByteReader reader{ pData, size };
        if ( kFormat.read( reader, sourceLabel, NavMeshAssetInternal::kFixedHeadBytes ) == false )
            return false;
        const uint64 headSize = reader.readPod<uint64>();
        if ( headSize < NavMeshAssetInternal::kFixedHeadBytes || headSize > size )
        {
            SW_LOG_ERROR_ID( "NavMeshAsset.Truncated", "'%#' is truncated, longer than its entries or holds non-finite bounds", sourceLabel );
            return false;
        }
        return readHead( pData, static_cast<size_t>( headSize ), size - headSize, pData + headSize, sourceLabel );
    }

    bool NavMeshAsset::readHead( const uint8* pHead, size_t headSize, uint64 contentSize, const uint8* pContent, string_view sourceLabel )
    {
        _listEntry.clear();
        ByteReader reader{ pHead, headSize };
        if ( kFormat.read( reader, sourceLabel ) == false )
            return false;
        uint64 headSizeField = 0;
        string backendName;
        uint32 backendVersion = 0;
        uint32 entryCount     = 0;
        headSizeField         = reader.readPod<uint64>();
        backendName           = reader.readString();
        backendVersion        = reader.readPod<uint32>();
        entryCount            = reader.readPod<uint32>();
        if ( reader.hasOverflowed() )
        {
            SW_LOG_ERROR_ID( "NavMeshAsset.Truncated", "'%#' is truncated, longer than its entries or holds non-finite bounds", sourceLabel );
            return false;
        }
        if ( backendName != NavMeshBackend::getBackendName() || backendVersion != NavMeshBackend::getBackendFormatVersion() )
        {
            // 낡은 쿠킹본은 쿠킹이 빠졌다는 뜻이다(쿠킹은 코드 지문으로 다시 굽는다) — 경고로 넘기면 배포본이 매 기동 런타임 베이크로 조용히 느려진다.
            SW_LOG_ERROR_ID( "NavMeshAsset.CookedNavmeshBackendV", "'%#' was cooked by navmesh backend '%#' v%# - this build uses '%#' v%#; cook again", sourceLabel, backendName.c_str(), backendVersion,
                             NavMeshBackend::getBackendName(), NavMeshBackend::getBackendFormatVersion() );
            return false;
        }
        uint64     nextContentOffset = 0;
        const bool bEntriesRead      = reader.readList( _listEntry, entryCount, NavMeshAssetInternal::kMinEntryBytes, [&]( NavMeshAssetEntry& entry ) -> bool
             {
            string agentType;
            uint32 tileCount    = 0;
            agentType           = reader.readString();
            entry._settingsHash = reader.readPod<decltype( entry._settingsHash )>();
            entry._inputHash    = reader.readPod<decltype( entry._inputHash )>();
            entry._bounds       = reader.readPod<decltype( entry._bounds )>();
            tileCount           = reader.readPod<uint32>();
            const bool bHead    = reader.hasOverflowed() == false;
            // 경계는 통째로 읽었다 — 유한한지는 여기서 본다(`readFloat32` 의 규칙).
            const bool bFiniteBounds = entry._bounds._min.isFinite() && entry._bounds._max.isFinite();
            if ( bHead == false || bFiniteBounds == false )
                return false;
            entry._agentType = hashed_string( agentType );
            entry._listTileRange.clear();
            entry._listTileInputHash.clear();
            const bool bTiles = reader.readList( entry._listTile, tileCount, NavMeshAssetInternal::kMinTileBytes, [&]( NavTileData& tile ) -> bool
                 {
                NavTileRange range{};
                uint64       inputHash = 0;
                tile._tileX            = reader.readPod<decltype( tile._tileX )>();
                tile._tileZ            = reader.readPod<decltype( tile._tileZ )>();
                range._offset          = reader.readPod<decltype( range._offset )>();
                range._size            = reader.readPod<decltype( range._size )>();
                inputHash              = reader.readPod<uint64>();
                const bool bTile       = reader.hasOverflowed() == false;
                // 타일 바이트는 머리의 순서대로 빈틈없이 놓인다 — 그 밖의 구간(겹침 · 틈 · 내용 밖)은 깨진 파일이다.
                if ( bTile == false || range._offset != nextContentOffset || range._size > contentSize - nextContentOffset )
                    return false;
                if ( pContent != nullptr )
                    tile._bytes.assign( pContent + range._offset, pContent + range._offset + range._size );
                nextContentOffset += range._size;
                range._offset += headSize; // 파일 처음에서 센 자리로 둔다
                entry._listTileRange.push_back( range );
                entry._listTileInputHash.push_back( inputHash );
                return true;
            } );
            return bTiles && entry._clusterGraph.read( reader );
        } );
        if ( bEntriesRead == false || reader.isFullyRead() == false || nextContentOffset != contentSize || headSizeField != headSize )
        {
            SW_LOG_ERROR_ID( "NavMeshAsset.Truncated", "'%#' is truncated, longer than its entries or holds non-finite bounds", sourceLabel );
            _listEntry.clear();
            return false;
        }
        return true;
    }

    bool NavMeshAsset::loadFromAsset( const AssetID& assetID )
    {
        const string_view assetPath = assetID.view();
        vector<uint8>     bytes;
        if ( AssetIO::readBinaryAsset( assetID, bytes ) == false )
            return false;
        return readBytes( bytes.data(), bytes.size(), assetPath );
    }

    bool NavMeshAsset::loadHeadFromAsset( const AssetID& assetID )
    {
        _listEntry.clear();
        _bHeadOnly                      = SW_FALSE;
        const string_view      label    = assetID.view();
        const optional<uint64> fileSize = AssetIO::findAssetSize( assetID );
        vector<uint8>          fixedHead;
        if ( fileSize.has_value() == false || AssetIO::readBinaryAssetRange( assetID, 0, NavMeshAssetInternal::kFixedHeadBytes, fixedHead ) == false )
        {
            SW_LOG_ERROR_ID( "NavMeshAsset.NotNavmeshFormat", "'%#' is not a navmesh of format %#", label, kFormat._version );
            return false;
        }
        ByteReader reader{ fixedHead.data(), fixedHead.size() };
        if ( kFormat.read( reader, label, NavMeshAssetInternal::kFixedHeadBytes ) == false )
            return false;
        const uint64  headSize = reader.readPod<uint64>();
        vector<uint8> head;
        const bool    bHeadRead = headSize >= NavMeshAssetInternal::kFixedHeadBytes && headSize <= *fileSize && AssetIO::readBinaryAssetRange( assetID, 0, headSize, head );
        if ( bHeadRead == false || readHead( head.data(), head.size(), *fileSize - headSize, nullptr, label ) == false )
        {
            SW_LOG_ERROR_ID( "NavMeshAsset.Truncated", "'%#' is truncated, longer than its entries or holds non-finite bounds", label );
            return false;
        }
        _bHeadOnly = SW_TRUE;
        return true;
    }

    bool NavMeshAsset::saveToFile( const AbsolutePath& absPath ) const
    {
        vector<uint8> bytes;
        writeBytes( bytes );
        const AbsolutePath directory = absPath.getParent();
        if ( directory.empty() == false && FileUtil::ensureDirectoryExists( directory ) == false )
            return false;
        return FileUtil::writeFile( absPath, bytes.data(), bytes.size() );
    }

    const NavMeshAssetEntry* NavMeshAsset::findEntry( const hashed_string& agentType ) const
    {
        return VectorUtil::findBy<&NavMeshAssetEntry::_agentType>( _listEntry, agentType );
    }

    void NavMeshAsset::setEntry( NavMeshAssetEntry entry )
    {
        for ( NavMeshAssetEntry& existing : _listEntry )
        {
            if ( existing._agentType == entry._agentType )
            {
                existing = std::move( entry );
                return;
            }
        }
        _listEntry.push_back( std::move( entry ) );
    }
} // namespace sw

namespace sw
{
    bool NavMeshAsset::bakeAllTiles( INavMeshTileStore& navMesh, NavMeshGeometry& geometry, const vector<NavConvexVolume>* pExtraVolume, NavMeshBakeStats* pOutStats )
    {
        const Stopwatch    stopwatch;
        const NavTileGrid& grid      = navMesh.getTileGrid();
        const uint32       tileCount = static_cast<uint32>( grid.getTileCount() );
        // 색인 셀은 타일보다 잘게 나눈다. 타일 하나가 몇 셀만 훑게 된다.
        geometry.rebuildSpatialIndex( MathUtil::max( 1.0f, grid._tileWorldSize * 0.25f ) );

        vector<NavMeshAssetInternal::TileBake> listBake( tileCount );
        NavMeshAssetInternal::BakeJob          job{};
        job._pNavMesh     = &navMesh;
        job._pGeometry    = &geometry;
        job._pExtraVolume = pExtraVolume;
        job._tileCountX   = MathUtil::max( 1, grid._tileCountX );
        engine::runParallelInto<NavMeshAssetInternal::TileBake>( listBake, 2, job );

        uint32 failedCount = 0;
        uint32 filledCount = 0;
        for ( const NavMeshAssetInternal::TileBake& bake : listBake )
        {
            if ( bake._bSucceeded == SW_FALSE )
            {
                ++failedCount;
                continue;
            }
            if ( bake._tile._bytes.empty() )
                continue;
            if ( navMesh.replaceTile( bake._tile ) )
                ++filledCount;
            else
                ++failedCount;
        }
        if ( failedCount > 0 )
            SW_LOG_ERROR_ID( "NavMeshAsset.NavmeshTilesFailedBake", "Navmesh '%#': %# of %# tiles failed to bake", navMesh.getAgentType()._name.c_str(), failedCount, tileCount );
        if ( pOutStats != nullptr )
        {
            pOutStats->_milliseconds    = static_cast<float64>( stopwatch.getElapsedMicroseconds() ) / constant::time::kMicrosecondsPerMillisecond;
            pOutStats->_tileCount       = tileCount;
            pOutStats->_filledTileCount = filledCount;
            pOutStats->_failedTileCount = failedCount;
            pOutStats->_polygonCount    = navMesh.getPolygonCount();
        }
        return failedCount == 0;
    }

    bool NavMeshAsset::installTiles( INavMeshTileStore& navMesh, const vector<NavTileData>& listTile )
    {
        bool bAll = true;
        for ( const NavTileData& tile : listTile )
        {
            bAll = navMesh.replaceTile( tile ) && bAll;
        }
        return bAll;
    }

    AABB NavMeshAsset::computeBakeBounds( const NavMeshGeometry& geometry )
    {
        if ( geometry.getBounds().isValid() )
            return geometry.getBounds();
        return AABB{
            float3{-1.0f, -1.0f, -1.0f},
            float3{ 1.0f,  1.0f,  1.0f}
        };
    }
} // namespace sw

namespace sw
{
    SW_ASSET_TYPE( NavMeshAsset, NavMeshAsset::kAssetType );
} // namespace sw
