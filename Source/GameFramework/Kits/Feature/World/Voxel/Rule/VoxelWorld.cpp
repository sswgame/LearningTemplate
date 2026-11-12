#include "pch.h"

#include "GameFramework/Kits/Feature/World/Voxel/Rule/VoxelWorld.h"

#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

namespace sw
{
    namespace
    {
        /** @brief 청크 안 블록의 인덱스입니다. */
        constexpr int32 computeLocalIndex( int32 localX, int32 y, int32 localZ )
        {
            return ( y * kVoxelChunkSize + localZ ) * kVoxelChunkSize + localX;
        }
    } // namespace
} // namespace sw

namespace sw
{
    VoxelWorld::VoxelWorld()
        : _listChunk{}
        , _pCatalog{ nullptr }
        , _land{}
        , _chunkCountX{ 0 }
        , _chunkCountZ{ 0 }
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
        _listChunk.clear();
        _listChunk.resize( static_cast<size_t>( _chunkCountX * _chunkCountZ ) );
        for ( VoxelChunk& chunk : _listChunk )
        {
            chunk._listBlock.assign( static_cast<size_t>( kVoxelChunkVolume ), kVoxelAirBlock );
        }
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

    bool VoxelWorld::isInside( int32 x, int32 y, int32 z ) const
    {
        return x >= 0 && y >= 0 && z >= 0 && x < getSizeX() && y < kVoxelChunkHeight && z < getSizeZ();
    }

    VoxelBlockIndex VoxelWorld::getBlock( int32 x, int32 y, int32 z ) const
    {
        if ( isInside( x, y, z ) == false )
            return kVoxelAirBlock;
        const VoxelChunk& chunk = _listChunk[static_cast<size_t>( ( z / kVoxelChunkSize ) * _chunkCountX + x / kVoxelChunkSize )];
        return chunk._listBlock[static_cast<size_t>( computeLocalIndex( x % kVoxelChunkSize, y, z % kVoxelChunkSize ) )];
    }

