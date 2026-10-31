/**
 * @file ReceiptProviderSet.h
 * @brief 설정 목록(`ReceiptProviderConfig`)에서 실제 영수증 검증 제공자를 만들고, 제공자마다 쓸 HTTP 클라이언트와 TLS 컨텍스트를 소유합니다(OnlineKits H3).
 * @details - 설정 파일은 `{"providers":[{"kind":"steam","appId":"480","keySecretEnvironment":"<변수>","sandbox":false,"reportPollSeconds":3600},
 *            {"kind":"google","packageName":"com.example.game","serviceAccountEnvironment":"<변수>","voidedPollSeconds":3600},
 *            {"kind":"apple","issuerID":"<발급자 id>","keyId":"<키 id>","bundleId":"com.example.game","signingKeySecretEnvironment":"<변수>","rootCertificateFile":"Config/AppleRootCA-G3.pem","historyPollSeconds":3600}]}` 이다. 자격(퍼블리셔 키, 서비스 계정 키)은
 *            파일에 쓰지 않고 환경 변수에서 읽는다. 변수가 없거나 자격이 틀리면 **서버 기동이 실패한다**(가짜로 대신 뜨지 않는다). 설정 파일이 없으면 실제 제공자를 만들지 않는다.
 *          - `baseUrl` · `tokenURL` 은 비우면 제공자의 공식 주소이고, 가짜 서버(테스트)를 가리킬 때만 쓴다. `https://` 주소가 있는데 신뢰 파일(CA 번들 PEM)이 없으면 오류다.
 *          - 제공자는 응답을 자기만 회수하므로 제공자마다 HTTP 클라이언트 하나를 둔다. 경제 서비스와 레지스트리를 내린 뒤에 `shutdown` 해야 한다(레지스트리가 제공자를 빌려 쓴다).
 *          제품 이름(`steam` · `google` · `apple`)은 이 설정의 `kind` 값과 `Provider/<제품>/` 안에만 쓴다. 애플은 응답 JWS 의 `x5c` 사슬을 설정의 루트 인증서 파일까지 이어 확인한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Time/Clock.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/ReceiptValidator.h"

namespace sw
{
    class JSONValue;
    class OwnedHTTPClient;

    /** @brief 영수증 제공자 하나의 설정입니다. 쓰지 않는 칸은 비워 둡니다. */
    struct ReceiptProviderConfig
    {
        string _kind{};                        ///< 제공자 종류(`steam`, `google`, `apple`)
        string _appID{};                       ///< steam — 앱 id(십진 숫자 글)
        string _keySecretEnvironment{};        ///< steam — 퍼블리셔 웹 API 키를 담은 환경 변수 이름
        string _packageName{};                 ///< google — 앱 패키지 이름
        string _serviceAccountEnvironment{};   ///< google — 서비스 계정 JSON 을 담은 환경 변수 이름
        string _issuerID{};                    ///< apple — 앱 스토어 커넥트 발급자 id
        string _keyID{};                       ///< apple — 서명 키 id
        string _bundleID{};                    ///< apple — 앱 번들 id
        string _signingKeySecretEnvironment{}; ///< apple — 서명 키(.p8 PEM)를 담은 환경 변수 이름
        string _rootCertificateFile{};         ///< apple — 애플 루트 인증서(Apple Root CA - G3) PEM 파일(프로젝트 루트 기준)
        string _baseURL{};                     ///< 비우면 제공자의 공식 주소
        string _tokenURL{};                    ///< google — 비우면 서비스 계정의 `token_uri`
        int64  _pollSeconds{ 3600 };           ///< 환불 조회 주기(steam 보고서, google 무효 구매 목록, apple 알림 기록)
        uint8  _bSandbox{ SW_FALSE };          ///< steam · apple — 샌드박스 서버를 쓴다
    };
} // namespace sw

namespace sw
{
    /**
     * @class ReceiptProviderSet
     * @brief 서버 하나의 실제 영수증 제공자 그룹입니다. 서비스 스레드에서만 씁니다.
     */
    class SW_KIT_SERVER_API ReceiptProviderSet
    {
    public:
        ReceiptProviderSet();
        ~ReceiptProviderSet();

        ReceiptProviderSet( const ReceiptProviderSet& )            = delete;
        ReceiptProviderSet& operator=( const ReceiptProviderSet& ) = delete;

        /** @brief 제공자 파일(`OnlineProviderFile::read`)의 항목 하나를 읽습니다. 모르는 키, 모르는 종류, 빠진 필수 키는 오류입니다 — 철자가 틀린 설정이 조용히 기본값이 되지 않게 한다. */
        [[nodiscard]] static bool readEntry( const JSONValue& entry, ReceiptProviderConfig& outConfig, string& outError );

        /** @brief 설정마다 비밀 읽기, HTTP 클라이언트, TLS 컨텍스트, 제공자를 만듭니다. 하나라도 실패하면 만든 것을 모두 버리고 false 와 @p outError 입니다. */
        [[nodiscard]] bool initialize( const vector<ReceiptProviderConfig>& listSettings, string_view trustFile, const IClock& clock, string& outError );
        /** @brief HTTP 클라이언트를 먼저 종료하고 제공자와 컨텍스트를 버립니다. */
        void shutdown();

        int32              getProviderCount() const { return static_cast<int32>( _listEntry.size() ); }
        IReceiptValidator* getProvider( int32 index ) const { return _listEntry[static_cast<size_t>( index )]._provider.get(); }

    private:
        struct Entry
        {
            unique_ptr<OwnedHTTPClient>   _httpClient{}; ///< TLS 컨텍스트와 함께 소유한다
            unique_ptr<IReceiptValidator> _provider{};   ///< HTTP 클라이언트를 빌려 쓰므로 먼저 소멸한다(나중 선언)
        };

        vector<Entry> _listEntry;
    };
} // namespace sw
