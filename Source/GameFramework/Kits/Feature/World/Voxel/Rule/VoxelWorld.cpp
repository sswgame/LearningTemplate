#include "pch.h"

#include "GameFramework/Kits/Feature/World/Voxel/Rule/VoxelWorld.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/FormatUpgrade.h"

namespace sw
{
    namespace
    {
        struct VoxelWorldInternal
        {
            /** @brief 청크 안 블록의 인덱스입니다. */
            static constexpr int32 computeLocalIndex( int32 localX, int32 y, int32 localZ ) { return ( y * kVoxelChunkSize + localZ ) * kVoxelChunkSize + localX; }

            static constexpr int32 kNeighborReach = 1; ///< 청크를 올리고 내릴 때 다시 메싱할 이웃의 거리(여덟 이웃)

            /** @brief 블록을 같은 값이 이어지는 구간으로 줄입니다. */
            static void compressRuns( const vector<VoxelBlockIndex>& listBlock, vector<VoxelBlockRun>& outListRun )
            {
                outListRun.clear();
                for ( const VoxelBlockIndex block : listBlock )
                {
                    if ( outListRun.empty() == false && outListRun.back()._block == block )
                        ++outListRun.back()._count;
                    else
                        outListRun.push_back( VoxelBlockRun{ block, 1 } );
                }
            }

            /** @brief 구간을 블록으로 풉니다. 구간 합은 청크 부피다(읽을 때 · 줄일 때 지킨다). */
            static void expandRuns( const vector<VoxelBlockRun>& listRun, vector<VoxelBlockIndex>& outListBlock )
            {
                outListBlock.clear();
                outListBlock.reserve( static_cast<size_t>( kVoxelChunkVolume ) );
                for ( const VoxelBlockRun& run : listRun )
                {
                    outListBlock.insert( outListBlock.end(), run._count, run._block );
                }
            }

            /** @brief 구간 하나를 씁니다(팔레트 번호 uint16 + 길이 uint32 — 런타임 번호 폭을 넓혀도 형식은 그대로다). 길이 0 이 청크를 닫는다. */
            static void writeRun( StateIO& io, VoxelBlockIndex block, uint32 length )
            {
                uint16 paletteNumber = block;
                io.value( paletteNumber );
                io.value( length );
            }

            /**
             * @brief 판 2 → 3: 모든 청크를 순서대로 싣던 판을 (청크 수, 청크마다 x · z · 구간) 판으로 옮깁니다. 머리 · 청크 수 · 팔레트와 구간 바이트는 그대로다.
             *        판 2 는 바뀐 청크를 가리지 않았으므로 모든 청크를 싣는다 — 옛 세이브의 월드(옛 지형 생성 결과 포함)가 그대로 남는다.
             */
            [[nodiscard]] static bool upgradeStateFrom2( Archive& inOldBody, Archive& outNewBody )
            {
                StateIO               reader( inOldBody );
                StateIO               writer( outNewBody );
                int32                 chunkCountX = 0;
                int32                 chunkCountZ = 0;
                vector<hashed_string> listPaletteID;
                reader.value( chunkCountX );
                reader.value( chunkCountZ );
                reader.list( listPaletteID );
                if ( reader.check( 0 <= chunkCountX && 0 <= chunkCountZ ) == false )
                    return false;
                writer.value( chunkCountX );
                writer.value( chunkCountZ );
                writer.list( listPaletteID );
                uint32 chunkCount = static_cast<uint32>( chunkCountX ) * static_cast<uint32>( chunkCountZ );
                writer.value( chunkCount );
                for ( int32 chunkZ = 0; chunkZ < chunkCountZ && reader.isOk(); ++chunkZ )
                {
                    for ( int32 chunkX = 0; chunkX < chunkCountX && reader.isOk(); ++chunkX )
                    {
                        int32 savedX = chunkX;
                        int32 savedZ = chunkZ;
                        writer.value( savedX );
                        writer.value( savedZ );
                        uint32 length = 0;
                        do
                        {
                            uint16 paletteNumber = 0;
                            reader.value( paletteNumber );
                            reader.value( length );
                            writer.value( paletteNumber );
                            writer.value( length );
                        } while ( length != 0 && reader.isOk() );
                    }
                }
                return reader.isOk();
            }
        };
    } // namespace

    SW_STATE_FORMAT_UPGRADE( voxelWorld2To3, "VXWL", 2, 3, &VoxelWorldInternal::upgradeStateFrom2 );
} // namespace sw

