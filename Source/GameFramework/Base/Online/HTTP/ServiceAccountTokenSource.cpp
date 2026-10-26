#include "pch.h"

#include "GameFramework/Base/Online/HTTP/ServiceAccountTokenSource.h"

#include "Core/Network/Security/INetSecurityProvider.h"

#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/JSON/JSONWriter.h"

#include "GameFramework/Base/Online/HTTP/HTTPClient.h"
#include "GameFramework/Base/Online/Security/JSONWebToken.h"

namespace sw
{
    SW_LOG_CALLER( "ServiceAccountTokenSource" );

    namespace
    {
        struct ServiceAccountTokenSourceInternal
        {
            static constexpr int32 kStatusTooManyRequests = 429;
            static constexpr int32 kStatusServerError     = 500;
            static constexpr int64 kMillisecondsPerSecond = 1000;

            [[nodiscard]] static bool readText( const JSONValue& object, const utf8* pKey, string& outText )
            {
                const JSONValue value = object.get( pKey );
                if ( value.isString() == false || value.asString().empty() )
                    return false;
                outText = value.asString();
                return true;
            }

            /** @brief 없어도 되는 글 칸을 읽습니다. 없으면 @p outText 를 그대로 둡니다. */
            static void readOptionalText( const JSONValue& object, const utf8* pKey, string& outText )
            {
                const JSONValue value = object.get( pKey );
                if ( value.isString() && value.asString().empty() == false )
                    outText = value.asString();
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool ServiceAccountCredentials::read( string_view json, ServiceAccountCredentials& outCredentials, string& outError )
    {
        using Internal = ServiceAccountTokenSourceInternal;
        JSONDocument document;
        if ( document.tryParse( json ) == false || document.getRoot().isObject() == false )
        {
            outError = "the service account is not a JSON object";
            return false;
        }
        const JSONValue root        = document.getRoot();
        const bool      bHasProject = Internal::readText( root, "project_id", outCredentials._projectID );
        const bool      bHasEmail   = Internal::readText( root, "client_email", outCredentials._clientEmail );
        const bool      bHasKey     = Internal::readText( root, "private_key", outCredentials._privateKeyPem );
        const bool      bComplete   = bHasProject && bHasEmail && bHasKey;
        if ( bComplete == false )
        {
            outError = "the service account needs project_id, client_email and private_key";
            return false;
        }
        Internal::readOptionalText( root, "private_key_id", outCredentials._keyID ); // 없어도 서명은 된다
        Internal::readOptionalText( root, "token_uri", outCredentials._tokenURL );   // 없으면 구글의 기본 주소를 쓴다
        return true;
    }

    void ServiceAccountCredentials::wipe()
    {
        Memory::set( _privateKeyPem.data(), 0, _privateKeyPem.size() );
        _privateKeyPem.clear();
    }

    ServiceAccountTokenSource::ServiceAccountTokenSource( ServiceAccountCredentials credentials, string scope, INetSecurityProvider* pSecurity, HTTPClient* pHTTPClient,
                                                          int64 requestTimeoutMs )
        : _credentials{ std::move( credentials ) }
        , _scope{ std::move( scope ) }
        , _accessToken{}
        , _pSecurity{ pSecurity }
        , _pHTTPClient{ pHTTPClient }
        , _requestTimeoutMs{ requestTimeoutMs }
        , _expiresMs{ 0 }
        , _requestID{ 0 }
    {
    }

    ServiceAccountTokenSource::~ServiceAccountTokenSource()
    {
        _credentials.wipe(); // 서명 키와 토큰을 메모리에 남기지 않는다
        Memory::set( _accessToken.data(), 0, _accessToken.size() );
    }

    bool ServiceAccountTokenSource::request( UnixMilliseconds now )
    {
        using Internal = ServiceAccountTokenSourceInternal;
        if ( _requestID != 0 )
            return true;
        const int64 issuedAtSec = now.toSeconds();
        const int64 expiresSec  = issuedAtSec + kJwtLifetimeMs / Internal::kMillisecondsPerSecond;
        JSONWriter  writer;
        writer.beginObject();
        writer.key( "iss" );
        writer.valueString( _credentials._clientEmail );
        writer.key( "scope" );
        writer.valueString( _scope );
        writer.key( "aud" );
        writer.valueString( _credentials._tokenURL );
        writer.key( "iat" );
        writer.valueInt( issuedAtSec );
        writer.key( "exp" );
        writer.valueInt( expiresSec );
        writer.endObject();
        const std::optional<JSONText> payload = writer.finish();
        string                        assertion;
        const bool                    bSigned = payload.has_value() &&
                             JwtUtil::makeSigned( *_pSecurity, NetSignatureAlgorithm::RsaPkcs1Sha256, _credentials._keyID, *payload, _credentials._privateKeyPem, assertion );
        if ( bSigned == false )
        {
            SW_LOG_ERROR( "Could not sign the token request of service account '%#' with its key", _credentials._clientEmail.c_str() );
            return false;
        }
        vector<HTTPHeader> listForm;
        listForm.push_back( HTTPHeader{ "grant_type", "urn:ietf:params:oauth:grant-type:jwt-bearer" } );
        listForm.push_back( HTTPHeader{ "assertion", assertion } );
        const string      body = HTTPUtil::encodeForm( listForm );
        HTTPClientRequest httpRequest;
        httpRequest._method    = HTTPMethod::Post;
        httpRequest._url       = _credentials._tokenURL;
        httpRequest._timeoutMs = _requestTimeoutMs;
        httpRequest._bodyBytes.assign( reinterpret_cast<const uint8*>( body.data() ), reinterpret_cast<const uint8*>( body.data() ) + body.size() );
        httpRequest._listHeader.push_back( HTTPHeader{ "Content-Type", "application/x-www-form-urlencoded" } );
        _requestID = _pHTTPClient->submitRequest( httpRequest, now.count() );
        return true;
    }

    ServiceAccountTokenSource::Outcome ServiceAccountTokenSource::handleResponse( const HTTPClientResponse& response, UnixMilliseconds now )
    {
        using Internal = ServiceAccountTokenSourceInternal;
        if ( _requestID == 0 || response._requestID != _requestID )
            return Outcome::None;
        _requestID = 0;
        JSONDocument document;
        string       token;
        int64        expiresInSec = 0;
        const bool   bGranted     = response.isSuccess() && document.tryParse( response.getBodyText() ) && Internal::readText( document.getRoot(), "access_token", token ) &&
                              document.getRoot().readOptionalInto( "expires_in", expiresInSec ) && expiresInSec > 0;
        if ( bGranted )
        {
            Memory::set( _accessToken.data(), 0, _accessToken.size() );
            _accessToken = std::move( token );
            _expiresMs   = now.count() + expiresInSec * Internal::kMillisecondsPerSecond;
            return Outcome::Granted;
        }
        SW_LOG_WARNING( "The access token request of service account '%#' failed (status %#)", _credentials._clientEmail.c_str(), response._statusCode );
        // 서버 오류 · 한도 · 전송 실패(그리고 읽을 수 없는 성공 응답)는 곧 나아질 수 있고, 그 밖의 4xx 는 키나 범위가 틀린 것이라 다시 해도 같다.
        const bool bRetryable = response.isSuccess() || response._bTransportFailed == SW_TRUE || response._statusCode == Internal::kStatusTooManyRequests ||
                                response._statusCode >= Internal::kStatusServerError;
        return bRetryable ? Outcome::Retryable : Outcome::Rejected;
    }

    void ServiceAccountTokenSource::invalidate()
    {
        Memory::set( _accessToken.data(), 0, _accessToken.size() );
        _accessToken.clear();
        _expiresMs = 0;
    }
} // namespace sw
