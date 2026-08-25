#include "pch.h"

#include "Core/Memory/FrameArenaAllocator.h"

namespace sw
{
	FrameArenaAllocator::FrameArenaAllocator( size_t defaultCapacity )
		: _defaultCapacity{ defaultCapacity }
		, _totalAllocatedBytes{ 0 }
		, _usedBytes{ 0 }
		, _currentChunkIndex{ 0 }
		, _listChunks{}
	{
		_listChunks.reserve( 8 );
		allocateNewChunk( _defaultCapacity );
	}

	FrameArenaAllocator::~FrameArenaAllocator()
	{
		for ( Chunk& chunk : _listChunks )
		{
			if ( chunk._pBuffer != nullptr )
			{
				Memory::freeMemory( chunk._pBuffer );
				chunk._pBuffer = nullptr;
			}
		}
		_listChunks.clear();
	}

	void* FrameArenaAllocator::allocate( size_t size, size_t alignment )
	{
		if ( size == 0 )
			return nullptr;

		if ( alignment == 0 || ( alignment & ( alignment - 1 ) ) != 0 )
			alignment = alignof( std::max_align_t );

		while ( _currentChunkIndex < _listChunks.size() )
		{
			Chunk&	  chunk	  = _listChunks[_currentChunkIndex];
			uintptr_t current = reinterpret_cast<uintptr_t>( chunk._pBuffer + chunk._offset );

			uintptr_t aligned = ( current + ( alignment - 1 ) ) & ~( static_cast<uintptr_t>( alignment - 1 ) );
			size_t	  padding = aligned - current;

			if ( chunk._offset + padding + size <= chunk._capacity )
			{
				chunk._offset += padding + size;
				_usedBytes += padding + size;
				return reinterpret_cast<void*>( aligned );
			}

			_currentChunkIndex++;
		}

		allocateNewChunk( size + alignment );
		_currentChunkIndex = _listChunks.size() - 1;
		return allocate( size, alignment );
	}

	void FrameArenaAllocator::reset()
	{
		for ( Chunk& chunk : _listChunks )
		{
			chunk._offset = 0;
		}
		_currentChunkIndex = 0;
		_usedBytes		   = 0;
	}

	FrameArenaAllocator::Marker FrameArenaAllocator::createMarker() const
	{
		Marker marker;
		marker._chunkIndex = _currentChunkIndex;
		marker._offset	   = _listChunks.empty() == false ? _listChunks[_currentChunkIndex]._offset : 0;
		marker._usedBytes  = _usedBytes;
		return marker;
	}

	void FrameArenaAllocator::rollbackToMarker( const Marker& marker )
	{
		if ( marker._chunkIndex < _listChunks.size() )
		{
			_currentChunkIndex						= marker._chunkIndex;
			_listChunks[_currentChunkIndex]._offset = marker._offset;

			for ( size_t chunkIndex = _currentChunkIndex + 1; chunkIndex < _listChunks.size(); ++chunkIndex )
			{
				_listChunks[chunkIndex]._offset = 0;
			}
			_usedBytes = marker._usedBytes;
		}
	}

	void FrameArenaAllocator::allocateNewChunk( size_t minSize )
	{
		size_t chunkSize = MathUtil::max( _defaultCapacity, minSize );
		uint8* pBuf		 = static_cast<uint8*>( Memory::allocMemory( chunkSize ) );
		_listChunks.push_back( Chunk{ pBuf, chunkSize, 0 } );
		_totalAllocatedBytes += chunkSize;
	}

	FrameArenaAllocator& getThreadLocalFrameArena()
	{
		thread_local FrameArenaAllocator t_frameArena( 64 * 1024 );
		return t_frameArena;
	}
} // namespace sw
