/**
 * @file AccountTypes.h
 * @brief 계정 키트의 와이어 타입입니다. 클라이언트와 서버가 공유하며, 크기 상수, 결과 코드, 세션 종료 사유, 세션 토큰, 게임 접속 티켓, 클라이언트 정보, 빌드 버전 비교를 담고 있습니다.
 * @details 값은 와이어 형식이므로 바꾸면 `AccountProtocol::kVersion` 을 올려야 한다.
 *          서버 전용 타입(설정, 저장, 암호)은 `GF_Server_Account` 의 `LoginTypes.h` 에 있다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionMacros.h"

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
        static constexpr int32 kTokenWireSize         = 8 + kTokenSecretSize; ///< 세션 id(리틀 엔디언) 뒤에 비밀을 이어 붙인다
        static constexpr int32 kMinLoginNameSize      = 3;
        static constexpr int32 kMaxLoginNameSize      = 16;
        static constexpr int32 kMinPasswordSize       = 8;    ///< NIST 800-63B 를 따라 길이만 검사하고 조합 규칙은 두지 않는다
        static constexpr int32 kMaxPasswordSize       = 128;  ///< 느린 해시에 거대한 입력을 넣는 공격을 막기 위한 상한이다
        static constexpr int32 kDeviceSecretSize      = 32;   ///< 게스트 장치 비밀이다. 클라이언트가 처음 실행할 때 만든다
        static constexpr int32 kMaxProviderNameSize   = 16;   ///< 외부 로그인 제공자 이름 `[a-z0-9_]`
        static constexpr int32 kMaxPlatformTicketSize = 8192; ///< 외부 로그인 티켓(ID 토큰, 액세스 토큰)의 최대 크기이다
        static constexpr int32 kMaxDisplayNameSize    = 32;
        static constexpr int32 kMaxBuildTextSize      = 32;
        static constexpr int32 kMaxPlatformTextSize   = 16;
        static constexpr int32 kMaxReasonCodeSize     = 64;
    };
} // namespace sw

namespace sw
{
    /** @brief 계정 호출의 결과입니다. 와이어 형식이므로 값을 바꾸면 `AccountProtocol::kVersion` 을 올려야 하며, 새 값은 뒤에만 추가합니다. */
    ENUM()
    enum class LoginResult : uint8
    {
        Ok = 0,
        InvalidName,         ///< 가입과 연동에서 이름 형식이 틀렸다. ASCII 영숫자와 `_` 로 3~16자여야 한다
        InvalidPassword,     ///< 가입과 연동에서 비밀번호 길이가 틀렸다. 8~128바이트여야 한다
        NameTaken,           ///< 가입에서 이미 있는 이름이다
        WrongCredentials,    ///< 없는 계정이거나 틀린 비밀번호이다. 둘을 구분하지 않는다
        AccountLocked,       ///< 실패가 누적되어 잠겼다. `_retryAfterMs` 뒤에 다시 시도한다
        RateLimited,         ///< 이 연결(주소)의 시도가 너무 많다. `_retryAfterMs` 뒤에 다시 시도한다
        InvalidToken,        ///< 형식이나 비밀이 틀렸거나 없는 세션이다
        Expired,             ///< 세션 수명이나 재접속 유예 시간이 지났다
        Revoked,             ///< 다른 곳에서 로그인했거나 운영 조치로 세션에서 밀려났다. 이유는 `_revokeReason` 에 있다
        AlreadyLoggedIn,     ///< `RejectNew` 정책에서 이미 접속해 있는 계정이다
        StoreUnavailable,    ///< 저장소에 닿지 못했거나 레코드가 깨졌다. 다시 시도한다
        AlreadyLinked,       ///< 연동에서 이름이나 외부 계정이 이미 다른 계정에 묶여 있거나, 이 계정에 이미 이름이 있다. 계정을 자동으로 합치지는 않는다
        AccountSuspended,    ///< 정지 또는 영구 정지 상태이다. 종료 시각은 `_sanctionUntilMs`, 사유는 `_sanctionReasonCode` 에 있다
        UpdateRequired,      ///< 클라이언트 빌드가 최소 버전보다 낮다(상점 주소는 `_storeURL`). 온라인 스키마 해시가 달라도(`kVersionMismatch`) 이 값이며, 그때 `_storeURL` 은 비어 있다
        ProviderUnavailable, ///< 없는 제공자이거나 제공자 서버에 닿지 못했다. 다시 시도한다
        ProviderRejected,    ///< 제공자가 티켓을 거절했다. 서명, 만료, 대상이 틀린 경우이다
        LastLoginMethod,     ///< 연동 해제에서 마지막 로그인 수단은 해제할 수 없다
        NotLinked,           ///< 연동 해제에서 이 계정에 그 제공자가 연동돼 있지 않다
        InvalidRequest       ///< 본문이 깨졌거나 상한을 넘었다
    };

    /** @brief 세션이 끝난 이유입니다. 묘비와 알림에 남으며 와이어 형식입니다. */
    enum class LoginRevokeReason : uint8
    {
        None = 0,
        DuplicateLogin, ///< 같은 계정이 다른 곳에서 로그인했다
        LoggedOut,
        Administrative, ///< 운영자나 게임이 끊었다(`revokeAccountSessions`)
        Sanctioned,     ///< 정지 또는 영구 정지가 걸렸다
        AccountDeleted  ///< 계정 삭제를 요청했다
    };
} // namespace sw

