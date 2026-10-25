#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/vector.h"
#include "Core/Process/ProcessEnvironment.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Common/PlatformOsHeaders.h"

    #include <cstdlib>

namespace sw
{
    namespace
    {
        struct WindowsProcessEnvironmentInternal
        {
            /** @brief 읽는 동안 다른 스레드가 값을 바꿔 길이가 달라지면 다시 읽는 횟수의 상한입니다. */
            static constexpr uint32 kMaxReadAttempts = 4;

            static bool isValidName( string_view name ) { return name.empty() == false && name.find( '=' ) == string_view::npos; }
        };
    } // namespace

    bool ProcessEnvironment::tryRead( string_view name, string& outValue )
    {
        using Internal = WindowsProcessEnvironmentInternal;
        outValue.clear();
        if ( Internal::isValidName( name ) == false )
            return false;
        const wstring wideName = StringUtil::utf8ToUtf16( string{ name }.c_str() );
        for ( uint32 attempt = 0; attempt < Internal::kMaxReadAttempts; ++attempt )
        {
            // 값이 빈 변수도 0 을 돌려준다 — 오류 코드를 먼저 지워 "없음"(ERROR_ENVVAR_NOT_FOUND)과 가른다.
            SetLastError( ERROR_SUCCESS );
            const DWORD required = GetEnvironmentVariableW( wideName.c_str(), nullptr, 0 );
            if ( required == 0 )
                return GetLastError() != ERROR_ENVVAR_NOT_FOUND; // 빈 값이면 true
            vector<utf16> listBuffer( static_cast<size_t>( required ), L'\0' );
            const DWORD   written = GetEnvironmentVariableW( wideName.c_str(), listBuffer.data(), required );
            if ( written == 0 || written >= required )
                continue;                                            // 그 사이 바뀌었다 — 다시 길이를 잰다
            outValue = StringUtil::utf16ToUtf8( listBuffer.data() ); // 끝의 널은 API 가 적었다
            return true;
        }
        return false;
    }

    bool ProcessEnvironment::trySet( string_view name, string_view value )
    {
        if ( WindowsProcessEnvironmentInternal::isValidName( name ) == false )
            return false;
        const wstring wideName  = StringUtil::utf8ToUtf16( string{ name }.c_str() );
        const wstring wideValue = StringUtil::utf8ToUtf16( string{ value }.c_str() );
        // OS 환경과 CRT 복사본을 함께 바꾼다 — CRT 의 `getenv` 를 읽는 서드파티 코드(Tracy 클라이언트)는 복사본을 본다.
        // `_wputenv_s` 는 빈 값을 "지우기"로 읽어 옛 값이 복사본에 남지 않게 한다. 마지막에 OS 환경을 최종 값(빈 값 포함)으로 맞춘다.
        const bool bCrtSet = _wputenv_s( wideName.c_str(), wideValue.c_str() ) == 0;
        const bool bOsSet  = SetEnvironmentVariableW( wideName.c_str(), wideValue.c_str() ) != FALSE;
        return bCrtSet && bOsSet;
    }

    bool ProcessEnvironment::tryUnset( string_view name )
    {
        if ( WindowsProcessEnvironmentInternal::isValidName( name ) == false )
            return false;
        const wstring wideName = StringUtil::utf8ToUtf16( string{ name }.c_str() );
        // 없던 변수를 지울 때 `SetEnvironmentVariableW` 는 ERROR_ENVVAR_NOT_FOUND 로 실패한다 — 지워진 상태가 목적이므로 성공으로 본다.
        const bool bOsCleared = SetEnvironmentVariableW( wideName.c_str(), nullptr ) != FALSE || GetLastError() == ERROR_ENVVAR_NOT_FOUND;
        return bOsCleared && _wputenv_s( wideName.c_str(), L"" ) == 0;
    }
} // namespace sw
#endif
