/**
 * @file AccountTypes.h
 * @brief 계정 키트의 와이어 타입(클라이언트 · 서버 공유) — 크기 상수 · 결과 · 끝난 까닭 · 세션 토큰 · 게임 접속 표 · 클라이언트 정보 · 빌드 판 비교입니다.
 * @details 값은 와이어 형식이다 — 바꾸면 `AccountProtocol::kVersion` 을 올린다. 서버 전용(설정 · 저장 · 암호)은 `GF_Server_Account` 의 `LoginTypes.h`.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 계정 키트의 크기 상수입니다. */
    struct LoginConstant
    {
        static constexpr int32 kSaltSize              = 16;
        static constexpr int32 kPasswordHashSize      = 32;
        static constexpr int32 kDigestSize            = 32;
        static constexpr int32 kTokenSecretSize       = 32;
        static constexpr int32 kTokenWireSize         = 8 + kTokenSecretSize; ///< 세션 id(LE) ‖ 비밀
        static constexpr int32 kMinLoginNameSize      = 3;
        static constexpr int32 kMaxLoginNameSize      = 16;
        static constexpr int32 kMinPasswordSize       = 8;    ///< NIST 800-63B — 길이만 본다(조합 규칙 없음)
        static constexpr int32 kMaxPasswordSize       = 128;  ///< 느린 해시에 거대한 입력을 넣는 공격을 막는다
        static constexpr int32 kDeviceSecretSize      = 32;   ///< 게스트 장치 비밀(클라이언트가 처음 실행에 만든다)
        static constexpr int32 kMaxProviderNameSize   = 16;   ///< 외부 로그인 제공자 이름 `[a-z0-9_]`
        static constexpr int32 kMaxPlatformTicketSize = 8192; ///< 외부 로그인 표(ID 토큰 · 액세스 토큰)
        static constexpr int32 kMaxDisplayNameSize    = 32;
        static constexpr int32 kMaxBuildTextSize      = 32;
        static constexpr int32 kMaxPlatformTextSize   = 16;
        static constexpr int32 kMaxReasonCodeSize     = 64;
    };
} // namespace sw

namespace sw
{
    /** @brief 계정 호출의 결과입니다. 와이어 형식이다(값을 바꾸면 `AccountProtocol::kVersion` 을 올린다 — 뒤에만 더한다). */
    enum class LoginResult : uint8
    {
        Ok = 0,
        InvalidName,         ///< 가입 · 연동 — 이름 형식(ASCII 영숫자 · _ 3..16)
        InvalidPassword,     ///< 가입 · 연동 — 길이(8..128 바이트)
        NameTaken,           ///< 가입 — 이미 있다
        WrongCredentials,    ///< 없는 계정 · 틀린 비밀번호(가리지 않는다)
        AccountLocked,       ///< 실패 누적 — `_retryAfterMs` 뒤에
        RateLimited,         ///< 이 연결(주소)의 시도가 많다 — `_retryAfterMs` 뒤에
        InvalidToken,        ///< 형식 · 비밀이 틀렸거나 없는 세션
        Expired,             ///< 세션 수명 · 재접속 유예가 지났다
        Revoked,             ///< 밀려났다(다른 곳에서 로그인 · 운영) — `_revokeReason`
        AlreadyLoggedIn,     ///< `RejectNew` 정책에서 이미 붙어 있는 계정
        StoreUnavailable,    ///< 저장소에 닿지 못했다 · 레코드가 깨졌다 — 다시 시도
        AlreadyLinked,       ///< 연동 — 이름 · 외부 계정이 이미 다른 계정에 묶였다(자동 합치기 없음) · 이 계정에 이미 이름이 있다
        AccountSuspended,    ///< 정지 · 영구 정지 — `_sanctionUntilMs` · `_sanctionReasonCode`
        UpdateRequired,      ///< 클라이언트 빌드가 최소 판보다 낮다 — `_storeURL`
        ProviderUnavailable, ///< 없는 제공자 · 제공자 서버에 닿지 못했다 — 다시 시도
        ProviderRejected,    ///< 제공자가 표를 거절했다(서명 · 만료 · 대상)
        LastLoginMethod,     ///< 연동 해제 — 마지막 로그인 수단은 풀 수 없다
        NotLinked,           ///< 연동 해제 — 이 계정에 그 제공자가 없다
        InvalidRequest       ///< 몸이 깨졌다 · 상한을 넘었다
    };

    SW_GF_API const utf8* toString( LoginResult result );

    /** @brief 세션이 끝난 까닭입니다(묘비 · 알림에 남는다). 와이어 형식이다. */
    enum class LoginRevokeReason : uint8
    {
        None = 0,
        DuplicateLogin, ///< 같은 계정이 다른 곳에서 로그인했다
        LoggedOut,
        Administrative, ///< 운영자 · 게임이 끊었다(`revokeAccountSessions`)
        Sanctioned,     ///< 정지 · 영구 정지가 걸렸다
        AccountDeleted  ///< 계정 삭제를 요청했다
    };
} // namespace sw

