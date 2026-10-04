#include "pch.h"

#include "Engine/Graphics/RHI/Support/RHIGpuTimestamp.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    bool RHIGpuTimestamp::resolve( const uint64* pTick, uint32 readyMask, float64 nanosPerTick, RHIGpuTimestampFrame& outFrame )
    {
        outFrame._listMicro.clear();
        outFrame._originNanos = 0;
        if ( pTick == nullptr || readyMask == 0 )
            return false;

        uint64 origin{ UINT64_MAX };
        for ( uint32 slotIndex = 0; slotIndex < constant::kMaxGpuTimestampSlot; ++slotIndex )
        {
            if ( ( readyMask & ( 1u << slotIndex ) ) != 0 && pTick[slotIndex] < origin )
                origin = pTick[slotIndex];
        }

        const float64 microPerTick = nanosPerTick / 1000.0;
        outFrame._originNanos      = convertTickToNanos( origin, nanosPerTick );
        outFrame._listMicro.resize( constant::kMaxGpuTimestampSlot );
        for ( uint32 slotIndex = 0; slotIndex < constant::kMaxGpuTimestampSlot; ++slotIndex )
        {
            if ( ( readyMask & ( 1u << slotIndex ) ) == 0 )
            {
                outFrame._listMicro[slotIndex] = -1.0f;
                continue;
            }
            const uint64 ticks             = ( pTick[slotIndex] >= origin ) ? ( pTick[slotIndex] - origin ) : 0;
            outFrame._listMicro[slotIndex] = static_cast<float32>( static_cast<float64>( ticks ) * microPerTick );
        }
        return true;
    }

    int64 RHIGpuTimestamp::convertTickToNanos( uint64 tick, float64 nanosPerTick )
    {
        // float64 는 53 비트 정밀도다 — 2^53 ns(약 104 일)까지 1 ns 안이다.
        return static_cast<int64>( static_cast<float64>( tick ) * nanosPerTick );
    }
} // namespace sw
