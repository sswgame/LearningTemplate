/**
 * @file SteamReceiptValidator.h
 * @brief 스팀 결제 영수증 검증 제공자입니다(OnlineKits H3, 스팀 → 구글 → 애플 순서의 첫째). 스토어 이름은 `steam` 입니다.
 * @details - 영수증은 주문 번호(십진 숫자 글)입니다. 게임 서버가 스팀 결제를 열고(InitTxn) 승인을 마친(FinalizeTxn) 주문을 이 서버가 `ISteamMicroTxn/QueryTxn` 으로 확인합니다.
 *            키(퍼블리셔 웹 API 키)는 설정 파일에 쓰지 않고 환경 변수에서 읽습니다(`ReceiptProviderSet`).
 *          - 상태 대응: `Succeeded` Valid, `Refunded` · `PartialRefund` · `Chargedback` · `RefundedSuspected` · `PartialRefundSuspected` · `RefundedFraudulent` Refunded,
 *            `Init` · `Approved`(아직 마무리되지 않음) Retry, `Failed` 와 그 밖의 값 · 결과가 `OK` 가 아님 Invalid. 연결 실패 · 시한 · 5xx · 429 · 401 · 403(키 문제) 은 Retry 다.
 *            거래 id 는 주문 번호이고 상품 id 는 첫 항목의 `itemid`(십진 글)이다 — 상점 카탈로그의 상품 id 를 그 값으로 적는다.
 *          - 환불은 `ISteamMicroTxn/GetReport` 를 주기적으로 조회해(`_reportPollIntervalMs`) 환불 상태가 된 주문을 알린다. 한 번에 1000 건까지 읽고, 지난 조회 시각에서
 *            `_reportLookbackMs` 만큼 겹쳐 읽는다(겹친 주문은 한 번만 알린다). 재시작 뒤 겹친 알림은 서비스의 환불 회수가 거래 id 로 한 번만 처리한다.
 *          - 샌드박스 인터페이스(`ISteamMicroTxnSandbox`)를 쓰는 설정이면 결과가 샌드박스 결제로 표시된다 — 서비스가 스테이징 서버가 아니면 거절한다.
 *          시계를 읽지 않는다 — HTTP 시한 · 조회 주기 · 보고서 구간을 모두 서버 루프가 넘긴 `now`(유닉스 밀리초)로 잰다. 서비스 스레드 하나에서 쓴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/Base/Online/HTTP/HTTPTypes.h"
#include "GameFramework/Base/Online/Platform/PlatformServices.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/ReceiptValidator.h"

namespace sw
{
    class HTTPClient;

    /** @brief 스팀 제공자의 설정입니다. 시간은 밀리초입니다. */
    struct SteamReceiptSettings
    {
        static constexpr const utf8* kDefaultBaseURL = "https://partner.steam-api.com";

        string _appID{}; ///< 십진 숫자 글
        string _key{};   ///< 퍼블리셔 웹 API 키(쿼리에 실을 수 있는 글)
        string _baseURL{ kDefaultBaseURL };
        int64  _requestTimeoutMs{ 10000 };
        int64  _reportPollIntervalMs{ 3600000 };
        int64  _reportLookbackMs{ 172800000 }; ///< 보고서를 지난 조회 시각보다 이만큼 앞에서부터 읽는다(2 일)
        uint8  _bSandbox{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class SteamReceiptValidator
     * @brief 스팀 영수증 검증 제공자입니다.
     */
    class SW_KIT_SERVER_API SteamReceiptValidator final : public IReceiptValidator
    {
    public:
        static constexpr int32 kReportMaxResults = 1000;

        /** @brief @p pHTTPClient 는 빌려 쓰며 이 제공자만 응답을 회수합니다. */
        SteamReceiptValidator( SteamReceiptSettings settings, HTTPClient* pHTTPClient );
        ~SteamReceiptValidator() override;

        const utf8* getStoreName() const override { return PlatformStoreName::kSteam; }
        void        submitValidation( const ReceiptValidationRequest& request, UnixMilliseconds now ) override;
        /** @brief HTTP 클라이언트를 돌리고, 주기가 되면 환불 보고서를 조회하고, 끝난 응답을 처리합니다. */
        void update( UnixMilliseconds now ) override;
        void pollCompletions( vector<ReceiptValidationResult>& outListResult ) override;
        void pollRefunds( vector<ReceiptRefundNotice>& outListNotice ) override;
        void shutdown() override;

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
        struct PendingValidation
        {
            ReceiptValidationRequest _request{};
            string                   _orderID{};
        };

        void finishValidation( const PendingValidation& pending, const HTTPClientResponse& response );

        SteamReceiptSettings                     _settings;
        unordered_map<uint64, PendingValidation> _mapRequestToPending;
        vector<ReceiptValidationResult>          _listDone;
        vector<ReceiptRefundNotice>              _listRefund;
        unordered_set<string>                    _uniqueSetNotifiedRefund;
        HTTPClient*                              _pHTTPClient;
        int64                                    _nextReportMs;      ///< 다음 보고서 조회 시각
        int64                                    _lastReportUnixMs;  ///< 지난 성공한 조회의 시작 시각(0 이면 아직 성공한 적이 없다)
        int64                                    _reportStartUnixMs; ///< 지금 조회의 시작 시각
        uint64                                   _reportRequestID;   ///< 0 이면 보고서 요청이 없다
        uint8                                    _bStopped;          ///< `shutdown` 뒤에는 새 요청을 보내지 않는다
    };
} // namespace sw
