/**
 * @file AppleReceiptValidator.h
 * @brief 애플 앱 스토어 결제 영수증 검증 제공자입니다(OnlineKits H3, 스팀 → 구글 → 애플 순서의 셋째). 스토어 이름은 `apple` 입니다.
 * @details App Store Server API 를 씁니다. 영수증은 StoreKit 2 의 거래 id(십진 숫자 글)이다.
 *          - 인증: 서버가 서명 키(.p8 PEM)로 만든 JWT(ES256: `kid` = 키 id, `iss` = 발급자 id, `aud` = `appstoreconnect-v1`, `bid` = 번들 id, 수명 20 분)를 `Authorization: Bearer` 로 보낸다.
 *            서명 키는 설정 파일에 쓰지 않고 환경 변수에서 읽는다(`ReceiptProviderSet`).
 *          - 검증: `GET /inApps/v1/transactions/{id}` 의 `signedTransactionInfo`(JWS)를 확인한다. JWS 머리의 `x5c` 사슬을 설정이 정한 애플 루트 인증서(`rootCertificateFile`)까지 이어
 *            (`INetSecurityProvider::verifyCertificateChain` — 리프와 그 발급자의 애플 확장 표지 포함) 확인하고 리프 키로 서명을 확인한다. 번들 id 와 거래 id 가 요청과 같아야 한다.
 *            `revocationDate` 가 있으면 환불, 없으면 지급이다. `environment` 가 Sandbox 면 샌드박스 결제로 표시한다.
 *          - 상태 대응: 200 + 확인 통과 Valid/Refunded, 확인 실패 · 404 · 그 밖의 4xx Invalid, 401(우리 JWT 가 거절됨) · 429 · 5xx · 전송 실패 Retry.
 *          - 환불: `POST /inApps/v1/notification/history`(종류 `REFUND`)를 주기 조회해(`_historyPollIntervalMs`) 알림의 `signedPayload` 와 그 안의 `signedTransactionInfo` 를 확인하고 거래 id 를 알린다.
 *            서버 알림(웹훅)은 바깥에서 받는 시설이 있어야 해서 쓰지 않는다. 쪽(`hasMore`)이 이어지면 `paginationToken` 으로 한 주기에 열 쪽까지 읽는다.
 *            지난 성공 시각보다 이틀 앞에서 읽고 겹친 거래는 한 번만 알린다.
 *          - `sandbox` 설정이면 샌드박스 서버(`api.storekit-sandbox`)를 쓴다(스테이징 서버).
 *          시계를 읽지 않는다 — HTTP 시한 · JWT · 서명 검증 시각 · 조회 주기 · 이력 구간을 모두 서버 루프가 넘긴 `now`(유닉스 밀리초)로 잰다. 서비스 스레드 하나에서 쓴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/Base/Online/HTTP/HTTPTypes.h"
#include "GameFramework/Base/Online/Security/JSONWebToken.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/ReceiptValidator.h"

#include <optional>

namespace sw
{
    class HTTPClient;
    class INetSecurityProvider;

    /** @brief 애플 제공자의 설정입니다. 시간은 밀리초입니다. */
    struct AppleReceiptSettings
    {
        static constexpr const utf8* kProductionBaseURL = "https://api.storekit.itunes.apple.com";
        static constexpr const utf8* kSandboxBaseURL    = "https://api.storekit-sandbox.itunes.apple.com";
        static constexpr const utf8* kAudience          = "appstoreconnect-v1";
        static constexpr const utf8* kLeafOid           = "1.2.840.113635.100.6.11.1"; ///< 영수증 서명 리프 인증서의 애플 표지
        static constexpr const utf8* kIntermediateOid   = "1.2.840.113635.100.6.2.1";  ///< 그 리프를 발급한 중간 인증서의 애플 표지

        string _issuerID{};
        string _keyID{};
        string _privateKeyPem{}; ///< 서명 키(.p8)
        string _bundleID{};
        string _baseURL{ kProductionBaseURL };
        string _trustedRootsPem{}; ///< 애플 루트 인증서(Apple Root CA - G3) PEM
        int64  _requestTimeoutMs{ 10000 };
        int64  _historyPollIntervalMs{ 3600000 };
        int64  _historyLookbackMs{ 172800000 };  ///< 알림 기록을 지난 성공 조회 시각보다 이만큼 앞에서부터 읽는다(2 일)
        uint8  _bRequireAppleMarkers{ SW_TRUE }; ///< 리프와 발급 인증서의 애플 확장 표지를 요구한다. 끄는 것은 표지를 만들지 않는 가짜 사슬(테스트)뿐이다
        uint8  _bSandboxServer{ SW_FALSE };      ///< 샌드박스 서버를 쓰는 설정이다(결과를 샌드박스 결제로 표시한다)
    };
} // namespace sw

