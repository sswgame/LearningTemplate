#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/Provider/Steam/SteamReceiptValidator.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/CalendarDate.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

#include "GameFramework/Runtime/Online/HTTP/HTTPClient.h"

namespace sw
{
    namespace
    {
        struct SteamReceiptValidatorInternal
        {
            static constexpr size_t kMaxOrderIDSize = 20;

            /** @brief 비어 있지 않고 모두 십진 숫자면 true 입니다. 부호 · 공백을 받아 주는 `StringUtil::parseInt64` 앞에서 거른다. */
            static bool isAllDigits( string_view text )
            {
                if ( text.empty() )
                    return false;
                for ( const utf8 character : text )
                {
                    if ( character < '0' || character > '9' )
                        return false;
                }
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SteamReceiptValidator::SteamReceiptValidator( SteamReceiptSettings settings, HTTPClient* pHTTPClient )
        : StoreReceiptValidator{
              settings._buyerRule, ReceiptPollWindow{ settings._reportPollIntervalMs, settings._reportLookbackMs, 0 },
              pHTTPClient
    },
        _settings{ std::move( settings ) }
    {
    }

    SteamReceiptSettings::~SteamReceiptSettings()
    {
        Memory::wipe( _key.data(), _key.size() );
    }

    SteamReceiptValidator::~SteamReceiptValidator() = default; // 키는 _settings 의 소멸자가 지운다

    bool SteamReceiptValidator::isOrderID( string_view text )
    {
        return text.size() <= SteamReceiptValidatorInternal::kMaxOrderIDSize && SteamReceiptValidatorInternal::isAllDigits( text );
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
            const string_view field = text.substr( arrStart[index], arrSize[index] );
            if ( Internal::isAllDigits( field ) == false || StringUtil::parseInt64( field, arrField[index] ) == false )
                return false;
        }
        const bool bRangeOk = arrField[1] >= 1 && arrField[1] <= 12 && arrField[2] >= 1 && arrField[2] <= 31 && arrField[3] <= 23 && arrField[4] <= 59 && arrField[5] <= 60;
        if ( bRangeOk == false )
            return false;
        const CalendarDate date{ arrField[0], static_cast<int32>( arrField[1] ), static_cast<int32>( arrField[2] ) };
        if ( date.isValid() == false )
            return false; // 그 달에 없는 날(02-31)이 다음 달로 넘어가 다른 시각이 되지 않게 한다
        outUnixMs = ( date.toUnixDays() * constant::time::kSecondsPerDay + arrField[3] * 3600 + arrField[4] * 60 + arrField[5] ) * constant::time::kMillisecondsPerSecond;
        return true;
    }

    string SteamReceiptValidator::formatTime( int64 unixMs )
    {
        const int64                             totalSec = unixMs / constant::time::kMillisecondsPerSecond;
        const int64                             days     = CalendarDate::floorDivide( totalSec, constant::time::kSecondsPerDay );
        const int64                             secOfDay = totalSec - days * constant::time::kSecondsPerDay;
        const CalendarDate                      date     = CalendarDate::fromUnixDays( days );
        StringBuilder<constant::buffer::kMax64> text;
        text.appendFormat( "%04d-%02d-%02dT%02d:%02d:%02d", date._year, date._month, date._day, secOfDay / 3600, secOfDay / 60 % 60, secOfDay % 60 );
        return string( text.view() );
    }

    string SteamReceiptValidator::makeQueryURL( string_view orderID ) const
    {
        const utf8*                              pInterface = _settings._bSandbox == SW_TRUE ? "ISteamMicroTxnSandbox" : "ISteamMicroTxn";
        StringBuilder<constant::buffer::kMax512> url;
        url.appendFormat( "%#/%#/QueryTxn/v3/?key=%#&appid=%#&orderid=%#", _settings._baseURL, pInterface, _settings._key, _settings._appID, orderID );
        return string( url.view() );
    }

    string SteamReceiptValidator::makeReportURL( int64 sinceUnixMs ) const
    {
        const utf8*                              pInterface = _settings._bSandbox == SW_TRUE ? "ISteamMicroTxnSandbox" : "ISteamMicroTxn";
        StringBuilder<constant::buffer::kMax512> url;
        url.appendFormat( "%#/%#/GetReport/v4/?key=%#&appid=%#&type=STANDARD&time=%#&maxresults=%#", _settings._baseURL, pInterface, _settings._key, _settings._appID,
                          formatTime( sinceUnixMs ), kReportMaxResults );
        return string( url.view() );
    }

