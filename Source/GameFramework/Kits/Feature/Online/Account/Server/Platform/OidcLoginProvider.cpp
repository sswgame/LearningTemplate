#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/OidcLoginProvider.h"

#include "GameFramework/Base/Online/HTTP/HTTPClient.h"

namespace sw
{
    SW_LOG_CALLER( "OidcLoginProvider" );
} // namespace sw

namespace sw
{
    OidcLoginProvider::OidcLoginProvider( const PlatformLoginProviderConfig& settings, INetSecurityProvider* pProvider, HTTPClient* pHTTPClient )
        : _settings{ settings }
        , _keyCache{}
        , _listPending{}
        , _listDone{}
        , _pProvider{ pProvider }
        , _pHTTPClient{ pHTTPClient }
        , _verificationIDAllocator{}
        , _jwksRequestID{ 0 }
        , _lastFetchStartMs{ 0 }
        , _fetchGeneration{ 0 }
        , _jwksFetchCount{ 0 }
        , _bLastFetchFailed{ SW_FALSE }
    {
    }

    uint64 OidcLoginProvider::submitVerification( const vector<uint8>& ticketBytes, int64 nowMs )
    {
        (void)nowMs;
        const string_view text( reinterpret_cast<const utf8*>( ticketBytes.data() ), ticketBytes.size() );
        const size_t      bar     = text.find( '|' );
        PendingTicket&    pending = _listPending.emplace_back();
        pending._token            = string( text.substr( 0, bar ) );
        pending._nonce            = bar == string_view::npos ? string{} : string( text.substr( bar + 1 ) );
        pending._verificationID   = _verificationIDAllocator.allocate();
        return pending._verificationID;
    }

    int32 OidcLoginProvider::pollVerifications( vector<PlatformLoginVerification>& outListVerification )
    {
        const int32 count = static_cast<int32>( _listDone.size() );
        for ( PlatformLoginVerification& verification : _listDone )
        {
            outListVerification.push_back( std::move( verification ) );
        }
        _listDone.clear();
        return count;
    }

    void OidcLoginProvider::update( int64 nowMs )
    {
        _pHTTPClient->update( nowMs );
        vector<HTTPClientResponse> listResponse;
        (void)_pHTTPClient->pollResponses( listResponse );
        for ( const HTTPClientResponse& response : listResponse )
        {
            if ( response._requestID != _jwksRequestID )
                continue;
            const bool bReplaced = response.isSuccess() && _keyCache.replaceFromJwks( response.getBodyText(), nowMs );
            if ( bReplaced == false )
                SW_LOG_WARNING( "JWKS fetch for '%#' failed: %#", _settings._name.c_str(), response._bTransportFailed == SW_TRUE ? response._failureText.c_str() : "bad response" );
            _bLastFetchFailed = bReplaced ? SW_FALSE : SW_TRUE;
            _jwksRequestID    = 0;
            ++_fetchGeneration;
        }

        const bool bStale     = _keyCache.hasKeys() && nowMs - _keyCache.getFetchedAtMs() >= _settings._jwksRefreshMs;
        const bool bMayFetch  = _jwksFetchCount == 0 || nowMs - _lastFetchStartMs >= _settings._minRefetchMs;
        const bool bWantFirst = _keyCache.hasKeys() == false && _listPending.empty() == false;
        const bool bFetchKeys = _jwksRequestID == 0 && ( bStale || bWantFirst ) && bMayFetch;
        if ( bFetchKeys )
            startJwksFetch( nowMs );

        for ( size_t pendingIndex = 0; pendingIndex < _listPending.size(); )
        {
            PendingTicket& pending  = _listPending[pendingIndex];
            const bool     bWaiting = pending._waitedFetchGeneration > _fetchGeneration;
            if ( bWaiting )
            {
                ++pendingIndex;
                continue;
            }
            PlatformLoginVerification verification;
            verification._verificationID = pending._verificationID;
            if ( evaluate( pending, UnixMilliseconds{ nowMs }, verification ) == Decision::NeedKey ) // 서버 루프의 nowMs 는 유닉스 밀리초다
            {
                const bool bAlreadyWaited = pending._waitedFetchGeneration != 0;
                const bool bCanRefetch    = _jwksRequestID != 0 || _jwksFetchCount == 0 || nowMs - _lastFetchStartMs >= _settings._minRefetchMs;
                if ( bAlreadyWaited == false && bCanRefetch )
                {
                    if ( _jwksRequestID == 0 )
                        startJwksFetch( nowMs );
                    pending._waitedFetchGeneration = _fetchGeneration + 1;
                    ++pendingIndex;
                    continue;
                }
                // 받아 봤는데도 키가 없다. 받기가 실패했으면 제공자 문제이고, 받았다면 티켓이 틀린 것이다(모르는 kid).
                if ( _bLastFetchFailed == SW_TRUE )
                    verification._bUnavailable = SW_TRUE;
                else
                    verification._bRejected = SW_TRUE;
            }
            _listDone.push_back( std::move( verification ) );
            _listPending.erase( _listPending.begin() + static_cast<ptrdiff_t>( pendingIndex ) );
        }
    }

    void OidcLoginProvider::startJwksFetch( int64 nowMs )
    {
        HTTPClientRequest request;
        request._url       = _settings._jwksURL;
        request._timeoutMs = _settings._requestTimeoutMs;
        request._listHeader.push_back( HTTPHeader{ "Accept", "application/json" } );
        _jwksRequestID    = _pHTTPClient->submitRequest( request, nowMs );
        _lastFetchStartMs = nowMs;
        ++_jwksFetchCount;
    }

    OidcLoginProvider::Decision OidcLoginProvider::evaluate( const PendingTicket& pending, UnixMilliseconds now, PlatformLoginVerification& outVerification )
    {
        JwtUnverified unverified;
        if ( unverified.parse( pending._token ) == false )
        {
            outVerification._bRejected = SW_TRUE;
            return Decision::Done;
        }
        const NetPublicKey* pKey = _keyCache.findKey( unverified.getKeyID() );
        if ( pKey == nullptr )
            return Decision::NeedKey;
        // 서명과 iss · aud · exp · iat 는 `verify` 가 한 번에 본다. 여기서는 제공자마다 다른 nonce · subject 만 본다.
        const std::optional<JwtVerified> token =
            std::move( unverified ).verify( *_pProvider, *pKey, JwtClaimRule::makeIDToken( _settings._issuer, _settings._listClientID, now.count(), _settings._clockSkewMs ) );
        if ( token.has_value() == false )
        {
            outVerification._bRejected = SW_TRUE;
            return Decision::Done;
        }
        string     subject;
        string     nonce;
        const bool bNonceOk       = _settings._bRequireNonce == SW_FALSE || ( pending._nonce.empty() == false && token->findText( "nonce", nonce ) && nonce == pending._nonce );
        const bool bSubjectOk     = token->findText( _settings._subjectPath, subject ) && subject.empty() == false;
        const bool bTokenRejected = bNonceOk == false || bSubjectOk == false;
        if ( bTokenRejected )
        {
            outVerification._bRejected = SW_TRUE;
            return Decision::Done;
        }
        outVerification._subject = subject;
        if ( _settings._displayNamePath.empty() == false )
            (void)token->findText( _settings._displayNamePath, outVerification._displayName );
        return Decision::Done;
    }
} // namespace sw
