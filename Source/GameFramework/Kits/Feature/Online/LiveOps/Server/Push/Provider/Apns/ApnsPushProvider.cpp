#include "pch.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/Provider/Apns/ApnsPushProvider.h"

#include "Core/Container/StringUtil.h"
#include "Core/Network/Security/INetSecurityProvider.h"

#include "Engine/Serialization/JSON/JSONWriter.h"

#include "GameFramework/Base/Online/HTTP/HTTP2Client.h"
#include "GameFramework/Base/Online/Security/JSONWebToken.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/PushPayloadUtil.h"

namespace sw
{
    SW_LOG_CALLER( "ApnsPushProvider" );

    namespace
    {
        struct ApnsPushProviderInternal
        {
            static constexpr int32       kStatusGone            = 410;
            static constexpr int32       kStatusTooManyRequests = 429;
            static constexpr int32       kStatusServerError     = 500;
            static constexpr int64       kMillisecondsPerSecond = 1000;
            static constexpr size_t      kMaxDeviceTokenSize    = 200;
            static constexpr const utf8* kPushTypeAlert         = "alert";
            static constexpr const utf8* kPushTypeBackground    = "background";
            static constexpr const utf8* kBackgroundPriority    = "5"; ///< 조용한 알림은 우선순위 5 여야 한다(APNs 규칙)
        };
    } // namespace
} // namespace sw

namespace sw
{
    ApnsPushProvider::ApnsPushProvider( ApnsPushSettings settings, INetSecurityProvider* pSecurity, HTTP2Client* pHTTPClient )
        : _settings{ std::move( settings ) }
        , _providerToken{}
        , _deviceURLPrefix{}
        , _listDone{}
        , _mapRequestToDelivery{}
        , _pSecurity{ pSecurity }
        , _pHTTPClient{ pHTTPClient }
        , _tokenIssuedMs{ 0 }
    {
        _deviceURLPrefix = _settings._baseURL + "/3/device/";
    }

    ApnsPushProvider::~ApnsPushProvider()
    {
        Memory::set( _settings._privateKeyPem.data(), 0, _settings._privateKeyPem.size() ); // 키가 든 글을 남기지 않는다
    }

    bool ApnsPushProvider::hasAlert( const PushNotificationMessage& message )
    {
        return message._titleKey.empty() == false || message._bodyKey.empty() == false;
    }

    bool ApnsPushProvider::isValidDeviceToken( string_view token )
    {
        if ( token.empty() || token.size() > ApnsPushProviderInternal::kMaxDeviceTokenSize )
            return false;
        for ( const utf8 character : token )
        {
            const bool bHex = ( character >= '0' && character <= '9' ) || ( character >= 'a' && character <= 'f' ) || ( character >= 'A' && character <= 'F' );
            if ( bHex == false )
                return false;
        }
        return true;
    }

    string ApnsPushProvider::makePayloadJSON( const PushNotificationMessage& message )
    {
        const int32 argumentCount = PushPayloadUtil::countArguments( message );
        string      json;
        JSONWriter  writer{ json };
        writer.beginObject();
        writer.key( "aps" );
        writer.beginObject();
        if ( hasAlert( message ) )
        {
            writer.key( "alert" );
            writer.beginObject();
            PushPayloadUtil::writeOptionalText( writer, "title-loc-key", message._titleKey );
            PushPayloadUtil::writeOptionalText( writer, "loc-key", message._bodyKey );
            PushPayloadUtil::writeArguments( writer, "loc-args", message, argumentCount );
            writer.endObject();
            if ( message._badgeCount >= 0 )
            {
                writer.key( "badge" );
                writer.valueInt( message._badgeCount );
            }
        }
        else
        {
            writer.key( "content-available" ); // 조용한 알림 — 앱이 깨어나 스스로 갱신한다
            writer.valueInt( 1 );
        }
        writer.endObject();
        PushPayloadUtil::writeOptionalText( writer, "deep_link", message._deepLink );
        PushPayloadUtil::writeOptionalText( writer, "title_key", message._titleKey );
        PushPayloadUtil::writeOptionalText( writer, "body_key", message._bodyKey );
        for ( int32 index = 0; index < argumentCount; ++index )
        {
            writer.key( "arg" + to_string( index ) );
            writer.valueString( message._arrArgument[index] );
        }
        writer.endObject();
        return json;
    }

    PushDeliveryStatus ApnsPushProvider::mapStatus( int32 statusCode, bool bTransportFailed )
    {
        using Internal = ApnsPushProviderInternal;
        if ( bTransportFailed )
            return PushDeliveryStatus::Transient;
        if ( 200 <= statusCode && statusCode <= 299 )
            return PushDeliveryStatus::Delivered;
        if ( statusCode == Internal::kStatusGone )
            return PushDeliveryStatus::InvalidToken;
        if ( statusCode == Internal::kStatusTooManyRequests )
            return PushDeliveryStatus::RateLimited;
        if ( statusCode >= Internal::kStatusServerError )
            return PushDeliveryStatus::Transient;
        return PushDeliveryStatus::Rejected; // 403(제공자 토큰)과 그 밖의 4xx
    }

