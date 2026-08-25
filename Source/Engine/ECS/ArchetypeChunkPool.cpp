#include "pch.h"

#include "Engine/ECS/ArchetypeChunkPool.h"

namespace sw
{
	ArchetypeChunk::ArchetypeChunk( const vector<ComponentColumnLayout>& listLayouts )
		: _listLayouts{ listLayouts }
		, _listColumnOffsets{}
		, _listEntityIds{}
		, _pChunkMemory{ nullptr }
		, _chunkMemorySize{ 0 }
		, _count{ 0 }
	{
		_listEntityIds.assign( kChunkCapacity, 0 );
		size_t currentOffset = 0;
		for ( const ComponentColumnLayout& layout : _listLayouts )
		{
			// 정렬 맞춤 (0-정렬 방어)
			const size_t alignment = MathUtil::max( layout._alignment, size_t{ 1 } );
			const size_t alignMask = alignment - 1;
			currentOffset		   = ( currentOffset + alignMask ) & ~alignMask;

			_listColumnOffsets.push_back( currentOffset );
			currentOffset += layout._elementSize * kChunkCapacity;
		}

		_chunkMemorySize = currentOffset;
		if ( _chunkMemorySize > 0 )
		{
			_pChunkMemory = static_cast<uint8*>( Memory::alignedAlloc( _chunkMemorySize, 64 ) );
			if ( _pChunkMemory != nullptr )
				Memory::set( _pChunkMemory, 0, _chunkMemorySize );
		}
	}

	ArchetypeChunk::~ArchetypeChunk()
	{
		if ( _pChunkMemory != nullptr )
		{
			for ( size_t colIndex = 0; colIndex < _listLayouts.size(); ++colIndex )
			{
				const auto& layout = _listLayouts[colIndex];
				if ( layout._destructFn != nullptr )
				{
					uint8* const pColBase = _pChunkMemory + _listColumnOffsets[colIndex];
					for ( size_t rowIndex = 0; rowIndex < _count; ++rowIndex )
					{
						layout._destructFn( pColBase + ( rowIndex * layout._elementSize ) );
					}
				}
			}

			Memory::alignedFree( _pChunkMemory );
			_pChunkMemory = nullptr;
		}
	}

	ArchetypeChunk::ArchetypeChunk( ArchetypeChunk&& rhs ) noexcept
		: _listLayouts{ std::move( rhs._listLayouts ) }
		, _listColumnOffsets{ std::move( rhs._listColumnOffsets ) }
		, _listEntityIds{ std::move( rhs._listEntityIds ) }
		, _pChunkMemory{ rhs._pChunkMemory }
		, _chunkMemorySize{ rhs._chunkMemorySize }
		, _count{ rhs._count }
	{
		rhs._pChunkMemory	 = nullptr;
		rhs._chunkMemorySize = 0;
		rhs._count			 = 0;
	}

	ArchetypeChunk& ArchetypeChunk::operator=( ArchetypeChunk&& rhs ) noexcept
	{
		if ( this != &rhs )
		{
			if ( _pChunkMemory != nullptr )
			{
				for ( size_t colIndex = 0; colIndex < _listLayouts.size(); ++colIndex )
				{
					const auto& layout = _listLayouts[colIndex];
					if ( layout._destructFn != nullptr )
					{
						uint8* const pColBase = _pChunkMemory + _listColumnOffsets[colIndex];
						for ( size_t rowIndex = 0; rowIndex < _count; ++rowIndex )
						{
							layout._destructFn( pColBase + ( rowIndex * layout._elementSize ) );
						}
					}
				}
				Memory::alignedFree( _pChunkMemory );
			}

			_listLayouts	   = std::move( rhs._listLayouts );
			_listColumnOffsets = std::move( rhs._listColumnOffsets );
			_listEntityIds	   = std::move( rhs._listEntityIds );
			_pChunkMemory	   = rhs._pChunkMemory;
			_chunkMemorySize   = rhs._chunkMemorySize;
			_count			   = rhs._count;

			rhs._pChunkMemory	 = nullptr;
			rhs._chunkMemorySize = 0;
			rhs._count			 = 0;
		}
		return *this;
	}

	size_t ArchetypeChunk::allocateRow( uint64 entityId )
	{
		if ( _count >= kChunkCapacity )
			return static_cast<size_t>( -1 );

		const size_t assignedRow	= _count++;
		_listEntityIds[assignedRow] = entityId;
		return assignedRow;
	}