namespace sw
{
    VoxelWorld::VoxelWorld()
        : _mapChunk{}
        , _mapStoredChunk{}
        , _generator{}
        , _pCatalog{ nullptr }
        , _land{}
        , _chunkCountX{ 0 }
        , _chunkCountZ{ 0 }
        , _lastRevision{ 0 }
    {
    }

    bool VoxelWorld::initialize( int32 chunkCountX, int32 chunkCountZ, const VoxelBlockCatalog* pCatalog )
    {
        if ( chunkCountX < 1 || chunkCountZ < 1 )
        {
            SW_LOG_ERROR_ID( "VoxelWorld.VoxelWorldRefusedChunkCounts", "VoxelWorld: refused chunk counts %# x %# - both must be at least 1", chunkCountX, chunkCountZ );
            return false;
        }
        _pCatalog    = pCatalog;
        _chunkCountX = chunkCountX;
        _chunkCountZ = chunkCountZ;
        _mapChunk.clear();
        _mapStoredChunk.clear();
        return true;
    }

    bool VoxelWorld::bindLand( LandRegistry* pLand, const int2& origin )
    {
        LandBinding land;
        land.bind( pLand, origin, hashed_string( "Voxel" ) );
        if ( land.claimRect( 0, 0, getSizeX() - 1, getSizeZ() - 1, true ) == false )
            return false;
        _land = land;
        return true;
    }

    bool VoxelWorld::fillGeneratedBlocks( int32 chunkX, int32 chunkZ, vector<VoxelBlockIndex>& outBlocks ) const
    {
        outBlocks.assign( static_cast<size_t>( kVoxelChunkVolume ), kVoxelAirBlock );
        if ( _generator.isBound() == false )
        {
            SW_LOG_ERROR_ONCE_ID( "VoxelWorld.NoChunkGenerator", "VoxelWorld: chunk (%#, %#) has no saved blocks and the world has no chunk generator", chunkX, chunkZ );
            return false;
        }
        _generator( *this, chunkX, chunkZ, outBlocks );
        return true;
    }

    bool VoxelWorld::loadChunk( int32 chunkX, int32 chunkZ )
    {
        if ( isChunkInside( chunkX, chunkZ ) == false )
            return false;
        const uint64 key = makeChunkKey( chunkX, chunkZ );
        if ( _mapChunk.contains( key ) )
            return true;
        VoxelChunk chunk;
        const auto storedIt = _mapStoredChunk.find( key );
        if ( storedIt != _mapStoredChunk.end() )
            VoxelWorldInternal::expandRuns( storedIt->second, chunk._listBlock ); // 저장본은 남겨 둔다 — 바뀌지 않은 채 내리면 다시 적지 않는다
        else if ( fillGeneratedBlocks( chunkX, chunkZ, chunk._listBlock ) == false )
            return false;
        chunk._revision = ++_lastRevision;
        _mapChunk[key]  = std::move( chunk );
        for ( int32 offsetZ = -VoxelWorldInternal::kNeighborReach; offsetZ <= VoxelWorldInternal::kNeighborReach; ++offsetZ )
        {
            for ( int32 offsetX = -VoxelWorldInternal::kNeighborReach; offsetX <= VoxelWorldInternal::kNeighborReach; ++offsetX )
            {
                if ( offsetX != 0 || offsetZ != 0 )
                    advanceChunkRevision( chunkX + offsetX, chunkZ + offsetZ ); // 이웃의 경계 면과 꼭짓점 그늘이 바뀐다
            }
        }
        return true;
    }

    void VoxelWorld::unloadChunk( int32 chunkX, int32 chunkZ )
    {
        const uint64 key     = makeChunkKey( chunkX, chunkZ );
        const auto   chunkIt = _mapChunk.find( key );
        if ( chunkIt == _mapChunk.end() )
            return;
        if ( chunkIt->second._bModified != SW_FALSE )
            VoxelWorldInternal::compressRuns( chunkIt->second._listBlock, _mapStoredChunk[key] );
        _mapChunk.erase( chunkIt );
        for ( int32 offsetZ = -VoxelWorldInternal::kNeighborReach; offsetZ <= VoxelWorldInternal::kNeighborReach; ++offsetZ )
        {
            for ( int32 offsetX = -VoxelWorldInternal::kNeighborReach; offsetX <= VoxelWorldInternal::kNeighborReach; ++offsetX )
            {
                if ( offsetX != 0 || offsetZ != 0 )
                    advanceChunkRevision( chunkX + offsetX, chunkZ + offsetZ );
            }
        }
    }

