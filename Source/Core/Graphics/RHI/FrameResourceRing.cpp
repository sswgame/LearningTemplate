/**
 * @file FrameResourceRing.cpp
 * @brief Frame ring fence / upload offset helpers.
 */
#include "FrameResourceRing.h"

namespace sw
{
	namespace
	{
		uint64 alignUp( uint64 value, uint64 alignment )
		{
			if ( alignment <= 1 )
				return value;
			return ( value + ( alignment - 1 ) ) & ~( alignment - 1 );
		}
	} // namespace

	FrameResourceRing::FrameResourceRing()
		: FrameResourceRing( 0 )
	{
	}

	FrameResourceRing::FrameResourceRing( uint64 uploadCapacityBytes )
	{
		reset( uploadCapacityBytes );
	}

	void FrameResourceRing::reset( uint64 uploadCapacityBytes )
	{
		_uploadCapacity = uploadCapacityBytes;
		_frameIndex		= 0;
		for ( Slot& slot : _slots )
		{
			slot._fenceValue   = 0;
			slot._uploadOffset = 0;
		}
	}

	bool FrameResourceRing::beginFrame( uint64 completedFenceValue )
	{
		const uint32 nextIndex = ( _frameIndex + 1 ) % kFrameCount;
		if ( _slots[nextIndex]._fenceValue > completedFenceValue )
			return false;

		_frameIndex						 = nextIndex;
		_slots[_frameIndex]._uploadOffset = 0;
		return true;
	}

	void FrameResourceRing::advanceFrame()
	{
		_frameIndex						 = ( _frameIndex + 1 ) % kFrameCount;
		_slots[_frameIndex]._uploadOffset = 0;
	}

	uint64 FrameResourceRing::getFenceValue( uint32 index ) const
	{
		if ( index >= kFrameCount )
			return 0;
		return _slots[index]._fenceValue;
	}

	void FrameResourceRing::setFenceValue( uint32 index, uint64 fenceValue )
	{
		if ( index >= kFrameCount )
			return;
		_slots[index]._fenceValue = fenceValue;
	}

	uint64& FrameResourceRing::currentFenceValue()
	{
		return _slots[_frameIndex]._fenceValue;
	}

	bool FrameResourceRing::tryAllocate( uint64 sizeBytes, uint64 alignment, uint64& outOffset )
	{
		Slot&		 slot   = _slots[_frameIndex];
		const uint64 offset = alignUp( slot._uploadOffset, alignment == 0 ? 1 : alignment );
		if ( offset > _uploadCapacity || sizeBytes > ( _uploadCapacity - offset ) )
			return false;
		outOffset			= offset;
		slot._uploadOffset	= offset + sizeBytes;
		return true;
	}

	void FrameResourceRing::resetUploadOffset()
	{
		_slots[_frameIndex]._uploadOffset = 0;
	}
} // namespace sw
