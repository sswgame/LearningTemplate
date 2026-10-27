#include "pch.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/Provider/Fcm/FcmPushProvider.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Serialization/JSON/JSONWriter.h"

#include "GameFramework/Base/Online/HTTP/HTTPClient.h"

namespace sw
{
    SW_LOG_CALLER( "FcmPushProvider" );

    namespace
    {
        struct FcmPushProviderInternal
        {
            static constexpr int32 kStatusTooManyRequests = 429;
            static constexpr int32 kStatusServerError     = 500;
            static constexpr int64 kMillisecondsPerSecond = 1000;

            static vector<uint8> toBytes( string_view text )
            {
                return vector<uint8>( reinterpret_cast<const uint8*>( text.data() ), reinterpret_cast<const uint8*>( text.data() ) + text.size() );
            }

            /** @brief 알림의 앞쪽에서 비지 않은 인자 수입니다(첫 빈 인자에서 멈춘다). */
            static int32 countArguments( const PushNotificationMessage& message )
            {
                int32 count = 0;
                while ( count < PushNotificationMessage::kArgumentCount && message._arrArgument[count].empty() == false )
                {
                    ++count;
                }
                return count;
            }

            /** @brief 글이 비지 않았을 때만 멤버 하나를 씁니다. */
            static void writeOptionalText( JSONWriter& writer, string_view key, string_view text )
            {
                if ( text.empty() )
                    return;
                writer.key( key );
                writer.valueString( text );
            }

