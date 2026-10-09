#include "pch.h"

#include "Engine/Navigation/NavMeshAsset.h"

#include "Core/Common/FourCcUtil.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Common/EngineParallel.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Navigation/NavMeshSettings.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Serialization/Core/BinaryStream.h"

namespace sw
{
    SW_LOG_CALLER( "NavMeshAsset" );

    namespace
    {
        struct NavMeshAssetInternal
        {
            static constexpr uint32 kMagic = FourCcUtil::make( "SWNV" );
            /** @brief 씬 저작 · 쿠킹 접미사(긴 것부터)입니다. */
            static constexpr string_view kArrSceneSuffix[] = { ".scene.xml", ".scene.bin", ".scene" };
            /** @brief 타일 베이크 한 덩어리 — 워커는 자기 구간의 칸만 쓴다. */
            struct BakeJob
            {
                const INavMesh*                _pNavMesh{ nullptr };
                const NavMeshGeometry*         _pGeometry{ nullptr };
                const vector<NavConvexVolume>* _pExtraVolume{ nullptr };
                NavTileData*                   _pTile{ nullptr };
                uint8*                         _pSucceeded{ nullptr };
                int32                          _tileCountX{ 0 };

                void run( uint32 start, uint32 end )
                {
                    for ( uint32 tileIndex = start; tileIndex < end; ++tileIndex )
                    {
                        const int32 tileX      = static_cast<int32>( tileIndex ) % _tileCountX;
                        const int32 tileZ      = static_cast<int32>( tileIndex ) / _tileCountX;
                        _pSucceeded[tileIndex] = _pNavMesh->bakeTile( *_pGeometry, _pExtraVolume, tileX, tileZ, _pTile[tileIndex] ) ? SW_TRUE : SW_FALSE;
                    }
                }
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    NavMeshAsset::NavMeshAsset()
        : _listEntry{}
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
        BinaryStreamWriter writer{ outBytes };
        writer.write( NavMeshAssetInternal::kMagic );
        writer.write( kVersion );
        writer.writeString( NavMeshBackend::getBackendName() );
        writer.write( NavMeshBackend::getBackendFormatVersion() );
        writer.write( static_cast<uint32>( _listEntry.size() ) );
        for ( const NavMeshAssetEntry& entry : _listEntry )
        {
            writer.writeString( entry._agentType.view() );
            writer.write( entry._settingsHash );
            writer.write( entry._inputHash );
            writer.write( entry._bounds );
            writer.write( static_cast<uint32>( entry._listTile.size() ) );
            for ( const NavTileData& tile : entry._listTile )
            {
                writer.write( tile._tileX );
                writer.write( tile._tileZ );
                writer.writeBytes( tile._bytes );
            }
        }
    }

    bool NavMeshAsset::readBytes( const uint8* pData, size_t size, string_view sourceLabel )
    {
        _listEntry.clear();
        BinaryStreamReader reader{ pData, size };
        uint32             magic   = 0;
        uint32             version = 0;
        string             backendName;
        uint32             backendVersion = 0;
        uint32             entryCount     = 0;
        const bool         bHeader        = reader.read( magic ) && reader.read( version ) && reader.readString( backendName ) && reader.read( backendVersion ) &&
                             reader.read( entryCount );
        if ( bHeader == false || magic != NavMeshAssetInternal::kMagic || version != kVersion )
        {
            SW_LOG_ERROR( "'%#' is not a navmesh of format %#", sourceLabel, kVersion );
            return false;
        }
        if ( backendName != NavMeshBackend::getBackendName() || backendVersion != NavMeshBackend::getBackendFormatVersion() )
        {
            SW_LOG_WARNING( "'%#' was cooked by navmesh backend '%#' v%# - this build uses '%#' v%#", sourceLabel, backendName.c_str(), backendVersion,
                            NavMeshBackend::getBackendName(), NavMeshBackend::getBackendFormatVersion() );
            return false;
        }
        for ( uint32 entryIndex = 0; entryIndex < entryCount; ++entryIndex )
        {
            NavMeshAssetEntry entry;
            string            agentType;
            uint32            tileCount = 0;
            if ( reader.readString( agentType ) == false || reader.read( entry._settingsHash ) == false || reader.read( entry._inputHash ) == false ||
                 reader.read( entry._bounds ) == false || reader.read( tileCount ) == false )
            {
                SW_LOG_ERROR( "'%#' is truncated", sourceLabel );
                _listEntry.clear();
                return false;
            }
            entry._agentType = hashed_string( agentType );
            entry._listTile.resize( tileCount );
            for ( NavTileData& tile : entry._listTile )
            {
                if ( reader.read( tile._tileX ) == false || reader.read( tile._tileZ ) == false || reader.readBytes( tile._bytes ) == false )
                {
                    SW_LOG_ERROR( "'%#' is truncated", sourceLabel );
                    _listEntry.clear();
                    return false;
                }
            }
            _listEntry.push_back( std::move( entry ) );
        }
        return true;
    }