    expected<void> SteamReceiptValidator::readPayload( string_view payload, ReceiptQuery& outQuery ) const
    {
        if ( isOrderID( payload ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "steam receipt must be the numeric order id" } );
        outQuery._transactionID = string( payload );
        return {};
    }

    HTTPClientRequest SteamReceiptValidator::makeHTTPRequest( const ReceiptCall& call, UnixMilliseconds /*now*/ )
    {
        HTTPClientRequest request;
        request._url       = call._kind == ReceiptCall::Kind::RefundPage ? makeReportURL( getRefundWindow().getFromMs() ) : makeQueryURL( call._query._transactionID );
        request._timeoutMs = _settings._requestTimeoutMs;
        request._listHeader.push_back( HTTPHeader{ "Accept", "application/json" } );
        return request;
    }

    void SteamReceiptValidator::parseOrder( const JSONValue& root, const ReceiptCall& /*call*/, UnixMilliseconds /*now*/, ReceiptOrderInfo& outInfo )
    {
        if ( readText( root, "response.result" ) != "OK" )
        {
            outInfo._status = ReceiptStatus::Invalid;
            StringBuilder<constant::buffer::kMax256> failureText;
            failureText.appendFormat( "steam answered %#", readText( root, "response.error.errordesc" ) );
            outInfo._failureText = string( failureText.view() );
            return;
        }
        const string status = readText( root, "response.params.status" );
        outInfo._status     = mapOrderStatus( status );
        StringBuilder<constant::buffer::kMax128> failureText;
        if ( outInfo._status != ReceiptStatus::Valid )
            failureText.appendFormat( "steam order status %#", status );
        outInfo._failureText   = string( failureText.view() );
        outInfo._transactionID = readText( root, "response.params.orderid" );
        outInfo._storeBuyerID  = readText( root, "response.params.steamid" );
        outInfo._bSandbox      = _settings._bSandbox;
        const JSONValue items  = root.get( "response" ).get( "params" ).get( "items" );
        for ( size_t index = 0; items.isArray() && index < items.size(); ++index )
        {
            const JSONValue  itemValue = items.at( index );
            ReceiptLineItem& item      = outInfo._listItem.emplace_back();
            item._productID            = readText( itemValue, "itemid" );
            if ( StringUtil::parseInt64( readText( itemValue, "qty" ), item._quantity ) == false )
                item._quantity = 0; // 읽지 못한 수다 — 틀이 거절한다
        }
        int64 purchaseMs = 0;
        if ( parseTime( readText( root, "response.params.time" ), purchaseMs ) )
            outInfo._purchaseTimeMs = purchaseMs;
    }

    void SteamReceiptValidator::parseRefundPage( const JSONValue& root, UnixMilliseconds now, ReceiptRefundPage& outPage )
    {
        // 결과가 OK 가 아닌 200 도 실패다 — 성공으로 치면 읽지 못한 구간을 지나쳐 그 환불을 영영 놓친다.
        if ( readText( root, "response.result" ) != "OK" )
        {
            outPage._bIncomplete = SW_TRUE;
            SW_LOG_WARNING_ID( "SteamReceiptValidator.SteamRefundReportNotOk", "Steam refund report answered '%#' - the next poll reads from the last successful one", readText( root, "response.error.errordesc" ) );
            return;
        }
        const JSONValue orders = root.get( "response" ).get( "params" ).get( "orders" );
        if ( orders.isArray() == false )
            return;
        for ( size_t index = 0; index < orders.size(); ++index )
        {
            const JSONValue order    = orders.at( index );
            int64           orderMs  = 0;
            const bool      bHasTime = parseTime( readText( order, "time" ), orderMs );
            if ( bHasTime && orderMs > outPage._cursorMs )
                outPage._cursorMs = orderMs; // 잘리면 다음 조회는 여기서 잇는다(스팀 보고서는 쪽 토큰이 없다)
            if ( isRefundStatus( readText( order, "status" ) ) == false )
                continue;
            const string orderID = readText( order, "orderid" );
            if ( isOrderID( orderID ) == false ) // 지급 때의 거래 id 는 늘 주문 번호다 — 다른 모양은 깨진 줄이다
                continue;
            addRefund( orderID, bHasTime ? orderMs : now.count() );
        }
        outPage._bTruncated = orders.size() >= static_cast<size_t>( kReportMaxResults ) ? SW_TRUE : SW_FALSE;
    }

    void SteamReceiptValidator::onCredentialRejected( const HTTPClientResponse& response )
    {
        SW_LOG_ERROR_ID( "SteamReceiptValidator.SteamRejectedPublisherKey", "Steam rejected the publisher key (status %#) - check the key environment variable", response.getStatusCode() );
    }
} // namespace sw
