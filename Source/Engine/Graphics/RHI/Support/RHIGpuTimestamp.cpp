#include "pch.h"

#include "Engine/Graphics/RHI/Support/RHIGpuTimestamp.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    bool RHIGpuTimestamp::resolveMicro( const uint64* pTick, uint32 readyMask, float64 microPerTick, vector<float32>& outListMicro )
    {
        outListMicro.clear();
        if ( pTick == nullptr || readyMask == 0 )
            return false;

        uint64 origin{ UINT64_MAX };
        for ( uint32 slotIndex = 0; slotIndex < constant::kMaxGpuTimestampSlot; ++slotIndex )
        {
            if ( ( readyMask & ( 1u << slotIndex ) ) != 0 && pTick[slotIndex] < origin )
                origin = pTick[slotIndex];
        }

        outListMicro.resize( constant::kMaxGpuTimestampSlot );
        for ( uint32 slotIndex = 0; slotIndex < constant::kMaxGpuTimestampSlot; ++slotIndex )
        {
            if ( ( readyMask & ( 1u << slotIndex ) ) == 0 )
            {
                outListMicro[slotIndex] = -1.0f;
                continue;
            }
            const uint64 ticks      = ( pTick[slotIndex] >= origin ) ? ( pTick[slotIndex] - origin ) : 0;
            outListMicro[slotIndex] = static_cast<float32>( static_cast<float64>( ticks ) * microPerTick );
        }
        return true;
    }
} // namespace sw
