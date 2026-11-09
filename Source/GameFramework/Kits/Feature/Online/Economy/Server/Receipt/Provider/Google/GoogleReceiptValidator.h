/**
 * @file GoogleReceiptValidator.h
 * @brief 구글 플레이 결제 영수증 검증 제공자입니다(OnlineKits H3, 스팀 → 구글 → 애플 순서의 둘째). 스토어 이름은 `google` 입니다. 판단은 틀(`StoreReceiptValidator`)이 하고 여기는 요청과 파싱만 적습니다.
 * @details - 영수증은 `<상품 id>|<구매 토큰>` 입니다. 서버가 서비스 계정 OAuth 토큰(범위 `androidpublisher`, `ServiceAccountTokenSource`)으로
 *            `purchases.products.get` 을 불러 확인합니다. 서비스 계정 JSON 은 설정 파일에 쓰지 않고 환경 변수에서 읽습니다(`ReceiptProviderSet`).
 *          - 상태 대응: `purchaseState` 0(구매됨) Valid, 1(취소됨) Refunded, 2(보류) Retry. 401 · 403 은 토큰을 버린다. `purchaseType` 0 은 라이선스 테스트 구매라 샌드박스로 표시한다. 거래 id 는 `orderId` 다.
 *            답이 `productId` · `purchaseToken` 을 말하면 물은 것과 같아야 한다. 구매자는 `obfuscatedExternalAccountId` 다 — 클라이언트가 `BillingFlowParams.setObfuscatedAccountId` 에 `ReceiptBuyerToken` 을 찍어야 온다.
 *          - 구매 확인(acknowledge): 구글 문서의 순서는 "검증 → 지급 → 확인" 이고, 3 일 안에 확인하지 않으면 자동 환불되며, 지급하지 못한 구매는 확인하면 안 된다. 그래서 여기서 보내지 않는다 — 확인 상태가 0 인 Valid 결과에 키(`_deliveryKey`)와
 *            기한(구매 시각 + 3 일)만 싣고, 서비스가 지급 분개와 한 트랜잭션으로 저장소에 남긴 뒤 `confirmDelivery` 로 맡긴다. 닿지 못하면 레코드가 남아 다시 보내고(재시작을 넘어), 기한이 지나면 구글이 환불하고 환불 조회가 지급을 회수한다.
 *            소비 가능 상품의 소비(consume)도 확인을 겸한다 — 지급 응답을 받은 클라이언트가 한다(`IStoreBillingBridge::finishPurchase`).
 *          - 환불은 `purchases.voidedpurchases.list` 를 주기적으로 조회해(`_voidedPollIntervalMs`) 무효가 된 주문(`orderID`)을 알린다. 실시간 알림(RTDN)은 Pub/Sub 구독 기반 시설이 있어야
 *            하므로 쓰지 않고, 같은 정보를 조회로 받는다. 지난 조회 시각에서 `_voidedLookbackMs` 만큼 겹쳐 읽고, 1000 건을 넘으면 `tokenPagination.nextPageToken` 을 따라 끝까지 읽는다. 시작점은 구글이 받는 30 일 안으로 자른다.
 *          시계를 읽지 않는다 — HTTP 시한 · 토큰(JWT `iat`) · 조회 주기 · 취소 목록 구간을 모두 서버 루프가 넘긴 `now`(유닉스 밀리초)로 잰다. 서비스 스레드 하나에서 쓴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/StoreReceiptValidator.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/HTTP/ServiceAccountTokenSource.h"
#include "GameFramework/Runtime/Online/HTTP/HTTPTypes.h"

namespace sw
{
    class HTTPClient;
    class INetSecurityProvider;
    class JSONValue;

    /** @brief 구글 제공자의 설정입니다. 시간은 밀리초입니다. */
    struct GoogleReceiptSettings
    {
        static constexpr const utf8* kDefaultBaseURL      = "https://androidpublisher.googleapis.com";
        static constexpr const utf8* kScope               = "https://www.googleapis.com/auth/androidpublisher";
        static constexpr int64       kAcknowledgeWindowMs = 3LL * 24 * 60 * 60 * 1000; ///< 구글 문서: 이 안에 확인하지 않으면 자동 환불된다

        ServiceAccountCredentials _credentials{}; ///< `_tokenURL` 은 서비스 계정의 `token_uri` 이고 설정이 덮어쓸 수 있다(가짜 서버)
        string                    _packageName{};
        string                    _baseURL{ kDefaultBaseURL };
        ReceiptBuyerRule          _buyerRule{}; ///< 설정의 `buyerBinding`(`ReceiptProviderSet`). 구글은 `AccountToken` 이나 명시적 `None` 이다
        int64                     _requestTimeoutMs{ 10000 };
        int64                     _voidedPollIntervalMs{ 3600000 };
        int64                     _voidedLookbackMs{ 172800000 }; ///< 무효 구매 목록을 지난 조회 시각보다 이만큼 앞에서부터 읽는다(2 일)
        int64                     _voidedMaxAgeMs{ 2505600000 };  ///< 시작점의 한계(29 일) — 구글은 지난 30 일까지만 받는다
    };
} // namespace sw

namespace sw
{
    /**
     * @class GoogleReceiptValidator
     * @brief 구글 플레이 영수증 검증 제공자입니다.
     */
    class SW_KIT_SERVER_API GoogleReceiptValidator final : public StoreReceiptValidator
    {
    public:
        static constexpr int32 kVoidedMaxResults = 1000;

        /** @brief @p pHTTPClient 는 빌려 쓰며 이 제공자만 응답을 회수합니다. */
        GoogleReceiptValidator( GoogleReceiptSettings settings, INetSecurityProvider* pSecurity, HTTPClient* pHTTPClient );
        ~GoogleReceiptValidator() override;

        const utf8* getStoreName() const override { return "google"; }
        bool        usesDeliveryConfirmation() const override { return true; }

        /** @brief 영수증 `<상품 id>|<구매 토큰>` 을 나눕니다. 두 부분이 `[A-Za-z0-9._-]`(토큰은 `_` · `-` 포함, 1~512 자)가 아니면 false 입니다. */
        static bool splitPayload( string_view payload, string& outProductID, string& outPurchaseToken );
        /** @brief `purchaseState` 를 영수증 상태로 바꿉니다. */
        static ReceiptStatus mapPurchaseState( int32 purchaseState );

        string makeProductURL( string_view productID, string_view purchaseToken ) const;
        string makeAcknowledgeURL( string_view productID, string_view purchaseToken ) const;
        string makeVoidedURL( int64 sinceUnixMs, string_view pageToken ) const;

    private:
        [[nodiscard]] expected<void> readPayload( string_view payload, ReceiptAsk& outAsk ) const override;
        HTTPClientRequest            makeHTTPRequest( const ReceiptCall& call, UnixMilliseconds now ) override;
        void                         parseOrder( const JSONValue& root, const ReceiptCall& call, UnixMilliseconds now, ReceiptOrderFacts& outFacts ) override;
        void                         parseRefundPage( const JSONValue& root, UnixMilliseconds now, ReceiptRefundPage& outPage ) override;
        void                         onCredentialRejected( const HTTPClientResponse& response ) override;
        AuthState                    prepareAuth( UnixMilliseconds now ) override;
        bool                         consumeAuthResponse( const HTTPClientResponse& response, UnixMilliseconds now ) override;

    private:
        GoogleReceiptSettings     _settings;
        ServiceAccountTokenSource _tokenSource;
    };
} // namespace sw
