/**
 * @file LoginTypes.h
 * @brief 계정 키트(서버)의 공통 타입입니다. 설정, 자격, 발급 결과, 이벤트와 암호 인터페이스(`ILoginCrypto`)를 정의합니다. 와이어 타입(결과, 토큰, 티켓)은 공유 모듈 `GF_Account` 의 `AccountTypes.h` 에 있습니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Network/Security/NetSecurityTypes.h"

#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Account/Shared/Protocol/AccountTypes.h"

namespace sw
{
    /** @brief 같은 계정이 또 로그인할 때의 정책입니다. */
    enum class LoginDuplicatePolicy : uint8
    {
        KickExisting = 0, ///< 새 로그인이 이전 세션을 밀어낸다. MMO 의 관례이며 사용자가 정한 기본값이다
        RejectNew         ///< 이전 세션이 접속해 있으면 새 로그인을 거절한다. 끊겨서 재접속을 기다리는 세션은 밀어낸다
    };
} // namespace sw

namespace sw
{
    /** @brief 로그인 서비스 설정입니다. 시간은 밀리초입니다. */
    struct LoginSettings
    {
        NetPasswordHashParams _passwordHashParams{};                                                ///< Argon2id 해시 파라미터이다. 기본값은 OWASP 권장(19 MiB, 2회, 1레인)이며, 바꾸면 다음 로그인에서 다시 해시한다
        int64                 _sessionLifetimeMs{ 12ll * 3600 * constant::kMillisecondsPerSecond }; ///< 세션의 절대 수명이다. 지나면 비밀번호로 다시 로그인해야 한다
        int64                 _reconnectGraceMs{ 120 * constant::kMillisecondsPerSecond };          ///< 연결이 끊긴 뒤 토큰으로 돌아올 수 있는 시간이다
        int64                 _tombstoneLifetimeMs{ 600 * constant::kMillisecondsPerSecond };       ///< 밀려난 세션이 `Revoked` 로 응답하는 시간이다
        int64                 _ticketLifetimeMs{ 60 * constant::kMillisecondsPerSecond };           ///< 게임 접속 티켓의 시한이다. 새 UDP 연결만 막는다
        int64                 _lockoutMs{ 300 * constant::kMillisecondsPerSecond };
        int64                 _attemptRefillMs{ 2000 };                                                ///< 연결(주소)마다 시도 횟수 하나가 채워지는 간격이다
        int64                 _deletionGraceMs{ 30ll * 24 * 3600 * constant::kMillisecondsPerSecond }; ///< 탈퇴를 요청한 뒤 계정을 지울 때까지의 유예 시간이다. 그 안에 취소할 수 있다
        int32                 _maxFailedCount{ 5 };                                                    ///< 이만큼 틀리면 계정을 잠근다
        int32                 _attemptBurst{ 10 };                                                     ///< 연결(주소)마다 한꺼번에 할 수 있는 시도 횟수이다
        LoginDuplicatePolicy  _duplicatePolicy{ LoginDuplicatePolicy::KickExisting };
        uint8                 _bKeepGuestDeviceAfterLink{ SW_TRUE }; ///< 연동한 뒤에도 같은 장치가 게스트 비밀로 들어올 수 있게 한다
    };
} // namespace sw

namespace sw
{
    /** @brief 가입과 로그인에 쓰는 자격입니다. 로그인 이름이 표시 이름이기도 합니다. 입력한 글자 그대로 보이며, 찾을 때는 대소문자를 무시합니다. */
    struct LoginCredential
    {
        string _loginName{};
        string _password{};
    };
} // namespace sw

namespace sw
{
    /** @brief 로그인 서비스에서 일어난 일을 알리는 이벤트입니다. 바인딩이 연결을 닫거나 다시 묶고, 채팅과 거래가 접속 여부를 바꿉니다. */
    struct LoginEvent
    {
        enum class Kind : uint8
        {
            LoggedIn = 0, ///< 새 세션이다
            Resumed,      ///< 토큰으로 돌아왔다. 같은 세션이고 비밀은 새로 받았다
            Revoked,      ///< 세션이 밀려났다. 이유는 `_reason` 에 있으며, 이 서버에 접속해 있던 세션이면 바인딩이 그 연결을 닫는다
            LoggedOut,
            Disconnected ///< 연결이 끊겨 재접속 유예에 들어갔다
        };
        uint64            _accountID{ 0 };
        uint64            _sessionID{ 0 };
        LoginRevokeReason _reason{ LoginRevokeReason::None };
        Kind              _kind{ Kind::LoggedIn };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ILoginCrypto
     * @brief 로그인 키트가 쓰는 암호 연산의 인터페이스입니다. 실제 구현은 `INetSecurityProvider`(OpenSSL)를 감싼 `NetSecurityLoginCrypto` 이고, 테스트는 결과가 결정적인 가짜 구현을 씁니다.
     * @details 모든 함수는 스레드 안전해야 한다. 해시는 저장소 워커에서, 접속 티켓 확인은 네트워크 스레드에서 부르기 때문이다.
     *          실패하면 false 를 반환하며 호출하는 쪽은 `StoreUnavailable` 처럼 다룬다.
     */
    class ILoginCrypto
    {
    public:
        ILoginCrypto()          = default;
        virtual ~ILoginCrypto() = default;

        ILoginCrypto( const ILoginCrypto& )            = delete;
        ILoginCrypto& operator=( const ILoginCrypto& ) = delete;

        /** @brief 운영체제의 암호용 난수를 채웁니다. */
        [[nodiscard]] virtual bool fillRandom( uint8* pOut, int32 size ) = 0;
        /** @brief 느린 비밀번호 해시(Argon2id)를 계산합니다. */
        [[nodiscard]] virtual bool computePasswordHash( string_view password, const uint8* pSalt, int32 saltSize, const NetPasswordHashParams& params, uint8* pOut,
                                                        int32 outSize ) = 0;
        /** @brief 키를 쓰는 해시(HKDF-SHA256, 추출과 확장, 정보는 @p pInfo)를 계산합니다. 토큰 다이제스트, 티켓 태그, 티켓 비밀, 장치와 외부 주체의 다이제스트에 씁니다. */
        [[nodiscard]] virtual bool computeKeyedHash( const uint8* pKey, int32 keySize, const uint8* pInfo, int32 infoSize, uint8* pOut, int32 outSize ) = 0;
    };
} // namespace sw
