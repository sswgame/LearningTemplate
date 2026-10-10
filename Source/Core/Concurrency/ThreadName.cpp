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

    bool ThreadName::tryGetCurrentThreadName( utf8* pOutName, uint32 capacity )
    {
        if ( pOutName == nullptr || capacity == 0 )
            return false;
        pOutName[0] = '\0';

#if defined( SW_PLATFORM_WINDOWS )
        PWSTR pWide = nullptr;
        if ( FAILED( GetThreadDescription( GetCurrentThread(), &pWide ) ) || pWide == nullptr )
            return false;
        const int32 written = WideCharToMultiByte( CP_UTF8, 0, pWide, -1, pOutName, static_cast<int32>( capacity ), nullptr, nullptr );
        LocalFree( pWide );
        if ( written <= 0 )
        {
            pOutName[0] = '\0';
            return false;
        }
        pOutName[capacity - 1] = '\0';
        return pOutName[0] != '\0';
#elif defined( SW_PLATFORM_LINUX )
        utf8 arrShort[kMaxPosixLength + 1]{};
        if ( pthread_getname_np( pthread_self(), arrShort, sizeof( arrShort ) ) != 0 )
            return false;
        uint32 length = 0;
        while ( length + 1 < capacity && arrShort[length] != '\0' )
        {
            pOutName[length] = arrShort[length];
            ++length;
        }
        pOutName[length] = '\0';
        return length > 0;
#else
        return false;
#endif
    }
} // namespace sw
