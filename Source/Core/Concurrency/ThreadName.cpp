#include "pch.h"

#include "Core/Concurrency/ThreadName.h"

#include "Core/Common/Defines.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Common/PlatformOsHeaders.h"
#elif defined( SW_PLATFORM_LINUX )
    #include "Core/Common/PlatformOsHeaders.h"
#endif

namespace sw
{
    void ThreadName::setCurrentThreadName( const utf8* pName )
    {
        if ( pName == nullptr || pName[0] == '\0' )
            return;

#if defined( SW_PLATFORM_WINDOWS )
        // SetThreadDescription 은 UTF-16 을 받는다. 이름은 짧은 고정 문자열이라 스택 버퍼로 바꾼다.
        utf16       arrWide[constant::kMaxBuffer64]{};
        const int32 written = MultiByteToWideChar( CP_UTF8, 0, pName, -1, arrWide, static_cast<int32>( constant::kMaxBuffer64 ) );
        if ( written <= 0 )
            return;
        SetThreadDescription( GetCurrentThread(), arrWide );
#elif defined( SW_PLATFORM_LINUX )
        // comm 은 15 바이트까지다. 넘기면 pthread_setname_np 가 ERANGE 로 아무것도 하지 않으므로 잘라서 넘긴다.
        utf8   arrShort[kMaxPosixLength + 1]{};
        size_t length = 0;
        while ( length < kMaxPosixLength && pName[length] != '\0' )
        {
            arrShort[length] = pName[length];
            ++length;
        }
        pthread_setname_np( pthread_self(), arrShort );
#endif
    }
} // namespace sw