	bool ArchetypeChunk::freeRow( size_t rowIndex )
	{
		if ( rowIndex >= _count || _count == 0 )
			return false;

		const size_t lastIndex = _count - 1;
		if ( rowIndex != lastIndex )
		{
			// Swap with last row
			_listEntityIds[rowIndex] = _listEntityIds[lastIndex];

			for ( size_t colIndex = 0; colIndex < _listLayouts.size(); ++colIndex )
			{
				const auto&	 layout	  = _listLayouts[colIndex];
				const size_t elemSize = layout._elementSize;
				uint8* const pColBase = _pChunkMemory + _listColumnOffsets[colIndex];
				uint8* const pDst	  = pColBase + ( rowIndex * elemSize );
				uint8* const pSrc	  = pColBase + ( lastIndex * elemSize );

				if ( layout._moveAssignFn != nullptr )
					layout._moveAssignFn( pDst, pSrc );
				else
					Memory::copy( pDst, pSrc, elemSize );

				if ( layout._destructFn != nullptr )
					layout._destructFn( pSrc );
			}
		}
		else
		{
			// Last row removed directly
			for ( size_t colIndex = 0; colIndex < _listLayouts.size(); ++colIndex )
			{
				const auto&	 layout	  = _listLayouts[colIndex];
				const size_t elemSize = layout._elementSize;
				uint8* const pColBase = _pChunkMemory + _listColumnOffsets[colIndex];
				uint8* const pTarget  = pColBase + ( lastIndex * elemSize );

				if ( layout._destructFn != nullptr )
					layout._destructFn( pTarget );
			}
		}

		_listEntityIds[lastIndex] = 0;
		--_count;
		return true;
	}

	uint64 ArchetypeChunk::getEntityId( size_t rowIndex ) const
	{
		if ( rowIndex < _count )
			return _listEntityIds[rowIndex];
		return 0;
	}

	void* ArchetypeChunk::getComponentColumn( size_t columnIndex )
	{
		if ( columnIndex < _listColumnOffsets.size() && _pChunkMemory != nullptr )
			return _pChunkMemory + _listColumnOffsets[columnIndex];
		return nullptr;
	}

	const void* ArchetypeChunk::getComponentColumn( size_t columnIndex ) const
	{
		if ( columnIndex < _listColumnOffsets.size() && _pChunkMemory != nullptr )
			return _pChunkMemory + _listColumnOffsets[columnIndex];
		return nullptr;
	}

	void* ArchetypeChunk::getComponent( size_t columnIndex, size_t rowIndex )
	{
		if ( columnIndex >= _listLayouts.size() || rowIndex >= _count || _pChunkMemory == nullptr )
			return nullptr;

		const size_t elemSize = _listLayouts[columnIndex]._elementSize;
		return _pChunkMemory + _listColumnOffsets[columnIndex] + ( rowIndex * elemSize );
	}

	const void* ArchetypeChunk::getComponent( size_t columnIndex, size_t rowIndex ) const
	{
		if ( columnIndex >= _listLayouts.size() || rowIndex >= _count || _pChunkMemory == nullptr )
			return nullptr;

		const size_t elemSize = _listLayouts[columnIndex]._elementSize;
		return _pChunkMemory + _listColumnOffsets[columnIndex] + ( rowIndex * elemSize );
	}

	ArchetypeChunkPool::ArchetypeChunkPool( const vector<ComponentColumnLayout>& listLayouts )
		: _listLayouts{ listLayouts }
		, _listChunks{}
		, _totalEntities{ 0 }
	{
	}

	uint64 ArchetypeChunkPool::allocateEntity( uint64 entityId, size_t& outChunkIndex, size_t& outRowIndex )
	{
		for ( size_t chunkIndex = 0; chunkIndex < _listChunks.size(); ++chunkIndex )
		{
			if ( _listChunks[chunkIndex]->isFull() == false )
			{
				outChunkIndex = chunkIndex;
				outRowIndex	  = _listChunks[chunkIndex]->allocateRow( entityId );
				++_totalEntities;
				return entityId;
			}
		}

		// 새 청크 할당
		auto newChunk = sw::make_unique<ArchetypeChunk>( _listLayouts );
		outChunkIndex = _listChunks.size();
		outRowIndex	  = newChunk->allocateRow( entityId );
		_listChunks.push_back( std::move( newChunk ) );
		++_totalEntities;

		return entityId;
	}

	bool ArchetypeChunkPool::freeEntity( size_t chunkIndex, size_t rowIndex )
	{
		if ( chunkIndex >= _listChunks.size() )
			return false;

		const bool bOk = _listChunks[chunkIndex]->freeRow( rowIndex );
		if ( bOk && _totalEntities > 0 )
			--_totalEntities;

		return bOk;
	}

	ArchetypeChunk* ArchetypeChunkPool::getChunk( size_t chunkIndex )
	{
		if ( chunkIndex < _listChunks.size() )
			return _listChunks[chunkIndex].get();
		return nullptr;
	}

	const ArchetypeChunk* ArchetypeChunkPool::getChunk( size_t chunkIndex ) const
	{
		if ( chunkIndex < _listChunks.size() )
			return _listChunks[chunkIndex].get();
		return nullptr;
	}
} // namespace sw