            /** @brief 앞쪽 인자 @p count 개를 글 배열 멤버로 씁니다. 없으면 멤버를 쓰지 않습니다. */
            static void writeArguments( JSONWriter& writer, string_view key, const PushNotificationMessage& message, int32 count )
            {
                if ( count == 0 )
                    return;
                writer.key( key );
                writer.beginArray();
                for ( int32 index = 0; index < count; ++index )
                {
                    writer.valueString( message._arrArgument[index] );
                }
                writer.endArray();
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    FcmPushProvider::FcmPushProvider( FcmPushSettings settings, INetSecurityProvider* pSecurity, HTTPClient* pHTTPClient )
        : _tokenSource{ std::move( settings._credentials ), FcmPushSettings::kScope, pSecurity, pHTTPClient, settings._requestTimeoutMs }
        , _sendURL{}
        , _listWaiting{}
        , _listDone{}
        , _mapRequestToDelivery{}
        , _pHTTPClient{ pHTTPClient }
        , _requestTimeoutMs{ settings._requestTimeoutMs }
    {
        _sendURL = settings._sendBaseURL + "/v1/projects/" + _tokenSource.getProjectID() + "/messages:send";
    }

    FcmPushProvider::~FcmPushProvider() = default;

    bool FcmPushProvider::isValidTopic( string_view topic )
    {
        if ( topic.empty() )
            return false;
        for ( const utf8 character : topic )
        {
            const bool bAlphaNumeric = ( character >= '0' && character <= '9' ) || ( character >= 'a' && character <= 'z' ) || ( character >= 'A' && character <= 'Z' );
            const bool bMark         = character == '-' || character == '_' || character == '.' || character == '~' || character == '%';
            if ( bAlphaNumeric == false && bMark == false )
                return false;
        }
        return true;
    }

    string FcmPushProvider::makeMessageJSON( string_view token, string_view topic, const PushNotificationMessage& message )
    {
        using Internal            = FcmPushProviderInternal;
        const int32 argumentCount = Internal::countArguments( message );
        const bool  bHasAlert     = message._titleKey.empty() == false || message._bodyKey.empty() == false;
        const bool  bHasData      = bHasAlert || message._deepLink.empty() == false || argumentCount > 0;
        string      json;
        JSONWriter  writer{ json };
        writer.beginObject();
        writer.key( "message" );
        writer.beginObject();
        Internal::writeOptionalText( writer, "token", token );
        Internal::writeOptionalText( writer, "topic", topic );
        if ( bHasData )
        {
            writer.key( "data" );
            writer.beginObject();
            Internal::writeOptionalText( writer, "deep_link", message._deepLink );
            Internal::writeOptionalText( writer, "title_key", message._titleKey );
            Internal::writeOptionalText( writer, "body_key", message._bodyKey );
            for ( int32 index = 0; index < argumentCount; ++index )
            {
                writer.key( "arg" + to_string( index ) );
                writer.valueString( message._arrArgument[index] );
            }
            writer.endObject();
        }
        if ( bHasAlert )
        {
            writer.key( "android" );
            writer.beginObject();
            writer.key( "notification" );
            writer.beginObject();
            Internal::writeOptionalText( writer, "title_loc_key", message._titleKey );
            Internal::writeOptionalText( writer, "body_loc_key", message._bodyKey );
            Internal::writeArguments( writer, "body_loc_args", message, argumentCount );
            writer.endObject();
            writer.endObject();

            writer.key( "apns" );
            writer.beginObject();
            writer.key( "payload" );
            writer.beginObject();
            writer.key( "aps" );
            writer.beginObject();
            writer.key( "alert" );
            writer.beginObject();
            Internal::writeOptionalText( writer, "title-loc-key", message._titleKey );
            Internal::writeOptionalText( writer, "loc-key", message._bodyKey );
            Internal::writeArguments( writer, "loc-args", message, argumentCount );
            writer.endObject();
            if ( message._badgeCount >= 0 )
            {
                writer.key( "badge" );
                writer.valueInt( message._badgeCount );
            }
            writer.endObject();
            writer.endObject();
            writer.endObject();
        }
        writer.endObject();
        writer.endObject();
        return json;
    }

    PushDeliveryStatus FcmPushProvider::mapStatus( int32 statusCode, bool bTransportFailed, string_view body )
    {
        using Internal = FcmPushProviderInternal;
        if ( bTransportFailed )
            return PushDeliveryStatus::Transient;
        if ( 200 <= statusCode && statusCode <= 299 )
            return PushDeliveryStatus::Delivered;
        if ( statusCode == HTTPConstant::kStatusNotFound || body.find( "UNREGISTERED" ) != string_view::npos )
            return PushDeliveryStatus::InvalidToken;
        if ( statusCode == Internal::kStatusTooManyRequests )
            return PushDeliveryStatus::RateLimited;
        if ( statusCode >= Internal::kStatusServerError )
            return PushDeliveryStatus::Transient;
        return PushDeliveryStatus::Rejected; // 401 · 403 과 그 밖의 4xx
    }

    void FcmPushProvider::send( uint64 deliveryID, const string& deviceToken, const string& locale, const PushNotificationMessage& message, UnixMilliseconds nowMs )
    {
        (void)locale; // 문구는 현지화 키라 기기가 푼다
        queueSend( deliveryID, makeMessageJSON( deviceToken, {}, message ), nowMs );
    }

    void FcmPushProvider::sendToTopic( uint64 deliveryID, string_view topic, const PushNotificationMessage& message, UnixMilliseconds nowMs )
    {
        if ( isValidTopic( topic ) == false )
        {
            SW_LOG_WARNING( "FCM topic '%#' is not a valid topic name", string( topic ).c_str() );
            complete( deliveryID, PushDeliveryStatus::Rejected );
            return;
        }
        queueSend( deliveryID, makeMessageJSON( {}, topic, message ), nowMs );
    }

    void FcmPushProvider::queueSend( uint64 deliveryID, string&& bodyJSON, UnixMilliseconds nowMs )
    {
        vector<uint8>          bodyBytes = FcmPushProviderInternal::toBytes( bodyJSON );
        const UnixMilliseconds now{ nowMs }; // 서버 루프의 nowMs 는 유닉스 밀리초다 — 서명 토큰의 iat · exp 가 된다
        if ( _tokenSource.hasValidToken( now ) )
        {
            submitSend( deliveryID, bodyBytes, nowMs );
            return;
        }
        Waiting& waiting    = _listWaiting.emplace_back();
        waiting._deliveryID = deliveryID;
        waiting._bodyBytes  = std::move( bodyBytes );
        if ( _tokenSource.request( now ) == false )
            flushWaiting( ServiceAccountTokenSource::Outcome::Rejected, nowMs ); // 서명하지 못했다 — 키가 틀렸다
    }

    void FcmPushProvider::submitSend( uint64 deliveryID, const vector<uint8>& bodyBytes, UnixMilliseconds /*nowMs*/ )
    {
        HTTPClientRequest request;
        request._method    = HTTPMethod::Post;
        request._url       = _sendURL;
        request._timeoutMs = _requestTimeoutMs;
        request._bodyBytes = bodyBytes;
        request._listHeader.push_back( HTTPHeader{ "Authorization", "Bearer " + _tokenSource.getToken() } );
        request._listHeader.push_back( HTTPHeader{ "Content-Type", "application/json" } );
        _mapRequestToDelivery[_pHTTPClient->submitRequest( request )] = deliveryID;
    }

    void FcmPushProvider::flushWaiting( ServiceAccountTokenSource::Outcome outcome, UnixMilliseconds nowMs )
    {
        vector<Waiting> listWaiting;
        listWaiting.swap( _listWaiting );
        for ( const Waiting& waiting : listWaiting )
        {
            if ( outcome == ServiceAccountTokenSource::Outcome::Granted )
                submitSend( waiting._deliveryID, waiting._bodyBytes, nowMs );
            else
                complete( waiting._deliveryID, outcome == ServiceAccountTokenSource::Outcome::Retryable ? PushDeliveryStatus::Transient : PushDeliveryStatus::Rejected );
        }
    }

    void FcmPushProvider::finishSend( uint64 deliveryID, const HTTPClientResponse& response )
    {
        using Internal                        = FcmPushProviderInternal;
        const PushDeliveryStatus status       = mapStatus( response._statusCode, response._bTransportFailed == SW_TRUE, response.getBodyText() );
        int64                    retryAfterMs = 0;
        if ( const HTTPHeader* pRetryAfter = response.findHeader( "Retry-After" ) )
        {
            int64 seconds = 0;
            if ( StringUtil::parseInt64( pRetryAfter->_value, seconds ) && seconds > 0 )
                retryAfterMs = seconds * Internal::kMillisecondsPerSecond;
        }
        const bool bAuthFailed = response._statusCode == HTTPConstant::kStatusUnauthorized || response._statusCode == HTTPConstant::kStatusForbidden;
        if ( bAuthFailed )
        {
            // 토큰이 거절됐다 — 버려서 다음 배달이 새로 받게 한다.
            _tokenSource.invalidate();
            SW_LOG_WARNING( "FCM rejected the access token (status %#) - it will be requested again", response._statusCode );
        }
        complete( deliveryID, status, retryAfterMs );
    }

    void FcmPushProvider::complete( uint64 deliveryID, PushDeliveryStatus status, int64 retryAfterMs )
    {
        PushDeliveryResult& result = _listDone.emplace_back();
        result._deliveryID         = deliveryID;
        result._status             = status;
        result._retryAfterMs       = retryAfterMs;
    }

    void FcmPushProvider::update( UnixMilliseconds nowMs )
    {
        _pHTTPClient->update();
        vector<HTTPClientResponse> listResponse;
        (void)_pHTTPClient->pollResponses( listResponse );
        for ( const HTTPClientResponse& response : listResponse )
        {
            const ServiceAccountTokenSource::Outcome outcome = _tokenSource.handleResponse( response, nowMs );
            if ( outcome != ServiceAccountTokenSource::Outcome::None )
            {
                flushWaiting( outcome, nowMs );
                continue;
            }
            const auto requestIt = _mapRequestToDelivery.find( response._requestID );
            if ( requestIt == _mapRequestToDelivery.end() )
                continue;
            const uint64 deliveryID = requestIt->second;
            _mapRequestToDelivery.erase( requestIt );
            finishSend( deliveryID, response );
        }
    }

    int32 FcmPushProvider::pollResults( vector<PushDeliveryResult>& outListResult )
    {
        const int32 count = static_cast<int32>( _listDone.size() );
        outListResult.insert( outListResult.end(), _listDone.begin(), _listDone.end() );
        _listDone.clear();
        return count;
    }
} // namespace sw
