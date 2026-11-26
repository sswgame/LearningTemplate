#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Runtime/PlatformLoginClient.h"

#include "Core/Process/Process.h"
#include "Core/String/StringUtil.h"

namespace sw
{
    FakePlatformLoginClient::FakePlatformLoginClient()
        : _listPreset{}
        , _listPending{}
        , _resultBuffer{}
        , _requestIDAllocator{}
    {
    }

    void FakePlatformLoginClient::setTicket( string_view provider, string_view ticketText )
    {
        for ( Preset& preset : _listPreset )
        {
            if ( preset._provider == provider )
            {
                preset._ticketText = string( ticketText );
                return;
            }
        }
        _listPreset.push_back( Preset{ string( provider ), string( ticketText ) } );
    }

    uint64 FakePlatformLoginClient::beginLogin( string_view provider, MonotonicMilliseconds nowMs )
    {
        (void)nowMs;
        PlatformLoginClientResult& result = _listPending.emplace_back();
        result._requestID                 = _requestIDAllocator.allocate();
        result._provider                  = string( provider );
        return result._requestID;
    }

    void FakePlatformLoginClient::update( MonotonicMilliseconds nowMs )
    {
        (void)nowMs;
        for ( PlatformLoginClientResult& result : _listPending )
        {
            const Preset* pPreset = nullptr;
            for ( const Preset& preset : _listPreset )
            {
                if ( preset._provider == result._provider )
                    pPreset = &preset;
            }
            if ( pPreset == nullptr )
            {
                result._failureText = "no ticket for this provider";
            }
            else
            {
                result._ticket.assign( reinterpret_cast<const uint8*>( pPreset->_ticketText.data() ), reinterpret_cast<const uint8*>( pPreset->_ticketText.data() ) + pPreset->_ticketText.size() );
                result._bSucceeded = SW_TRUE;
            }
            _resultBuffer.push( std::move( result ) );
        }
        _listPending.clear();
    }

    int32 FakePlatformLoginClient::pollResults( vector<PlatformLoginClientResult>& outListResult )
    {
        const int32 count = static_cast<int32>( _resultBuffer.getCount() );
        _resultBuffer.moveAllTo( outListResult );
        return count;
    }
} // namespace sw

namespace sw
{
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
            SW_LOG_ERROR_ID( "SystemExternalBrowser.RefusedOpenURLNot", "Refused to open a URL that is not plain http(s): %#", string( url ).c_str() );
            return false;
        }
        // http(s) 만 넘긴다(위) — `launchURL` 은 무엇이든 그 형식의 기본 처리기에 넘긴다.
        if ( Process::launchURL( url ) == false )
        {
            SW_LOG_ERROR_ID( "SystemExternalBrowser.FailedLaunchSystemBrowser", "Failed to launch the system browser" );
            return false;
        }
        return true;
    }
} // namespace sw