namespace sw
{
    /**
     * @class AppleReceiptValidator
     * @brief 애플 영수증 검증 제공자입니다.
     */
    class SW_KIT_SERVER_API AppleReceiptValidator final : public IReceiptValidator
    {
    public:
        static constexpr int32 kHistoryMaxPages = 10;

        /** @brief @p pHTTPClient 는 빌려 쓰며 이 제공자만 응답을 회수합니다. */
        AppleReceiptValidator( AppleReceiptSettings settings, INetSecurityProvider* pSecurity, HTTPClient* pHTTPClient );
        ~AppleReceiptValidator() override;

        const utf8* getStoreName() const override { return "apple"; }
        void        submitValidation( const ReceiptValidationRequest& request, UnixMilliseconds now ) override;
        /** @brief HTTP 클라이언트를 돌리고, 주기가 되면 환불 알림 기록을 조회하고, 끝난 응답을 처리합니다. */
        void update( UnixMilliseconds now ) override;
        void pollCompletions( vector<ReceiptValidationResult>& outListResult ) override;
        void pollRefunds( vector<ReceiptRefundNotice>& outListNotice ) override;
        void shutdown() override;

        /** @brief 거래 id(1~20 자리 십진 숫자)면 true 입니다. 경로에 그대로 싣기 전의 검사다. */
        static bool isTransactionID( string_view text );
        /** @brief JWS 를 확인합니다 — `x5c` 사슬이 애플 루트까지 이어지고 리프 키로 서명이 맞아야 본문이 담긴 값이고, 아니면 비어 있다. */
        [[nodiscard]] std::optional<JWTVerified> verifyJws( string_view compact, int64 unixSeconds ) const;

        string makeTransactionURL( string_view transactionID ) const;
        string makeHistoryURL( string_view paginationToken ) const;

    private:
        struct Call
        {
            enum class Kind : uint8
            {
                Validate = 0,
                History
            };

            ReceiptValidationRequest _request{}; ///< Validate
            string                   _transactionID{};
            int32                    _page{ 0 }; ///< History — 몇 번째 쪽인가
            Kind                     _kind{ Kind::Validate };
        };

        string makeAuthorization( UnixMilliseconds now );
        void   submitCall( const Call& call, string_view paginationToken, UnixMilliseconds now );
        void   startHistory( UnixMilliseconds now );
        void   finishValidation( const Call& call, const HTTPClientResponse& response, UnixMilliseconds now );
        void   finishHistory( const Call& call, const HTTPClientResponse& response, UnixMilliseconds now );
        void   completeRetry( const ReceiptValidationRequest& request, const utf8* pText );

        AppleReceiptSettings            _settings;
        string                          _authorization;
        unordered_map<uint64, Call>     _mapRequestToCall;
        vector<ReceiptValidationResult> _listDone;
        vector<ReceiptRefundNotice>     _listRefund;
        unordered_set<string>           _uniqueSetNotifiedRefund;
        INetSecurityProvider*           _pSecurity;
        HTTPClient*                     _pHTTPClient;
        int64                           _authExpiresWallMs;
        int64                           _nextHistoryMs;      ///< 다음 알림 기록 조회 시각
        int64                           _lastHistoryUnixMs;  ///< 지난 성공한 조회의 시작 시각(0 이면 아직 성공한 적이 없다)
        int64                           _historyStartUnixMs; ///< 지금 조회의 시작 시각
        uint8                           _bHistoryInFlight;
        uint8                           _bStopped;
    };
} // namespace sw
