/**
 * @file GoogleReceiptValidator.h
 * @brief 구글 플레이 결제 영수증 검증 제공자입니다(OnlineKits H3, 스팀 → 구글 → 애플 순서의 둘째). 스토어 이름은 `google` 입니다.
 * @details - 영수증은 `<상품 id>|<구매 토큰>` 입니다. 서버가 서비스 계정 OAuth 토큰(범위 `androidpublisher`, `ServiceAccountTokenSource`)으로
 *            `purchases.products.get` 을 불러 확인합니다. 서비스 계정 JSON 은 설정 파일에 쓰지 않고 환경 변수에서 읽습니다(`ReceiptProviderSet`).
 *          - 상태 대응: `purchaseState` 0(구매됨) Valid, 1(취소됨) Refunded, 2(보류) Retry. 404 · 410 · 400(모르는 토큰) Invalid, 401 · 403(토큰 거절 · 권한 없음)은 토큰을 버리고 Retry,
 *            429 · 5xx · 전송 실패 Retry. `purchaseType` 0 은 라이선스 테스트 구매라 샌드박스로 표시한다. 거래 id 는 `orderId` 다.
 *          - 구매 확인(acknowledge): 확인 상태가 0 인 Valid 결과를 낸 뒤 `:acknowledge` 를 한 번 보낸다. 3 일 안에 확인하지 않으면 구글이 환불하기 때문이다. 실패하면 같은 토큰을
 *            다시 확인할 때 다시 보낸다(확인 상태가 아직 0 이다). 소비 가능 상품의 소비(consume)는 게임이 지급을 마친 뒤의 일이라 여기서 하지 않는다.
 *          - 환불은 `purchases.voidedpurchases.list` 를 주기적으로 조회해(`_voidedPollIntervalMs`) 무효가 된 주문(`orderId`)을 알린다. 실시간 알림(RTDN)은 Pub/Sub 구독 기반 시설이 있어야
 *            하므로 쓰지 않고, 같은 정보를 조회로 받는다. 지난 조회 시각에서 `_voidedLookbackMs` 만큼 겹쳐 읽고 겹친 주문은 한 번만 알린다.
 *          시계를 읽지 않는다 — HTTP 시한 · 토큰(JWT `iat`) · 조회 주기 · 취소 목록 구간을 모두 서버 루프가 넘긴 `now`(유닉스 밀리초)로 잰다. 서비스 스레드 하나에서 쓴다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/HTTP/HTTPTypes.h"
#include "GameFramework/Base/Online/HTTP/ServiceAccountTokenSource.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/ReceiptValidator.h"

namespace sw
{
    class HTTPClient;
    class INetSecurityProvider;

    /** @brief 구글 제공자의 설정입니다. 시간은 밀리초입니다. */
    struct GoogleReceiptSettings
    {
        static constexpr const utf8* kDefaultBaseURL = "https://androidpublisher.googleapis.com";
        static constexpr const utf8* kScope          = "https://www.googleapis.com/auth/androidpublisher";

        ServiceAccountCredentials _credentials{}; ///< `_tokenURL` 은 서비스 계정의 `token_uri` 이고 설정이 덮어쓸 수 있다(가짜 서버)
        string                    _packageName{};
        string                    _baseURL{ kDefaultBaseURL };
        int64                     _requestTimeoutMs{ 10000 };
        int64                     _voidedPollIntervalMs{ 3600000 };
        int64                     _voidedLookbackMs{ 172800000 }; ///< 무효 구매 목록을 지난 조회 시각보다 이만큼 앞에서부터 읽는다(2 일)
    };
} // namespace sw

namespace sw
{
    /**
     * @class GoogleReceiptValidator
     * @brief 구글 플레이 영수증 검증 제공자입니다.
     */
    class SW_KIT_SERVER_API GoogleReceiptValidator final : public IReceiptValidator
    {
    public:
        static constexpr int32 kVoidedMaxResults = 1000;

        /** @brief @p pHTTPClient 는 빌려 쓰며 이 제공자만 응답을 회수합니다. */
        GoogleReceiptValidator( GoogleReceiptSettings settings, INetSecurityProvider* pSecurity, HTTPClient* pHTTPClient );
        ~GoogleReceiptValidator() override;

        const utf8* getStoreName() const override { return "google"; }
        void        submitValidation( const ReceiptValidationRequest& request, UnixMilliseconds now ) override;
        /** @brief HTTP 클라이언트를 돌리고, 주기가 되면 무효 구매 목록을 조회하고, 끝난 응답을 처리합니다. */
        void update( UnixMilliseconds now ) override;
        void pollCompletions( vector<ReceiptValidationResult>& outListResult ) override;
        void pollRefunds( vector<ReceiptRefundNotice>& outListNotice ) override;
        void shutdown() override;

        /** @brief 영수증 `<상품 id>|<구매 토큰>` 을 나눕니다. 두 부분이 `[A-Za-z0-9._-]`(토큰은 `_` · `-` 포함, 1~512 자)가 아니면 false 입니다. */
        static bool splitPayload( string_view payload, string& outProductID, string& outPurchaseToken );
        /** @brief `purchaseState` 를 영수증 상태로 바꿉니다. */
        static ReceiptStatus mapPurchaseState( int32 purchaseState );

        string makeProductURL( string_view productID, string_view purchaseToken ) const;
        string makeAcknowledgeURL( string_view productID, string_view purchaseToken ) const;
        string makeVoidedURL( int64 sinceUnixMs ) const;

    private:
        struct PendingValidation
        {
            ReceiptValidationRequest _request{};
            string                   _productID{};
            string                   _purchaseToken{};
        };

        /** @brief 토큰이 필요한 요청 하나입니다 — 토큰이 오면 보낸다. */
        struct WaitingCall
        {
            enum class Kind : uint8
            {
                Validate = 0,
                Acknowledge,
                Voided
            };

            PendingValidation _pending{};
            Kind              _kind{ Kind::Validate };
        };

        void submitCall( const WaitingCall& call, UnixMilliseconds now );
        void finishValidation( const PendingValidation& pending, const HTTPClientResponse& response, UnixMilliseconds now );
        void finishVoided( const HTTPClientResponse& response, UnixMilliseconds now );
        void flushWaiting( ServiceAccountTokenSource::Outcome outcome, UnixMilliseconds now );
        void completeRetry( const PendingValidation& pending, const utf8* pText );

        GoogleReceiptSettings              _settings;
        ServiceAccountTokenSource          _tokenSource;
        unordered_map<uint64, WaitingCall> _mapRequestToCall;
        vector<WaitingCall>                _listWaiting;
        vector<ReceiptValidationResult>    _listDone;
        vector<ReceiptRefundNotice>        _listRefund;
        unordered_set<string>              _uniqueSetNotifiedRefund;
        HTTPClient*                        _pHTTPClient;
        int64                              _nextVoidedMs;      ///< 다음 무효 목록 조회 시각
        int64                              _lastVoidedUnixMs;  ///< 지난 성공한 조회의 시작 시각(0 이면 아직 성공한 적이 없다)
        int64                              _voidedStartUnixMs; ///< 지금 조회의 시작 시각
        uint8                              _bVoidedInFlight;
        uint8                              _bStopped;
    };
} // namespace sw
