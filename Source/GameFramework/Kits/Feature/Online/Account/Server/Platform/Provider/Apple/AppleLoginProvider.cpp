#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/Provider/Apple/AppleLoginProvider.h"

#include "Engine/Config/Server/ServerSecret.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

#include "GameFramework/Base/Online/HTTP/HTTPClient.h"
#include "GameFramework/Base/Online/Security/JSONWebToken.h"

namespace sw
{
    SW_LOG_CALLER( "AppleLoginProvider" );

    namespace
    {
        struct AppleLoginProviderInternal
        {
            static constexpr int64 kSecretLifetimeMs      = 24 * constant::kSecondsPerHour * constant::kMillisecondsPerSecond;
            static constexpr int64 kSecretRefreshMarginMs = constant::kSecondsPerHour * constant::kMillisecondsPerSecond;
            static constexpr int32 kStatusBadRequest      = 400;

            static vector<uint8> toBytes( string_view text )
            {
                return vector<uint8>( reinterpret_cast<const uint8*>( text.data() ), reinterpret_cast<const uint8*>( text.data() ) + text.size() );
            }

            static string quote( string_view text ) { return "\"" + JSONDocument::escapeString( text ) + "\""; }
        };
    } // namespace
} // namespace sw

namespace sw
{
    unique_ptr<IPlatformLoginProvider> AppleLoginProvider::create( const PlatformLoginProviderConfig& settings, INetSecurityProvider* pSecurity, HTTPClient* pOidcHTTPClient,
                                                                   HTTPClient* pAppleHTTPClient )
    {
        string signingKeyPem;
        if ( ServerSecret::read( settings._keySecretEnvironment, signingKeyPem ) == false || signingKeyPem.empty() )
        {
            SW_LOG_ERROR( "Login provider '%#': the signing key environment variable '%#' is not set", settings._name, settings._keySecretEnvironment );
            return nullptr;
        }
        return make_unique<AppleLoginProvider>( settings, std::move( signingKeyPem ), pSecurity, pOidcHTTPClient, pAppleHTTPClient );
    }

    AppleLoginProvider::AppleLoginProvider( const PlatformLoginProviderConfig& settings, string signingKeyPem, INetSecurityProvider* pSecurity, HTTPClient* pOidcHTTPClient,
                                            HTTPClient* pAppleHTTPClient )
        : _settings{ settings }
        , _oidc{ settings, pSecurity, pOidcHTTPClient }
        , _signingKeyPem{ std::move( signingKeyPem ) }
        , _clientSecret{}
        , _tokenURL{ settings._tokenURL.empty() ? string( kDefaultTokenURL ) : settings._tokenURL }
        , _revokeURL{ settings._revokeURL.empty() ? string( kDefaultRevokeURL ) : settings._revokeURL }
        , _listPending{}
        , _listDone{}
        , _listRevocationDone{}
        , _listInnerScratch{}
        , _mapRequestToRevocation{}
        , _pSecurity{ pSecurity }
        , _pAppleHTTPClient{ pAppleHTTPClient }
        , _secretExpiresMs{ 0 }
        , _nextVerificationID{ 1 }
        , _nextRevocationID{ 1 }
    {
    }

    AppleLoginProvider::~AppleLoginProvider()
    {
        Memory::set( _signingKeyPem.data(), 0, _signingKeyPem.size() ); // 서명 키를 메모리에 남기지 않는다
        Memory::set( _clientSecret.data(), 0, _clientSecret.size() );
    }

    void AppleLoginProvider::splitTicket( string_view ticket, string& outIDToken, string& outNonce, string& outCode )
    {
        outIDToken.clear();
        outNonce.clear();
        outCode.clear();
        const size_t firstBar = ticket.find( '|' );
        outIDToken            = string( ticket.substr( 0, firstBar ) );
        if ( firstBar == string_view::npos )
            return;
        const string_view rest      = ticket.substr( firstBar + 1 );
        const size_t      secondBar = rest.find( '|' );
        outNonce                    = string( rest.substr( 0, secondBar ) );
        if ( secondBar != string_view::npos )
            outCode = string( rest.substr( secondBar + 1 ) );
    }