namespace sw
{
    /** @brief 세션 토큰 — 선택자(세션 id) + 검증자(비밀)입니다. 클라이언트만 비밀을 갖고, 저장소에는 비밀의 다이제스트만 있다. */
    struct SW_GF_API LoginSessionToken
    {
        uint64 _sessionID{ 0 };
        uint8  _arrSecret[LoginConstant::kTokenSecretSize]{};

        /** @brief 와이어 바이트(40 B — 세션 id LE ‖ 비밀)로 씁니다. */
        void writeBytes( uint8 ( &outBytes )[LoginConstant::kTokenWireSize] ) const;
        /** @brief 와이어 바이트에서 읽습니다. 크기가 다르면 false 입니다. */
        [[nodiscard]] bool readBytes( const uint8* pData, int32 size );
        bool               isEmpty() const { return _sessionID == 0; }
    };
} // namespace sw

namespace sw
{
    /** @brief 클라이언트가 받는 게임(UDP) 접속 표입니다(TLS 로) — 표는 UDP 연결 때 내밀고, 비밀은 내밀지 않고 키 유도에만 쓴다. */
    struct NetGameTicket
    {
        static constexpr int32 kBodySize   = 48;
        static constexpr int32 kTagSize    = 16;
        static constexpr int32 kTokenSize  = kBodySize + kTagSize; ///< 64
        static constexpr int32 kSecretSize = 32;

        uint8 _arrToken[kTokenSize]{};
        uint8 _arrSecret[kSecretSize]{};
        int64 _expiresAtMs{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 로그인 · 재접속의 결과입니다. */
    struct LoginGrant
    {
        AccountIdentity   _identity{};
        LoginSessionToken _token{};
        string            _sanctionReasonCode{};                    ///< AccountSuspended — 사유 코드
        string            _storeURL{};                              ///< UpdateRequired · 권장 — 원격 설정의 상점 주소
        int64             _expiresAtMs{ 0 };                        ///< 세션의 절대 시한
        int64             _retryAfterMs{ 0 };                       ///< AccountLocked · RateLimited — 이만큼 뒤에
        int64             _sanctionUntilMs{ 0 };                    ///< AccountSuspended — 끝 시각(영구 정지는 `ServiceSanctionState::kPermanentMs`)
        int64             _deletionDueMs{ 0 };                      ///< 탈퇴 예약 — 0 이 아니면 이 때 지운다(취소할 수 있다)
        uint64            _replacedSessionID{ 0 };                  ///< KickExisting 으로 밀어낸 옛 세션(없으면 0)
        LoginRevokeReason _revokeReason{ LoginRevokeReason::None }; ///< Revoked — 왜
        uint8             _bCreated{ SW_FALSE };                    ///< 게스트 · 외부 로그인이 새 계정을 만들었다
        uint8             _bUpdateRecommended{ SW_FALSE };          ///< 권장 빌드보다 낮다(로그인은 됐다)
    };
} // namespace sw

namespace sw
{
    /** @brief 로그인 · 게스트 · 재접속 요청에 싣는 클라이언트 정보 — 서버가 원격 설정의 최소 · 권장 빌드와 견준다. */
    struct AccountClientInfo
    {
        string _build{};    ///< 게임 빌드 판 "1.4.2"(점으로 나눈 정수열)
        string _platform{}; ///< `[a-z0-9_]` — "windows" · "linux" · "android" · "ios"
    };
} // namespace sw

namespace sw
{
    /** @brief 계정의 로그인 수단 요약입니다(연동 화면 · 해제 규칙). */
    struct AccountLinkSummary
    {
        vector<string> _listProvider{};             ///< 연동한 외부 제공자 이름(이름 순)
        int64          _deletionDueMs{ 0 };         ///< 탈퇴 예약 시각(0 = 없음)
        uint8          _bHasCredential{ SW_FALSE }; ///< 이름 · 비밀번호가 있다
        uint8          _bGuest{ SW_FALSE };         ///< 아직 게스트다(이름 · 외부 계정이 없다)

        /** @brief 남은 로그인 수단 수(이름 + 외부 계정) — 게스트 장치는 세지 않는다(재설치로 잃는다). */
        int32 getLoginMethodCount() const { return static_cast<int32>( _listProvider.size() ) + ( _bHasCredential == SW_TRUE ? 1 : 0 ); }
    };
} // namespace sw

namespace sw
{
    /** @brief 계정 키트 도우미입니다. */
    struct SW_GF_API AccountUtil
    {
        /**
         * @brief 점으로 나눈 정수열 판을 견줍니다("1.10.0" > "1.9.9"). 빈 칸은 0, 숫자가 아닌 글자가 있으면 그 칸부터 0 으로 본다.
         * @return 왼쪽이 작으면 음수, 같으면 0, 크면 양수
         */
        static int32 compareBuild( string_view left, string_view right );
        /** @brief 제공자 · 플랫폼 이름 규칙(`[a-z0-9_]`, 1..@p maxSize)인가입니다. */
        static bool isValidLowerToken( string_view text, int32 maxSize );
    };
} // namespace sw
