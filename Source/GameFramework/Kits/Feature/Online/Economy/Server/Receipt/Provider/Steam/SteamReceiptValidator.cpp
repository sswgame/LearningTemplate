#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/Provider/Steam/SteamReceiptValidator.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

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

            /** @brief 점 경로의 글입니다. 없으면 빈 글입니다(응답 판정이 빈 글을 "다르다"로 읽는다). */
            static string readText( const JSONValue& object, string_view path )
            {
                string text;
                (void)object.findPathText( path, text );
                return text;
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

    SteamReceiptValidator::~SteamReceiptValidator()
    {
        Memory::set( _settings._key.data(), 0, _settings._key.size() ); // 키를 메모리에 남기지 않는다
    }

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
        const int64 days      = Internal::daysFromCivil( arrField[0], arrField[1], arrField[2] );
        int64       echoYear  = 0;
        int64       echoMonth = 0;
        int64       echoDay   = 0;
        Internal::civilFromDays( days, echoYear, echoMonth, echoDay );
        if ( echoMonth != arrField[1] || echoDay != arrField[2] )
            return false; // 그 달에 없는 날(02-31)이 다음 달로 넘어가 다른 시각이 되지 않게 한다
        outUnixMs = ( days * constant::time::kSecondsPerDay + arrField[3] * 3600 + arrField[4] * 60 + arrField[5] ) * constant::time::kMillisecondsPerSecond;
        return true;
    }

    string SteamReceiptValidator::formatTime( int64 unixMs )
    {
        using Internal       = SteamReceiptValidatorInternal;
        const int64 totalSec = unixMs / constant::time::kMillisecondsPerSecond;
        const int64 days     = totalSec >= 0 ? totalSec / constant::time::kSecondsPerDay : -( ( -totalSec + constant::time::kSecondsPerDay - 1 ) / constant::time::kSecondsPerDay );
        const int64 secOfDay = totalSec - days * constant::time::kSecondsPerDay;
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
        const utf8*                              pInterface = _settings._bSandbox == SW_TRUE ? "ISteamMicroTxnSandbox" : "ISteamMicroTxn";
        StringBuilder<constant::buffer::kMax256> textBuilder;
        textBuilder.appendFormat( "&maxresults=%#", kReportMaxResults );
        return _settings._baseURL + "/" + pInterface + "/GetReport/v4/?key=" + _settings._key + "&appid=" + _settings._appID + "&type=STANDARD&time=" + formatTime( sinceUnixMs ) +
               string( textBuilder.view() );
    }

    expected<void> SteamReceiptValidator::readPayload( string_view payload, ReceiptAsk& outAsk ) const
    {
        if ( isOrderID( payload ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "steam receipt must be the numeric order id" } );
        outAsk._transactionID = string( payload );
        return {};
    }

    HTTPClientRequest SteamReceiptValidator::makeHTTPRequest( const ReceiptCall& call, UnixMilliseconds /*now*/ )
    {
        HTTPClientRequest request;
        request._url       = call._kind == ReceiptCall::Kind::RefundPage ? makeReportURL( getRefundWindow().getFromMs() ) : makeQueryURL( call._ask._transactionID );
        request._timeoutMs = _settings._requestTimeoutMs;
        request._listHeader.push_back( HTTPHeader{ "Accept", "application/json" } );
        return request;
    }

    void SteamReceiptValidator::parseOrder( const JSONValue& root, const ReceiptCall& /*call*/, UnixMilliseconds /*now*/, ReceiptOrderFacts& outFacts )
    {
        using Internal = SteamReceiptValidatorInternal;
        if ( Internal::readText( root, "response.result" ) != "OK" )
        {
            outFacts._status      = ReceiptStatus::Invalid;
            outFacts._failureText = "steam answered " + Internal::readText( root, "response.error.errordesc" );
            return;
        }
        const string status     = Internal::readText( root, "response.params.status" );
        outFacts._status        = mapOrderStatus( status );
        outFacts._failureText   = outFacts._status == ReceiptStatus::Valid ? string() : "steam order status " + status;
        outFacts._transactionID = Internal::readText( root, "response.params.orderid" );
        outFacts._storeBuyerID  = Internal::readText( root, "response.params.steamid" );
        outFacts._bSandbox      = _settings._bSandbox;
        const JSONValue items   = root.get( "response" ).get( "params" ).get( "items" );
        if ( items.isArray() && items.size() > 0 )
            outFacts._productID = Internal::readText( items.at( 0 ), "itemid" );
        int64 purchaseMs = 0;
        if ( parseTime( Internal::readText( root, "response.params.time" ), purchaseMs ) )
            outFacts._purchaseTimeMs = purchaseMs;
    }

    void SteamReceiptValidator::parseRefundPage( const JSONValue& root, UnixMilliseconds now, ReceiptRefundPage& outPage )
    {
        using Internal = SteamReceiptValidatorInternal;
        // 결과가 OK 가 아닌 200 도 실패다 — 성공으로 치면 읽지 못한 구간을 지나쳐 그 환불을 영영 놓친다.
        if ( Internal::readText( root, "response.result" ) != "OK" )
        {
            outPage._bIncomplete = SW_TRUE;
            SW_LOG_WARNING_ID( "SteamReceiptValidator.SteamRefundReportNotOk", "Steam refund report answered '%#' - the next poll reads from the last successful one", Internal::readText( root, "response.error.errordesc" ) );
            return;
        }
        const JSONValue orders = root.get( "response" ).get( "params" ).get( "orders" );
        if ( orders.isArray() == false )
            return;
        for ( size_t index = 0; index < orders.size(); ++index )
        {
            const JSONValue order    = orders.at( index );
            int64           orderMs  = 0;
            const bool      bHasTime = parseTime( Internal::readText( order, "time" ), orderMs );
            if ( bHasTime && orderMs > outPage._cursorMs )
                outPage._cursorMs = orderMs; // 잘리면 다음 조회는 여기서 잇는다(스팀 보고서는 쪽 토큰이 없다)
            if ( isRefundStatus( Internal::readText( order, "status" ) ) == false )
                continue;
            const string orderID = Internal::readText( order, "orderid" );
            if ( isOrderID( orderID ) == false ) // 지급 때의 거래 id 는 늘 주문 번호다 — 다른 모양은 깨진 줄이다
                continue;
            addRefund( orderID, bHasTime ? orderMs : now.count() );
        }
        outPage._bTruncated = orders.size() >= static_cast<size_t>( kReportMaxResults ) ? SW_TRUE : SW_FALSE;
    }

    void SteamReceiptValidator::onCredentialRejected( const HTTPClientResponse& response )
    {
        SW_LOG_ERROR_ID( "SteamReceiptValidator.SteamRejectedPublisherKey", "Steam rejected the publisher key (status %#) - check the key environment variable", response._statusCode );
    }
} // namespace sw
