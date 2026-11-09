#include "pch.h"

#include "Core/Math/SecureRandom.h"

#if defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_ANDROID ) || defined( SW_PLATFORM_WEB )
    #include <cerrno>
    #include <cstdlib>
    #include <cstring>
    #include <unistd.h>

namespace sw
{
    void SecureRandom::fill( uint8* pOutBytes, size_t byteCount )
    {
        // getentropy 하나로 세 대상을 덮는다 — glibc 2.25+ · bionic API 28+(대상 29) · Emscripten(crypto.getRandomValues). getrandom 은 Emscripten 이 링크하지 않는다.
        static constexpr size_t kMaxChunk = 256; // getentropy 의 한 번 상한(POSIX)
        while ( byteCount > 0 )
        {
            const size_t chunk = byteCount < kMaxChunk ? byteCount : kMaxChunk;
            if ( getentropy( pOutBytes, chunk ) != 0 )
            {
                if ( errno == EINTR )
                    continue;
                SW_LOW_LEVEL_FATAL( "getentropy failed (%s) - no OS randomness, refusing a predictable value", std::strerror( errno ) );
            }
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
