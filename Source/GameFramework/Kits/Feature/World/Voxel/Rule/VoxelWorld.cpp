#include "pch.h"

#include "GameFramework/Kits/Feature/World/Voxel/Rule/VoxelWorld.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

namespace sw
{
    namespace
    {
        struct VoxelWorldInternal
        {
            /** @brief 청크 안 블록 자리입니다. */
            static constexpr int32 computeLocalIndex( int32 localX, int32 y, int32 localZ )
            {
                return ( y * kVoxelChunkSize + localZ ) * kVoxelChunkSize + localX;
            }
        };
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

    void VoxelWorld::initialize( int32 chunkCountX, int32 chunkCountZ, const VoxelBlockCatalog* pCatalog )
    {
        _pCatalog    = pCatalog;
        _chunkCountX = MathUtil::max( 1, chunkCountX );
        _chunkCountZ = MathUtil::max( 1, chunkCountZ );
        _listChunk.clear();
        _listChunk.resize( static_cast<size_t>( _chunkCountX * _chunkCountZ ) );
        for ( VoxelChunk& chunk : _listChunk )
        {
            chunk._listBlock.assign( static_cast<size_t>( kVoxelChunkVolume ), kVoxelAirBlock );
        }
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
        return chunk._listBlock[static_cast<size_t>( VoxelWorldInternal::computeLocalIndex( x % kVoxelChunkSize, y, z % kVoxelChunkSize ) )];
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
        VoxelBlockIndex& slot   = chunk._listBlock[static_cast<size_t>( VoxelWorldInternal::computeLocalIndex( localX, y, localZ ) )];
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

    int32 VoxelWorld::findTopSolidY( int32 x, int32 z ) const
    {
        for ( int32 y = kVoxelChunkHeight - 1; y >= 0; --y )
        {
            if ( isSolid( x, y, z ) )
                return y;
        }
        return -1;
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

    void VoxelWorld::writeState( Archive& outArchive ) const
    {
        outArchive << _chunkCountX;
        outArchive << _chunkCountZ;
        for ( const VoxelChunk& chunk : _listChunk )
        {
            // 구간 수를 먼저 세지 않고 끝에 0 길이 구간으로 닫는다.
            size_t index = 0;
            while ( index < chunk._listBlock.size() )
            {
                const VoxelBlockIndex block = chunk._listBlock[index];
                uint32                run   = 1;
                while ( index + run < chunk._listBlock.size() && chunk._listBlock[index + run] == block )
                {
                    ++run;
                }
                outArchive << block;
                outArchive << run;
                index += run;
            }
            outArchive << kVoxelAirBlock;
            outArchive << uint32( 0 );
        }
    }

    bool VoxelWorld::readState( Archive& archive )
    {
        int32 chunkCountX = 0;
        int32 chunkCountZ = 0;
        archive >> chunkCountX;
        archive >> chunkCountZ;
        if ( archive.isError() || chunkCountX != _chunkCountX || chunkCountZ != _chunkCountZ || _pCatalog == nullptr )
            return false;
        const size_t       blockLimit = _pCatalog->getBlocks().size(); // 0 은 공기, 1 부터 카탈로그 순
        vector<VoxelChunk> listChunk( _listChunk.size() );
        for ( VoxelChunk& chunk : listChunk )
        {
            chunk._listBlock.reserve( static_cast<size_t>( kVoxelChunkVolume ) );
            while ( true )
            {
                VoxelBlockIndex block = kVoxelAirBlock;
                uint32          run   = 0;
                archive >> block;
                archive >> run;
                if ( archive.isError() || static_cast<size_t>( block ) > blockLimit )
                    return false;
                if ( run == 0 )
                    break;
                if ( chunk._listBlock.size() + run > static_cast<size_t>( kVoxelChunkVolume ) )
                    return false;
                chunk._listBlock.insert( chunk._listBlock.end(), run, block );
            }
            if ( chunk._listBlock.size() != static_cast<size_t>( kVoxelChunkVolume ) )
                return false;
        }
        for ( size_t chunkIndex = 0; chunkIndex < listChunk.size(); ++chunkIndex )
        {
            listChunk[chunkIndex]._revision = _listChunk[chunkIndex]._revision + 1; // 지은 메시가 낡았다
            listChunk[chunkIndex]._bDirty   = SW_TRUE;
        }
        _listChunk = std::move( listChunk );
        return true;
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
        if ( chunkX < 0 || chunkZ < 0 || chunkX >= _chunkCountX || chunkZ >= _chunkCountZ )
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
        if ( chunkX < 0 || chunkZ < 0 || chunkX >= _chunkCountX || chunkZ >= _chunkCountZ )
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