    string AppleLoginProvider::makeClientSecret( int64 nowMs )
    {
        using Internal = AppleLoginProviderInternal;
        if ( _clientSecret.empty() == false && nowMs + Internal::kSecretRefreshMarginMs < _secretExpiresMs )
            return _clientSecret;
        const int64  issuedAtSec = nowMs / constant::kMillisecondsPerSecond;
        const int64  expiresAtMs = nowMs + Internal::kSecretLifetimeMs;
        const string clientID    = _settings._listClientID.empty() ? string() : _settings._listClientID.front();
        const string payload     = "{\"iss\":" + Internal::quote( _settings._teamID ) + ",\"iat\":" + to_string( issuedAtSec ) +
                               ",\"exp\":" + to_string( expiresAtMs / constant::kMillisecondsPerSecond ) + ",\"aud\":" + Internal::quote( kAudience ) +
                               ",\"sub\":" + Internal::quote( clientID ) + "}";
        string secret;
        if ( JSONWebToken::makeSigned( *_pSecurity, NetSignatureAlgorithm::EcdsaP256Sha256, _settings._signingKeyID, payload, _signingKeyPem, secret ) == false )
        {
            SW_LOG_ERROR( "Could not sign the client secret of login provider '%#' with its key", _settings._name.c_str() );
            return {};
        }
        _clientSecret    = std::move( secret );
        _secretExpiresMs = expiresAtMs;
        return _clientSecret;
    }

    uint64 AppleLoginProvider::submitForm( const string& url, const vector<HTTPHeader>& listForm, int64 nowMs )
    {
        const string      body = HTTPUtil::encodeForm( listForm );
        HTTPClientRequest request;
        request._method    = HTTPMethod::Post;
        request._url       = url;
        request._timeoutMs = _settings._requestTimeoutMs;
        request._bodyBytes = AppleLoginProviderInternal::toBytes( body );
        request._listHeader.push_back( HTTPHeader{ "Content-Type", "application/x-www-form-urlencoded" } );
        request._listHeader.push_back( HTTPHeader{ "Accept", "application/json" } );
        return _pAppleHTTPClient->submitRequest( request, nowMs );
    }

    uint64 AppleLoginProvider::submitVerification( const vector<uint8>& ticketBytes, int64 nowMs )
    {
        string idToken;
        string nonce;
        string code;
        splitTicket( string_view( reinterpret_cast<const utf8*>( ticketBytes.data() ), ticketBytes.size() ), idToken, nonce, code );
        // 안쪽 OIDC 구현은 `ID 토큰|nonce` 를 읽는다. 인가 코드는 이 제공자가 들고 있다.
        const string         innerTicket = nonce.empty() ? idToken : idToken + "|" + nonce;
        PendingVerification& pending     = _listPending.emplace_back();
        pending._outerID                 = _nextVerificationID++;
        pending._code                    = std::move( code );
        pending._innerID                 = _oidc.submitVerification( AppleLoginProviderInternal::toBytes( innerTicket ), nowMs );
        return pending._outerID;
    }

    void AppleLoginProvider::submitExchange( PendingVerification& pending, int64 nowMs )
    {
        const string clientSecret = makeClientSecret( nowMs );
        if ( clientSecret.empty() )
        {
            _listDone.push_back( pending._verified ); // 연동 비밀 없이 끝낸다 — 로그인은 성공이다
            pending._outerID = 0;
            return;
        }
        vector<HTTPHeader> listForm;
        listForm.push_back( HTTPHeader{ "client_id", _settings._listClientID.front() } );
        listForm.push_back( HTTPHeader{ "client_secret", clientSecret } );
        listForm.push_back( HTTPHeader{ "code", pending._code } );
        listForm.push_back( HTTPHeader{ "grant_type", "authorization_code" } );
        pending._exchangeRequestID = submitForm( _tokenURL, listForm, nowMs );
    }

    void AppleLoginProvider::finishExchange( PendingVerification& pending, const HTTPClientResponse& response )
    {
        JSONDocument document;
        if ( response.isSuccess() && document.tryParse( response.getBodyText() ) )
        {
            const JSONValue refreshToken = document.getRoot().get( "refresh_token", false );
            if ( refreshToken.isString() )
                pending._verified._linkSecret = refreshToken.asString();
        }
        if ( pending._verified._linkSecret.empty() )
            SW_LOG_WARNING( "Login provider '%#' could not exchange the authorization code (status %#) - the account has no token to revoke later", _settings._name.c_str(),
                            response._statusCode );
        _listDone.push_back( std::move( pending._verified ) );
        pending._outerID = 0;
    }

    int32 AppleLoginProvider::pollVerifications( vector<PlatformLoginVerification>& outListVerification )
    {
        const int32 count = static_cast<int32>( _listDone.size() );
        for ( PlatformLoginVerification& verification : _listDone )
        {
            outListVerification.push_back( std::move( verification ) );
        }
        _listDone.clear();
        return count;
    }

