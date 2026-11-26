#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/ProfileAPILoginProvider.h"

#include "Engine/Config/Server/ServerSecret.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/HTTP/URLTextUtil.h"
#include "GameFramework/Runtime/Online/HTTP/HTTPClient.h"

namespace sw
{
    unique_ptr<IPlatformLoginProvider> ProfileAPILoginProvider::create( const PlatformLoginProviderConfig& settings, HTTPClient* pHTTPClient )
    {
        if ( settings._kind != PlatformLoginProviderKind::TicketQuery )
            return sw::make_unique<ProfileAPILoginProvider>( settings, pHTTPClient );
        string key;
        if ( ServerSecret::read( settings._keySecretEnvironment, key ) == false || key.empty() )
        {
            SW_LOG_ERROR_ID( "ProfileAPILoginProvider.LoginProviderKeyEnvironment", "Login provider '%#': the key environment variable '%#' is not set", settings._name, settings._keySecretEnvironment );
            return nullptr;
        }
        if ( isQuerySafe( key ) == false )
        {
            SW_LOG_ERROR_ID( "ProfileAPILoginProvider.LoginProviderKeyCharacters", "Login provider '%#': the key has characters that cannot go into a query", settings._name );
            return nullptr;
        }
        return sw::make_unique<ProfileAPILoginProvider>( settings, pHTTPClient, std::move( key ) );
    }

    ProfileAPILoginProvider::ProfileAPILoginProvider( const PlatformLoginProviderConfig& settings, HTTPClient* pHTTPClient, string key )
        : _settings{ settings }
        , _key{ std::move( key ) }
        , _mapRequestToVerification{}
        , _verificationBuffer{}
        , _pHTTPClient{ pHTTPClient }
        , _verificationIDAllocator{}
    {
    }

    ProfileAPILoginProvider::~ProfileAPILoginProvider()
    {
        Memory::wipe( _key.data(), _key.size() ); // 비밀 키를 메모리에 남기지 않는다
    }

    bool ProfileAPILoginProvider::isQuerySafe( string_view text )
    {
        return URLTextUtil::isSafeText( text, "-_." );
    }

    string ProfileAPILoginProvider::makeRequestURL( string_view ticket ) const
    {
        StringBuilder<constant::buffer::kMax2048> url;
        url.appendFormat( "%#%#%#=%#&%#=%#&%#=%#", _settings._profileURL, _settings._profileURL.find( '?' ) == string::npos ? "?" : "&", _settings._keyParam, _key,
                          _settings._ticketParam, ticket, _settings._identityParam, _settings._identity );
        return string( url.view() );
    }

    uint64 ProfileAPILoginProvider::submitVerification( const vector<uint8>& ticketBytes, UnixMilliseconds /*nowMs*/ )
    {
        const uint64      verificationID = _verificationIDAllocator.allocate();
        const string_view ticket( reinterpret_cast<const utf8*>( ticketBytes.data() ), ticketBytes.size() );
        HTTPClientRequest request;
        request._timeoutMs = _settings._requestTimeoutMs;
        if ( _settings._kind == PlatformLoginProviderKind::TicketQuery )
        {
            if ( isQuerySafe( ticket ) == false )
            {
                // 쿼리에 실을 수 없는 티켓은 제공자에 묻지 않고 거절한다(다른 인자를 끼워 넣는 길을 막는다).
                _verificationBuffer.push( PlatformLoginVerification{} );
                PlatformLoginVerification& verification = _verificationBuffer.getLast();
                verification._verificationID            = verificationID;
                verification._bRejected                 = SW_TRUE;
                return verificationID;
            }
            request._url = makeRequestURL( ticket );
        }
        else
        {
            request._url = _settings._profileURL;
            StringBuilder<constant::buffer::kMax2048> authorization;
            authorization.appendFormat( "Bearer %#", ticket );
            request._listHeader.push_back( HTTPHeader{ "Authorization", string( authorization.view() ) } );
        }
        request._listHeader.push_back( HTTPHeader{ "Accept", "application/json" } );
        _mapRequestToVerification[_pHTTPClient->submitRequest( request )] = verificationID;
        return verificationID;
    }

    int32 ProfileAPILoginProvider::pollVerifications( vector<PlatformLoginVerification>& outListVerification )
    {
        const int32 count = static_cast<int32>( _verificationBuffer.getCount() );
        _verificationBuffer.moveAllTo( outListVerification );
        return count;
    }

    void ProfileAPILoginProvider::update( UnixMilliseconds /*nowMs*/ )
    {
        _pHTTPClient->update();
        vector<HTTPClientResponse> listResponse;
        (void)_pHTTPClient->pollResponses( listResponse );
        for ( const HTTPClientResponse& response : listResponse )
        {
            const auto requestIt = _mapRequestToVerification.find( response._requestID );
            if ( requestIt == _mapRequestToVerification.end() )
                continue;
            _verificationBuffer.push( PlatformLoginVerification{} );
            PlatformLoginVerification& verification = _verificationBuffer.getLast();
            verification._verificationID            = requestIt->second;
            _mapRequestToVerification.erase( requestIt );
            readVerification( response, verification );
        }
    }

    void ProfileAPILoginProvider::readVerification( const HTTPClientResponse& response, PlatformLoginVerification& outVerification ) const
    {
        if ( response.getKind() == HTTPResponseKind::Denied )
        {
            outVerification._bRejected = SW_TRUE;
            return;
        }
        JSONDocument document;
        if ( response.isSuccess() == false || document.tryParse( response.getBodyText() ) == false )
        {
            outVerification._bUnavailable = SW_TRUE;
            return;
        }
        bool bResultOk = true;
        if ( _settings._kind == PlatformLoginProviderKind::TicketQuery )
        {
            string result;
            bResultOk = document.getRoot().findPathText( _settings._resultPath, result ) && result == _settings._resultValue;
        }
        const bool bSubject = bResultOk && document.getRoot().findPathText( _settings._subjectPath, outVerification._subject );
        if ( bSubject == false || outVerification._subject.empty() )
        {
            outVerification._subject.clear();
            outVerification._bRejected = SW_TRUE;
            return;
        }
        if ( _settings._displayNamePath.empty() == false )
            (void)document.getRoot().findPathText( _settings._displayNamePath, outVerification._displayName ); // 선택 값이다 — 없으면 비워 둔다
    }
} // namespace sw
