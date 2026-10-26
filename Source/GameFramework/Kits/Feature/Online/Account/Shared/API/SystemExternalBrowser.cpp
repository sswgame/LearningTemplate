#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Shared/API/SystemExternalBrowser.h"

#include "Core/Container/StringUtil.h"
#include "Core/Process/Process.h"

namespace sw
{
    SW_LOG_CALLER( "SystemExternalBrowser" );

    namespace
    {
        struct SystemExternalBrowserInternal
        {
            static constexpr string_view kAllowedPunctuation = "-._~:/?#[]@!&()*+,;=%"; ///< RFC 3986 에서 따옴표만 뺐다

            static bool isAllowedCharacter( utf8 ch )
            {
                const bool bLetter = ( 'a' <= ch && ch <= 'z' ) || ( 'A' <= ch && ch <= 'Z' );
                const bool bDigit  = '0' <= ch && ch <= '9';
                return bLetter || bDigit || kAllowedPunctuation.find( ch ) != string_view::npos;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool SystemExternalBrowser::isOpenableURL( string_view url )
    {
        const bool bHTTP  = StringUtil::startsWith( url, "http://", true );
        const bool bHTTPS = StringUtil::startsWith( url, "https://", true );
        if ( bHTTP == false && bHTTPS == false )
            return false;
        for ( const utf8 ch : url )
        {
            if ( SystemExternalBrowserInternal::isAllowedCharacter( ch ) == false )
                return false;
        }
        return true;
    }

    bool SystemExternalBrowser::openURL( string_view url )
    {
        if ( isOpenableURL( url ) == false )
        {
            SW_LOG_ERROR( "Refused to open a URL that is not plain http(s): %#", string( url ).c_str() );
            return false;
        }
#if defined( SW_PLATFORM_WINDOWS )
        // rundll32 는 `<dll>,<함수>` 뒤의 줄을 인자로 나누지 않고 통째로 넘긴다 — 표준 인용의 따옴표가 주소에 남으므로 날 인자 줄로 준다.
        // 주소에는 공백 · 따옴표가 없다(isOpenableURL). url.dll 이 기본 브라우저를 고른다.
        ProcessCommand command;
        command._executable             = "rundll32.exe";
        command._rawWindowsArgumentLine = "url.dll,FileProtocolHandler " + string( url );
#else
        const ProcessCommand command{ "xdg-open", { string( url ) } };
#endif
        if ( Process::launchDetached( command ) == false )
        {
            SW_LOG_ERROR( "Failed to launch the system browser" );
            return false;
        }
        return true;
    }
} // namespace sw
