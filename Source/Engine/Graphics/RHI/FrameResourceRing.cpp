#include "pch.h"

#include "Engine/Graphics/RHI/FrameResourceRing.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    FrameResourceRing::FrameResourceRing()
        : FrameResourceRing( 0 )
    {
    }

    FrameResourceRing::FrameResourceRing( uint64 uploadCapacityBytes )
        : _arrSlot{}
        , _uploadCapacity{ uploadCapacityBytes }
        , _frameIndex{ constant::kMaxFrameCountInFlight - 1 }
    {
    }

    void FrameResourceRing::reset( uint64 uploadCapacityBytes )
    {
        _uploadCapacity = uploadCapacityBytes;
        _frameIndex     = constant::kMaxFrameCountInFlight - 1;
        for ( Slot& slot : _arrSlot )
        {
            slot._fenceValue   = 0;
            slot._uploadOffset = 0;
        }
    }

    bool FrameResourceRing::beginFrame( uint64 completedFenceValue )
    {
        const uint32 nextIndex = ( _frameIndex + 1 ) % constant::kMaxFrameCountInFlight;
        if ( _arrSlot[nextIndex]._fenceValue > completedFenceValue )
            return false;

        _frameIndex                         = nextIndex;
        _arrSlot[_frameIndex]._uploadOffset = 0;
        return true;
    }

    void FrameResourceRing::advanceFrame()
    {
        _frameIndex                         = ( _frameIndex + 1 ) % constant::kMaxFrameCountInFlight;
        _arrSlot[_frameIndex]._uploadOffset = 0;
    }

    uint64 FrameResourceRing::getFenceValue( uint32 index ) const
    {
        if ( index >= constant::kMaxFrameCountInFlight )
            return 0;
        return _arrSlot[index]._fenceValue;
    }

    void FrameResourceRing::setFenceValue( uint32 index, uint64 fenceValue )
    {
        if ( index >= constant::kMaxFrameCountInFlight )
            return;
        _arrSlot[index]._fenceValue = fenceValue;
    }

    uint64& FrameResourceRing::currentFenceValue()
    {
        return _arrSlot[_frameIndex]._fenceValue;
    }

    bool FrameResourceRing::tryAllocate( uint64 sizeBytes, uint64 alignment, uint64& outOffset )
    {
        Slot&        slot   = _arrSlot[_frameIndex];
        const uint64 offset = MathUtil::align( slot._uploadOffset, alignment == 0 ? uint64{ 1 } : alignment );
        if ( offset > _uploadCapacity || sizeBytes > ( _uploadCapacity - offset ) )
            return false;
        outOffset          = offset;
        slot._uploadOffset = offset + sizeBytes;
        return true;
    }

    void FrameResourceRing::resetUploadOffset()
    {
        _arrSlot[_frameIndex]._uploadOffset = 0;
    }
} // namespace sw
