/**
 * @file AppleReceiptValidator.h
 * @brief 애플 앱 스토어 결제 영수증 검증 제공자입니다(OnlineKits H3, 스팀 → 구글 → 애플 순서의 셋째). 스토어 이름은 `apple` 입니다. 판단은 틀(`StoreReceiptValidator`)이 하고 여기는 요청과 파싱만 적습니다.
 * @details App Store Server API 를 씁니다. 영수증은 StoreKit 2 의 거래 id(십진 숫자 글)이다.
 *          - 인증: 서버가 서명 키(.p8 PEM)로 만든 JWT(ES256: `kid` = 키 id, `iss` = 발급자 id, `aud` = `appstoreconnect-v1`, `bid` = 번들 id, 수명 20 분)를 `Authorization: Bearer` 로 보낸다.
 *            서명 키는 설정 파일에 쓰지 않고 환경 변수에서 읽는다(`ReceiptProviderSet`).
 *          - 검증: `GET /inApps/v1/transactions/{id}` 의 `signedTransactionInfo`(JWS)를 확인한다. JWS 머리의 `x5c` 사슬을 설정이 정한 애플 루트 인증서(`rootCertificateFile`)까지 이어
 *            (`INetSecurityProvider::verifyCertificateChain` — 리프와 그 발급자의 애플 확장 표지 포함) 확인하고 리프 키로 서명을 확인한다. 번들 id 와 거래 id 가 요청과 같아야 한다.
 *            `revocationDate` 가 있으면 환불, 없으면 지급이다. `environment` 가 정확히 Production 인 것만 운영 결제이고 없으면 거절한다. 구매자는 `appAccountToken` 이다 — 클라이언트가 구매 옵션에 `ReceiptBuyerToken` 을 찍어야 온다.
 *          - 상태 대응: 200 + 확인 통과 Valid/Refunded, 번들이 다름 · 404 · 그 밖의 4xx Invalid, 401 · 403(우리 JWT 가 거절됨) · 429 · 5xx · 전송 실패 · 서명 확인 실패 Retry — 확인 실패는 루트 파일 탓일 수 있어 영수증을 버리지 않는다.
 *          - 환불: `POST /inApps/v1/notifications/history`(종류 `REFUND`)를 주기 조회해(`_historyPollIntervalMs`) 알림의 `signedPayload` 와 그 안의 `signedTransactionInfo` 를 확인하고 거래 id 를 알린다.
 *            서버 알림(웹훅)은 바깥에서 받는 시설이 있어야 해서 쓰지 않는다. 쪽(`hasMore`)이 이어지면 `paginationToken` 으로 끝까지 읽는다. 알림의 번들 · 환경과 거래의 번들이 우리 것이어야 한다.
 *            이 이력은 "애플이 우리 서버 주소로 보내려고 한 알림" 이다(애플 문서) — 앱 스토어 커넥트에 서버 알림 V2 주소를 적어 두어야 이력이 쌓인다.
 *            지난 성공 시각보다 이틀 앞에서 읽고, 확인하지 못한 알림이 있으면 구간을 옮기지 않는다. 시작점은 애플이 받는 구간(운영 180 일, 샌드박스 30 일 — `getHistoryMaxAgeMs`) 안으로 자른다.
 *          - `sandbox` 설정이면 샌드박스 서버(`api.storekit-sandbox`)를 쓴다(스테이징 서버).
 *          시계를 읽지 않는다 — HTTP 시한 · JWT · 서명 검증 시각 · 조회 주기 · 이력 구간을 모두 서버 루프가 넘긴 `now`(유닉스 밀리초)로 잰다. 서비스 스레드 하나에서 쓴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/StoreReceiptValidator.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Security/JSONWebToken.h"
#include "GameFramework/Runtime/Online/HTTP/HTTPTypes.h"

namespace sw
{
    class HTTPClient;
    class INetSecurityProvider;
    class JSONValue;

    /** @brief 애플 제공자의 설정입니다. 시간은 밀리초입니다. */
    struct AppleReceiptSettings
    {
        static constexpr const utf8* kProductionBaseURL = "https://api.storekit.itunes.apple.com";
        static constexpr const utf8* kSandboxBaseURL    = "https://api.storekit-sandbox.itunes.apple.com";
        static constexpr const utf8* kAudience          = "appstoreconnect-v1";
        static constexpr const utf8* kLeafOid           = "1.2.840.113635.100.6.11.1"; ///< 영수증 서명 리프 인증서의 애플 표지
        static constexpr const utf8* kIntermediateOid   = "1.2.840.113635.100.6.2.1";  ///< 그 리프를 발급한 중간 인증서의 애플 표지

        static constexpr int64 kProductionHistoryMaxAgeMs = 179LL * 24 * 60 * 60 * 1000; ///< 시작점의 한계(하루 여유) — 애플 문서: 운영은 지난 180 일까지 받는다
        static constexpr int64 kSandboxHistoryMaxAgeMs    = 29LL * 24 * 60 * 60 * 1000;  ///< 시작점의 한계(하루 여유) — 애플 문서: 샌드박스는 지난 30 일까지 받는다

        string           _issuerID{};
        string           _keyID{};
        string           _privateKeyPem{}; ///< 서명 키(.p8)
        string           _bundleID{};
        string           _baseURL{ kProductionBaseURL };
        string           _trustedRootsPem{}; ///< 애플 루트 인증서(Apple Root CA - G3) PEM
        ReceiptBuyerRule _buyerRule{};       ///< 설정의 `buyerBinding`(`ReceiptProviderSet`). 애플은 `AccountToken` 이나 명시적 `None` 이다
        int64            _requestTimeoutMs{ 10000 };
        int64            _historyPollIntervalMs{ 3600000 };
        int64            _historyLookbackMs{ 172800000 };  ///< 알림 기록을 지난 성공 조회 시각보다 이만큼 앞에서부터 읽는다(2 일)
        uint8            _bRequireAppleMarkers{ SW_TRUE }; ///< 리프와 발급 인증서의 애플 확장 표지를 요구한다. 끄는 것은 표지를 만들지 않는 가짜 사슬(테스트)뿐이다
        uint8            _bSandboxServer{ SW_FALSE };      ///< 샌드박스 서버를 쓰는 설정이다(결과를 샌드박스 결제로 표시한다)

        /** @brief 이 환경에서 알림 이력의 시작점으로 쓸 수 있는 가장 오랜 나이입니다. 칸으로 두지 않는다 — 서버(`_bSandboxServer`)와 어긋난 한계는 조회를 영원히 거절당하게 한다. */
        int64 getHistoryMaxAgeMs() const { return _bSandboxServer == SW_TRUE ? kSandboxHistoryMaxAgeMs : kProductionHistoryMaxAgeMs; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class AppleReceiptValidator
     * @brief 애플 영수증 검증 제공자입니다.
     */
    class SW_KIT_SERVER_API AppleReceiptValidator final : public StoreReceiptValidator
    {
    public:
        /** @brief @p pHTTPClient 는 빌려 쓰며 이 제공자만 응답을 회수합니다. */
        AppleReceiptValidator( AppleReceiptSettings settings, INetSecurityProvider* pSecurity, HTTPClient* pHTTPClient );
        ~AppleReceiptValidator() override;

        const utf8* getStoreName() const override { return "apple"; }

        /** @brief 거래 id(1~20 자리 십진 숫자)면 true 입니다. 경로에 그대로 싣기 전의 검사다. */
        static bool isTransactionID( string_view text );
        /** @brief JWS 를 확인합니다 — `x5c` 사슬이 애플 루트까지 이어지고 리프 키로 서명이 맞아야 본문이 담긴 값이고, 아니면 비어 있다. */
        [[nodiscard]] optional<JWTVerified> verifyJws( string_view compact, int64 unixSeconds ) const;

        string makeTransactionURL( string_view transactionID ) const;
        string makeHistoryURL( string_view paginationToken ) const;

    private:
        [[nodiscard]] expected<void> readPayload( string_view payload, ReceiptAsk& outAsk ) const override;
        HTTPClientRequest            makeHTTPRequest( const ReceiptCall& call, UnixMilliseconds now ) override;
        void                         parseOrder( const JSONValue& root, const ReceiptCall& call, UnixMilliseconds now, ReceiptOrderFacts& outFacts ) override;
        void                         parseRefundPage( const JSONValue& root, UnixMilliseconds now, ReceiptRefundPage& outPage ) override;
        void                         onCredentialRejected( const HTTPClientResponse& response ) override;
        string                       makeAuthorization( UnixMilliseconds now );

    private:
        AppleReceiptSettings  _settings;
        string                _authorization;
        INetSecurityProvider* _pSecurity;
        int64                 _authExpiresWallMs;
    };
} // namespace sw
