#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/Provider/Apple/AppleLoginProvider.h"

#include "Core/String/StringUtil.h"

#include "Engine/Config/Server/ServerSecret.h"
#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/JSON/JSONWriter.h"

#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Security/JSONWebToken.h"
#include "GameFramework/Runtime/Online/HTTP/HTTPClient.h"

namespace sw
{
    namespace
    {
        struct AppleLoginProviderInternal
        {
            static constexpr int64 kSecretLifetimeMs      = 24 * constant::time::kSecondsPerHour * constant::time::kMillisecondsPerSecond;
            static constexpr int64 kSecretRefreshMarginMs = constant::time::kSecondsPerHour * constant::time::kMillisecondsPerSecond;
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
            SW_LOG_ERROR_ID( "AppleLoginProvider.LoginProviderSigningKey", "Login provider '%#': the signing key environment variable '%#' is not set", settings._name, settings._keySecretEnvironment );
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
        , _innerVerifications{}
        , _verificationBuffer{}
        , _revocationBuffer{}
        , _listInnerScratch{}
        , _exchangeTable{}
        , _pSecurity{ pSecurity }
        , _pAppleHTTPClient{ pAppleHTTPClient }
        , _secretExpiresMs{ 0 }
        , _nowMs{ 0 }
        , _verificationIDAllocator{}
        , _revocationIDAllocator{}
    {
    }

