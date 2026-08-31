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
				Memory::freeMemory( chunk._pBuffer );
				chunk._pBuffer = nullptr;
			}
		}
		_listChunk.clear();
	}

	void* FrameArenaAllocator::allocateSlow( size_t size, size_t alignment )
	{
		while ( _currentChunkIndex < _listChunk.size() )
		{
			Chunk&			chunk	= _listChunk[_currentChunkIndex];
			const uintptr_t current = reinterpret_cast<uintptr_t>( chunk._pBuffer + chunk._offset );

			const uintptr_t aligned = MathUtil::align( current, static_cast<uintptr_t>( alignment ) );
			const size_t	padding = static_cast<size_t>( aligned - current );

			if ( chunk._offset + padding + size <= chunk._capacity )
			{
				chunk._offset += padding + size;
				_usedBytes += padding + size;
				return reinterpret_cast<void*>( aligned );
			}

			_currentChunkIndex++;
		}

		allocateNewChunk( size + alignment );
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
		_usedBytes		   = 0;
	}

	FrameArenaAllocator::Marker FrameArenaAllocator::createMarker() const
	{
		Marker marker;
		marker._chunkIndex = _currentChunkIndex;
		marker._offset	   = ( _currentChunkIndex < _listChunk.size() ) ? _listChunk[_currentChunkIndex]._offset : 0;
		marker._usedBytes  = _usedBytes;
		return marker;
	}

	void FrameArenaAllocator::rollbackToMarker( const Marker& marker )
	{
		if ( marker._chunkIndex < _listChunk.size() )
		{
			_currentChunkIndex					   = marker._chunkIndex;
			_listChunk[_currentChunkIndex]._offset = marker._offset;

			for ( size_t chunkIndex = _currentChunkIndex + 1; chunkIndex < _listChunk.size(); ++chunkIndex )
			{
				_listChunk[chunkIndex]._offset = 0;
			}
			_usedBytes = marker._usedBytes;
		}
	}

	void FrameArenaAllocator::allocateNewChunk( size_t minSize )
	{
		size_t chunkSize = MathUtil::max( _defaultCapacity, minSize );
		uint8* pBuf		 = static_cast<uint8*>( Memory::allocMemory( chunkSize ) );
		_listChunk.push_back( Chunk{ pBuf, chunkSize, 0 } );
		_totalAllocatedBytes += chunkSize;
	}

	FrameArenaAllocator& FrameArenaAllocator::getThreadLocal()
	{
		thread_local FrameArenaAllocator t_frameArena( 64 * 1024 );
		return t_frameArena;
	}

	namespace
	{

		FrameDoubleBuffer* s_pFrameDoubleBuffer{ nullptr };

	} // namespace

	void FrameDoubleBuffer::bind( FrameDoubleBuffer* pBuffer )
	{
		s_pFrameDoubleBuffer = pBuffer;
	}

	FrameDoubleBuffer& FrameDoubleBuffer::get()
	{
		SW_LOG_ASSERT( s_pFrameDoubleBuffer != nullptr, "FrameDoubleBuffer is not bound" );
		return *s_pFrameDoubleBuffer;
	}
} // namespace sw
