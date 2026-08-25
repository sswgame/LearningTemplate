#include "pch.h"

#include "Engine/Graphics/RHI/FrameResourceRing.h"

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
		: _arrSlots{}
		, _frameIndex{ kFrameCount - 1 }
		, _uploadCapacity{ uploadCapacityBytes }
	{
	}

	void FrameResourceRing::reset( uint64 uploadCapacityBytes )
	{
		_uploadCapacity = uploadCapacityBytes;
		_frameIndex		= kFrameCount - 1;
		for ( Slot& slot : _arrSlots )
		{
			slot._fenceValue   = 0;
			slot._uploadOffset = 0;
		}
	}

	bool FrameResourceRing::beginFrame( uint64 completedFenceValue )
	{
		const uint32 nextIndex = ( _frameIndex + 1 ) % kFrameCount;
		if ( _arrSlots[nextIndex]._fenceValue > completedFenceValue )
			return false;

		_frameIndex							 = nextIndex;
		_arrSlots[_frameIndex]._uploadOffset = 0;
		return true;
	}

	void FrameResourceRing::advanceFrame()
	{
		_frameIndex							 = ( _frameIndex + 1 ) % kFrameCount;
		_arrSlots[_frameIndex]._uploadOffset = 0;
	}

	uint64 FrameResourceRing::getFenceValue( uint32 index ) const
	{
		if ( index >= kFrameCount )
			return 0;
		return _arrSlots[index]._fenceValue;
	}

	void FrameResourceRing::setFenceValue( uint32 index, uint64 fenceValue )
	{
		if ( index >= kFrameCount )
			return;
		_arrSlots[index]._fenceValue = fenceValue;
	}

	uint64& FrameResourceRing::currentFenceValue()
	{
		return _arrSlots[_frameIndex]._fenceValue;
	}

	bool FrameResourceRing::tryAllocate( uint64 sizeBytes, uint64 alignment, uint64& outOffset )
	{
		Slot&		 slot	= _arrSlots[_frameIndex];
		const uint64 offset = alignUp( slot._uploadOffset, alignment == 0 ? 1 : alignment );
		if ( offset > _uploadCapacity || sizeBytes > ( _uploadCapacity - offset ) )
			return false;
		outOffset		   = offset;
		slot._uploadOffset = offset + sizeBytes;
		return true;
	}

	void FrameResourceRing::resetUploadOffset()
	{
		_arrSlots[_frameIndex]._uploadOffset = 0;
	}
} // namespace sw
