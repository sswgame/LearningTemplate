#include "pch.h"

#include "Engine/Graphics/RHI/Backend/RHIBufferSize.h"

namespace sw
{
    SW_LOG_CALLER( "RHIBufferSize" );

    bool RHIBufferSize::computeStructuredBytes( uint32 elementSize, uint32 elementCount, uint32& outTotalBytes )
    {
        const uint64 totalBytes = static_cast<uint64>( elementSize ) * static_cast<uint64>( elementCount );
        if ( totalBytes > static_cast<uint64>( ~uint32{ 0 } ) )
        {
            SW_LOG_ERROR( "The structured buffer does not fit in a 32-bit size (%# x %# = %# bytes).", elementSize, elementCount, totalBytes );
            outTotalBytes = 0;
            return false;
        }
        outTotalBytes = static_cast<uint32>( totalBytes );
        return true;
    }
} // namespace sw
