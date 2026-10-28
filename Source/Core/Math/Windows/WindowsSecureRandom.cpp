#include "pch.h"

#include "Core/Math/SecureRandom.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Common/PlatformOSHeaders.h"

    #include <bcrypt.h>
    #include <cstdlib>

namespace sw
{
    void SecureRandom::fill( uint8* pOutBytes, size_t byteCount )
    {
        constexpr size_t kMaxChunk = 0xFFFFFFFFu; // BCryptGenRandom 은 길이를 ULONG 으로 받는다
        while ( byteCount > 0 )
        {
            const size_t   chunk  = byteCount < kMaxChunk ? byteCount : kMaxChunk;
            const NTSTATUS status = BCryptGenRandom( nullptr, pOutBytes, static_cast<ULONG>( chunk ), BCRYPT_USE_SYSTEM_PREFERRED_RNG );
            if ( BCRYPT_SUCCESS( status ) == false )
                SW_LOW_LEVEL_FATAL( "BCryptGenRandom failed (NTSTATUS 0x%08x) - no OS randomness, refusing a predictable value", static_cast<uint32>( status ) );
            pOutBytes += chunk;
            byteCount -= chunk;
        }
    }

    uint64 SecureRandom::nextUint64()
    {
        uint64 value{ 0 };
        fill( reinterpret_cast<uint8*>( &value ), sizeof( value ) );
        return value;
    }
} // namespace sw
#endif
