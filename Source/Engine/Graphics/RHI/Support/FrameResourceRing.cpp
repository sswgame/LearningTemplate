#include "pch.h"

#include "Engine/Graphics/RHI/Support/FrameResourceRing.h"

namespace sw
{
    FrameResourceRing::FrameResourceRing()
        : _arrFenceValue{}
        , _frameIndex{ constant::kMaxFrameCountInFlight - 1 }
    {
    }

    void FrameResourceRing::reset()
    {
        _frameIndex = constant::kMaxFrameCountInFlight - 1;
        for ( uint64& fenceValue : _arrFenceValue )
            fenceValue = 0;
    }

    bool FrameResourceRing::beginFrame( uint64 completedFenceValue )
    {
        const uint32 nextIndex = ( _frameIndex + 1 ) % constant::kMaxFrameCountInFlight;
        if ( _arrFenceValue[nextIndex] > completedFenceValue )
            return false;

        _frameIndex = nextIndex;
        return true;
    }

    uint64 FrameResourceRing::getFenceValue( uint32 index ) const
    {
        if ( index >= constant::kMaxFrameCountInFlight )
            return 0;
        return _arrFenceValue[index];
    }

    void FrameResourceRing::setFenceValue( uint32 index, uint64 fenceValue )
    {
        if ( index >= constant::kMaxFrameCountInFlight )
            return;
        _arrFenceValue[index] = fenceValue;
    }
} // namespace sw