    bool VoxelWorld::setBlock( int32 x, int32 y, int32 z, VoxelBlockIndex block )
    {
        if ( isInside( x, y, z ) == false )
            return false;
        const int32      chunkX = x / kVoxelChunkSize;
        const int32      chunkZ = z / kVoxelChunkSize;
        const int32      localX = x % kVoxelChunkSize;
        const int32      localZ = z % kVoxelChunkSize;
        VoxelChunk&      chunk  = _listChunk[static_cast<size_t>( chunkZ * _chunkCountX + chunkX )];
        VoxelBlockIndex& slot   = chunk._listBlock[static_cast<size_t>( computeLocalIndex( localX, y, localZ ) )];
        if ( slot == block )
            return true;
        slot = block;
        markChunkDirty( chunkX, chunkZ );
        if ( localX == 0 )
            markChunkDirty( chunkX - 1, chunkZ );
        if ( localX == kVoxelChunkSize - 1 )
            markChunkDirty( chunkX + 1, chunkZ );
        if ( localZ == 0 )
            markChunkDirty( chunkX, chunkZ - 1 );
        if ( localZ == kVoxelChunkSize - 1 )
            markChunkDirty( chunkX, chunkZ + 1 );
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

    bool VoxelWorld::isChunkDirty( int32 chunkX, int32 chunkZ ) const
    {
        const VoxelChunk* pChunk = findChunk( chunkX, chunkZ );
        return pChunk != nullptr && pChunk->_bDirty != SW_FALSE;
    }

    void VoxelWorld::clearChunkDirty( int32 chunkX, int32 chunkZ )
    {
        VoxelChunk* pChunk = findChunkMutable( chunkX, chunkZ );
        if ( pChunk != nullptr )
            pChunk->_bDirty = SW_FALSE;
    }

    void VoxelWorld::serializeState( StateIO& io )
    {
        io.header( kStateTag, kStateVersion ); // 판 1(머리표 없음 · 카탈로그 순서 번호)은 거절한다
        io.expect( _chunkCountX );
        io.expect( _chunkCountZ );
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
        { return _pCatalog->findBlockIndex( blockID ) != kVoxelAirBlock; } ); // 지금 카탈로그에 없는 블록은 공기로 바꾸지 않고 거절한다
        if ( io.isOk() == false )
            return;
        if ( io.isWriting() )
        {
            // 구간 수를 먼저 세지 않고 끝에 길이 0인 구간을 넣어 닫는다. 런 길이 부호는 방향마다 할 일이 달라 갈린다.
            for ( const VoxelChunk& chunk : _listChunk )
            {
                size_t index = 0;
                while ( index < chunk._listBlock.size() )
                {
                    const VoxelBlockIndex block = chunk._listBlock[index];
                    uint32                run   = 1;
                    while ( index + run < chunk._listBlock.size() && chunk._listBlock[index + run] == block )
                    {
                        ++run;
                    }
                    uint16 paletteNumber = block; // 팔레트 번호는 uint16 으로 적는다 — 런타임 번호 폭(VoxelBlockIndex)을 넓혀도 형식은 그대로다
                    io.value( paletteNumber );
                    io.value( run );
                    index += run;
                }
                uint16 closeNumber = 0;
                uint32 closeRun    = 0;
                io.value( closeNumber );
                io.value( closeRun );
            }
            return;
        }
        // 세이브 번호 → 지금 카탈로그 번호. 0 은 공기다.
        vector<VoxelBlockIndex> listRemap;
        listRemap.reserve( listPaletteID.size() + 1 );
        listRemap.push_back( kVoxelAirBlock );
        for ( const hashed_string& blockID : listPaletteID )
        {
            listRemap.push_back( _pCatalog->findBlockIndex( blockID ) );
        }
        vector<VoxelChunk> listChunk( _listChunk.size() ); // 사본 대신 새 목록에 읽는다 — 옛 청크는 어차피 버린다
        for ( VoxelChunk& chunk : listChunk )
        {
            chunk._listBlock.reserve( static_cast<size_t>( kVoxelChunkVolume ) );
            while ( true )
            {
                uint16 paletteNumber = 0;
                uint32 run           = 0;
                io.value( paletteNumber );
                io.value( run );
                if ( io.check( static_cast<size_t>( paletteNumber ) < listRemap.size() ) == false )
                    return;
                if ( run == 0 )
                    break;
                if ( io.check( chunk._listBlock.size() + run <= static_cast<size_t>( kVoxelChunkVolume ) ) == false )
                    return;
                chunk._listBlock.insert( chunk._listBlock.end(), run, listRemap[paletteNumber] );
            }
            if ( io.check( chunk._listBlock.size() == static_cast<size_t>( kVoxelChunkVolume ) ) == false )
                return;
        }
        for ( size_t chunkIndex = 0; chunkIndex < listChunk.size(); ++chunkIndex )
        {
            listChunk[chunkIndex]._revision = _listChunk[chunkIndex]._revision + 1; // 이미 만든 메시가 오래된 것이 된다
            listChunk[chunkIndex]._bDirty   = SW_TRUE;
        }
        _listChunk = std::move( listChunk );
    }

    void VoxelWorld::markAllChunksDirty()
    {
        for ( VoxelChunk& chunk : _listChunk )
        {
            chunk._bDirty = SW_TRUE;
            ++chunk._revision;
        }
    }

    const VoxelChunk* VoxelWorld::findChunk( int32 chunkX, int32 chunkZ ) const
    {
        const bool bChunkOutOfRange = chunkX < 0 || chunkZ < 0 || chunkX >= _chunkCountX || chunkZ >= _chunkCountZ;
        if ( bChunkOutOfRange )
            return nullptr;
        return &_listChunk[static_cast<size_t>( chunkZ * _chunkCountX + chunkX )];
    }

    uint32 VoxelWorld::countBlocks( VoxelBlockIndex block ) const
    {
        uint32 count = 0;
        for ( const VoxelChunk& chunk : _listChunk )
        {
            for ( const VoxelBlockIndex value : chunk._listBlock )
            {
                if ( value == block )
                    ++count;
            }
        }
        return count;
    }

    VoxelChunk* VoxelWorld::findChunkMutable( int32 chunkX, int32 chunkZ )
    {
        const bool bChunkOutOfRange = chunkX < 0 || chunkZ < 0 || chunkX >= _chunkCountX || chunkZ >= _chunkCountZ;
        if ( bChunkOutOfRange )
            return nullptr;
        return &_listChunk[static_cast<size_t>( chunkZ * _chunkCountX + chunkX )];
    }

    void VoxelWorld::markChunkDirty( int32 chunkX, int32 chunkZ )
    {
        VoxelChunk* pChunk = findChunkMutable( chunkX, chunkZ );
        if ( pChunk == nullptr )
            return;
        pChunk->_bDirty = SW_TRUE;
        ++pChunk->_revision;
    }
} // namespace sw
