/**
 * @file LoginStoreLogic.h
 * @brief 로그인의 저장 부분입니다. 저장소 스레드에서 `IServiceStoreWork::run` 이 만들어 한 번 쓰고 버립니다. 서비스 상태는 건드리지 않고 `LoginStoreOutcome` 에 결과를 적습니다.
 * @details 저장소 테이블은 다음과 같고 키는 ASCII 이다.
 *          - `login_account`: 소문자 이름에서 비밀번호 레코드로 가는 테이블이다.
 *          - `login_account_id`: 계정 id(16진)에서 계정 프로필로 가는 테이블이다. 프로필에는 이름 키(없으면 빈 문자열), 표시 이름, 게스트 여부, 탈퇴 예약이 있다.
 *          - `login_session`: 세션 id(16진)에서 세션 레코드로 가는 테이블이다.
 *          - `login_account_session`: 계정 id(16진)에서 현재 세션 id 로 가는 테이블이다. 중복 로그인을 판정하고 교체하는 조건부 쓰기가 여기에서 일어난다.
 *          - `account_guest`: 장치 비밀 다이제스트(16진 64자)에서 계정 id 로 가는 테이블이다.
 *          - `login_external`: `<제공자>/<주체 다이제스트 16진 64자>` 에서 계정 id 로 가는 테이블이다.
 *          - `login_account_external`: `<계정 16진>/<제공자>` 에서 주체 다이제스트로 가는 테이블이다. 연동 목록과 해제에 쓴다.
 *          - `account_deletion`: `<예약 시각 16진>/<계정 16진>` 키이며 지울 순서를 찾는 인덱스이다.
 *          - `login_account_secret`: `<계정 16진>/<제공자>` 에서 연동 비밀(암호화된 refresh token)로 가는 테이블이다. 같은 제공자로 로그인할 때마다 덮어쓴다.
 *          - `login_revoke_queue`: `<제공자>/<계정 16진>` 에서 암호화된 연동 비밀로 가는 테이블이다. 탈퇴 정리와 연동 해제가 비밀을 여기로 옮기고, 서비스가 제공자에 철회한 뒤 지운다.
 *
 *          레코드의 첫 바이트는 형식 버전(1)이며 다른 버전은 읽지 않는다. 비밀번호, 장치 비밀, 외부 주체는 다이제스트만 저장한다.
 *          같은 계정의 동시 로그인은 `login_account_session` 의 조건부 쓰기로 한쪽만 이기고, 진 쪽은 다시 읽어 이긴 세션을 밀어낸다. 마지막 로그인이 이기는 것이다.
 *          연동은 이름과 외부 주체 레코드를 "없어야 함" 조건으로 넣는다. 이미 다른 계정의 것이면 `AlreadyLinked` 이며, 계정을 자동으로 합치지 않는 것은 사용자가 정한 정책이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/LoginTypes.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginTicketAuthority.h"

namespace sw
{
    enum class ServiceStoreResult : uint8;

    class IServiceStoreConnection;
    class ServiceTransaction;

    /** @brief 철회 대기열의 항목 하나입니다(탈퇴한 계정의 연동 비밀 — 암호화된 채로). */
    struct LoginRevocationEntry
    {
        vector<uint8> _sealedSecret{};
        string        _key{}; ///< 대기열의 키(`<제공자>/<계정 16진>`). 철회가 끝나면 이 키로 지운다
        string        _provider{};
        uint64        _accountID{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief (계정, 세션) 한 쌍과 이유입니다. */
    struct LoginSessionRef
    {
        uint64            _accountID{ 0 };
        uint64            _sessionID{ 0 };
        LoginRevokeReason _reason{ LoginRevokeReason::None };
    };
} // namespace sw

namespace sw
{
    /** @brief 저장 부분이 서비스에 넘기는 부작용입니다. `applyCompletion` 이 밀려남, 오프라인, 끊김, 온라인, 이벤트 순서로 적용합니다. */
    struct LoginStoreOutcome
    {
        vector<LoginSessionRef> _listRevoked{};      ///< 이 프로세스에 접속해 있으면 `Revoked` 이벤트를 내고 접속 테이블에서 뺀다
        vector<LoginSessionRef> _listOffline{};      ///< 접속 테이블의 세션이 이것이면 뺀다. 이벤트는 `_listEvent` 가 가진다
        vector<LoginSessionRef> _listDisconnected{}; ///< 접속 테이블의 세션이 이것이면 빼고 `Disconnected` 이벤트를 낸다
        vector<LoginSessionRef> _listOnline{};       ///< 접속 테이블에 넣는다. 같은 계정의 다른 세션이 있으면 그것은 Revoked 가 된다
        vector<LoginEvent>      _listEvent{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class LoginStoreLogic
     * @brief 로그인 흐름의 저장 부분입니다. 작업 하나가 도는 동안만 사는 객체입니다.
     */
    class LoginStoreLogic
    {
    public:
        LoginStoreLogic( IServiceStoreConnection& connection, ILoginCrypto& crypto, const LoginServiceConfig& settings, const LoginTicketAuthority& ticketAuthority,
                         LoginStoreOutcome& outOutcome );

        LoginResult registerAccount( const LoginCredential& credential, uint64* pOutAccountID );
        LoginResult login( const LoginCredential& credential, int64 nowMs, LoginGrant& outGrant );
        /** @brief 장치 비밀로 들어옵니다. 그 장치의 게스트 계정이 없으면 만들고 `_bCreated` 를 켭니다. 비밀은 저장하지 않고 다이제스트만 저장합니다. */
        LoginResult guestLogin( const uint8 ( &arrDeviceSecret )[LoginConstant::kDeviceSecretSize], int64 nowMs, LoginGrant& outGrant );
        /** @brief 제공자가 확인한 주체로 들어옵니다. 연결된 계정이 없으면 만들고 `_bCreated` 를 켜며, 표시 이름은 @p displayNameHint 에서 가져옵니다. */
        LoginResult platformLogin( string_view provider, string_view subject, string_view displayNameHint, const vector<uint8>* pSealedLinkSecret, int64 nowMs,
                                   LoginGrant& outGrant );
        LoginResult resumeSession( const LoginSessionToken& token, int64 nowMs, LoginGrant& outGrant );
        LoginResult validateSession( const LoginSessionToken& token, int64 nowMs, AccountIdentity& outIdentity, LoginRevokeReason* pOutRevokeReason = nullptr );
        LoginResult logout( const LoginSessionToken& token, int64 nowMs );
        void        markDisconnected( uint64 sessionID, int64 nowMs );
        LoginResult revokeAccountSessions( uint64 accountID, LoginRevokeReason reason, int64 nowMs );
        LoginResult issueGameTicket( const LoginSessionToken& token, const hashed_string& serverID, int64 nowMs, NetGameTicket& outTicket );

        /** @brief 세션의 계정에 이름과 비밀번호를 붙여 게스트를 정식 계정으로 바꿉니다. 이 계정에 이미 이름이 있거나 그 이름이 다른 계정의 것이면 `AlreadyLinked` 입니다. */
        LoginResult linkCredential( const LoginSessionToken& token, const LoginCredential& credential, int64 nowMs, AccountIdentity& outIdentity );
        /** @brief 세션의 계정에 외부 계정을 붙입니다. 그 주체가 다른 계정의 것이거나 이 계정에 그 제공자가 이미 있으면 `AlreadyLinked` 입니다. */
        LoginResult linkPlatform( const LoginSessionToken& token, string_view provider, string_view subject, const vector<uint8>* pSealedLinkSecret, int64 nowMs,
                                  AccountIdentity& outIdentity );
        /** @brief 외부 계정 연결을 해제합니다. 마지막 로그인 수단(이름과 외부 계정의 합이 1)이면 `LastLoginMethod` 입니다. */
        LoginResult unlinkPlatform( const LoginSessionToken& token, string_view provider, int64 nowMs );
        LoginResult listLinks( const LoginSessionToken& token, int64 nowMs, AccountLinkSummary& outSummary );
        /** @brief 탈퇴를 예약하고 세션을 끊습니다. 유예 시간은 `_deletionGraceMs` 이며, 유예 안에 다시 로그인해 `cancelDeletion` 으로 취소할 수 있습니다. 이미 예약돼 있으면 그 시각을 반환합니다. */
        LoginResult requestDeletion( const LoginSessionToken& token, int64 nowMs, int64& outDueMs );
        LoginResult cancelDeletion( const LoginSessionToken& token, int64 nowMs );
        /**
         * @brief 예약 시각이 지난 계정을 @p maxCount 개까지 지웁니다. 이름, 프로필, 게스트, 외부 연결, 세션을 지웁니다.
         * @details 원장과 감사 기록은 남긴다. 거기에는 계정 id 만 있고 개인정보는 없다.
         * @return 지운 계정 수입니다. 저장소에 문제가 생기면 그 지점에서 멈춥니다.
         */
        int32 purgeDueDeletions( int64 nowMs, int32 maxCount );

        /** @brief 서비스가 고른 세션을 다시 읽어, 다른 프로세스가 밀어냈거나 끝난 세션을 `_listRevoked` 에 넣습니다. 저장소에 문제가 있으면 아무도 끊지 않습니다. */
        void refreshSessions( const vector<LoginSessionRef>& listOnline, int64 nowMs );
        /** @brief 철회 대기열을 @p maxCount 개까지 읽어 @p outListEntry 뒤에 붙입니다(키 순서). 키가 깨진 항목은 건너뜁니다. */
        [[nodiscard]] static ServiceStoreResult listRevocations( IServiceStoreConnection& connection, int32 maxCount, vector<LoginRevocationEntry>& outListEntry );
        /** @brief 철회가 끝난 항목을 지웁니다. */
        [[nodiscard]] static ServiceStoreResult eraseRevocation( IServiceStoreConnection& connection, string_view key );
        /** @brief 계정 id 의 공개 신원을 반환합니다. 이름 인덱스 `AccountNameIndex` 가 다른 키트의 저장소 작업 안에서 부릅니다. */
        [[nodiscard]] static ServiceStoreResult readIdentity( IServiceStoreConnection& connection, uint64 accountID, AccountIdentity& outIdentity );
        /** @brief 정식 계정을 이름으로 찾습니다. 정식 계정은 소문자 로그인 이름이 곧 표시 이름입니다. 규칙에 맞지 않는 이름이면 NotFound 를 반환합니다. */
        [[nodiscard]] static ServiceStoreResult readIdentityByDisplayName( IServiceStoreConnection& connection, string_view displayName, AccountIdentity& outIdentity );

    private:
        LoginResult recordFailure( string_view nameKey, const vector<uint8>& accountBytes, uint64 accountVersion, int64 nowMs, int64& outRetryAfterMs );
        /** @brief 없는 계정에도 해시를 한 번 돌립니다. 응답 시간으로 계정 유무가 드러나지 않게 하려는 것입니다. */
        void burnPasswordHash( string_view password );
        /** @brief 로그인을 막는 제재(정지, 영구 정지)가 있으면 `AccountSuspended` 와 종료 시각, 사유를 @p outGrant 에 채웁니다. 읽지 못하면 `StoreUnavailable` 입니다. */
        LoginResult evaluateSanction( uint64 accountID, int64 nowMs, LoginGrant& outGrant );
        /** @brief 이전 세션을 정책대로 거절하거나 묘비로 바꾸고, 새 세션과 계정에서 세션으로 가는 연결을 @p inoutTransaction 에 붙입니다. */
        LoginResult stageSessionOpen( uint64 accountID, int64 nowMs, ServiceTransaction& inoutTransaction, LoginSessionToken& outToken, int64& outExpiresAtMs,
                                      uint64& outReplacedSessionID );
        /** @brief 커밋된 새 세션을 결과(밀려남, 온라인, 이벤트)에 적습니다. */
        void recordSessionOpened( uint64 accountID, const LoginSessionToken& token, uint64 replacedSessionID );
        /** @brief 계정의 현재 세션을 @p reason 이유의 묘비로 바꾸고 연결을 지우는 쓰기를 붙입니다. 세션이 없으면 아무것도 붙이지 않고 @p outSessionID 는 0입니다. */
        LoginResult stageSessionRevoke( uint64 accountID, LoginRevokeReason reason, int64 nowMs, ServiceTransaction& inoutTransaction, uint64& outSessionID );
        LoginResult purgeAccount( uint64 accountID, string_view deletionKey, int64 nowMs );

        IServiceStoreConnection&    _connection;
        ILoginCrypto&               _crypto;
        const LoginServiceConfig&   _settings;
        const LoginTicketAuthority& _ticketAuthority;
        LoginStoreOutcome&          _outcome;
    };
} // namespace sw
