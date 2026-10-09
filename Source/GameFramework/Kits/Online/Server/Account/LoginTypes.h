/**
 * @file LoginTypes.h
 * @brief 계정 키트(서버)의 공통 타입 — 설정 · 자격 · 발급 결과 · 사건, 암호 창구(`ILoginCrypto`)입니다. 와이어 타입(결과 · 토큰 · 표)은 공유 `GF_Account` 의 `AccountTypes.h`.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Network/Security/NetSecurityTypes.h"

#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Online/Account/Protocol/AccountTypes.h"

namespace sw
{
    /** @brief 같은 계정이 또 로그인할 때의 정책입니다. */
    enum class LoginDuplicatePolicy : uint8
    {
        KickExisting = 0, ///< 새 로그인이 옛 세션을 밀어낸다(MMO 관례 — 기본, 사용자 결정)
        RejectNew         ///< 옛 세션이 붙어 있으면 거절한다(끊겨 재접속을 기다리는 세션은 밀어낸다)
    };
} // namespace sw

namespace sw
{
    /** @brief 로그인 서비스 설정입니다. 시간은 밀리초입니다. */
    struct LoginSettings
    {
        NetPasswordHashParams _passwordHashParams{};                    ///< Argon2id — 기본 OWASP(19 MiB · 2 회 · 1 레인). 바꾸면 다음 로그인에 다시 해시
        int64                 _sessionLifetimeMs{ 12ll * 3600 * 1000 }; ///< 세션의 절대 수명 — 지나면 비밀번호로 다시 로그인
        int64                 _reconnectGraceMs{ 120 * 1000 };          ///< 끊긴 뒤 토큰으로 돌아올 수 있는 시간
        int64                 _tombstoneLifetimeMs{ 600 * 1000 };       ///< 밀려난 세션이 `Revoked` 로 답하는 시간
        int64                 _ticketLifetimeMs{ 60 * 1000 };           ///< 게임 접속 표의 시한(새 UDP 연결만 막는다)
        int64                 _lockoutMs{ 300 * 1000 };
        int64                 _attemptRefillMs{ 2000 };                    ///< 연결(주소)마다 시도 하나가 차는 간격
        int64                 _deletionGraceMs{ 30ll * 24 * 3600 * 1000 }; ///< 탈퇴 요청 뒤 지우기까지(그 안에 취소할 수 있다)
        int32                 _maxFailedCount{ 5 };                        ///< 이만큼 틀리면 잠근다
        int32                 _attemptBurst{ 10 };                         ///< 연결(주소)마다 몰아 할 수 있는 시도
        LoginDuplicatePolicy  _duplicatePolicy{ LoginDuplicatePolicy::KickExisting };
        uint8                 _bKeepGuestDeviceAfterLink{ SW_TRUE }; ///< 연동 뒤에도 같은 장치는 게스트 비밀로 들어온다
    };
} // namespace sw

namespace sw
{
    /** @brief 가입 · 로그인 자격입니다. 로그인 이름은 표시 이름이기도 하다(친 글자 그대로 보이고, 찾기는 대소문자 무시). */
    struct LoginCredential
    {
        string _loginName{};
        string _password{};
    };
} // namespace sw

namespace sw
{
    /** @brief 로그인 서비스에서 생긴 일입니다 — 바인딩이 연결을 닫거나 다시 묶고, 채팅 · 거래가 접속 여부를 바꾼다. */
    struct LoginEvent
    {
        enum class Kind : uint8
        {
            LoggedIn = 0, ///< 새 세션
            Resumed,      ///< 토큰으로 돌아왔다(같은 세션, 새 비밀)
            Revoked,      ///< 세션이 밀려났다 — `_reason`. 이 서버에 붙어 있던 것이면 바인딩이 그 연결을 닫는다
            LoggedOut,
            Disconnected ///< 끊겨 재접속 유예에 들어갔다
        };
        uint64            _accountId{ 0 };
        uint64            _sessionId{ 0 };
        LoginRevokeReason _reason{ LoginRevokeReason::None };
        Kind              _kind{ Kind::LoggedIn };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ILoginCrypto
     * @brief 로그인 키트가 쓰는 암호 원시 연산의 창구입니다. 실제 구현은 `INetSecurityProvider`(OpenSSL)를 감싼 `NetSecurityLoginCrypto`, 시험은 결정적 가짜.
     * @details 모두 스레드 안전이어야 한다(해시는 저장소 워커에서, 접속 표 확인은 네트워크 스레드에서 부른다). 실패하면 false — 부르는 쪽은 `StoreUnavailable` 처럼 다룬다.
     */
    class SW_GF_API ILoginCrypto
    {
    public:
        ILoginCrypto()          = default;
        virtual ~ILoginCrypto() = default;

        ILoginCrypto( const ILoginCrypto& )            = delete;
        ILoginCrypto& operator=( const ILoginCrypto& ) = delete;

        /** @brief 운영체제 암호 난수입니다. */
        [[nodiscard]] virtual bool fillRandom( uint8* pOut, int32 size ) = 0;
        /** @brief 느린 비밀번호 해시(Argon2id)입니다. */
        [[nodiscard]] virtual bool computePasswordHash( string_view password, const uint8* pSalt, int32 saltSize, const NetPasswordHashParams& params, uint8* pOut,
                                                        int32 outSize ) = 0;
        /** @brief 키 있는 해시(HKDF-SHA256 — 추출 + 확장, 정보 = @p pInfo)입니다. 토큰 다이제스트 · 표 태그 · 표 비밀 · 장치 · 외부 주체 다이제스트가 쓴다. */
        [[nodiscard]] virtual bool computeKeyedHash( const uint8* pKey, int32 keySize, const uint8* pInfo, int32 infoSize, uint8* pOut, int32 outSize ) = 0;
    };
} // namespace sw
