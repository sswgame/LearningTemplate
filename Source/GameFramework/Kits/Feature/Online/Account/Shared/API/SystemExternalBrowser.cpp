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
        // cmd 를 거치지 않으므로 `&` 가 명령을 나누지 않는다. url.dll 이 기본 브라우저를 고른다
        const string command = "rundll32.exe url.dll,FileProtocolHandler " + string( url );
#else
        // `sh -c` 를 거치므로 작은따옴표로 감싼다. 주소에는 따옴표가 없다(isOpenableURL)
        const string command = "xdg-open '" + string( url ) + "'";
#endif
        if ( Process::launchDetached( command ) == false )
        {
            SW_LOG_ERROR( "Failed to launch the system browser" );
            return false;
        }
        return true;
    }
} // namespace sw
