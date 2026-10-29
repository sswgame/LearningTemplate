#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/Provider/Apple/AppleReceiptValidator.h"

#include "Core/Container/StringUtil.h"
#include "Core/Foundation/Defines.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/JSON/JSONWriter.h"

#include "GameFramework/Base/Online/HTTP/HTTPClient.h"
#include "GameFramework/Base/Online/Security/JSONWebToken.h"

namespace sw
{
    SW_LOG_CALLER( "AppleReceiptValidator" );

    namespace
    {
        struct AppleReceiptValidatorInternal
        {
            static constexpr size_t      kMaxTransactionIDSize   = 20;
            static constexpr int64       kMillisecondsPerSecond  = 1000;
            static constexpr int64       kAuthLifetimeMs         = 20 * constant::kSecondsPerMinute * constant::kMillisecondsPerSecond;
            static constexpr int64       kAuthRefreshMarginMs    = 5 * constant::kSecondsPerMinute * constant::kMillisecondsPerSecond;
            static constexpr int32       kStatusTooManyRequests  = 429;
            static constexpr int32       kStatusServerError      = 500;
            static constexpr size_t      kMaxNotifiedRefundCount = 100000;
            static constexpr const utf8* kEnvironmentSandbox     = "Sandbox";

            static bool isTransientStatus( const HTTPClientResponse& response )
            {
                return response._bTransportFailed == SW_TRUE || response._statusCode == kStatusTooManyRequests || response._statusCode >= kStatusServerError ||
                       response._statusCode == HTTPConstant::kStatusUnauthorized;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    AppleReceiptValidator::AppleReceiptValidator( AppleReceiptSettings settings, INetSecurityProvider* pSecurity, HTTPClient* pHTTPClient )
        : _settings{ std::move( settings ) }
        , _authorization{}
        , _mapRequestToCall{}
        , _listDone{}
        , _listRefund{}
        , _uniqueSetNotifiedRefund{}
        , _pSecurity{ pSecurity }
        , _pHTTPClient{ pHTTPClient }
        , _authExpiresWallMs{ 0 }
        , _nextHistoryMs{ 0 }
        , _lastHistoryUnixMs{ 0 }
        , _historyStartUnixMs{ 0 }
        , _bHistoryInFlight{ SW_FALSE }
        , _bStopped{ SW_FALSE }
    {
    }

    AppleReceiptValidator::~AppleReceiptValidator()
    {
        Memory::set( _settings._privateKeyPem.data(), 0, _settings._privateKeyPem.size() ); // 서명 키를 메모리에 남기지 않는다
        Memory::set( _authorization.data(), 0, _authorization.size() );
    }

    bool AppleReceiptValidator::isTransactionID( string_view text )
    {
        if ( text.empty() || text.size() > AppleReceiptValidatorInternal::kMaxTransactionIDSize )
            return false;
        for ( const utf8 character : text )
        {
            if ( character < '0' || character > '9' )
                return false;
        }
        return true;
    }

    string AppleReceiptValidator::makeTransactionURL( string_view transactionID ) const
    {
        return _settings._baseURL + "/inApps/v1/transactions/" + string( transactionID );
    }

    string AppleReceiptValidator::makeHistoryURL( string_view paginationToken ) const
    {
        string url = _settings._baseURL + "/inApps/v1/notification/history";
        if ( paginationToken.empty() == false )
            url += "?paginationToken=" + HTTPUtil::encodePercent( paginationToken );
        return url;
    }

    std::optional<JWTVerified> AppleReceiptValidator::verifyJws( string_view compact, int64 unixSeconds ) const
    {
        JWTUnverified token;
        if ( token.parse( compact ) == false || token.getAlgorithm() != NetSignatureAlgorithm::EcdsaP256Sha256 )
            return std::nullopt;
        const bool   bMarkers = _settings._bRequireAppleMarkers == SW_TRUE;
        NetPublicKey leafKey;
        const bool   bChainOk = _pSecurity->verifyCertificateChain( token.getCertificateChain(), _settings._trustedRootsPem, unixSeconds,
                                                                  bMarkers ? string_view( AppleReceiptSettings::kLeafOid ) : string_view{},
                                                                  bMarkers ? string_view( AppleReceiptSettings::kIntermediateOid ) : string_view{}, leafKey );
        if ( bChainOk == false )
            return std::nullopt;
        // 애플 서명 거래에는 iss · aud · exp 가 없다 — 신뢰는 위에서 확인한 애플 루트 사슬의 리프 키에서 온다.
        return std::move( token ).verify( *_pSecurity, leafKey, JWTClaimRule::makeSignatureOnly() );
    }

    string AppleReceiptValidator::makeAuthorization( UnixMilliseconds now )
    {
        using Internal = AppleReceiptValidatorInternal;
        if ( _authorization.empty() == false && now.count() + Internal::kAuthRefreshMarginMs < _authExpiresWallMs )
            return _authorization;
        const int64 issuedAtSec = now.toSeconds();
        const int64 expiresMs   = now.count() + Internal::kAuthLifetimeMs;
        JSONWriter  writer;
        writer.beginObject();
        writer.key( "iss" );
        writer.valueString( _settings._issuerID );
        writer.key( "iat" );
        writer.valueInt( issuedAtSec );
        writer.key( "exp" );
        writer.valueInt( expiresMs / Internal::kMillisecondsPerSecond );
        writer.key( "aud" );
        writer.valueString( AppleReceiptSettings::kAudience );
        writer.key( "bid" );
        writer.valueString( _settings._bundleID );
        writer.endObject();
        const std::optional<JSONText> payload = writer.finish();
        string                        token;
        const bool                    bSigned =
            payload.has_value() && JWTUtil::makeSigned( *_pSecurity, NetSignatureAlgorithm::EcdsaP256Sha256, _settings._keyID, *payload, _settings._privateKeyPem, token );
        if ( bSigned == false )
        {
            SW_LOG_ERROR( "Could not sign the App Store Server API token with the key '%#'", _settings._keyID.c_str() );
            return {};
        }
        _authorization     = "Bearer " + token;
        _authExpiresWallMs = expiresMs;
        return _authorization;
    }

    void AppleReceiptValidator::submitCall( const Call& call, string_view paginationToken, UnixMilliseconds now )
    {
        using Internal = AppleReceiptValidatorInternal;
        HTTPClientRequest request;
        request._timeoutMs = _settings._requestTimeoutMs;
        request._listHeader.push_back( HTTPHeader{ "Authorization", makeAuthorization( now ) } );
        request._listHeader.push_back( HTTPHeader{ "Accept", "application/json" } );
        if ( call._kind == Call::Kind::Validate )
            request._url = makeTransactionURL( call._transactionID );
        else
        {
            string     body;
            JSONWriter writer{ body };
            writer.beginObject();
            writer.key( "startDate" );
            writer.valueInt( ( _lastHistoryUnixMs != 0 ? _lastHistoryUnixMs : _historyStartUnixMs ) - _settings._historyLookbackMs );
            writer.key( "endDate" );
            writer.valueInt( _historyStartUnixMs );
            writer.key( "notificationType" );
            writer.valueString( "REFUND" );
            writer.endObject();
            request._method    = HTTPMethod::Post;
            request._url       = makeHistoryURL( paginationToken );
            request._bodyBytes = StringUtil::toBytes( body );
            request._listHeader.push_back( HTTPHeader{ "Content-Type", "application/json" } );
        }
        _mapRequestToCall[_pHTTPClient->submitRequest( request )] = call;
    }

    void AppleReceiptValidator::submitValidation( const ReceiptValidationRequest& request, UnixMilliseconds now )
    {
        if ( isTransactionID( request._payload ) == false )
        {
            // 경로에 실을 수 없는 영수증은 애플에 묻지 않고 거절한다.
            ReceiptValidationResult& result = _listDone.emplace_back();
            result._storeName               = request._storeName;
            result._ticket                  = request._ticket;
            result._accountID               = request._accountID;
            result._status                  = ReceiptStatus::Invalid;
            result._failureText             = "apple receipt must be the numeric transaction id";
            return;
        }
        Call call;
        call._kind          = Call::Kind::Validate;
        call._request       = request;
        call._transactionID = request._payload;
        submitCall( call, {}, now );
    }

    void AppleReceiptValidator::completeRetry( const ReceiptValidationRequest& request, const utf8* pText )
    {
        ReceiptValidationResult& result = _listDone.emplace_back();
        result._storeName               = request._storeName;
        result._ticket                  = request._ticket;
        result._accountID               = request._accountID;
        result._transactionID           = request._payload;
        result._status                  = ReceiptStatus::Retry;
        result._failureText             = pText;
    }

    void AppleReceiptValidator::finishValidation( const Call& call, const HTTPClientResponse& response, UnixMilliseconds now )
    {
        using Internal = AppleReceiptValidatorInternal;
        if ( response._statusCode == HTTPConstant::kStatusUnauthorized )
        {
            _authorization.clear(); // 우리 토큰이 거절됐다 — 다음 요청이 새로 만든다
            SW_LOG_ERROR( "Apple rejected the App Store Server API token (issuer '%#', key '%#') - check issuerID, keyId and the signing key", _settings._issuerID.c_str(),
                          _settings._keyID.c_str() );
        }
        if ( Internal::isTransientStatus( response ) )
        {
            completeRetry( call._request, response._bTransportFailed == SW_TRUE ? response._failureText.c_str() : "apple is not available" );
            return;
        }
        ReceiptValidationResult& result = _listDone.emplace_back();
        result._storeName               = call._request._storeName;
        result._ticket                  = call._request._ticket;
        result._accountID               = call._request._accountID;
        result._transactionID           = call._transactionID;
        result._status                  = ReceiptStatus::Invalid;
        JSONDocument document;
        if ( response.isSuccess() == false || document.tryParse( response.getBodyText() ) == false )
        {
            result._failureText = formatToString( "apple does not know this transaction (status %#)", response._statusCode );
            return;
        }
        const JSONValue                  signedInfo = document.getRoot().get( "signedTransactionInfo" );
        const std::optional<JWTVerified> verified =
            signedInfo.isString() ? verifyJws( signedInfo.asString(), now.toSeconds() ) : std::nullopt;
        if ( verified.has_value() == false )
        {
            result._failureText = "the signed transaction does not verify against the Apple root";
            SW_LOG_ERROR( "The signed transaction %# from Apple does not verify - check rootCertificateFile", call._transactionID.c_str() );
            return;
        }
        const JWTVerified& token = *verified;
        string             transactionID;
        string             bundleID;
        const bool         bIDsOk           = token.findText( "transactionId", transactionID ) && token.findText( "bundleId", bundleID );
        const bool         bSameTransaction = bIDsOk && transactionID == call._transactionID && bundleID == _settings._bundleID;
        if ( bSameTransaction == false )
        {
            result._failureText = "the signed transaction belongs to another transaction or bundle";
            return;
        }
        const bool bHasProduct = token.findText( "productId", result._productID );
        string     environment;
        if ( token.findText( "environment", environment ) )
            result._bSandbox = environment == Internal::kEnvironmentSandbox ? SW_TRUE : SW_FALSE;
        result._bSandbox = result._bSandbox == SW_TRUE || _settings._bSandboxServer == SW_TRUE ? SW_TRUE : SW_FALSE;
        int64 purchaseMs = 0;
        if ( token.findInteger( "purchaseDate", purchaseMs ) )
            result._purchaseTimeMs = purchaseMs; // 이 숫자는 밀리초다(findInteger 가 초 클레임용으로 쓰지만 값은 그대로 읽는다)
        int64 revocationMs = 0;
        if ( token.findInteger( "revocationDate", revocationMs ) )
        {
            result._status      = ReceiptStatus::Refunded;
            result._failureText = "apple revoked this transaction";
            return;
        }
        if ( bHasProduct == false || result._productID.empty() )
        {
            result._failureText = "apple transaction has no product";
            return;
        }
        result._status = ReceiptStatus::Valid;
    }

    void AppleReceiptValidator::startHistory( UnixMilliseconds now )
    {
        _historyStartUnixMs = now.count();
        _bHistoryInFlight   = SW_TRUE;
        _nextHistoryMs      = now.count() + _settings._historyPollIntervalMs;
        Call call;
        call._kind = Call::Kind::History;
        call._page = 0;
        submitCall( call, {}, now );
    }

    void AppleReceiptValidator::finishHistory( const Call& call, const HTTPClientResponse& response, UnixMilliseconds now )
    {
        using Internal = AppleReceiptValidatorInternal;
        JSONDocument document;
        if ( response._statusCode == HTTPConstant::kStatusUnauthorized )
        {
            _authorization.clear();
            SW_LOG_ERROR( "Apple rejected the App Store Server API token for the notification history - check issuerID, keyId and the signing key" );
        }
        if ( response.isSuccess() == false || document.tryParse( response.getBodyText() ) == false )
        {
            SW_LOG_WARNING( "Apple notification history failed (status %#) - the next poll reads from the last successful one", response._statusCode );
            _bHistoryInFlight = SW_FALSE; // 지난 성공 시각을 그대로 둔다 — 다음 조회가 이번 구간까지 다시 읽는다
            return;
        }
        const JSONValue history = document.getRoot().get( "notificationHistory" );
        for ( size_t index = 0; history.isArray() && index < history.size(); ++index )
        {
            const JSONValue                  item          = history.at( index );
            const JSONValue                  signedPayload = item.get( "signedPayload" );
            const std::optional<JWTVerified> notification =
                signedPayload.isString() ? verifyJws( signedPayload.asString(), now.toSeconds() ) : std::nullopt;
            if ( notification.has_value() == false )
            {
                SW_LOG_ERROR( "An Apple notification does not verify against the Apple root - it is skipped" );
                continue;
            }
            string     notificationType;
            string     bundleID;
            string     signedTransaction;
            const bool bShapeOk = notification->findText( "notificationType", notificationType ) && notification->findTextPath( "data.bundleId", bundleID ) &&
                                  notification->findTextPath( "data.signedTransactionInfo", signedTransaction );
            const bool bOurRefund = bShapeOk && notificationType == "REFUND" && bundleID == _settings._bundleID;
            if ( bOurRefund == false )
                continue;
            const std::optional<JWTVerified> transaction = verifyJws( signedTransaction, now.toSeconds() );
            string                           transactionID;
            if ( transaction.has_value() == false || transaction->findText( "transactionId", transactionID ) == false )
                continue;
            if ( _uniqueSetNotifiedRefund.count( transactionID ) != 0 )
                continue;
            if ( _uniqueSetNotifiedRefund.size() >= Internal::kMaxNotifiedRefundCount )
                _uniqueSetNotifiedRefund.clear(); // 상한 — 겹친 알림은 서비스가 거래 id 로 한 번만 처리한다
            _uniqueSetNotifiedRefund.insert( transactionID );
            int64 refundedMs   = now.count();
            int64 revocationMs = 0;
            if ( transaction->findInteger( "revocationDate", revocationMs ) )
                refundedMs = revocationMs;
            _listRefund.push_back( ReceiptRefundNotice{ getStoreName(), transactionID, refundedMs } );
        }
        bool       bHasMore = false;
        string     paginationToken;
        const bool bMoreRead = document.getRoot().readOptionalInto( "hasMore", bHasMore ) && document.getRoot().readOptionalInto( "paginationToken", paginationToken );
        const bool bNextPage = bMoreRead && bHasMore && paginationToken.empty() == false && call._page + 1 < kHistoryMaxPages;
        if ( bNextPage )
        {
            Call next  = call;
            next._page = call._page + 1;
            submitCall( next, paginationToken, now );
            return;
        }
        _lastHistoryUnixMs = _historyStartUnixMs;
        _bHistoryInFlight  = SW_FALSE;
    }

    void AppleReceiptValidator::update( UnixMilliseconds now )
    {
        // 서버 루프의 시각(유닉스 밀리초) 하나로 HTTP 시한 · 토큰 · 서명 검증 시각 · 조회 주기 · 이력 구간을 모두 잰다.
        _pHTTPClient->update();
        const bool bMayPoll = _bStopped == SW_FALSE && _bHistoryInFlight == SW_FALSE;
        if ( bMayPoll && now.count() >= _nextHistoryMs )
            startHistory( now );
        vector<HTTPClientResponse> listResponse;
        (void)_pHTTPClient->pollResponses( listResponse );
        for ( const HTTPClientResponse& response : listResponse )
        {
            const auto callIt = _mapRequestToCall.find( response._requestID );
            if ( callIt == _mapRequestToCall.end() )
                continue;
            const Call call = std::move( callIt->second );
            _mapRequestToCall.erase( callIt );
            if ( call._kind == Call::Kind::Validate )
                finishValidation( call, response, now );
            else
                finishHistory( call, response, now );
        }
    }

    void AppleReceiptValidator::pollCompletions( vector<ReceiptValidationResult>& outListResult )
    {
        for ( ReceiptValidationResult& result : _listDone )
        {
            outListResult.push_back( std::move( result ) );
        }
        _listDone.clear();
    }

    void AppleReceiptValidator::pollRefunds( vector<ReceiptRefundNotice>& outListNotice )
    {
        for ( ReceiptRefundNotice& notice : _listRefund )
        {
            outListNotice.push_back( std::move( notice ) );
        }
        _listRefund.clear();
    }

    void AppleReceiptValidator::shutdown()
    {
        // 날아가던 검증은 같은 영수증으로 다시 시도하게 Retry 로 끝낸다.
        for ( const auto& [requestID, call] : _mapRequestToCall )
        {
            if ( call._kind == Call::Kind::Validate )
                completeRetry( call._request, "apple validation was interrupted by shutdown" );
        }
        _mapRequestToCall.clear();
        _bHistoryInFlight = SW_FALSE;
        _bStopped         = SW_TRUE;
    }
} // namespace sw