    uint32 VoxelWorld::updateResidency( int32 centerChunkX, int32 centerChunkZ, int32 radius, int32 maxLoads )
    {
        SW_PROFILE_SCOPE( "GT.VoxelWorld.updateResidency" );
        uint32 changedCount = 0;
        // 내리면 사전이 원소를 옮기므로 먼 청크의 키를 먼저 모은다. 멀어진 청크가 없는 프레임은 할당이 없다.
        vector<uint64> listFarKey;
        for ( const auto& entry : _mapChunk )
        {
            const int32 distance = MathUtil::max( MathUtil::abs( toChunkX( entry.first ) - centerChunkX ),
                                                  MathUtil::abs( toChunkZ( entry.first ) - centerChunkZ ) );
            if ( distance > radius )
                listFarKey.push_back( entry.first );
        }
        for ( const uint64 key : listFarKey )
        {
            unloadChunk( toChunkX( key ), toChunkZ( key ) );
            ++changedCount;
        }
        int32 loadedCount = 0;
        for ( int32 ring = 0; ring <= radius && loadedCount < maxLoads; ++ring )
        {
            for ( int32 chunkZ = centerChunkZ - ring; chunkZ <= centerChunkZ + ring && loadedCount < maxLoads; ++chunkZ )
            {
                for ( int32 chunkX = centerChunkX - ring; chunkX <= centerChunkX + ring && loadedCount < maxLoads; ++chunkX )
                {
                    const bool bOnRing = MathUtil::max( MathUtil::abs( chunkX - centerChunkX ), MathUtil::abs( chunkZ - centerChunkZ ) ) == ring;
                    if ( bOnRing == false || isChunkInside( chunkX, chunkZ ) == false || _mapChunk.contains( makeChunkKey( chunkX, chunkZ ) ) )
                        continue;
                    if ( loadChunk( chunkX, chunkZ ) == false )
                        continue;
                    ++loadedCount;
                    ++changedCount;
                }
            }
        }
        return changedCount;
    }

    bool VoxelWorld::isInside( int32 x, int32 y, int32 z ) const
    {
        return x >= 0 && y >= 0 && z >= 0 && x < getSizeX() && y < kVoxelChunkHeight && z < getSizeZ();
    }

    VoxelBlockIndex VoxelWorld::getBlock( int32 x, int32 y, int32 z ) const
    {
        if ( isInside( x, y, z ) == false )
            return kVoxelAirBlock;
        const VoxelChunk* pChunk = findChunk( x / kVoxelChunkSize, z / kVoxelChunkSize );
        if ( pChunk == nullptr )
            return kVoxelAirBlock;
        return pChunk->_listBlock[static_cast<size_t>( VoxelWorldInternal::computeLocalIndex( x % kVoxelChunkSize, y, z % kVoxelChunkSize ) )];
    }

    bool VoxelWorld::setBlock( int32 x, int32 y, int32 z, VoxelBlockIndex block )
    {
        if ( isInside( x, y, z ) == false )
            return false;
        const int32 chunkX = x / kVoxelChunkSize;
        const int32 chunkZ = z / kVoxelChunkSize;
        const int32 localX = x % kVoxelChunkSize;
        const int32 localZ = z % kVoxelChunkSize;
        VoxelChunk* pChunk = findChunkMutable( chunkX, chunkZ );
        if ( pChunk == nullptr )
            return false;
        VoxelBlockIndex& slot = pChunk->_listBlock[static_cast<size_t>( VoxelWorldInternal::computeLocalIndex( localX, y, localZ ) )];
        if ( slot == block )
            return true;
        slot               = block;
        pChunk->_bModified = SW_TRUE;
        advanceChunkRevision( chunkX, chunkZ );
        if ( localX == 0 )
            advanceChunkRevision( chunkX - 1, chunkZ );
        if ( localX == kVoxelChunkSize - 1 )
            advanceChunkRevision( chunkX + 1, chunkZ );
        if ( localZ == 0 )
            advanceChunkRevision( chunkX, chunkZ - 1 );
        if ( localZ == kVoxelChunkSize - 1 )
            advanceChunkRevision( chunkX, chunkZ + 1 );
        return true;
    }

    bool VoxelWorld::isSolid( int32 x, int32 y, int32 z ) const
    {
        if ( y < 0 )
            return true;
        return _pCatalog != nullptr && _pCatalog->isSolid( getBlock( x, y, z ) );
    }

    bool VoxelWorld::isOpaque( int32 x, int32 y, int32 z ) const
    {
        return _pCatalog != nullptr && _pCatalog->isOpaque( getBlock( x, y, z ) );
    }

    optional<int32> VoxelWorld::findTopSolidY( int32 x, int32 z ) const
    {
        for ( int32 y = kVoxelChunkHeight - 1; y >= 0; --y )
        {
            if ( isSolid( x, y, z ) )
                return y;
        }
        return nullopt;
    }

