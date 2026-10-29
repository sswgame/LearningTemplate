#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/Provider/Steam/SteamReceiptValidator.h"

#include "Core/Container/StringUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

#include "GameFramework/Base/Online/HTTP/HTTPClient.h"

namespace sw
{
    SW_LOG_CALLER( "SteamReceiptValidator" );

    namespace
    {
        struct SteamReceiptValidatorInternal
        {
            static constexpr size_t kMaxOrderIDSize         = 20;
            static constexpr int64  kMillisecondsPerSecond  = 1000;
            static constexpr int64  kSecondsPerDay          = 86400;
            static constexpr int32  kStatusTooManyRequests  = 429;
            static constexpr int32  kStatusServerError      = 500;
            static constexpr size_t kMaxNotifiedRefundCount = 100000; ///< 겹친 조회에서 같은 주문을 두 번 알리지 않으려고 기억하는 상한

            /** @brief 1970-01-01 부터 (년, 월, 일) 까지의 날 수입니다(그레고리력). */
            static int64 daysFromCivil( int64 year, int64 month, int64 day )
            {
                year -= month <= 2 ? 1 : 0;
                const int64 era       = ( year >= 0 ? year : year - 399 ) / 400;
                const int64 yearOfEra = year - era * 400;
                const int64 dayOfYear = ( 153 * ( month + ( month > 2 ? -3 : 9 ) ) + 2 ) / 5 + day - 1;
                const int64 dayOfEra  = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
                return era * 146097 + dayOfEra - 719468;
            }

            /** @brief 숫자를 앞에 0 을 채워 @p width 자리로 만듭니다. */
            static string padZero( int64 value, int32 width )
            {
                string text = to_string( value );
                while ( static_cast<int32>( text.size() ) < width )
                {
                    text = "0" + text;
                }
                return text;
            }

            static void civilFromDays( int64 days, int64& outYear, int64& outMonth, int64& outDay )
            {
                days += 719468;
                const int64 era       = ( days >= 0 ? days : days - 146096 ) / 146097;
                const int64 dayOfEra  = days - era * 146097;
                const int64 yearOfEra = ( dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096 ) / 365;
                const int64 dayOfYear = dayOfEra - ( 365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100 );
                const int64 monthPart = ( 5 * dayOfYear + 2 ) / 153;
                outDay                = dayOfYear - ( 153 * monthPart + 2 ) / 5 + 1;
                outMonth              = monthPart < 10 ? monthPart + 3 : monthPart - 9;
                outYear               = yearOfEra + era * 400 + ( outMonth <= 2 ? 1 : 0 );
            }

            static string readText( const JSONValue& object, string_view path )
            {
                JSONValue current = object;
                while ( path.empty() == false )
                {
                    const size_t      dot  = path.find( '.' );
                    const string_view part = path.substr( 0, dot );
                    path                   = dot == string_view::npos ? string_view{} : path.substr( dot + 1 );
                    if ( current.isObject() == false )
                        return {};
                    current = current.get( part );
                    if ( current.isValid() == false )
                        return {};
                }
                return current.isString() || current.isNumber() ? current.asString() : string();
            }