    bool ApnsPushProvider::ensureProviderToken( UnixMilliseconds nowMs )
    {
        if ( _providerToken.empty() == false && nowMs.count() - _tokenIssuedMs < kTokenRefreshMs )
            return true;
        JSONWriter writer;
        writer.beginObject();
        writer.key( "iss" );
        writer.valueString( _settings._teamID );
        writer.key( "iat" );
        writer.valueInt( nowMs.toSeconds() );
        writer.endObject();
        const std::optional<JSONText> payload = writer.finish();
        string                        token;
        const bool                    bSigned = payload.has_value() &&
                             JWTUtil::makeSigned( *_pSecurity, NetSignatureAlgorithm::EcdsaP256Sha256, _settings._keyID, *payload, _settings._privateKeyPem, token );
        if ( bSigned == false )
        {
            SW_LOG_ERROR( "Could not sign the APNs provider token with the key '%#' - check the authentication key", _settings._keyID.c_str() );
            return false;
        }
        _providerToken = std::move( token );
        _tokenIssuedMs = nowMs.count();
        return true;
    }

    void ApnsPushProvider::send( uint64 deliveryID, const string& deviceToken, const string& locale, const PushNotificationMessage& message, UnixMilliseconds nowMs )
    {
        using Internal = ApnsPushProviderInternal;
        (void)locale; // 문구는 현지화 키라 기기가 푼다
        if ( isValidDeviceToken( deviceToken ) == false )
        {
            complete( deliveryID, PushDeliveryStatus::InvalidToken ); // 경로에 넣을 수 없는 토큰이다 — 보내지 않는다
            return;
        }
        if ( ensureProviderToken( nowMs ) == false )
        {
            complete( deliveryID, PushDeliveryStatus::Rejected );
            return;
        }
        const bool        bAlert = hasAlert( message );
        HTTPClientRequest request;
        request._method    = HTTPMethod::Post;
        request._url       = _deviceURLPrefix + deviceToken;
        request._timeoutMs = _settings._requestTimeoutMs;
        request._bodyBytes = PushPayloadUtil::toBytes( makePayloadJSON( message ) );
        request._listHeader.push_back( HTTPHeader{ "authorization", "bearer " + _providerToken } );
        request._listHeader.push_back( HTTPHeader{ "apns-topic", _settings._topic } );
        request._listHeader.push_back( HTTPHeader{ "apns-push-type", bAlert ? Internal::kPushTypeAlert : Internal::kPushTypeBackground } );
        if ( bAlert == false )
            request._listHeader.push_back( HTTPHeader{ "apns-priority", Internal::kBackgroundPriority } );
        _mapRequestToDelivery[_pHTTPClient->submitRequest( request )] = deliveryID;
    }

    void ApnsPushProvider::finishSend( uint64 deliveryID, const HTTPClientResponse& response )
    {
        using Internal                        = ApnsPushProviderInternal;
        const PushDeliveryStatus status       = mapStatus( response._statusCode, response._bTransportFailed == SW_TRUE );
        int64                    retryAfterMs = 0;
        if ( const HTTPHeader* pRetryAfter = response.findHeader( "retry-after" ) )
        {
            int64 seconds = 0;
            if ( StringUtil::parseInt64( pRetryAfter->_value, seconds ) && seconds > 0 )
                retryAfterMs = seconds * Internal::kMillisecondsPerSecond;
        }
        if ( response._statusCode == HTTPConstant::kStatusForbidden )
        {
            // 제공자 토큰이 거절됐다(만료 · 서명 불일치) — 버려서 다음 배달이 새로 서명하게 한다.
            _providerToken.clear();
            SW_LOG_WARNING( "APNs rejected the provider token (status %#) - it will be signed again", response._statusCode );
        }
        complete( deliveryID, status, retryAfterMs );
    }

    void ApnsPushProvider::complete( uint64 deliveryID, PushDeliveryStatus status, int64 retryAfterMs )
    {
        PushDeliveryResult& result = _listDone.emplace_back();
        result._deliveryID         = deliveryID;
        result._status             = status;
        result._retryAfterMs       = retryAfterMs;
    }

    void ApnsPushProvider::update( UnixMilliseconds nowMs )
    {
        (void)nowMs;
        _pHTTPClient->update();
        vector<HTTPClientResponse> listResponse;
        (void)_pHTTPClient->pollResponses( listResponse );
        for ( const HTTPClientResponse& response : listResponse )
        {
            const auto requestIt = _mapRequestToDelivery.find( response._requestID );
            if ( requestIt == _mapRequestToDelivery.end() )
                continue;
            const uint64 deliveryID = requestIt->second;
            _mapRequestToDelivery.erase( requestIt );
            finishSend( deliveryID, response );
        }
    }

    int32 ApnsPushProvider::pollResults( vector<PushDeliveryResult>& outListResult )
    {
        const int32 count = static_cast<int32>( _listDone.size() );
        outListResult.insert( outListResult.end(), _listDone.begin(), _listDone.end() );
        _listDone.clear();
        return count;
    }
} // namespace sw
