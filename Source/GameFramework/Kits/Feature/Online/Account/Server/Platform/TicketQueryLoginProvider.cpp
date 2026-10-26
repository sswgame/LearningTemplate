#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/TicketQueryLoginProvider.h"

#include "Engine/Config/Server/ServerSecret.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

#include "GameFramework/Base/Online/HTTP/HTTPClient.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/ProfileAPILoginProvider.h"

namespace sw
{
    SW_LOG_CALLER( "TicketQueryLoginProvider" );

    unique_ptr<IPlatformLoginProvider> TicketQueryLoginProvider::create( const PlatformLoginProviderConfig& settings, HTTPClient* pHTTPClient )
    {
        string key;
        if ( ServerSecret::read( settings._keySecretEnvironment, key ) == false || key.empty() )
        {
            SW_LOG_ERROR( "Login provider '%#': the key environment variable '%#' is not set", settings._name, settings._keySecretEnvironment );
            return nullptr;
        }
        if ( isQuerySafe( key ) == false )
        {
            SW_LOG_ERROR( "Login provider '%#': the key has characters that cannot go into a query", settings._name );
            return nullptr;
        }
        return sw::make_unique<TicketQueryLoginProvider>( settings, std::move( key ), pHTTPClient );
    }

    TicketQueryLoginProvider::TicketQueryLoginProvider( const PlatformLoginProviderConfig& settings, string key, HTTPClient* pHTTPClient )
        : _settings{ settings }
        , _key{ std::move( key ) }
        , _mapRequestToVerification{}
        , _listDone{}
        , _pHTTPClient{ pHTTPClient }
        , _nextVerificationID{ 1 }
    {
    }

    TicketQueryLoginProvider::~TicketQueryLoginProvider()
    {
        Memory::set( _key.data(), 0, _key.size() ); // 비밀 키를 메모리에 남기지 않는다
    }

    bool TicketQueryLoginProvider::isQuerySafe( string_view text )
    {
        if ( text.empty() )
            return false;
        for ( const utf8 character : text )
        {
            const bool bAlphaNumeric = ( character >= '0' && character <= '9' ) || ( character >= 'a' && character <= 'z' ) || ( character >= 'A' && character <= 'Z' );
            const bool bMark         = character == '-' || character == '_' || character == '.';
            if ( bAlphaNumeric == false && bMark == false )
                return false;
        }
        return true;
    }

    string TicketQueryLoginProvider::makeRequestURL( string_view ticket ) const
    {
        string url = _settings._profileURL;
        url += url.find( '?' ) == string::npos ? "?" : "&";
        url += _settings._keyParam + "=" + _key + "&" + _settings._ticketParam + "=" + string( ticket );
        return url;
    }

    uint64 TicketQueryLoginProvider::submitVerification( const vector<uint8>& ticketBytes, UnixMilliseconds nowMs )
    {
        const uint64      verificationID = _nextVerificationID++;
        const string_view ticket( reinterpret_cast<const utf8*>( ticketBytes.data() ), ticketBytes.size() );
        if ( isQuerySafe( ticket ) == false )
        {
            // 쿼리에 실을 수 없는 티켓은 제공자에 묻지 않고 거절한다(다른 인자를 끼워 넣는 길을 막는다).
            PlatformLoginVerification& verification = _listDone.emplace_back();
            verification._verificationID            = verificationID;
            verification._bRejected                 = SW_TRUE;
            return verificationID;
        }
        HTTPClientRequest request;
        request._url       = makeRequestURL( ticket );
        request._timeoutMs = _settings._requestTimeoutMs;
        request._listHeader.push_back( HTTPHeader{ "Accept", "application/json" } );
        _mapRequestToVerification[_pHTTPClient->submitRequest( request )] = verificationID;
        return verificationID;
    }

    int32 TicketQueryLoginProvider::pollVerifications( vector<PlatformLoginVerification>& outListVerification )
    {
        const int32 count = static_cast<int32>( _listDone.size() );
        for ( PlatformLoginVerification& verification : _listDone )
        {
            outListVerification.push_back( std::move( verification ) );
        }
        _listDone.clear();
        return count;
    }

    void TicketQueryLoginProvider::update( UnixMilliseconds nowMs )
    {
        _pHTTPClient->update();
        vector<HTTPClientResponse> listResponse;
        (void)_pHTTPClient->pollResponses( listResponse );
        for ( const HTTPClientResponse& response : listResponse )
        {
            const auto requestIt = _mapRequestToVerification.find( response._requestID );
            if ( requestIt == _mapRequestToVerification.end() )
                continue;
            PlatformLoginVerification& verification = _listDone.emplace_back();
            verification._verificationID            = requestIt->second;
            _mapRequestToVerification.erase( requestIt );
            const bool bDenied = response._statusCode == HTTPConstant::kStatusUnauthorized || response._statusCode == HTTPConstant::kStatusForbidden;
            if ( bDenied )
            {
                verification._bRejected = SW_TRUE;
                continue;
            }
            JSONDocument document;
            if ( response.isSuccess() == false || document.tryParse( response.getBodyText() ) == false )
            {
                verification._bUnavailable = SW_TRUE;
                continue;
            }
            string     result;
            const bool bResultOk = ProfileAPILoginProvider::findPathText( document.getRoot(), _settings._resultPath, result ) && result == _settings._resultValue;
            const bool bSubject  = bResultOk && ProfileAPILoginProvider::findPathText( document.getRoot(), _settings._subjectPath, verification._subject );
            if ( bSubject == false || verification._subject.empty() )
            {
                verification._subject.clear();
                verification._bRejected = SW_TRUE;
                continue;
            }
            if ( _settings._displayNamePath.empty() == false )
                (void)ProfileAPILoginProvider::findPathText( document.getRoot(), _settings._displayNamePath, verification._displayName );
        }
    }
} // namespace sw
