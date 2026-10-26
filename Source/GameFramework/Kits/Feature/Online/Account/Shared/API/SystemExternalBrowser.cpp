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
        // http(s) 만 넘긴다(위) — `launchURL` 은 무엇이든 그 형식의 기본 처리기에 넘긴다.
        if ( Process::launchURL( url ) == false )
        {
            SW_LOG_ERROR( "Failed to launch the system browser" );
            return false;
        }
        return true;
    }
} // namespace sw