namespace sw
{
    /** @brief 세션 토큰입니다. 선택자(세션 id)와 검증자(비밀)로 이루어집니다. 비밀은 클라이언트만 가지고 있고, 저장소에는 비밀의 다이제스트만 남깁니다. */
    struct SW_KIT_API LoginSessionToken
    {
        uint64 _sessionID{ 0 };
        uint8  _arrSecret[LoginConstant::kTokenSecretSize]{};

        /** @brief 와이어 바이트로 씁니다. 40 B 이며 세션 id(리틀 엔디언) 뒤에 비밀이 붙습니다. */
        void writeBytes( uint8 ( &outBytes )[LoginConstant::kTokenWireSize] ) const;
        /** @brief 와이어 바이트에서 읽습니다. 크기가 다르면 false 를 반환합니다. */
        [[nodiscard]] bool readBytes( const uint8* pData, int32 size );
        bool               isEmpty() const { return _sessionID == 0; }
    };
} // namespace sw

namespace sw
{
    /** @brief 클라이언트가 TLS 로 받는 게임(UDP) 접속 티켓입니다. 티켓은 UDP 연결 때 제시하고, 비밀은 제시하지 않고 키 유도에만 씁니다. */
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
    /** @brief 로그인과 재접속의 결과입니다. */
    struct LoginGrant
    {
        AccountIdentity   _identity{};
        LoginSessionToken _token{};
        string            _sanctionReasonCode{};                    ///< AccountSuspended 일 때의 사유 코드이다
        string            _storeURL{};                              ///< UpdateRequired 이거나 업데이트 권장일 때 원격 설정에서 가져온 상점 주소이다
        int64             _expiresAtMs{ 0 };                        ///< 세션의 절대 시한이다
        int64             _retryAfterMs{ 0 };                       ///< AccountLocked 나 RateLimited 일 때 이만큼 뒤에 다시 시도한다
        int64             _sanctionUntilMs{ 0 };                    ///< AccountSuspended 일 때의 종료 시각이다. 영구 정지는 `ServiceSanctionState::kPermanentMs` 이다
        int64             _deletionDueMs{ 0 };                      ///< 탈퇴 예약 시각이다. 0이 아니면 이때 계정을 지우며 취소할 수 있다
        uint64            _replacedSessionID{ 0 };                  ///< KickExisting 으로 밀어낸 이전 세션이다. 없으면 0이다
        LoginRevokeReason _revokeReason{ LoginRevokeReason::None }; ///< Revoked 일 때의 이유이다
        uint8             _bCreated{ SW_FALSE };                    ///< 게스트 로그인이나 외부 로그인이 새 계정을 만들었다
        uint8             _bUpdateRecommended{ SW_FALSE };          ///< 권장 빌드보다 낮다. 로그인은 성공했다
    };
} // namespace sw

namespace sw
{
    /** @brief 로그인, 게스트 로그인, 재접속 요청에 싣는 클라이언트 정보입니다. 서버가 원격 설정의 최소 빌드와 권장 빌드와 비교합니다. */
    struct AccountClientInfo
    {
        string _build{};    ///< 게임 빌드 버전이다(예: "1.4.2"). 점으로 나눈 정수열이다
        string _platform{}; ///< `[a-z0-9_]` 형식이다(예: "windows", "linux", "android", "ios")
    };
} // namespace sw

namespace sw
{
    /** @brief 계정의 로그인 수단 요약입니다. 연동 화면과 해제 규칙에 씁니다. */
    struct AccountLinkSummary
    {
        vector<string> _listProvider{};             ///< 연동한 외부 제공자 이름이다. 이름 순으로 정렬한다
        int64          _deletionDueMs{ 0 };         ///< 탈퇴 예약 시각이다. 0이면 예약이 없다
        uint8          _bHasCredential{ SW_FALSE }; ///< 이름과 비밀번호가 있다
        uint8          _bGuest{ SW_FALSE };         ///< 아직 게스트이다. 이름과 외부 계정이 없다

        /** @brief 남은 로그인 수단의 수입니다. 이름과 외부 계정을 세고, 게스트 장치는 재설치로 잃을 수 있으므로 세지 않습니다. */
        int32 getLoginMethodCount() const { return static_cast<int32>( _listProvider.size() ) + ( _bHasCredential == SW_TRUE ? 1 : 0 ); }
    };
} // namespace sw

namespace sw
{
    /** @brief 계정 키트의 보조 함수 모음입니다. */
    struct SW_KIT_API AccountUtil
    {
        /**
         * @brief 점으로 나눈 정수열 버전을 비교합니다("1.10.0" > "1.9.9"). 빈 항목은 0으로 보고, 숫자가 아닌 글자가 있으면 그 항목부터 0으로 봅니다.
         * @return 왼쪽이 작으면 음수, 같으면 0, 크면 양수를 반환합니다.
         */
        static int32 compareBuild( string_view left, string_view right );
        /** @brief 제공자나 플랫폼 이름 규칙(`[a-z0-9_]`, 1~@p maxSize 자)에 맞으면 true 입니다. */
        static bool isValidLowerToken( string_view text, int32 maxSize );
    };
} // namespace sw
