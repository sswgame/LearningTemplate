/**
 * @file SteamReceiptValidator.h
 * @brief 스팀 결제 영수증 검증 제공자입니다(OnlineKits H3, 스팀 → 구글 → 애플 순서의 첫째). 스토어 이름은 `steam` 입니다. 판단은 틀(`StoreReceiptValidator`)이 하고 여기는 요청과 파싱만 적습니다.
 * @details - 영수증은 주문 번호(십진 숫자 글)입니다. 게임 서버가 스팀 결제를 열고(InitTxn) 승인을 마친(FinalizeTxn) 주문을 이 서버가 `ISteamMicroTxn/QueryTxn` 으로 확인합니다.
 *            키(퍼블리셔 웹 API 키)는 설정 파일에 쓰지 않고 환경 변수에서 읽습니다(`ReceiptProviderSet`).
 *          - 상태 대응: `Succeeded` Valid, `Refunded` · `PartialRefund` · `Chargedback` · `RefundedSuspected` · `PartialRefundSuspected` · `RefundedFraudulent` Refunded,
 *            `Init` · `Approved`(아직 마무리되지 않음) Retry, `Failed` 와 그 밖의 값 · 결과가 `OK` 가 아님 Invalid.
 *            거래 id 는 주문 번호이고 상품 id 는 첫 항목의 `itemid`(십진 글)이다 — 상점 카탈로그의 상품 id 를 그 값으로 적는다. 구매자는 답의 `steamid` 다(`ReceiptBuyerBinding::LinkedLogin`).
 *          - 환불은 `ISteamMicroTxn/GetReport` 를 주기적으로 조회해(`_reportPollIntervalMs`) 환불 상태가 된 주문을 알린다. 한 번에 1000 건까지 읽고, 지난 조회 시각에서
 *            `_reportLookbackMs` 만큼 겹쳐 읽는다. 결과가 `OK` 인 답만 구간을 옮긴다. 1000 건에서 잘리면 쪽 토큰이 없으므로 읽은 줄의 가장 늦은 시각에서 바로 잇는다.
 *          - 샌드박스 인터페이스(`ISteamMicroTxnSandbox`)를 쓰는 설정이면 결과가 샌드박스 결제로 표시된다 — 서비스가 스테이징 서버가 아니면 거절한다.
 *          시계를 읽지 않는다 — HTTP 시한 · 조회 주기 · 보고서 구간을 모두 서버 루프가 넘긴 `now`(유닉스 밀리초)로 잰다. 서비스 스레드 하나에서 쓴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/StoreReceiptValidator.h"
#include "GameFramework/Runtime/Online/HTTP/HTTPTypes.h"
#include "GameFramework/Runtime/Online/Platform/PlatformServices.h"

namespace sw
{
    class HTTPClient;
    class JSONValue;

    /** @brief 스팀 제공자의 설정입니다. 시간은 밀리초입니다. */
    struct SteamReceiptSettings
    {
        static constexpr const utf8* kDefaultBaseURL = "https://partner.steam-api.com";

        string           _appID{}; ///< 십진 숫자 글
        string           _key{};   ///< 퍼블리셔 웹 API 키(쿼리에 실을 수 있는 글)
        string           _baseURL{ kDefaultBaseURL };
        ReceiptBuyerRule _buyerRule{}; ///< 설정의 `buyerBinding`(`ReceiptProviderSet`). 스팀은 `LinkedLogin` 이나 명시적 `None` 이다
        int64            _requestTimeoutMs{ 10000 };
        int64            _reportPollIntervalMs{ 3600000 };
        int64            _reportLookbackMs{ 172800000 }; ///< 보고서를 지난 조회 시각보다 이만큼 앞에서부터 읽는다(2 일)
        uint8            _bSandbox{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class SteamReceiptValidator
     * @brief 스팀 영수증 검증 제공자입니다.
     */
    class SW_KIT_SERVER_API SteamReceiptValidator final : public StoreReceiptValidator
    {
    public:
        static constexpr int32 kReportMaxResults = 1000;

        /** @brief @p pHTTPClient 는 빌려 쓰며 이 제공자만 응답을 회수합니다. */
        SteamReceiptValidator( SteamReceiptSettings settings, HTTPClient* pHTTPClient );
        ~SteamReceiptValidator() override;

        const utf8* getStoreName() const override { return PlatformStoreName::kSteam; }

        /** @brief 주문 번호(1~20 자리 십진 숫자)면 true 입니다. 쿼리에 그대로 싣기 전의 검사다. */
        static bool isOrderID( string_view text );
        /** @brief 스팀 상태 글을 영수증 상태로 바꿉니다. 모르는 값은 Invalid 입니다. */
        static ReceiptStatus mapOrderStatus( string_view status );
        /** @brief 환불 상태(환불 · 부분 환불 · 결제 취소 · 의심)면 true 입니다. */
        static bool isRefundStatus( string_view status );
        /** @brief `YYYY-MM-DDTHH:MM:SS` (UTC, 끝의 `Z` 는 있어도 된다)를 유닉스 밀리초로 읽습니다. 형식이 틀리면 false 입니다. */
        [[nodiscard]] static bool parseTime( string_view text, int64& outUnixMs );
        /** @brief 유닉스 밀리초를 `YYYY-MM-DDTHH:MM:SS` (UTC)로 씁니다. */
        static string formatTime( int64 unixMs );

        string makeQueryURL( string_view orderID ) const;
        string makeReportURL( int64 sinceUnixMs ) const;

    private:
        [[nodiscard]] expected<void> readPayload( string_view payload, ReceiptAsk& outAsk ) const override;
        HTTPClientRequest            makeHTTPRequest( const ReceiptCall& call, UnixMilliseconds now ) override;
        void                         parseOrder( const JSONValue& root, const ReceiptCall& call, UnixMilliseconds now, ReceiptOrderFacts& outFacts ) override;
        void                         parseRefundPage( const JSONValue& root, UnixMilliseconds now, ReceiptRefundPage& outPage ) override;
        void                         onCredentialRejected( const HTTPClientResponse& response ) override;

    private:
        SteamReceiptSettings _settings;
    };
} // namespace sw