    void AppleLoginProvider::update( int64 nowMs )
    {
        _oidc.update( nowMs );
        _listInnerScratch.clear();
        (void)_oidc.pollVerifications( _listInnerScratch );
        for ( PlatformLoginVerification& inner : _listInnerScratch )
        {
            for ( PendingVerification& pending : _listPending )
            {
                const bool bAwaitingInner = pending._outerID != 0 && pending._exchangeRequestID == 0;
                if ( bAwaitingInner == false || pending._innerID != inner._verificationID )
                    continue;
                inner._verificationID = pending._outerID;
                pending._verified     = std::move( inner );
                if ( pending._verified.isVerified() && pending._code.empty() == false )
                    submitExchange( pending, nowMs );
                else
                {
                    _listDone.push_back( std::move( pending._verified ) );
                    pending._outerID = 0;
                }
                break;
            }
        }
        _pAppleHTTPClient->update( nowMs );
        vector<HTTPClientResponse> listResponse;
        (void)_pAppleHTTPClient->pollResponses( listResponse );
        for ( const HTTPClientResponse& response : listResponse )
        {
            const auto revocationIt = _mapRequestToRevocation.find( response._requestID );
            if ( revocationIt != _mapRequestToRevocation.end() )
            {
                const uint64 revocationID = revocationIt->second;
                _mapRequestToRevocation.erase( revocationIt );
                finishRevocation( revocationID, response );
                continue;
            }
            for ( PendingVerification& pending : _listPending )
            {
                const bool bExchanging = pending._outerID != 0 && pending._exchangeRequestID != 0;
                if ( bExchanging && pending._exchangeRequestID == response._requestID )
                {
                    finishExchange( pending, response );
                    break;
                }
            }
        }
        for ( size_t index = 0; index < _listPending.size(); )
        {
            if ( _listPending[index]._outerID == 0 )
                _listPending.erase( _listPending.begin() + static_cast<ptrdiff_t>( index ) );
            else
                ++index;
        }
    }

    uint64 AppleLoginProvider::submitRevocation( string_view linkSecret, int64 nowMs )
    {
        const uint64 revocationID = _nextRevocationID++;
        const string clientSecret = makeClientSecret( nowMs );
        if ( clientSecret.empty() )
        {
            PlatformLoginRevocation& done = _listRevocationDone.emplace_back();
            done._revocationID            = revocationID;
            done._bUnavailable            = SW_TRUE;
            return revocationID;
        }
        vector<HTTPHeader> listForm;
        listForm.push_back( HTTPHeader{ "client_id", _settings._listClientID.front() } );
        listForm.push_back( HTTPHeader{ "client_secret", clientSecret } );
        listForm.push_back( HTTPHeader{ "token", string( linkSecret ) } );
        listForm.push_back( HTTPHeader{ "token_type_hint", "refresh_token" } );
        _mapRequestToRevocation[submitForm( _revokeURL, listForm, nowMs )] = revocationID;
        return revocationID;
    }

    void AppleLoginProvider::finishRevocation( uint64 revocationID, const HTTPClientResponse& response )
    {
        using Internal                = AppleLoginProviderInternal;
        PlatformLoginRevocation& done = _listRevocationDone.emplace_back();
        done._revocationID            = revocationID;
        if ( response.isSuccess() )
            return; // 철회했다
        const bool bInvalidGrant = response._bTransportFailed == SW_FALSE && response._statusCode == Internal::kStatusBadRequest &&
                                   response.getBodyText().find( "invalid_grant" ) != string_view::npos;
        if ( bInvalidGrant )
            return; // 이미 무효인 토큰이다 — 더 철회할 것이 없다
        if ( response._bTransportFailed == SW_FALSE && response._statusCode == Internal::kStatusBadRequest )
            SW_LOG_ERROR( "Login provider '%#' rejected the revocation (status %#) - check teamId, signingKeyId and the signing key", _settings._name.c_str(), response._statusCode );
        done._bUnavailable = SW_TRUE;
    }

    int32 AppleLoginProvider::pollRevocations( vector<PlatformLoginRevocation>& outListRevocation )
    {
        const int32 count = static_cast<int32>( _listRevocationDone.size() );
        outListRevocation.insert( outListRevocation.end(), _listRevocationDone.begin(), _listRevocationDone.end() );
        _listRevocationDone.clear();
        return count;
    }
} // namespace sw
