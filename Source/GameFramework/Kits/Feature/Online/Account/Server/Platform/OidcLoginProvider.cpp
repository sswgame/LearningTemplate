#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/OidcLoginProvider.h"

#include "GameFramework/Base/Online/Http/HttpClient.h"

namespace sw
{
    SW_LOG_CALLER( "OidcLoginProvider" );
} // namespace sw

namespace sw
{
    OidcLoginProvider::OidcLoginProvider( const PlatformLoginProviderSettings& settings, INetSecurityProvider* pProvider, HttpClient* pHttpClient )
        : _settings{ settings }
        , _keyCache{}
        , _listPending{}
        , _listDone{}
        , _pProvider{ pProvider }
        , _pHttpClient{ pHttpClient }
        , _nextVerificationId{ 1 }
        , _jwksRequestId{ 0 }
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
        pending._verificationId   = _nextVerificationId++;
        return pending._verificationId;
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

    void OidcLoginProvider::tick( int64 nowMs )
    {
        _pHttpClient->tick( nowMs );
        vector<HttpClientResponse> listResponse;
        (void)_pHttpClient->pollResponses( listResponse );
        for ( const HttpClientResponse& response : listResponse )
        {
            if ( response._requestId != _jwksRequestId )
                continue;
            const bool bReplaced = response.isSuccess() && _keyCache.replaceFromJwks( response.getBodyText(), nowMs );
            if ( bReplaced == false )
                SW_LOG_WARNING( "JWKS fetch for '%#' failed: %#", _settings._name.c_str(), response._bTransportFailed == SW_TRUE ? response._failureText.c_str() : "bad response" );
            _bLastFetchFailed = bReplaced ? SW_FALSE : SW_TRUE;
            _jwksRequestId    = 0;
            ++_fetchGeneration;
        }

        const bool bStale     = _keyCache.hasKeys() && nowMs - _keyCache.getFetchedAtMs() >= _settings._jwksRefreshMs;
        const bool bMayFetch  = _jwksFetchCount == 0 || nowMs - _lastFetchStartMs >= _settings._minRefetchMs;
        const bool bWantFirst = _keyCache.hasKeys() == false && _listPending.empty() == false;
        if ( _jwksRequestId == 0 && ( bStale || bWantFirst ) && bMayFetch )
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
            verification._verificationId = pending._verificationId;
            if ( evaluate( pending, nowMs, verification ) == Decision::NeedKey )
            {
                const bool bAlreadyWaited = pending._waitedFetchGeneration != 0;
                const bool bCanRefetch    = _jwksRequestId != 0 || _jwksFetchCount == 0 || nowMs - _lastFetchStartMs >= _settings._minRefetchMs;
                if ( bAlreadyWaited == false && bCanRefetch )
                {
                    if ( _jwksRequestId == 0 )
                        startJwksFetch( nowMs );
                    pending._waitedFetchGeneration = _fetchGeneration + 1;
                    ++pendingIndex;
                    continue;
                }
                // 받아 봤는데도 키가 없다 — 받기가 실패했으면 제공자 문제, 받았으면 표가 틀렸다(모르는 kid).
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
        HttpClientRequest request;
        request._url       = _settings._jwksURL;
        request._timeoutMs = _settings._requestTimeoutMs;
        request._listHeader.push_back( HttpHeader{ "Accept", "application/json" } );
        _jwksRequestId    = _pHttpClient->submitRequest( request, nowMs );
        _lastFetchStartMs = nowMs;
        ++_jwksFetchCount;
    }

    OidcLoginProvider::Decision OidcLoginProvider::evaluate( const PendingTicket& pending, int64 nowMs, PlatformLoginVerification& outVerification )
    {
        JsonWebToken token;
        if ( token.parse( pending._token ) == false )
        {
            outVerification._bRejected = SW_TRUE;
            return Decision::Done;
        }
        const NetPublicKey* pKey = _keyCache.findKey( token.getKeyId() );
        if ( pKey == nullptr )
            return Decision::NeedKey;
        if ( token.verifySignature( *_pProvider, *pKey ) == false )
        {
            outVerification._bRejected = SW_TRUE;
            return Decision::Done;
        }
        string issuer;
        string subject;
        string nonce;
        int64  expiresAt = 0;
        int64  issuedAt  = 0;
        bool   bAudience = false;
        for ( const string& clientId : _settings._listClientId )
        {
            bAudience = bAudience || token.hasAudience( clientId );
        }
        const bool bIssuerOk  = token.findText( "iss", issuer ) && issuer == _settings._issuer;
        const bool bExpiresOk = token.findInteger( "exp", expiresAt ) && nowMs < expiresAt * 1000 + _settings._clockSkewMs;
        const bool bIssuedOk  = token.findInteger( "iat", issuedAt ) == false || issuedAt * 1000 <= nowMs + _settings._clockSkewMs;
        const bool bNonceOk   = _settings._bRequireNonce == SW_FALSE || ( pending._nonce.empty() == false && token.findText( "nonce", nonce ) && nonce == pending._nonce );
        const bool bSubjectOk = token.findText( _settings._subjectPath, subject ) && subject.empty() == false;
        if ( bAudience == false || bIssuerOk == false || bExpiresOk == false || bIssuedOk == false || bNonceOk == false || bSubjectOk == false )
        {
            outVerification._bRejected = SW_TRUE;
            return Decision::Done;
        }
        outVerification._subject = subject;
        if ( _settings._displayNamePath.empty() == false )
            (void)token.findText( _settings._displayNamePath, outVerification._displayName );
        return Decision::Done;
    }
} // namespace sw
