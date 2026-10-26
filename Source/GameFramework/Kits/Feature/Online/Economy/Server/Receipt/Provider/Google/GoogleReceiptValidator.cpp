#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/Provider/Google/GoogleReceiptValidator.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

#include "GameFramework/Base/Online/HTTP/HTTPClient.h"

namespace sw
{
    SW_LOG_CALLER( "GoogleReceiptValidator" );

    namespace
    {
        struct GoogleReceiptValidatorInternal
        {
            static constexpr size_t kMaxFieldSize           = 512;
            static constexpr int32  kStatusTooManyRequests  = 429;
            static constexpr int32  kStatusServerError      = 500;
            static constexpr size_t kMaxNotifiedRefundCount = 100000;

            static bool isFieldText( string_view text )
            {
                if ( text.empty() || text.size() > kMaxFieldSize )
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

            static string readText( const JSONValue& object, const utf8* pKey )
            {
                const JSONValue value = object.get( pKey, false );
                return value.isString() || value.isNumber() ? value.asString() : string();
            }

            static bool isTransientStatus( const HTTPClientResponse& response )
            {
                return response._bTransportFailed == SW_TRUE || response._statusCode == kStatusTooManyRequests || response._statusCode >= kStatusServerError;
            }

            static bool isAuthFailure( const HTTPClientResponse& response )
            {
                return response._statusCode == HTTPConstant::kStatusUnauthorized || response._statusCode == HTTPConstant::kStatusForbidden;
            }

            static HTTPClientRequest makeRequest( const string& url, int64 timeoutMs, const string& token, HTTPMethod method )
            {
                HTTPClientRequest request;
                request._method    = method;
                request._url       = url;
                request._timeoutMs = timeoutMs;
                request._listHeader.push_back( HTTPHeader{ "Authorization", "Bearer " + token } );
                request._listHeader.push_back( HTTPHeader{ "Accept", "application/json" } );
                if ( method == HTTPMethod::Post )
                {
                    request._listHeader.push_back( HTTPHeader{ "Content-Type", "application/json" } );
                    request._bodyBytes.push_back( '{' );
                    request._bodyBytes.push_back( '}' );
                }
                return request;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    GoogleReceiptValidator::GoogleReceiptValidator( GoogleReceiptSettings settings, INetSecurityProvider* pSecurity, HTTPClient* pHTTPClient )
        : _settings{ std::move( settings ) }
        , _tokenSource{ std::move( _settings._credentials ), GoogleReceiptSettings::kScope, pSecurity, pHTTPClient, _settings._requestTimeoutMs }
        , _mapRequestToCall{}
        , _listWaiting{}
        , _listDone{}
        , _listRefund{}
        , _uniqueSetNotifiedRefund{}
        , _pHTTPClient{ pHTTPClient }
        , _nextVoidedMs{ 0 }
        , _lastVoidedUnixMs{ 0 }
        , _voidedStartUnixMs{ 0 }
        , _bVoidedInFlight{ SW_FALSE }
        , _bStopped{ SW_FALSE }
    {
    }

    GoogleReceiptValidator::~GoogleReceiptValidator() = default;

    bool GoogleReceiptValidator::splitPayload( string_view payload, string& outProductID, string& outPurchaseToken )
    {
        const size_t bar = payload.find( '|' );
        if ( bar == string_view::npos )
            return false;
        const string_view product = payload.substr( 0, bar );
        const string_view token   = payload.substr( bar + 1 );
        if ( GoogleReceiptValidatorInternal::isFieldText( product ) == false || GoogleReceiptValidatorInternal::isFieldText( token ) == false )
            return false;
        outProductID     = string( product );
        outPurchaseToken = string( token );
        return true;
    }

    ReceiptStatus GoogleReceiptValidator::mapPurchaseState( int32 purchaseState )
    {
        if ( purchaseState == 0 )
            return ReceiptStatus::Valid;
        if ( purchaseState == 1 )
            return ReceiptStatus::Refunded;
        if ( purchaseState == 2 )
            return ReceiptStatus::Retry;
        return ReceiptStatus::Invalid;
    }

    string GoogleReceiptValidator::makeProductURL( string_view productID, string_view purchaseToken ) const
    {
        return _settings._baseURL + "/androidpublisher/v3/applications/" + HTTPUtil::encodePercent( _settings._packageName ) + "/purchases/products/" +
               HTTPUtil::encodePercent( productID ) + "/tokens/" + HTTPUtil::encodePercent( purchaseToken );
    }

    string GoogleReceiptValidator::makeAcknowledgeURL( string_view productID, string_view purchaseToken ) const
    {
        return makeProductURL( productID, purchaseToken ) + ":acknowledge";
    }

    string GoogleReceiptValidator::makeVoidedURL( int64 sinceUnixMs ) const
    {
        return _settings._baseURL + "/androidpublisher/v3/applications/" + HTTPUtil::encodePercent( _settings._packageName ) +
               "/purchases/voidedpurchases?startTime=" + to_string( sinceUnixMs ) + "&maxResults=" + to_string( kVoidedMaxResults ) + "&type=0";
    }

    void GoogleReceiptValidator::submitCall( const WaitingCall& call, int64 nowMs )
    {
        using Internal = GoogleReceiptValidatorInternal;
        string     url;
        HTTPMethod method = HTTPMethod::Get;
        if ( call._kind == WaitingCall::Kind::Validate )
            url = makeProductURL( call._pending._productID, call._pending._purchaseToken );
        else if ( call._kind == WaitingCall::Kind::Acknowledge )
        {
            url    = makeAcknowledgeURL( call._pending._productID, call._pending._purchaseToken );
            method = HTTPMethod::Post;
        }
        else
            url = makeVoidedURL( ( _lastVoidedUnixMs != 0 ? _lastVoidedUnixMs : _voidedStartUnixMs ) - _settings._voidedLookbackMs );
        _mapRequestToCall[_pHTTPClient->submitRequest( Internal::makeRequest( url, _settings._requestTimeoutMs, _tokenSource.getToken(), method ), nowMs )] = call;
    }

    void GoogleReceiptValidator::submitValidation( const ReceiptValidationRequest& request, int64 nowMs )
    {
        WaitingCall call;
        call._kind             = WaitingCall::Kind::Validate;
        call._pending._request = request;
        if ( splitPayload( request._payload, call._pending._productID, call._pending._purchaseToken ) == false )
        {
            ReceiptValidationResult& result = _listDone.emplace_back();
            result._storeName               = request._storeName;
            result._ticket                  = request._ticket;
            result._accountID               = request._accountID;
            result._status                  = ReceiptStatus::Invalid;
            result._failureText             = "google receipt must be <product id>|<purchase token>";
            return;
        }
        if ( _tokenSource.hasValidToken( nowMs ) )
        {
            submitCall( call, nowMs );
            return;
        }
        _listWaiting.push_back( std::move( call ) );
        if ( _tokenSource.request( nowMs ) == false )
            flushWaiting( ServiceAccountTokenSource::Outcome::Rejected, nowMs );
    }

    void GoogleReceiptValidator::completeRetry( const PendingValidation& pending, const utf8* pText )
    {
        ReceiptValidationResult& result = _listDone.emplace_back();
        result._storeName               = pending._request._storeName;
        result._ticket                  = pending._request._ticket;
        result._accountID               = pending._request._accountID;
        result._status                  = ReceiptStatus::Retry;
        result._failureText             = pText;
    }

    void GoogleReceiptValidator::flushWaiting( ServiceAccountTokenSource::Outcome outcome, int64 nowMs )
    {
        vector<WaitingCall> listWaiting;
        listWaiting.swap( _listWaiting );
        for ( const WaitingCall& call : listWaiting )
        {
            if ( outcome == ServiceAccountTokenSource::Outcome::Granted )
            {
                submitCall( call, nowMs );
                continue;
            }
            // 토큰을 받지 못했다 — 검증은 같은 영수증으로 다시 시도하게 하고, 확인 · 조회는 다음 기회에 한다.
            if ( call._kind == WaitingCall::Kind::Validate )
                completeRetry( call._pending, "google access token is not available" );
            else if ( call._kind == WaitingCall::Kind::Voided )
                _bVoidedInFlight = SW_FALSE;
        }
    }

    void GoogleReceiptValidator::finishValidation( const PendingValidation& pending, const HTTPClientResponse& response, int64 nowMs )
    {
        using Internal = GoogleReceiptValidatorInternal;
        if ( Internal::isAuthFailure( response ) )
        {
            _tokenSource.invalidate(); // 토큰이 거절됐다 — 다음 요청이 새로 받는다
            SW_LOG_ERROR( "Google rejected the access token or the service account has no access to '%#' (status %#)", _settings._packageName.c_str(), response._statusCode );
            completeRetry( pending, "google rejected the access token" );
            return;
        }
        if ( Internal::isTransientStatus( response ) )
        {
            completeRetry( pending, response._bTransportFailed == SW_TRUE ? response._failureText.c_str() : "google is not available" );
            return;
        }
        ReceiptValidationResult& result = _listDone.emplace_back();
        result._storeName               = pending._request._storeName;
        result._ticket                  = pending._request._ticket;
        result._accountID               = pending._request._accountID;
        result._productID               = pending._productID;
        JSONDocument document;
        if ( response.isSuccess() == false || document.tryParse( response.getBodyText() ) == false )
        {
            result._status      = ReceiptStatus::Invalid; // 모르는 토큰(400 · 404 · 410)
            result._failureText = "google does not know this purchase (status " + to_string( response._statusCode ) + ")";
            return;
        }
        const JSONValue root  = document.getRoot();
        result._transactionID = Internal::readText( root, "orderId" );
        int64 purchaseMs      = 0;
        if ( StringUtil::parseInt64( Internal::readText( root, "purchaseTimeMillis" ), purchaseMs ) )
            result._purchaseTimeMs = purchaseMs;
        int32      purchaseState        = -1;
        int32      purchaseType         = -1;
        int32      acknowledgementState = 1;
        const bool bPurchaseStateOk     = root.readOptionalInto( "purchaseState", purchaseState );
        const bool bPurchaseTypeOk      = root.readOptionalInto( "purchaseType", purchaseType );
        const bool bAcknowledgementOk   = root.readOptionalInto( "acknowledgementState", acknowledgementState );
        const bool bFieldsOk            = bPurchaseStateOk && bPurchaseTypeOk && bAcknowledgementOk;
        if ( bFieldsOk == false )
        {
            result._status      = ReceiptStatus::Invalid; // 구글이 이런 칸을 이렇게 주지 않는다 — 읽을 수 없는 응답이다(오류는 JSON 읽기가 남겼다)
            result._failureText = "google purchase has fields of the wrong type";
            return;
        }
        result._status   = mapPurchaseState( purchaseState );
        result._bSandbox = purchaseType == 0 ? SW_TRUE : SW_FALSE; // 라이선스 테스트 구매
        if ( result._status != ReceiptStatus::Valid )
            result._failureText = "google purchase state " + to_string( purchaseState );
        if ( result._transactionID.empty() && result._status != ReceiptStatus::Retry )
        {
            result._status      = ReceiptStatus::Invalid; // 주문 번호가 없으면 분개 키를 만들 수 없다
            result._failureText = "google purchase has no order id";
        }
        if ( result._status == ReceiptStatus::Valid && acknowledgementState == 0 )
        {
            WaitingCall call;
            call._kind    = WaitingCall::Kind::Acknowledge;
            call._pending = pending;
            submitCall( call, nowMs );
        }
    }

    void GoogleReceiptValidator::finishVoided( const HTTPClientResponse& response, int64 nowMs )
    {
        using Internal   = GoogleReceiptValidatorInternal;
        _bVoidedInFlight = SW_FALSE;
        if ( Internal::isAuthFailure( response ) )
        {
            _tokenSource.invalidate();
            SW_LOG_ERROR( "Google rejected the voided purchases query for '%#' (status %#)", _settings._packageName.c_str(), response._statusCode );
        }
        JSONDocument document;
        if ( response.isSuccess() == false || document.tryParse( response.getBodyText() ) == false )
        {
            SW_LOG_WARNING( "Google voided purchases query failed (status %#) - the next poll reads from the last successful one", response._statusCode );
            return; // 지난 성공 시각을 그대로 둔다 — 다음 조회가 이번 구간까지 다시 읽는다
        }
        _lastVoidedUnixMs      = _voidedStartUnixMs;
        const JSONValue voided = document.getRoot().get( "voidedPurchases", false );
        if ( voided.isArray() && voided.size() >= static_cast<size_t>( kVoidedMaxResults ) )
            SW_LOG_WARNING( "Google returned %# voided purchases (the limit) - some may be read at the next overlapping poll only", static_cast<int32>( voided.size() ) );
        for ( size_t index = 0; voided.isArray() && index < voided.size(); ++index )
        {
            const JSONValue entry   = voided.at( index );
            const string    orderID = Internal::readText( entry, "orderId" );
            if ( orderID.empty() || _uniqueSetNotifiedRefund.count( orderID ) != 0 )
                continue;
            if ( _uniqueSetNotifiedRefund.size() >= Internal::kMaxNotifiedRefundCount )
                _uniqueSetNotifiedRefund.clear(); // 상한 — 겹친 알림은 서비스가 거래 id 로 한 번만 처리한다
            _uniqueSetNotifiedRefund.insert( orderID );
            int64 voidedMs     = nowMs;
            int64 parsedVoided = 0;
            if ( StringUtil::parseInt64( Internal::readText( entry, "voidedTimeMillis" ), parsedVoided ) )
                voidedMs = parsedVoided;
            _listRefund.push_back( ReceiptRefundNotice{ getStoreName(), orderID, voidedMs } );
        }
    }

    void GoogleReceiptValidator::update( int64 nowMs )
    {
        // 서버 루프의 시각(유닉스 밀리초) 하나로 HTTP 시한 · 토큰 만료 · 조회 주기 · 취소 목록 구간을 모두 잰다.
        _pHTTPClient->update( nowMs );
        const bool bMayPoll = _bStopped == SW_FALSE && _bVoidedInFlight == SW_FALSE;
        if ( bMayPoll && nowMs >= _nextVoidedMs )
        {
            // 처음에는 바로, 그 뒤로는 주기마다. 이번 조회의 시작 시각을 먼저 정해 두고, 응답이 성공하면 다음 조회의 기준이 된다.
            _voidedStartUnixMs = nowMs;
            WaitingCall call;
            call._kind       = WaitingCall::Kind::Voided;
            _bVoidedInFlight = SW_TRUE;
            _nextVoidedMs    = nowMs + _settings._voidedPollIntervalMs;
            if ( _tokenSource.hasValidToken( nowMs ) )
                submitCall( call, nowMs );
            else
            {
                _listWaiting.push_back( std::move( call ) );
                if ( _tokenSource.request( nowMs ) == false )
                    flushWaiting( ServiceAccountTokenSource::Outcome::Rejected, nowMs );
            }
        }
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
            const auto callIt = _mapRequestToCall.find( response._requestID );
            if ( callIt == _mapRequestToCall.end() )
                continue;
            const WaitingCall call = std::move( callIt->second );
            _mapRequestToCall.erase( callIt );
            if ( call._kind == WaitingCall::Kind::Validate )
                finishValidation( call._pending, response, nowMs );
            else if ( call._kind == WaitingCall::Kind::Voided )
                finishVoided( response, nowMs );
            else if ( response.isSuccess() == false )
                SW_LOG_WARNING( "Google purchase acknowledge failed (status %#) - it is sent again when the token is validated again", response._statusCode );
        }
    }

    void GoogleReceiptValidator::pollCompletions( vector<ReceiptValidationResult>& outListResult )
    {
        for ( ReceiptValidationResult& result : _listDone )
        {
            outListResult.push_back( std::move( result ) );
        }
        _listDone.clear();
    }

    void GoogleReceiptValidator::pollRefunds( vector<ReceiptRefundNotice>& outListNotice )
    {
        for ( ReceiptRefundNotice& notice : _listRefund )
        {
            outListNotice.push_back( std::move( notice ) );
        }
        _listRefund.clear();
    }

    void GoogleReceiptValidator::shutdown()
    {
        // 날아가던 검증은 같은 영수증으로 다시 시도하게 Retry 로 끝낸다.
        for ( const auto& [requestID, call] : _mapRequestToCall )
        {
            if ( call._kind == WaitingCall::Kind::Validate )
                completeRetry( call._pending, "google validation was interrupted by shutdown" );
        }
        for ( const WaitingCall& call : _listWaiting )
        {
            if ( call._kind == WaitingCall::Kind::Validate )
                completeRetry( call._pending, "google validation was interrupted by shutdown" );
        }
        _mapRequestToCall.clear();
        _listWaiting.clear();
        _bStopped = SW_TRUE;
    }
} // namespace sw
