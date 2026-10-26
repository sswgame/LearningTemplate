#include "pch.h"

#include "Core/Math/SecureRandom.h"

#if defined( SW_PLATFORM_LINUX )
    #include <cerrno>
    #include <cstdlib>
    #include <cstring>
    #include <sys/random.h>

namespace sw
{
    void SecureRandom::fill( uint8* pOutBytes, size_t byteCount )
    {
        while ( byteCount > 0 )
        {
            // 256 바이트를 넘는 요청은 신호에 끊겨 일부만 채울 수 있다 — 채운 만큼 넘기고 이어 읽는다.
            const ssize_t written = getrandom( pOutBytes, byteCount, 0 );
            if ( written < 0 )
            {
                if ( errno == EINTR )
                    continue;
                SW_LOW_LEVEL_ERROR( "getrandom failed (%s) - no OS randomness, stopping instead of using a predictable value", std::strerror( errno ) );
                std::abort();
            }
            pOutBytes += written;
            byteCount -= static_cast<size_t>( written );
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