    void VoxelWorld::serializeState( StateIO& io )
    {
        io.header( kStateTag, kStateVersion ); // 판 1(머리표 없음 · 카탈로그 순서 번호)은 거절하고, 판 2 는 올림 단계(upgradeStateFrom2)가 옮긴다
        // 청크 수(월드 크기)는 설정이 정한다 — 저장된 수로 읽어 같은 청크 좌표에 옮긴다. 줄어 밖에 블록이 남은 청크가 있으면 거절한다(감사 A-6).
        int32 savedCountX = _chunkCountX;
        int32 savedCountZ = _chunkCountZ;
        io.value( savedCountX );
        io.value( savedCountZ );
        if ( io.check( 0 <= savedCountX && 0 <= savedCountZ ) == false )
            return;
        // 팔레트: 이 세이브의 번호 i(1 부터) → 블록 id. 마인크래프트 1.13+ 의 이름 팔레트와 같은 까닭으로, 카탈로그 순서가 바뀌어도 블록은 id 로 다시 짝지어진다.
        // 마인크래프트는 청크마다 쓰인 블록만 싣지만 여기는 세이브 하나에 카탈로그 전체를 한 번 싣는다 — 블록이 255 종 이하라 몇 KB 를 넘지 않는다.
        vector<hashed_string> listPaletteID;
        if ( io.isWriting() && _pCatalog != nullptr )
        {
            listPaletteID.reserve( _pCatalog->getBlocks().size() );
            for ( const VoxelBlockDef& block : _pCatalog->getBlocks() )
            {
                listPaletteID.push_back( block._id ); // 카탈로그 번호 = 목록 자리 + 1 이므로 쓸 때는 번호를 그대로 쓴다
            }
        }
        if ( io.isReading() && io.check( _pCatalog != nullptr ) == false )
            return;
        io.nameList( listPaletteID, [this]( const hashed_string& blockID )
        { return _pCatalog->findBlockIndex( blockID ).has_value(); } ); // 지금 카탈로그에 없는 블록은 공기로 바꾸지 않고 거절한다
        if ( io.isOk() == false )
            return;
        // 청크 하나는 적어도 좌표(4 + 4)와 닫는 구간(번호 2 + 길이 4)이다.
        constexpr uint32 kMinChunkBytes = sizeof( int32 ) * 2 + sizeof( uint16 ) + sizeof( uint32 );
        if ( io.isWriting() )
        {
            // 실을 청크: 저장본 + 올라와서 바뀐 청크. 좌표 순서로 써서 올라온 범위와 상관없이 같은 월드면 같은 바이트다.
            vector<uint64> listKey;
            listKey.reserve( _mapStoredChunk.size() + _mapChunk.size() );
            for ( const auto& entry : _mapStoredChunk )
            {
                listKey.push_back( entry.first );
            }
            for ( const auto& entry : _mapChunk )
            {
                if ( entry.second._bModified != SW_FALSE && _mapStoredChunk.contains( entry.first ) == false )
                    listKey.push_back( entry.first );
            }
            VectorUtil::sort( listKey );
            uint32 chunkCount = static_cast<uint32>( listKey.size() );
            io.count( kMinChunkBytes, chunkCount );
            vector<VoxelBlockRun> listScratchRun;
            for ( const uint64 key : listKey )
            {
                int32 chunkX = toChunkX( key );
                int32 chunkZ = toChunkZ( key );
                io.value( chunkX );
                io.value( chunkZ );
                const auto                   chunkIt = _mapChunk.find( key );
                const vector<VoxelBlockRun>* pListRun;
                if ( chunkIt != _mapChunk.end() && chunkIt->second._bModified != SW_FALSE )
                {
                    VoxelWorldInternal::compressRuns( chunkIt->second._listBlock, listScratchRun );
                    pListRun = &listScratchRun;
                }
                else
                {
                    pListRun = &_mapStoredChunk.find( key )->second;
                }
                for ( const VoxelBlockRun& run : *pListRun )
                {
                    VoxelWorldInternal::writeRun( io, run._block, run._count );
                }
                VoxelWorldInternal::writeRun( io, kVoxelAirBlock, 0 );
            }
            return;
        }
        // 세이브 번호 → 지금 카탈로그 번호. 0 은 공기다.
        vector<VoxelBlockIndex> listRemap;
        listRemap.reserve( listPaletteID.size() + 1 );
        listRemap.push_back( kVoxelAirBlock );
        for ( const hashed_string& blockID : listPaletteID )
        {
            listRemap.push_back( _pCatalog->findBlockIndex( blockID ).value() ); // 위 nameList 가 모르는 id 를 거절했다 — 없으면 멈춘다
        }
        uint32 chunkCount = 0;
        io.count( kMinChunkBytes, chunkCount ); // 남은 바이트로 담을 수 없는 청크 수를 거절한다
        if ( io.isOk() == false )
            return;
        unordered_map<uint64, vector<VoxelBlockRun>> mapStored; // 지역 사본에 읽는다 — 거절하면 상태는 그대로다
        for ( uint32 chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex )
        {
            int32 chunkX = 0;
            int32 chunkZ = 0;
            io.value( chunkX );
            io.value( chunkZ );
            if ( io.check( 0 <= chunkX && chunkX < savedCountX && 0 <= chunkZ && chunkZ < savedCountZ ) == false )
                return;
            const uint64 key = makeChunkKey( chunkX, chunkZ );
            if ( io.check( mapStored.contains( key ) == false ) == false )
                return; // 같은 청크가 두 번 실렸다
            vector<VoxelBlockRun> listRun;
            uint32                blockTotal = 0;
            while ( true )
            {
                uint16 paletteNumber = 0;
                uint32 length        = 0;
                io.value( paletteNumber );
                io.value( length );
                if ( io.check( static_cast<size_t>( paletteNumber ) < listRemap.size() ) == false )
                    return;
                if ( length == 0 )
                    break;
                if ( io.check( length <= static_cast<uint32>( kVoxelChunkVolume ) - blockTotal ) == false )
                    return;
                listRun.push_back( VoxelBlockRun{ listRemap[paletteNumber], static_cast<uint16>( length ) } );
                blockTotal += length;
            }
            if ( io.check( blockTotal == static_cast<uint32>( kVoxelChunkVolume ) ) == false )
                return;
            if ( isChunkInside( chunkX, chunkZ ) )
            {
                mapStored[key] = std::move( listRun );
                continue;
            }
            const bool bAllAir = VectorUtil::allOf( listRun, []( const VoxelBlockRun& run )
            { return run._block == kVoxelAirBlock; } );
            if ( io.check( bAllAir ) == false )
                return; // 줄어든 월드 밖에 블록이 남았다 — 버리지 않는다
        }
        _mapStoredChunk = std::move( mapStored );
        // 올라와 있던 청크는 읽은 월드로 다시 채운다. 실리지 않은 청크는 바뀌지 않은 청크라 생성 훅이 같은 블록을 낸다.
        for ( auto& entry : _mapChunk )
        {
            VoxelChunk& chunk    = entry.second;
            const auto  storedIt = _mapStoredChunk.find( entry.first );
            if ( storedIt != _mapStoredChunk.end() )
                VoxelWorldInternal::expandRuns( storedIt->second, chunk._listBlock );
            else
                (void)fillGeneratedBlocks( toChunkX( entry.first ), toChunkZ( entry.first ), chunk._listBlock ); // 훅이 없으면 오류를 남기고 공기다
            chunk._bModified = SW_FALSE;
            chunk._revision  = ++_lastRevision; // 이미 만든 메시가 오래된 것이 된다
        }
    }

