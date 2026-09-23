#include "pch.h"

#include "Core/Memory/FrameArenaAllocator.h"

namespace sw
{
    SW_LOG_CALLER( "FrameArenaAllocator" );
    FrameArenaAllocator::FrameArenaAllocator( size_t defaultCapacity )
        : _defaultCapacity{ defaultCapacity }
        , _totalAllocatedBytes{ 0 }
        , _usedBytes{ 0 }
        , _currentChunkIndex{ 0 }
        , _listChunk{}
    {
        _listChunk.reserve( 8 );
        allocateNewChunk( _defaultCapacity );
    }

    FrameArenaAllocator::~FrameArenaAllocator()
    {
        for ( Chunk& chunk : _listChunk )
        {
            if ( chunk._pBuffer != nullptr )
            {
                Memory::free( chunk._pBuffer );
                chunk._pBuffer = nullptr;
            }
        }
        _listChunk.clear();
    }

    void* FrameArenaAllocator::allocateSlow( size_t size, size_t alignment )
    {
        while ( _currentChunkIndex < _listChunk.size() )
        {
            Chunk&          chunk   = _listChunk[_currentChunkIndex];
            const uintptr_t current = reinterpret_cast<uintptr_t>( chunk._pBuffer + chunk._offset );

            const uintptr_t aligned = MathUtil::align( current, static_cast<uintptr_t>( alignment ) );
            const size_t    padding = static_cast<size_t>( aligned - current );

            if ( fitsInChunk( chunk._capacity, chunk._offset, padding, size ) )
            {
                chunk._offset += padding + size;
                _usedBytes += padding + size;
                return reinterpret_cast<void*>( aligned );
            }

            _currentChunkIndex++;
        }

        // `size + alignment` 가 오버플로하면 **더 작은** 청크를 잡게 되고, 그 청크에도 들어가지 않으니 다시 여기로 와서 청크를
        // 끝없이 늘린다. 담을 수 없는 크기는 여기서 거른다.
        if ( size > SIZE_MAX - alignment )
            return nullptr;

        // 새 청크를 잡지 못하면 **여기서 끝낸다.** 예전에는 실패를 확인하지 않고 `_pBuffer` 가 nullptr 인 청크를 표에 넣었고,
        // 그다음 줄의 `allocate` 가 nullptr 에서 만든 주소를 정상 할당인 것처럼 돌려줬다(`nullptr + offset` 자체도 UB 다).
        if ( allocateNewChunk( size + alignment ) == false )
            return nullptr;

        _currentChunkIndex = _listChunk.size() - 1;
        return allocate( size, alignment );
    }

    void FrameArenaAllocator::reset()
    {
        for ( Chunk& chunk : _listChunk )
        {
            chunk._offset = 0;
        }
        _currentChunkIndex = 0;
        _usedBytes         = 0;
    }

    FrameArenaAllocator::Marker FrameArenaAllocator::createMarker() const
    {
        Marker marker;
        marker._chunkIndex = _currentChunkIndex;
        marker._offset     = ( _currentChunkIndex < _listChunk.size() ) ? _listChunk[_currentChunkIndex]._offset : 0;
        marker._usedBytes  = _usedBytes;
        return marker;
    }

    void FrameArenaAllocator::rollbackToMarker( const Marker& marker )
    {
        if ( marker._chunkIndex < _listChunk.size() )
        {
            _currentChunkIndex                     = marker._chunkIndex;
            _listChunk[_currentChunkIndex]._offset = marker._offset;

            for ( size_t chunkIndex = _currentChunkIndex + 1; chunkIndex < _listChunk.size(); ++chunkIndex )
            {
                _listChunk[chunkIndex]._offset = 0;
            }
            _usedBytes = marker._usedBytes;
        }
    }

    bool FrameArenaAllocator::allocateNewChunk( size_t minSize )
    {
        size_t chunkSize = MathUtil::max( _defaultCapacity, minSize );
        uint8* pBuf      = static_cast<uint8*>( Memory::allocate( chunkSize ) );
        if ( pBuf == nullptr )
        {
            SW_LOG_ERROR( "Failed to allocate a %# byte frame arena chunk.", static_cast<uint64>( chunkSize ) );
            return false;
        }

        _listChunk.push_back( Chunk{ pBuf, chunkSize, 0 } );
        _totalAllocatedBytes += chunkSize;
        return true;
    }
} // namespace sw
