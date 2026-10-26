/**
 * @file PlatformLoginProvider.h
 * @brief 외부 계정(구글, 애플, 카카오, 네이버, 스팀 등)의 로그인 티켓을 확인해 그 제공자 안의 주체 id 를 돌려주는 계약입니다.
 * @details - 확인은 제공자 서버(JWKS, 프로필 API)에 묻는 일이라 기다려야 한다. 그래서 확인을 맡기고(`submitVerification`) 결과를 회수한다(`pollVerifications`). 모든 호출은 서비스 스레드에서 한다.
 *          - 공통 구현은 둘이다. OIDC ID 토큰을 확인하는 `OidcLoginProvider` 는 서명, iss, aud, exp, nonce 를 검사하고 JWKS 를 캐시한다.
 *            액세스 토큰으로 조회하는 `ProfileAPILoginProvider` 는 프로필 API 응답에서 주체 id 의 경로를 읽는다.
 *            제공자는 **데이터**(`PlatformLoginProviderConfig`)로 정의하는 것이 기본이고, 코드가 필요한 예외만 `Provider/<제품>/` 폴더에 둔다. 테스트와 개발 서버는 가짜를 쓴다.
 *          - 주체 id(`sub`)만 키로 쓴다. 이메일은 비공개 릴레이가 있고 제공자마다 바뀔 수 있으므로 식별자로 쓰지 않는다.
 *          - 탈퇴한 계정의 연동 토큰을 철회하도록 요구하는 제공자(애플, App Store 심사 지침 5.1.1(v))는 확인 결과에 연동 비밀(`_linkSecret` — 인가 코드를 교환해 받은 refresh token)을 싣고
 *            `supportsRevocation` 이 true 이며 `submitRevocation` 으로 철회한다. 비밀은 로그인 서비스가 암호화해 저장하고(`LoginSecretVault`), 탈퇴 정리가 철회 대기열로 옮긴다.
 *
 *          언리얼 Online Subsystem 의 플랫폼별 `IOnlineIdentity` 구현과 EOS Connect 의 외부 계정 연결에 해당한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 끝난 확인 하나입니다. */
    struct PlatformLoginVerification
    {
        string _subject{};     ///< 성공했을 때 제공자 안의 주체 id(`sub`)이다
        string _displayName{}; ///< 성공했을 때 제공자가 준 표시 이름이다. 없으면 빈 문자열이며 계정 이름의 힌트일 뿐이다
        string _linkSecret{};  ///< 성공했을 때 제공자가 탈퇴 철회에 쓸 비밀(refresh token)을 줬으면 그 글이다. 없으면 빈 문자열이다. 서비스가 저장소에 쓰기 전에 암호화한다
        uint64 _verificationID{ 0 };
        uint8  _bRejected{ SW_FALSE };    ///< 티켓이 틀렸다. 서명, 만료, 대상이 틀렸거나 401 을 받은 경우이다
        uint8  _bUnavailable{ SW_FALSE }; ///< 제공자에 닿지 못했다. 다시 시도한다

        bool isVerified() const { return _bRejected == SW_FALSE && _bUnavailable == SW_FALSE && _subject.empty() == false; }
    };
} // namespace sw

namespace sw
{
    /** @brief 끝난 철회 하나입니다. */
    struct PlatformLoginRevocation
    {
        uint64 _revocationID{ 0 };
        uint8  _bUnavailable{ SW_FALSE }; ///< 제공자에 닿지 못했다. 대기열에 남겨 다시 시도한다. false 면 철회했거나 제공자가 이미 무효라고 한 것이다
    };
} // namespace sw

namespace sw
{
    /**
     * @class IPlatformLoginProvider
     * @brief 외부 로그인 제공자 하나입니다. 이름은 저장 키에 들어가며 `[a-z0-9_]` 형식으로 16자 이하입니다(예: "google", "apple", "kakao", "naver", "fake").
     */
    class SW_KIT_SERVER_API IPlatformLoginProvider
    {
    public:
        IPlatformLoginProvider()          = default;
        virtual ~IPlatformLoginProvider() = default;

        IPlatformLoginProvider( const IPlatformLoginProvider& )            = delete;
        IPlatformLoginProvider& operator=( const IPlatformLoginProvider& ) = delete;

        virtual const utf8* getName() const = 0;
        /** @brief 티켓 확인을 맡깁니다. 0이 아닌 확인 id 를 반환하며 `pollVerifications` 의 결과와 짝이 됩니다. */
        virtual uint64 submitVerification( const vector<uint8>& ticketBytes, UnixMilliseconds nowMs ) = 0;
        /** @brief 끝난 확인을 @p outListVerification 뒤에 붙이고 붙인 개수를 반환합니다. */
        virtual int32 pollVerifications( vector<PlatformLoginVerification>& outListVerification ) = 0;
        /** @brief 진행 중인 작업(네트워크 I/O, 시한)을 처리합니다. 서비스 `tick` 이 부릅니다. */
        virtual void update( UnixMilliseconds nowMs ) { (void)nowMs; }

        /** @brief 탈퇴한 계정의 연동 비밀을 철회하는 제공자면 true 입니다. 기본은 false 이며 true 인 제공자만 `submitRevocation` 을 구현합니다. */
        virtual bool supportsRevocation() const { return false; }
        /** @brief 연동 비밀(`PlatformLoginVerification::_linkSecret`)의 철회를 맡깁니다. 0이 아닌 철회 id 를 반환하며 `pollRevocations` 의 결과와 짝이 됩니다. */
        virtual uint64 submitRevocation( string_view linkSecret, UnixMilliseconds nowMs )
        {
            (void)linkSecret;
            (void)nowMs;
            return 0;
        }
        /** @brief 끝난 철회를 @p outListRevocation 뒤에 붙이고 붙인 개수를 반환합니다. */
        virtual int32 pollRevocations( vector<PlatformLoginRevocation>& outListRevocation )
        {
            (void)outListRevocation;
            return 0;
        }
    };
} // namespace sw