    bool NavMeshAsset::loadFromResource( string_view resourcePath )
    {
        vector<uint8> bytes;
        if ( ResourceUtil::readBinaryResource( resourcePath, bytes ) == false )
            return false;
        return readBytes( bytes.data(), bytes.size(), resourcePath );
    }

    bool NavMeshAsset::saveToFile( string_view absolutePath ) const
    {
        vector<uint8> bytes;
        writeBytes( bytes );
        const string directory = FileUtil::getDirectoryPart( absolutePath );
        if ( directory.empty() == false && FileUtil::ensureDirectoryExists( directory ) == false )
            return false;
        return FileUtil::writeFile( absolutePath, bytes.data(), bytes.size() );
    }

    const NavMeshAssetEntry* NavMeshAsset::findEntry( const hashed_string& agentType ) const
    {
        for ( const NavMeshAssetEntry& entry : _listEntry )
        {
            if ( entry._agentType == agentType )
                return &entry;
        }
        return nullptr;
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
    bool NavMeshBakeUtil::bakeAllTiles( INavMesh& navMesh, NavMeshGeometry& geometry, const vector<NavConvexVolume>* pExtraVolume, NavMeshBakeStats* pOutStats )
    {
        SW_MEMORY_SCOPE( Navigation );
        const Stopwatch    stopwatch;
        const NavTileGrid& grid      = navMesh.getTileGrid();
        const uint32       tileCount = static_cast<uint32>( grid.getTileCount() );
        // 색인 칸은 타일보다 잘게 — 타일 하나가 몇 칸만 훑는다.
        geometry.buildSpatialIndex( MathUtil::max( 1.0f, grid._tileWorldSize * 0.25f ) );

        vector<NavTileData>           listTile( tileCount );
        vector<uint8>                 listSucceeded( tileCount, SW_FALSE );
        NavMeshAssetInternal::BakeJob job{};
        job._pNavMesh     = &navMesh;
        job._pGeometry    = &geometry;
        job._pExtraVolume = pExtraVolume;
        job._pTile        = listTile.data();
        job._pSucceeded   = listSucceeded.data();
        job._tileCountX   = MathUtil::max( 1, grid._tileCountX );
        engine::runParallel( tileCount, 2, SW_DELEGATE_METHOD( ParallelBlockDelegate, &NavMeshAssetInternal::BakeJob::run, &job ) );

        uint32 failedCount = 0;
        uint32 filledCount = 0;
        for ( uint32 tileIndex = 0; tileIndex < tileCount; ++tileIndex )
        {
            if ( listSucceeded[tileIndex] == SW_FALSE )
            {
                ++failedCount;
                continue;
            }
            if ( listTile[tileIndex]._bytes.empty() )
                continue;
            if ( navMesh.replaceTile( listTile[tileIndex] ) )
                ++filledCount;
            else
                ++failedCount;
        }
        if ( failedCount > 0 )
            SW_LOG_ERROR( "Navmesh '%#': %# of %# tiles failed to bake", navMesh.getAgentType()._name.c_str(), failedCount, tileCount );
        if ( pOutStats != nullptr )
        {
            pOutStats->_milliseconds    = static_cast<float64>( stopwatch.getElapsedMicroseconds() ) / 1000.0;
            pOutStats->_tileCount       = tileCount;
            pOutStats->_filledTileCount = filledCount;
            pOutStats->_failedTileCount = failedCount;
            pOutStats->_polygonCount    = navMesh.getPolygonCount();
        }
        return failedCount == 0;
    }

    bool NavMeshBakeUtil::installTiles( INavMesh& navMesh, const vector<NavTileData>& listTile )
    {
        bool bAll = true;
        for ( const NavTileData& tile : listTile )
        {
            bAll = navMesh.replaceTile( tile ) && bAll;
        }
        return bAll;
    }

    AABB NavMeshBakeUtil::computeBakeBounds( const NavMeshGeometry& geometry )
    {
        if ( geometry.getBounds().isValid() )
            return geometry.getBounds();
        return AABB{
            float3{-1.0f, -1.0f, -1.0f},
            float3{ 1.0f,  1.0f,  1.0f}
        };
    }
} // namespace sw
