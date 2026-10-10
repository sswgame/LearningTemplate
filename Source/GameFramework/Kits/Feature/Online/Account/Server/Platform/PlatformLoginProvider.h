/**
 * @file PlatformLoginProvider.h
 * @brief 외부 계정(구글 · 애플 · 카카오 · 네이버 · 스팀 …)의 로그인 표를 확인해 그 제공자 안의 주체 id 를 돌려주는 계약입니다.
 * @details - 확인은 제공자 서버(JWKS · 프로필 API)에 묻는 일이라 기다린다 — 맡기고(`submitVerification`) 거둔다(`pollVerifications`). 모든 호출은 서비스 스레드다.
 *          - 공통 구현이 둘이다: OIDC ID 토큰(`OidcLoginProvider` — 서명 · iss · aud · exp · nonce, JWKS 캐시)과 액세스 토큰 조회(`ProfileAPILoginProvider` — 프로필 API 의
 *            주체 id 경로). 제공자는 **데이터**(`PlatformLoginProviderSettings`)가 기본이고, 코드가 필요한 예외만 `Provider/<제품>/` 폴더에 둔다. 시험 · 개발 서버는 가짜.
 *          - 주체 id(`sub`)만 키다 — 이메일은 식별자로 쓰지 않는다(비공개 릴레이 · 제공자마다 바뀐다).
 *          언리얼 Online Subsystem 의 플랫폼별 `IOnlineIdentity` 구현 · EOS Connect 의 외부 계정 연결과 같은 자리.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 끝난 확인 하나입니다. */
    struct PlatformLoginVerification
    {
        string _subject{};     ///< 성공 — 제공자 안의 주체 id(`sub`)
        string _displayName{}; ///< 성공 — 제공자가 준 표시 이름(없으면 빈 글, 계정 이름의 힌트일 뿐)
        uint64 _verificationId{ 0 };
        uint8  _bRejected{ SW_FALSE };    ///< 표가 틀렸다(서명 · 만료 · 대상 · 401)
        uint8  _bUnavailable{ SW_FALSE }; ///< 제공자에 닿지 못했다(다시 시도)

        bool isVerified() const { return _bRejected == SW_FALSE && _bUnavailable == SW_FALSE && _subject.empty() == false; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class IPlatformLoginProvider
     * @brief 외부 로그인 제공자 하나입니다. 이름은 저장 키에 든다(`[a-z0-9_]` 16 자 이하 — "google" · "apple" · "kakao" · "naver" · "fake").
     */
    class SW_GF_API IPlatformLoginProvider
    {
    public:
        IPlatformLoginProvider()          = default;
        virtual ~IPlatformLoginProvider() = default;

        IPlatformLoginProvider( const IPlatformLoginProvider& )            = delete;
        IPlatformLoginProvider& operator=( const IPlatformLoginProvider& ) = delete;

        virtual const utf8* getName() const = 0;
        /** @brief 표 확인을 맡깁니다. 0 이 아닌 확인 id 입니다(`pollVerifications` 의 짝). */
        virtual uint64 submitVerification( const vector<uint8>& ticketBytes, int64 nowMs ) = 0;
        /** @brief 끝난 확인을 @p outListVerification 뒤에 붙입니다. 붙인 수입니다. */
        virtual int32 pollVerifications( vector<PlatformLoginVerification>& outListVerification ) = 0;
        /** @brief 진행(네트워크 I/O · 시한)을 돌립니다. 서비스 `tick` 이 부른다. */
        virtual void tick( int64 nowMs ) { (void)nowMs; }
    };
} // namespace sw