            static bool isTransientStatus( const HTTPClientResponse& response )
            {
                return response._bTransportFailed == SW_TRUE || response._statusCode == kStatusTooManyRequests || response._statusCode >= kStatusServerError ||
                       response._statusCode == HTTPConstant::kStatusUnauthorized || response._statusCode == HTTPConstant::kStatusForbidden;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SteamReceiptValidator::SteamReceiptValidator( SteamReceiptSettings settings, HTTPClient* pHTTPClient )
        : _settings{ std::move( settings ) }
        , _mapRequestToPending{}
        , _listDone{}
        , _listRefund{}
        , _uniqueSetNotifiedRefund{}
        , _pHTTPClient{ pHTTPClient }
        , _nextReportMs{ 0 }
        , _lastReportUnixMs{ 0 }
        , _reportStartUnixMs{ 0 }
        , _reportRequestID{ 0 }
        , _bStopped{ SW_FALSE }
    {
    }

    SteamReceiptValidator::~SteamReceiptValidator()
    {
        Memory::set( _settings._key.data(), 0, _settings._key.size() ); // 키를 메모리에 남기지 않는다
    }

    bool SteamReceiptValidator::isOrderID( string_view text )
    {
        if ( text.empty() || text.size() > SteamReceiptValidatorInternal::kMaxOrderIDSize )
            return false;
        for ( const utf8 character : text )
        {
            if ( character < '0' || character > '9' )
                return false;
        }
        return true;
    }

    ReceiptStatus SteamReceiptValidator::mapOrderStatus( string_view status )
    {
        if ( status == "Succeeded" )
            return ReceiptStatus::Valid;
        if ( isRefundStatus( status ) )
            return ReceiptStatus::Refunded;
        if ( status == "Init" || status == "Approved" )
            return ReceiptStatus::Retry; // 구매자가 아직 승인하지 않았거나 게임 서버가 마무리하지 않았다
        return ReceiptStatus::Invalid;
    }

    bool SteamReceiptValidator::isRefundStatus( string_view status )
    {
        return status == "Refunded" || status == "PartialRefund" || status == "Chargedback" || status == "RefundedSuspected" || status == "PartialRefundSuspected" ||
               status == "RefundedFraudulent";
    }

    bool SteamReceiptValidator::parseTime( string_view text, int64& outUnixMs )
    {
        using Internal = SteamReceiptValidatorInternal;
        if ( text.size() > 0 && text.back() == 'Z' )
            text.remove_suffix( 1 );
        const bool bShapeOk = text.size() == 19 && text[4] == '-' && text[7] == '-' && text[10] == 'T' && text[13] == ':' && text[16] == ':';
        if ( bShapeOk == false )
            return false;
        int64        arrField[6]{};
        const size_t arrStart[6]{ 0, 5, 8, 11, 14, 17 };
        const size_t arrSize[6]{ 4, 2, 2, 2, 2, 2 };
        for ( int32 index = 0; index < 6; ++index )
        {
            if ( StringUtil::parseInt64( text.substr( arrStart[index], arrSize[index] ), arrField[index] ) == false )
                return false;
        }
        const bool bRangeOk = arrField[1] >= 1 && arrField[1] <= 12 && arrField[2] >= 1 && arrField[2] <= 31 && arrField[3] <= 23 && arrField[4] <= 59 && arrField[5] <= 60;
        if ( bRangeOk == false )
            return false;
        const int64 days = Internal::daysFromCivil( arrField[0], arrField[1], arrField[2] );
        outUnixMs        = ( days * Internal::kSecondsPerDay + arrField[3] * 3600 + arrField[4] * 60 + arrField[5] ) * Internal::kMillisecondsPerSecond;
        return true;
    }

    string SteamReceiptValidator::formatTime( int64 unixMs )
    {
        using Internal       = SteamReceiptValidatorInternal;
        const int64 totalSec = unixMs / Internal::kMillisecondsPerSecond;
        const int64 days     = totalSec >= 0 ? totalSec / Internal::kSecondsPerDay : -( ( -totalSec + Internal::kSecondsPerDay - 1 ) / Internal::kSecondsPerDay );
        const int64 secOfDay = totalSec - days * Internal::kSecondsPerDay;
        int64       year     = 0;
        int64       month    = 0;
        int64       day      = 0;
        Internal::civilFromDays( days, year, month, day );
        return Internal::padZero( year, 4 ) + "-" + Internal::padZero( month, 2 ) + "-" + Internal::padZero( day, 2 ) + "T" + Internal::padZero( secOfDay / 3600, 2 ) + ":" +
               Internal::padZero( secOfDay / 60 % 60, 2 ) + ":" + Internal::padZero( secOfDay % 60, 2 );
    }

    string SteamReceiptValidator::makeQueryURL( string_view orderID ) const
    {
        const utf8* pInterface = _settings._bSandbox == SW_TRUE ? "ISteamMicroTxnSandbox" : "ISteamMicroTxn";
        return _settings._baseURL + "/" + pInterface + "/QueryTxn/v3/?key=" + _settings._key + "&appid=" + _settings._appID + "&orderid=" + string( orderID );
    }

    string SteamReceiptValidator::makeReportURL( int64 sinceUnixMs ) const
    {
        const utf8* pInterface = _settings._bSandbox == SW_TRUE ? "ISteamMicroTxnSandbox" : "ISteamMicroTxn";
        return _settings._baseURL + "/" + pInterface + "/GetReport/v4/?key=" + _settings._key + "&appid=" + _settings._appID + "&type=STANDARD&time=" + formatTime( sinceUnixMs ) +
               formatToString( "&maxresults=%#", kReportMaxResults );
    }

    void SteamReceiptValidator::submitValidation( const ReceiptValidationRequest& request, UnixMilliseconds /*now*/ )
    {
        if ( isOrderID( request._payload ) == false )
        {
            // 쿼리에 실을 수 없는 영수증은 스팀에 묻지 않고 거절한다(다른 인자를 끼워 넣는 길을 막는다).
            ReceiptValidationResult& result = _listDone.emplace_back();
            result._storeName               = request._storeName;
            result._ticket                  = request._ticket;
            result._accountID               = request._accountID;
            result._status                  = ReceiptStatus::Invalid;
            result._failureText             = "steam receipt must be the numeric order id";
            return;
        }
        HTTPClientRequest httpRequest;
        httpRequest._url       = makeQueryURL( request._payload );
        httpRequest._timeoutMs = _settings._requestTimeoutMs;
        httpRequest._listHeader.push_back( HTTPHeader{ "Accept", "application/json" } );
        PendingValidation& pending = _mapRequestToPending[_pHTTPClient->submitRequest( httpRequest )];
        pending._request           = request;
        pending._orderID           = request._payload;
    }

    void SteamReceiptValidator::finishValidation( const PendingValidation& pending, const HTTPClientResponse& response )
    {
        using Internal                  = SteamReceiptValidatorInternal;
        ReceiptValidationResult& result = _listDone.emplace_back();
        result._storeName               = pending._request._storeName;
        result._ticket                  = pending._request._ticket;
        result._accountID               = pending._request._accountID;
        result._transactionID           = pending._orderID;
        if ( Internal::isTransientStatus( response ) )
        {
            result._status      = ReceiptStatus::Retry;
            result._failureText = response._bTransportFailed == SW_TRUE ? response._failureText : string( formatToString( "steam answered status %#", response._statusCode ) );
            if ( response._statusCode == HTTPConstant::kStatusUnauthorized || response._statusCode == HTTPConstant::kStatusForbidden )
                SW_LOG_ERROR( "Steam rejected the publisher key (status %#) - check the key environment variable", response._statusCode );
            return;
        }
        JSONDocument document;
        if ( response.isSuccess() == false || document.tryParse( response.getBodyText() ) == false )
        {
            result._status      = ReceiptStatus::Invalid; // 주문이 없으면(4xx) 스팀이 에러로 답한다
            result._failureText = formatToString( "steam does not know this order (status %#)", response._statusCode );
            return;
        }
        const JSONValue root = document.getRoot();
        if ( Internal::readText( root, "response.result" ) != "OK" )
        {
            result._status      = ReceiptStatus::Invalid;
            result._failureText = "steam answered " + Internal::readText( root, "response.error.errordesc" );
            return;
        }
        const string status   = Internal::readText( root, "response.params.status" );
        result._status        = mapOrderStatus( status );
        result._failureText   = result._status == ReceiptStatus::Valid ? string() : "steam order status " + status;
        const JSONValue items = root.get( "response" ).get( "params" ).get( "items" );
        if ( items.isArray() && items.size() > 0 )
            result._productID = Internal::readText( items.at( 0 ), "itemid" );
        int64 purchaseMs = 0;
        if ( parseTime( Internal::readText( root, "response.params.time" ), purchaseMs ) )
            result._purchaseTimeMs = purchaseMs;
        result._bSandbox = _settings._bSandbox;
        if ( result._status == ReceiptStatus::Valid && result._productID.empty() )
        {
            result._status      = ReceiptStatus::Invalid; // 상품이 없는 주문은 지급할 것이 없다
            result._failureText = "steam order has no item";
        }
    }

    void SteamReceiptValidator::update( UnixMilliseconds now )
    {
        using Internal = SteamReceiptValidatorInternal;
        // 서버 루프의 시각(유닉스 밀리초) 하나로 HTTP 시한 · 조회 주기 · 보고서 구간을 모두 잰다.
        _pHTTPClient->update();
        // 보고서 조회 — 처음에는 바로, 그 뒤로는 주기마다. 앞 요청이 끝나기 전에는 새로 보내지 않는다.
        const bool bMayReport = _bStopped == SW_FALSE && _reportRequestID == 0;
        if ( bMayReport && now.count() >= _nextReportMs )
        {
            const int64       sinceMs = ( _lastReportUnixMs != 0 ? _lastReportUnixMs : now.count() ) - _settings._reportLookbackMs;
            HTTPClientRequest request;
            request._url       = makeReportURL( sinceMs );
            request._timeoutMs = _settings._requestTimeoutMs;
            request._listHeader.push_back( HTTPHeader{ "Accept", "application/json" } );
            _reportRequestID   = _pHTTPClient->submitRequest( request );
            _reportStartUnixMs = now.count();
            _nextReportMs      = now.count() + _settings._reportPollIntervalMs;
        }
        vector<HTTPClientResponse> listResponse;
        (void)_pHTTPClient->pollResponses( listResponse );
        for ( const HTTPClientResponse& response : listResponse )
        {
            if ( _reportRequestID != 0 && response._requestID == _reportRequestID )
            {
                _reportRequestID = 0;
                JSONDocument document;
                if ( response.isSuccess() == false || document.tryParse( response.getBodyText() ) == false )
                {
                    SW_LOG_WARNING( "Steam refund report failed (status %#) - the next poll reads from the last successful one", response._statusCode );
                    continue; // 지난 성공 시각을 그대로 둔다 — 다음 조회가 이번 구간까지 다시 읽는다
                }
                _lastReportUnixMs      = _reportStartUnixMs;
                const JSONValue orders = document.getRoot().get( "response" ).get( "params" ).get( "orders" );
                if ( orders.isArray() && orders.size() >= static_cast<size_t>( kReportMaxResults ) )
                    SW_LOG_WARNING( "Steam refund report returned %# orders (the limit) - some refunds may be read at the next overlapping poll only", static_cast<int32>( orders.size() ) );
                for ( size_t index = 0; orders.isArray() && index < orders.size(); ++index )
                {
                    const JSONValue order = orders.at( index );
                    if ( isRefundStatus( Internal::readText( order, "status" ) ) == false )
                        continue;
                    const string orderID = Internal::readText( order, "orderid" );
                    if ( orderID.empty() || _uniqueSetNotifiedRefund.count( orderID ) != 0 )
                        continue;
                    if ( _uniqueSetNotifiedRefund.size() >= Internal::kMaxNotifiedRefundCount )
                        _uniqueSetNotifiedRefund.clear(); // 상한 — 겹친 알림은 서비스가 거래 id 로 한 번만 처리한다
                    _uniqueSetNotifiedRefund.insert( orderID );
                    int64 refundedMs   = now.count();
                    int64 parsedRefund = 0;
                    if ( parseTime( Internal::readText( order, "time" ), parsedRefund ) )
                        refundedMs = parsedRefund;
                    _listRefund.push_back( ReceiptRefundNotice{ getStoreName(), orderID, refundedMs } );
                }
                continue;
            }
            const auto pendingIt = _mapRequestToPending.find( response._requestID );
            if ( pendingIt == _mapRequestToPending.end() )
                continue;
            const PendingValidation pending = std::move( pendingIt->second );
            _mapRequestToPending.erase( pendingIt );
            finishValidation( pending, response );
        }
    }

    void SteamReceiptValidator::pollCompletions( vector<ReceiptValidationResult>& outListResult )
    {
        for ( ReceiptValidationResult& result : _listDone )
        {
            outListResult.push_back( std::move( result ) );
        }
        _listDone.clear();
    }

    void SteamReceiptValidator::pollRefunds( vector<ReceiptRefundNotice>& outListNotice )
    {
        for ( ReceiptRefundNotice& notice : _listRefund )
        {
            outListNotice.push_back( std::move( notice ) );
        }
        _listRefund.clear();
    }

    void SteamReceiptValidator::shutdown()
    {
        // 날아가던 검증은 같은 영수증으로 다시 시도하게 Retry 로 끝낸다.
        for ( auto& [requestID, pending] : _mapRequestToPending )
        {
            ReceiptValidationResult& result = _listDone.emplace_back();
            result._storeName               = pending._request._storeName;
            result._ticket                  = pending._request._ticket;
            result._accountID               = pending._request._accountID;
            result._transactionID           = pending._orderID;
            result._status                  = ReceiptStatus::Retry;
            result._failureText             = "steam validation was interrupted by shutdown";
        }
        _mapRequestToPending.clear();
        _reportRequestID = 0;
        _bStopped        = SW_TRUE;
    }
} // namespace sw
