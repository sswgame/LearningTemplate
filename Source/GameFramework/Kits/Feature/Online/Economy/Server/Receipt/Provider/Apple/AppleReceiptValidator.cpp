#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/Provider/Apple/AppleReceiptValidator.h"

#include "Core/Foundation/Constants.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/JSON/JSONWriter.h"

#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Security/JSONWebToken.h"
#include "GameFramework/Runtime/Online/HTTP/HTTPClient.h"

namespace sw
{
    namespace
    {
        struct AppleReceiptValidatorInternal
        {
            static constexpr size_t      kMaxTransactionIDSize  = 20;
            static constexpr int64       kAuthLifetimeMs        = 20 * constant::time::kSecondsPerMinute * constant::time::kMillisecondsPerSecond;
            static constexpr int64       kAuthRefreshMarginMs   = 5 * constant::time::kSecondsPerMinute * constant::time::kMillisecondsPerSecond;
            static constexpr const utf8* kEnvironmentProduction = "Production";
            static constexpr const utf8* kEnvironmentSandbox    = "Sandbox";
        };
    } // namespace
} // namespace sw

namespace sw
{
    AppleReceiptValidator::AppleReceiptValidator( AppleReceiptSettings settings, INetSecurityProvider* pSecurity, HTTPClient* pHTTPClient )
        : StoreReceiptValidator{
              settings._buyerRule, ReceiptPollWindow{ settings._historyPollIntervalMs, settings._historyLookbackMs, settings.getHistoryMaxAgeMs() },
              pHTTPClient
    },
        _settings{ std::move( settings ) },
        _authorization{},
        _pSecurity{ pSecurity },
        _authExpiresWallMs{ 0 }
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
        string url = _settings._baseURL + "/inApps/v1/notifications/history";
        if ( paginationToken.empty() == false )
            url += "?paginationToken=" + HTTPUtil::encodePercent( paginationToken );
        return url;
    }

    optional<JWTVerified> AppleReceiptValidator::verifyJws( string_view compact, int64 unixSeconds ) const
    {
        JWTUnverified token;
        if ( token.parse( compact ) == false || token.getAlgorithm() != NetSignatureAlgorithm::EcdsaP256Sha256 )
            return nullopt;
        const bool   bMarkers = _settings._bRequireAppleMarkers == SW_TRUE;
        NetPublicKey leafKey;
        const bool   bChainOk = _pSecurity->verifyCertificateChain( token.getCertificateChain(), _settings._trustedRootsPem, unixSeconds,
                                                                  bMarkers ? string_view( AppleReceiptSettings::kLeafOid ) : string_view{},
                                                                  bMarkers ? string_view( AppleReceiptSettings::kIntermediateOid ) : string_view{}, leafKey );
        if ( bChainOk == false )
            return nullopt;
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
        writer.valueInt( expiresMs / constant::time::kMillisecondsPerSecond );
        writer.key( "aud" );
        writer.valueString( AppleReceiptSettings::kAudience );
        writer.key( "bid" );
        writer.valueString( _settings._bundleID );
        writer.endObject();
        const auto payload = writer.finish();
        string     token;
        const bool bSigned =
            payload.has_value() && JWTUtil::makeSigned( *_pSecurity, NetSignatureAlgorithm::EcdsaP256Sha256, _settings._keyID, *payload, _settings._privateKeyPem, token );
        if ( bSigned == false )
        {
            SW_LOG_ERROR_ID( "AppleReceiptValidator.CouldNotSignApp", "Could not sign the App Store Server API token with the key '%#'", _settings._keyID.c_str() );
            return {};
        }
        _authorization     = "Bearer " + token;
        _authExpiresWallMs = expiresMs;
        return _authorization;
    }

    expected<void> AppleReceiptValidator::readPayload( string_view payload, ReceiptAsk& outAsk ) const
    {
        if ( isTransactionID( payload ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "apple receipt must be the numeric transaction id" } );
        outAsk._transactionID = string( payload );
        return {};
    }

    HTTPClientRequest AppleReceiptValidator::makeHTTPRequest( const ReceiptCall& call, UnixMilliseconds now )
    {
        HTTPClientRequest request;
        request._timeoutMs = _settings._requestTimeoutMs;
        request._listHeader.push_back( HTTPHeader{ "Authorization", makeAuthorization( now ) } );
        request._listHeader.push_back( HTTPHeader{ "Accept", "application/json" } );
        if ( call._kind != ReceiptCall::Kind::RefundPage )
        {
            request._url = makeTransactionURL( call._ask._transactionID );
            return request;
        }
        string     body;
        JSONWriter writer{ body };
        writer.beginObject();
        writer.key( "startDate" );
        writer.valueInt( getRefundWindow().getFromMs() );
        writer.key( "endDate" );
        writer.valueInt( getRefundWindow().getToMs() );
        writer.key( "notificationType" );
        writer.valueString( "REFUND" );
        writer.endObject();
        request._method    = HTTPMethod::Post;
        request._url       = makeHistoryURL( call._pageToken );
        request._bodyBytes = StringUtil::toBytes( body );
        request._listHeader.push_back( HTTPHeader{ "Content-Type", "application/json" } );
        return request;
    }

    void AppleReceiptValidator::parseOrder( const JSONValue& root, const ReceiptCall& call, UnixMilliseconds now, ReceiptOrderFacts& outFacts )
    {
        using Internal                         = AppleReceiptValidatorInternal;
        const JSONValue             signedInfo = root.get( "signedTransactionInfo" );
        const optional<JWTVerified> verified   = signedInfo.isString() ? verifyJws( signedInfo.asString(), now.toSeconds() ) : nullopt;
        if ( verified.has_value() == false )
        {
            // 영수증은 거래 id 뿐이고 이 글은 TLS 너머 애플이 준다 — 확인이 안 되면 루트 파일이나 중간 장비 탓이지 "그런 거래가 없다" 가 아니다. Invalid 면 결제한 사람의 영수증을 버린다.
            outFacts._status      = ReceiptStatus::Retry;
            outFacts._failureText = "the signed transaction does not verify against the Apple root";
            SW_LOG_ERROR_ID( "AppleReceiptValidator.SignedTransactionAppleDoes", "The signed transaction %# from Apple does not verify - check rootCertificateFile", call._ask._transactionID.c_str() );
            return;
        }
        const JWTVerified& token = *verified;
        (void)token.findText( "transactionId", outFacts._transactionID );  // 없으면 틀이 거절한다
        (void)token.findText( "productId", outFacts._productID );          // 없으면 틀이 거절한다(환불이면 상관없다)
        (void)token.findText( "appAccountToken", outFacts._storeBuyerID ); // 클라이언트가 구매 때 찍은 값(`ReceiptBuyerToken`)
        int64 purchaseMs = 0;
        if ( token.findInteger( "purchaseDate", purchaseMs ) )
            outFacts._purchaseTimeMs = purchaseMs; // 이 숫자는 밀리초다(findInteger 가 초 클레임용으로 쓰지만 값은 그대로 읽는다)
        string bundleID;
        if ( token.findText( "bundleId", bundleID ) == false || bundleID != _settings._bundleID )
        {
            outFacts._status      = ReceiptStatus::Invalid;
            outFacts._failureText = "the signed transaction belongs to another bundle";
            return;
        }
        string environment;
        if ( token.findText( "environment", environment ) == false )
        {
            outFacts._status      = ReceiptStatus::Invalid; // 서명한 답이 환경을 말하지 않으면 운영 결제로 치지 않는다
            outFacts._failureText = "the signed transaction has no environment";
            return;
        }
        // 운영이라고 정확히 말한 것만 운영 결제다 — Sandbox 말고도 Xcode · LocalTesting 같은 시험 환경이 있다(기억에 의존). 샌드박스 서버의 답은 모두 샌드박스다.
        outFacts._bSandbox = environment != Internal::kEnvironmentProduction || _settings._bSandboxServer == SW_TRUE ? SW_TRUE : SW_FALSE;
        int64 revocationMs = 0;
        if ( token.findInteger( "revocationDate", revocationMs ) )
        {
            outFacts._status      = ReceiptStatus::Refunded;
            outFacts._failureText = "apple revoked this transaction";
            return;
        }
        outFacts._status = ReceiptStatus::Valid;
    }

    void AppleReceiptValidator::parseRefundPage( const JSONValue& root, UnixMilliseconds now, ReceiptRefundPage& outPage )
    {
        using Internal                  = AppleReceiptValidatorInternal;
        const utf8*     pOurEnvironment = _settings._bSandboxServer == SW_TRUE ? Internal::kEnvironmentSandbox : Internal::kEnvironmentProduction;
        const JSONValue history         = root.get( "notificationHistory" );
        for ( size_t index = 0; history.isArray() && index < history.size(); ++index )
        {
            const JSONValue             signedPayload = history.at( index ).get( "signedPayload" );
            const optional<JWTVerified> notification  = signedPayload.isString() ? verifyJws( signedPayload.asString(), now.toSeconds() ) : nullopt;
            if ( notification.has_value() == false )
            {
                outPage._bIncomplete = SW_TRUE; // 읽지 못한 알림이 환불일 수 있다 — 구간을 옮기지 않는다
                SW_LOG_ERROR_ID( "AppleReceiptValidator.AppleNotificationDoesNot", "An Apple notification does not verify against the Apple root - the next poll reads it again" );
                continue;
            }
            string     notificationType;
            string     bundleID;
            string     environment;
            string     signedTransaction;
            const bool bShapeOk = notification->findText( "notificationType", notificationType ) && notification->findTextPath( "data.bundleId", bundleID ) &&
                                  notification->findTextPath( "data.environment", environment ) && notification->findTextPath( "data.signedTransactionInfo", signedTransaction );
            const bool bOurRefund = bShapeOk && notificationType == "REFUND" && bundleID == _settings._bundleID && environment == pOurEnvironment;
            if ( bOurRefund == false )
                continue;
            const optional<JWTVerified> transaction = verifyJws( signedTransaction, now.toSeconds() );
            if ( transaction.has_value() == false )
            {
                outPage._bIncomplete = SW_TRUE;
                SW_LOG_ERROR_ID( "AppleReceiptValidator.AppleRefundTransactionDoesNot", "The transaction of an Apple refund notification does not verify - the next poll reads it again" );
                continue;
            }
            string     transactionID;
            string     transactionBundleID;
            const bool bOurTransaction = transaction->findText( "transactionId", transactionID ) && isTransactionID( transactionID ) &&
                                         transaction->findText( "bundleId", transactionBundleID ) && transactionBundleID == _settings._bundleID;
            if ( bOurTransaction == false ) // 지급 때의 거래 id 는 늘 숫자 글이다 — 다른 모양 · 다른 앱은 깨진 줄이다
                continue;
            int64 refundedMs   = now.count();
            int64 revocationMs = 0;
            if ( transaction->findInteger( "revocationDate", revocationMs ) )
                refundedMs = revocationMs;
            addRefund( transactionID, refundedMs );
        }
        bool       bHasMore = false;
        string     paginationToken;
        const bool bMoreRead = root.readOptionalInto( "hasMore", bHasMore ) && root.readOptionalInto( "paginationToken", paginationToken );
        if ( bMoreRead == false || ( bHasMore && paginationToken.empty() ) )
        {
            outPage._bIncomplete = SW_TRUE; // 쪽이 더 있다는데 이을 수 없다
            return;
        }
        if ( bHasMore )
            outPage._nextPageToken = paginationToken;
    }

    void AppleReceiptValidator::onCredentialRejected( const HTTPClientResponse& response )
    {
        _authorization.clear(); // 우리 토큰이 거절됐다 — 다음 요청이 새로 만든다
        SW_LOG_ERROR_ID( "AppleReceiptValidator.AppleRejectedAppStore", "Apple rejected the App Store Server API token (status %#, issuer '%#', key '%#') - check issuerId, keyId and the signing key", response._statusCode, _settings._issuerID.c_str(),
                         _settings._keyID.c_str() );
    }
} // namespace sw
