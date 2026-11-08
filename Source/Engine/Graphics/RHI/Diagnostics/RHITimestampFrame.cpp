#include "pch.h"

#include "Engine/Graphics/RHI/Diagnostics/RHITimestampFrame.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    bool RHITimestampFrame::resolve( const uint64* pTick, const GPUTimestampSlotMask& readyMask, float64 nanosPerTick, RHITimestampFrame& outFrame )
    {
        outFrame._listMicro.clear();
        outFrame._originNanos = 0;
        if ( pTick == nullptr || readyMask.any() == false )
            return false;

        uint64 origin{ UINT64_MAX };
        for ( uint32 slotIndex = 0; slotIndex < constant::render::kMaxGPUTimestampSlot; ++slotIndex )
        {
            if ( readyMask.test( slotIndex ) && pTick[slotIndex] < origin )
                origin = pTick[slotIndex];
        }

        const float64 microPerTick = nanosPerTick / constant::time::kNanosecondsPerMicrosecond;
        outFrame._originNanos      = convertTickToNanos( origin, nanosPerTick );
        outFrame._listMicro.resize( constant::render::kMaxGPUTimestampSlot );
        for ( uint32 slotIndex = 0; slotIndex < constant::render::kMaxGPUTimestampSlot; ++slotIndex )
        {
            if ( readyMask.test( slotIndex ) == false )
            {
                outFrame._listMicro[slotIndex] = -1.0f;
                continue;
            }
            const uint64 ticks             = ( pTick[slotIndex] >= origin ) ? ( pTick[slotIndex] - origin ) : 0;
            outFrame._listMicro[slotIndex] = static_cast<float32>( static_cast<float64>( ticks ) * microPerTick );
        }
        return true;
    }

    int64 RHITimestampFrame::convertTickToNanos( uint64 tick, float64 nanosPerTick )
    {
        // float64 는 53비트 정밀도다. 2^53 ns(약 104 일)까지 1 ns 안이다.
        return static_cast<int64>( static_cast<float64>( tick ) * nanosPerTick );
    }
} // namespace sw
