#include "pch.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/Provider/Fcm/FcmPushProvider.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

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

            static string quote( string_view text ) { return "\"" + JSONDocument::escapeString( text ) + "\""; }

            static vector<uint8> toBytes( string_view text )
            {
                return vector<uint8>( reinterpret_cast<const uint8*>( text.data() ), reinterpret_cast<const uint8*>( text.data() ) + text.size() );
            }

            /** @brief 알림의 앞쪽 비지 않은 인자를 JSON 배열 글로 씁니다. 인자가 없으면 빈 글입니다. */
            static string makeArguments( const PushNotificationMessage& message )
            {
                int32 count = 0;
                while ( count < PushNotificationMessage::kArgumentCount && message._arrArgument[count].empty() == false )
                {
                    ++count;
                }
                if ( count == 0 )
                    return {};
                string list = "[";
                for ( int32 index = 0; index < count; ++index )
                {
                    list += ( index == 0 ? "" : "," ) + quote( message._arrArgument[index] );
                }
                return list + "]";
            }

            static void addMember( string& inoutObject, string_view name, string_view value )
            {
                inoutObject += ( inoutObject.empty() ? "" : "," ) + quote( name ) + ":" + string( value );
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
        using Internal = FcmPushProviderInternal;
        string body;
        if ( token.empty() == false )
            Internal::addMember( body, "token", Internal::quote( token ) );
        if ( topic.empty() == false )
            Internal::addMember( body, "topic", Internal::quote( topic ) );
        string data;
        if ( message._deepLink.empty() == false )
            Internal::addMember( data, "deep_link", Internal::quote( message._deepLink ) );
        if ( message._titleKey.empty() == false )
            Internal::addMember( data, "title_key", Internal::quote( message._titleKey ) );
        if ( message._bodyKey.empty() == false )
            Internal::addMember( data, "body_key", Internal::quote( message._bodyKey ) );
        for ( int32 index = 0; index < PushNotificationMessage::kArgumentCount && message._arrArgument[index].empty() == false; ++index )
        {
            Internal::addMember( data, "arg" + to_string( index ), Internal::quote( message._arrArgument[index] ) );
        }
        if ( data.empty() == false )
            Internal::addMember( body, "data", "{" + data + "}" );
        const string arguments = Internal::makeArguments( message );
        const bool   bHasAlert = message._titleKey.empty() == false || message._bodyKey.empty() == false;
        if ( bHasAlert )
        {
            string notification;
            if ( message._titleKey.empty() == false )
                Internal::addMember( notification, "title_loc_key", Internal::quote( message._titleKey ) );
            if ( message._bodyKey.empty() == false )
                Internal::addMember( notification, "body_loc_key", Internal::quote( message._bodyKey ) );
            if ( arguments.empty() == false )
                Internal::addMember( notification, "body_loc_args", arguments );
            Internal::addMember( body, "android", "{\"notification\":{" + notification + "}}" );
            string alert;
            if ( message._titleKey.empty() == false )
                Internal::addMember( alert, "title-loc-key", Internal::quote( message._titleKey ) );
            if ( message._bodyKey.empty() == false )
                Internal::addMember( alert, "loc-key", Internal::quote( message._bodyKey ) );
            if ( arguments.empty() == false )
                Internal::addMember( alert, "loc-args", arguments );
            string aps;
            Internal::addMember( aps, "alert", "{" + alert + "}" );
            if ( message._badgeCount >= 0 )
                Internal::addMember( aps, "badge", to_string( message._badgeCount ) );
            Internal::addMember( body, "apns", "{\"payload\":{\"aps\":{" + aps + "}}}" );
        }
        return "{\"message\":{" + body + "}}";
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

    void FcmPushProvider::send( uint64 deliveryID, const string& deviceToken, const string& locale, const PushNotificationMessage& message, int64 nowMs )
    {
        (void)locale; // 문구는 현지화 키라 기기가 푼다
        queueSend( deliveryID, makeMessageJSON( deviceToken, {}, message ), nowMs );
    }

    void FcmPushProvider::sendToTopic( uint64 deliveryID, string_view topic, const PushNotificationMessage& message, int64 nowMs )
    {
        if ( isValidTopic( topic ) == false )
        {
            SW_LOG_WARNING( "FCM topic '%#' is not a valid topic name", string( topic ).c_str() );
            complete( deliveryID, PushDeliveryStatus::Rejected );
            return;
        }
        queueSend( deliveryID, makeMessageJSON( {}, topic, message ), nowMs );
    }

    void FcmPushProvider::queueSend( uint64 deliveryID, string&& bodyJSON, int64 nowMs )
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

    void FcmPushProvider::submitSend( uint64 deliveryID, const vector<uint8>& bodyBytes, int64 nowMs )
    {
        HTTPClientRequest request;
        request._method    = HTTPMethod::Post;
        request._url       = _sendURL;
        request._timeoutMs = _requestTimeoutMs;
        request._bodyBytes = bodyBytes;
        request._listHeader.push_back( HTTPHeader{ "Authorization", "Bearer " + _tokenSource.getToken() } );
        request._listHeader.push_back( HTTPHeader{ "Content-Type", "application/json" } );
        _mapRequestToDelivery[_pHTTPClient->submitRequest( request, nowMs )] = deliveryID;
    }

    void FcmPushProvider::flushWaiting( ServiceAccountTokenSource::Outcome outcome, int64 nowMs )
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

    void FcmPushProvider::update( int64 nowMs )
    {
        _pHTTPClient->update( nowMs );
        vector<HTTPClientResponse> listResponse;
        (void)_pHTTPClient->pollResponses( listResponse );
        for ( const HTTPClientResponse& response : listResponse )
        {
            const ServiceAccountTokenSource::Outcome outcome = _tokenSource.handleResponse( response, UnixMilliseconds{ nowMs } );
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
