#include "pch.h"

#include "Engine/Input/InputSnapshot.h"

#include <cstring>

namespace sw
{
	uint32 InputSnapshot::serialize( uint8* pOutBuffer, uint32 bufferSize ) const
	{
		if ( pOutBuffer == nullptr || bufferSize < sizeof( InputSnapshot ) )
			return 0;

		std::memcpy( pOutBuffer, this, sizeof( InputSnapshot ) );
		return static_cast<uint32>( sizeof( InputSnapshot ) );
	}

	bool InputSnapshot::deserialize( const uint8* pBuffer, uint32 bufferSize )
	{
		if ( pBuffer == nullptr || bufferSize < sizeof( InputSnapshot ) )
			return false;

		std::memcpy( this, pBuffer, sizeof( InputSnapshot ) );
		return true;
	}

	InputHistoryBuffer::InputHistoryBuffer()
		: _arrHistory{}
		, _writeIndex{ 0 }
		, _count{ 0 }
		, _latestTick{ 0 }
	{
	}

	void InputHistoryBuffer::recordSnapshot( const InputSnapshot& snapshot )
	{
		_arrHistory[_writeIndex] = snapshot;
		_latestTick				 = snapshot._tickNumber;
		_writeIndex				 = ( _writeIndex + 1 ) % kDefaultCapacity;
		if ( _count < kDefaultCapacity )
			++_count;
	}

	const InputSnapshot* InputHistoryBuffer::getSnapshot( uint32 tickNumber ) const
	{
		if ( _count == 0 )
			return nullptr;

		for ( size_t index = 0; index < _count; ++index )
		{
			if ( _arrHistory[index]._tickNumber == tickNumber )
				return &_arrHistory[index];
		}
		return nullptr;
	}

	const InputSnapshot* InputHistoryBuffer::getLatestSnapshot() const
	{
		if ( _count == 0 )
			return nullptr;

		const size_t lastIndex = ( _writeIndex + kDefaultCapacity - 1 ) % kDefaultCapacity;
		return &_arrHistory[lastIndex];
	}

	void InputHistoryBuffer::clear()
	{
		_writeIndex = 0;
		_count		= 0;
		_latestTick = 0;
	}
} // namespace sw