    const VoxelChunk* VoxelWorld::findChunk( int32 chunkX, int32 chunkZ ) const
    {
        if ( isChunkInside( chunkX, chunkZ ) == false )
            return nullptr;
        const auto chunkIt = _mapChunk.find( makeChunkKey( chunkX, chunkZ ) );
        return chunkIt != _mapChunk.end() ? &chunkIt->second : nullptr;
    }

    uint32 VoxelWorld::countBlocks( VoxelBlockIndex block ) const
    {
        uint32 count = 0;
        for ( const auto& entry : _mapChunk )
        {
            for ( const VoxelBlockIndex value : entry.second._listBlock )
            {
                if ( value == block )
                    ++count;
            }
        }
        return count;
    }

    VoxelChunk* VoxelWorld::findChunkMutable( int32 chunkX, int32 chunkZ )
    {
        if ( isChunkInside( chunkX, chunkZ ) == false )
            return nullptr;
        const auto chunkIt = _mapChunk.find( makeChunkKey( chunkX, chunkZ ) );
        return chunkIt != _mapChunk.end() ? &chunkIt->second : nullptr;
    }

    void VoxelWorld::advanceChunkRevision( int32 chunkX, int32 chunkZ )
    {
        VoxelChunk* pChunk = findChunkMutable( chunkX, chunkZ );
        if ( pChunk != nullptr )
            pChunk->_revision = ++_lastRevision;
    }
} // namespace sw
