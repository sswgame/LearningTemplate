#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/Provider/Google/GoogleReceiptValidator.h"

#include "Core/Foundation/Constants.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

#include "GameFramework/Runtime/Online/HTTP/HTTPClient.h"

namespace sw
{
    namespace
    {
        struct GoogleReceiptValidatorInternal
        {
            static constexpr size_t kMaxFieldSize = 512;

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

            /** @brief 키의 글입니다. 없으면 빈 글입니다. */
            static string readText( const JSONValue& object, const utf8* pKey )
            {
                string text;
                (void)object.findPathText( pKey, text );
                return text;
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
        : StoreReceiptValidator{
              settings._buyerRule, ReceiptPollWindow{ settings._voidedPollIntervalMs, settings._voidedLookbackMs, settings._voidedMaxAgeMs },
              pHTTPClient
    },
        _settings{ std::move( settings ) },
        _tokenSource{ std::move( _settings._credentials ), GoogleReceiptSettings::kScope, pSecurity, pHTTPClient, _settings._requestTimeoutMs }
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

    string GoogleReceiptValidator::makeVoidedURL( int64 sinceUnixMs, string_view pageToken ) const
    {
        StringBuilder<constant::buffer::kMax256> textBuilder;
        textBuilder.appendFormat( "/purchases/voidedpurchases?startTime=%#&maxResults=%#&type=0", sinceUnixMs, kVoidedMaxResults );
        string url = _settings._baseURL + "/androidpublisher/v3/applications/" + HTTPUtil::encodePercent( _settings._packageName ) +
                     string( textBuilder.view() );
        if ( pageToken.empty() == false )
            url += "&token=" + HTTPUtil::encodePercent( pageToken );
        return url;
    }

    expected<void> GoogleReceiptValidator::readPayload( string_view payload, ReceiptAsk& outAsk ) const
    {
        if ( splitPayload( payload, outAsk._productID, outAsk._askedKey ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "google receipt must be <product id>|<purchase token>" } );
        return {};
    }

    StoreReceiptValidator::AuthState GoogleReceiptValidator::prepareAuth( UnixMilliseconds now )
    {
        if ( _tokenSource.hasValidToken( now ) )
            return AuthState::Ready;
        return _tokenSource.request( now ) ? AuthState::Waiting : AuthState::Failed;
    }

    bool GoogleReceiptValidator::consumeAuthResponse( const HTTPClientResponse& response, UnixMilliseconds now )
    {
        const ServiceAccountTokenSource::ResponseResult result = _tokenSource.handleResponse( response, now );
        if ( result == ServiceAccountTokenSource::ResponseResult::None )
            return false;
        flushWaiting( result == ServiceAccountTokenSource::ResponseResult::Granted, now );
        return true;
    }

    HTTPClientRequest GoogleReceiptValidator::makeHTTPRequest( const ReceiptCall& call, UnixMilliseconds /*now*/ )
    {
        using Internal = GoogleReceiptValidatorInternal;
        string     url;
        HTTPMethod method = HTTPMethod::Get;
        switch ( call._kind )
        {
            case ReceiptCall::Kind::Validate:
            {
                url = makeProductURL( call._ask._productID, call._ask._askedKey );
                break;
            }
            case ReceiptCall::Kind::ConfirmDelivery:
            {
                url    = makeAcknowledgeURL( call._ask._productID, call._ask._askedKey );
                method = HTTPMethod::Post;
                break;
            }
            case ReceiptCall::Kind::RefundPage:
            {
                url = makeVoidedURL( getRefundWindow().getFromMs(), call._pageToken );
                break;
            }
        }
        return Internal::makeRequest( url, _settings._requestTimeoutMs, _tokenSource.getToken(), method );
    }

    void GoogleReceiptValidator::parseOrder( const JSONValue& root, const ReceiptCall& /*call*/, UnixMilliseconds now, ReceiptOrderFacts& outFacts )
    {
        using Internal          = GoogleReceiptValidatorInternal;
        outFacts._transactionID = Internal::readText( root, "orderId" );
        outFacts._productID     = Internal::readText( root, "productId" );                   // 구글 문서: 없을 수 있다. 없으면 주소에 넣은 상품이다
        outFacts._answeredKey   = Internal::readText( root, "purchaseToken" );               // 구글 문서: 없을 수 있다
        outFacts._storeBuyerID  = Internal::readText( root, "obfuscatedExternalAccountId" ); // 클라이언트가 구매 때 찍은 값(`ReceiptBuyerToken`)
        int64 purchaseMs        = 0;
        if ( StringUtil::parseInt64( Internal::readText( root, "purchaseTimeMillis" ), purchaseMs ) )
            outFacts._purchaseTimeMs = purchaseMs;
        int32      purchaseState        = constant::invalid_index::kInt32;
        int32      purchaseType         = constant::invalid_index::kInt32;
        int32      acknowledgementState = 1;
        const bool bPurchaseStateOk     = root.readOptionalInto( "purchaseState", purchaseState );
        const bool bPurchaseTypeOk      = root.readOptionalInto( "purchaseType", purchaseType );
        const bool bAcknowledgementOk   = root.readOptionalInto( "acknowledgementState", acknowledgementState );
        const bool bPropertiesOk        = bPurchaseStateOk && bPurchaseTypeOk && bAcknowledgementOk;
        if ( bPropertiesOk == false )
        {
            outFacts._status      = ReceiptStatus::Invalid; // 구글이 이런 칸을 이렇게 주지 않는다 — 읽을 수 없는 응답이다(오류는 JSON 읽기가 남겼다)
            outFacts._failureText = "google purchase has fields of the wrong type";
            return;
        }
        outFacts._status   = mapPurchaseState( purchaseState );
        outFacts._bSandbox = purchaseType == 0 ? SW_TRUE : SW_FALSE; // 라이선스 테스트 구매
        if ( outFacts._status != ReceiptStatus::Valid )
        {
            StringBuilder<constant::buffer::kMax256> failureTextBuilder;
            failureTextBuilder.appendFormat( "google purchase state %#", purchaseState );
            outFacts._failureText = string( failureTextBuilder.view() );
        }
        outFacts._bNeedsDeliveryConfirm = acknowledgementState == 0 ? SW_TRUE : SW_FALSE;
        outFacts._deliveryDeadlineMs    = ( outFacts._purchaseTimeMs > 0 ? outFacts._purchaseTimeMs : now.count() ) + GoogleReceiptSettings::kAcknowledgeWindowMs;
    }

    void GoogleReceiptValidator::parseRefundPage( const JSONValue& root, UnixMilliseconds now, ReceiptRefundPage& outPage )
    {
        using Internal         = GoogleReceiptValidatorInternal;
        const JSONValue voided = root.get( "voidedPurchases" );
        for ( size_t index = 0; voided.isArray() && index < voided.size(); ++index )
        {
            const JSONValue entry        = voided.at( index );
            int64           voidedMs     = now.count();
            int64           parsedVoided = 0;
            if ( StringUtil::parseInt64( Internal::readText( entry, "voidedTimeMillis" ), parsedVoided ) )
                voidedMs = parsedVoided;
            addRefund( Internal::readText( entry, "orderId" ), voidedMs );
        }
        outPage._nextPageToken = Internal::readText( root, "tokenPagination.nextPageToken" );
        // 다음 쪽 토큰 없이 상한까지 찼으면 뒤쪽을 이을 길이 없다 — 틀이 구간을 옮기지 않는다.
        const bool bFull    = voided.isArray() && voided.size() >= static_cast<size_t>( kVoidedMaxResults );
        outPage._bTruncated = bFull && outPage._nextPageToken.empty() ? SW_TRUE : SW_FALSE;
    }

    void GoogleReceiptValidator::onCredentialRejected( const HTTPClientResponse& response )
    {
        _tokenSource.invalidate(); // 토큰이 거절됐다 — 다음 요청이 새로 받는다
        SW_LOG_ERROR_ID( "GoogleReceiptValidator.GoogleRejectedAccessToken", "Google rejected the access token or the service account has no access to '%#' (status %#)", _settings._packageName.c_str(), response._statusCode );
    }
} // namespace sw
