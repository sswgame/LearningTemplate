/**
 * @file FrameArenaAllocator.cpp
 * @brief Auto-generated documentation header
 */
#include "Core/CoreMinimal.h"

#include "FrameArenaAllocator.h"
#include <cstdlib>

namespace sw
{
	FrameArenaAllocator::FrameArenaAllocator( size_t defaultCapacity )
		: _defaultCapacity( defaultCapacity )
	{
		_chunks.reserve( 8 );
		allocateNewChunk( _defaultCapacity );
	}

	FrameArenaAllocator::~FrameArenaAllocator()
	{
		for ( Chunk& chunk : _chunks )
		{
			if ( chunk._buffer != nullptr )
			{
				std::free( chunk._buffer );
				chunk._buffer = nullptr;
			}
		}
		_chunks.clear();
	}

	void FrameArenaAllocator::allocateNewChunk( size_t minSize )
	{
		size_t chunkSize = std::max( _defaultCapacity, minSize );
		uint8* buf		 = static_cast<uint8*>( std::malloc( chunkSize ) );
		_chunks.push_back( Chunk{ buf, chunkSize, 0 } );
		_totalAllocatedBytes += chunkSize;
	}

	void* FrameArenaAllocator::allocate( size_t size, size_t alignment )
	{
		if ( size == 0 )
		{
			return nullptr;
		}

		if ( alignment == 0 || ( alignment & ( alignment - 1 ) ) != 0 )
		{
			alignment = alignof( std::max_align_t );
		}

		while ( _currentChunkIndex < _chunks.size() )
		{
			Chunk&	  chunk	  = _chunks[_currentChunkIndex];
			uintptr_t current = reinterpret_cast<uintptr_t>( chunk._buffer + chunk._offset );

			uintptr_t aligned = ( current + ( alignment - 1 ) ) & ~( alignment - 1 );
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
		_currentChunkIndex = _chunks.size() - 1;
		return allocate( size, alignment );
	}

	void FrameArenaAllocator::reset()
	{
		for ( Chunk& chunk : _chunks )
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
		marker._offset	   = _chunks.empty() == false ? _chunks[_currentChunkIndex]._offset : 0;
		marker._usedBytes  = _usedBytes;
		return marker;
	}

	void FrameArenaAllocator::rollbackToMarker( const Marker& marker )
	{
		if ( marker._chunkIndex < _chunks.size() )
		{
			_currentChunkIndex					= marker._chunkIndex;
			_chunks[_currentChunkIndex]._offset = marker._offset;

			for ( size_t i = _currentChunkIndex + 1; i < _chunks.size(); ++i )
			{
				_chunks[i]._offset = 0;
			}
			_usedBytes = marker._usedBytes;
		}
	}

	FrameArenaAllocator& getThreadLocalFrameArena()
	{
		thread_local FrameArenaAllocator threadLocalArena( 64 * 1024 );
		return threadLocalArena;
	}
}