    AppleLoginProvider::~AppleLoginProvider()
    {
        Memory::wipe( _signingKeyPem.data(), _signingKeyPem.size() ); // 서명 키를 메모리에 남기지 않는다
        Memory::wipe( _clientSecret.data(), _clientSecret.size() );
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

    string AppleLoginProvider::makeClientSecret( UnixMilliseconds now )
    {
        using Internal = AppleLoginProviderInternal;
        if ( _clientSecret.empty() == false && now.count() + Internal::kSecretRefreshMarginMs < _secretExpiresMs )
            return _clientSecret;
        const int64  issuedAtSec = now.toSeconds();
        const int64  expiresAtMs = now.count() + Internal::kSecretLifetimeMs;
        const string clientID    = _settings._listClientID.empty() ? string() : _settings._listClientID.front();
        JSONWriter   writer;
        writer.beginObject();
        writer.key( "iss" );
        writer.valueString( _settings._teamID );
        writer.key( "iat" );
        writer.valueInt( issuedAtSec );
        writer.key( "exp" );
        writer.valueInt( expiresAtMs / constant::time::kMillisecondsPerSecond );
        writer.key( "aud" );
        writer.valueString( kAudience );
        writer.key( "sub" );
        writer.valueString( clientID );
        writer.endObject();
        const auto payload = writer.finish();
        string     secret;
        const bool bSigned =
            payload.has_value() && JWTUtil::makeSigned( *_pSecurity, NetSignatureAlgorithm::EcdsaP256Sha256, _settings._signingKeyID, *payload, _signingKeyPem, secret );
        if ( bSigned == false )
        {
            SW_LOG_ERROR_ID( "AppleLoginProvider.CouldNotSignClient", "Could not sign the client secret of login provider '%#' with its key", _settings._name.c_str() );
            return {};
        }
        _clientSecret    = std::move( secret );
        _secretExpiresMs = expiresAtMs;
        return _clientSecret;
    }

    uint64 AppleLoginProvider::submitForm( const string& url, const vector<HTTPHeader>& listForm, UnixMilliseconds /*nowMs*/ )
    {
        const string      body = HTTPUtil::encodeForm( listForm );
        HTTPClientRequest request;
        request._method    = HTTPMethod::Post;
        request._url       = url;
        request._timeoutMs = _settings._requestTimeoutMs;
        request._bodyBytes = StringUtil::toBytes( body );
        request._listHeader.push_back( HTTPHeader{ "Content-Type", "application/x-www-form-urlencoded" } );
        request._listHeader.push_back( HTTPHeader{ "Accept", "application/json" } );
        return _pAppleHTTPClient->submitRequest( request );
    }

    uint64 AppleLoginProvider::submitVerification( const vector<uint8>& ticketBytes, UnixMilliseconds nowMs )
    {
        string idToken;
        string nonce;
        string code;
        splitTicket( string_view( reinterpret_cast<const utf8*>( ticketBytes.data() ), ticketBytes.size() ), idToken, nonce, code );
        // 안쪽 OIDC 구현은 `ID 토큰|nonce` 를 읽는다. 인가 코드는 이 제공자가 들고 있다.
        StringBuilder<constant::buffer::kMax2048> innerTicket;
        innerTicket.appendFormat( "%#", idToken );
        if ( nonce.empty() == false )
            innerTicket.appendFormat( "|%#", nonce );
        const uint64 outerID = _verificationIDAllocator.allocate();
        const uint64 innerID = _oidc.submitVerification( StringUtil::toBytes( innerTicket.view() ), nowMs );
        (void)_innerVerifications.track( innerID, 0, [this, outerID, code = std::move( code )]( const PlatformLoginVerification& inner )
        {
            PlatformLoginVerification verified = inner;
            verified._verificationID           = outerID;
            onVerified( std::move( verified ), code );
        } ); // 안쪽 번호는 겹치지 않는다
        return outerID;
    }

    void AppleLoginProvider::onVerified( PlatformLoginVerification verified, const string& code )
    {
        if ( verified.isVerified() && code.empty() == false )
            submitExchange( std::move( verified ), code );
        else
            _verificationBuffer.push( std::move( verified ) );
    }

    void AppleLoginProvider::submitExchange( PlatformLoginVerification verified, const string& code )
    {
        const string clientSecret = makeClientSecret( _nowMs );
        if ( clientSecret.empty() )
        {
            _verificationBuffer.push( std::move( verified ) ); // 연동 비밀 없이 끝낸다 — 로그인은 성공이다
            return;
        }
        vector<HTTPHeader> listForm;
        listForm.push_back( HTTPHeader{ "client_id", _settings._listClientID.front() } );
        listForm.push_back( HTTPHeader{ "client_secret", clientSecret } );
        listForm.push_back( HTTPHeader{ "code", code } );
        listForm.push_back( HTTPHeader{ "grant_type", "authorization_code" } );
        (void)_exchangeTable.track( submitForm( _tokenURL, listForm, _nowMs ), 0, [this, verified = std::move( verified )]( const HTTPClientResponse& response )
        { finishExchange( verified, response ); } ); // 요청 번호는 겹치지 않는다
    }

    void AppleLoginProvider::finishExchange( PlatformLoginVerification verified, const HTTPClientResponse& response )
    {
        JSONDocument document;
        if ( response.isSuccess() && document.tryParse( response.getBodyText() ) )
        {
            const JSONValue refreshToken = document.getRoot().get( "refresh_token" );
            if ( refreshToken.isString() )
                verified._linkSecret = refreshToken.asString();
        }
        if ( verified._linkSecret.empty() )
            SW_LOG_WARNING_ID( "AppleLoginProvider.LoginProviderCouldNot", "Login provider '%#' could not exchange the authorization code (status %#) - the account has no token to revoke later", _settings._name.c_str(),
                               response.getStatusCode() );
        _verificationBuffer.push( std::move( verified ) );
    }

    int32 AppleLoginProvider::pollVerifications( vector<PlatformLoginVerification>& outListVerification )
    {
        const int32 count = static_cast<int32>( _verificationBuffer.getCount() );
        _verificationBuffer.moveAllTo( outListVerification );
        return count;
    }

    void AppleLoginProvider::update( UnixMilliseconds nowMs )
    {
        _nowMs = nowMs;
        _oidc.update( nowMs );
        _listInnerScratch.clear();
        (void)_oidc.pollVerifications( _listInnerScratch );
        for ( const PlatformLoginVerification& inner : _listInnerScratch )
        {
            (void)_innerVerifications.answer( inner._verificationID, inner );
        }
        _pAppleHTTPClient->update();
        vector<HTTPClientResponse> listResponse;
        (void)_pAppleHTTPClient->pollResponses( listResponse );
        for ( const HTTPClientResponse& response : listResponse )
        {
            (void)_exchangeTable.answer( response._requestID, response );
        }
    }

    uint64 AppleLoginProvider::submitRevocation( string_view linkSecret, UnixMilliseconds nowMs )
    {
        const uint64 revocationID = _revocationIDAllocator.allocate();
        const string clientSecret = makeClientSecret( nowMs );
        if ( clientSecret.empty() )
        {
            _revocationBuffer.push( PlatformLoginRevocation{} );
            PlatformLoginRevocation& done = _revocationBuffer.getLast();
            done._revocationID            = revocationID;
            done._bUnavailable            = SW_TRUE;
            return revocationID;
        }
        vector<HTTPHeader> listForm;
        listForm.push_back( HTTPHeader{ "client_id", _settings._listClientID.front() } );
        listForm.push_back( HTTPHeader{ "client_secret", clientSecret } );
        listForm.push_back( HTTPHeader{ "token", string( linkSecret ) } );
        listForm.push_back( HTTPHeader{ "token_type_hint", "refresh_token" } );
        (void)_exchangeTable.track( submitForm( _revokeURL, listForm, nowMs ), 0, [this, revocationID]( const HTTPClientResponse& response )
        { finishRevocation( revocationID, response ); } ); // 요청 번호는 겹치지 않는다
        return revocationID;
    }

    void AppleLoginProvider::finishRevocation( uint64 revocationID, const HTTPClientResponse& response )
    {
        _revocationBuffer.push( PlatformLoginRevocation{} );
        PlatformLoginRevocation& done = _revocationBuffer.getLast();
        done._revocationID            = revocationID;
        if ( response.isSuccess() )
            return; // 철회했다
        const HTTPResponseKind kind = response.getKind();
        if ( kind == HTTPResponseKind::Refused && response.getBodyText().find( "invalid_grant" ) != string_view::npos )
            return; // 이미 무효인 토큰이다 — 더 철회할 것이 없다
        if ( response.isTransientFailure() == false )
            SW_LOG_ERROR_ID( "AppleLoginProvider.LoginProviderRejectedRevocation", "Login provider '%#' rejected the revocation (status %#) - check teamId, signingKeyId and the signing key", _settings._name.c_str(), response.getStatusCode() );
        done._bUnavailable = SW_TRUE;
    }

    int32 AppleLoginProvider::pollRevocations( vector<PlatformLoginRevocation>& outListRevocation )
    {
        const int32 count = static_cast<int32>( _revocationBuffer.getCount() );
        _revocationBuffer.moveAllTo( outListRevocation );
        return count;
    }
} // namespace sw
