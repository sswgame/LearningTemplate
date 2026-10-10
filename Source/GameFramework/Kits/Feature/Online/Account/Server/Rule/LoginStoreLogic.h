/**
 * @file LoginStoreLogic.h
 * @brief 로그인의 저장 부분 — 저장소 스레드에서 `IServiceStoreWork::run` 이 만들어 한 번 쓰고 버립니다. 서비스 상태는 만지지 않고 `LoginStoreOutcome` 에 적습니다.
 * @details 저장소 표(키는 ASCII):
 *          - `login_account`(소문자 이름 → 비밀번호 레코드) · `login_account_id`(계정 id 16 진 → 계정 프로필 — 이름 키(없으면 빈 글) · 표시 이름 · 게스트 · 탈퇴 예약)
 *          - `login_session`(세션 id 16 진 → 세션 레코드) · `login_account_session`(계정 id 16 진 → 지금 세션 id — 중복 로그인 판정과 갈음의 조건부 쓰기 자리)
 *          - `account_guest`(장치 비밀 다이제스트 16 진 64 → 계정 id) · `login_external`(`<제공자>/<주체 다이제스트 16 진 64>` → 계정 id) ·
 *            `login_account_external`(`<계정 16 진>/<제공자>` → 주체 다이제스트 — 연동 목록 · 해제) · `account_deletion`(`<예약 시각 16 진>/<계정 16 진>` — 지울 차례 색인)
 *          레코드 첫 바이트는 형식 판(1), 다른 판은 읽지 않는다. 비밀번호 · 장치 비밀 · 외부 주체는 다이제스트만 저장한다.
 *          같은 계정의 동시 로그인은 `login_account_session` 의 조건부 쓰기로 한 쪽만 이기고(진 쪽은 다시 읽어 이긴 세션을 밀어낸다 — 마지막 로그인이 이긴다).
 *          연동은 이름 · 외부 주체 레코드를 "없어야 함" 으로 넣는다 — 이미 다른 계정 것이면 `AlreadyLinked`(자동 합치기 없음, 사용자 결정).
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

    /** @brief (계정, 세션) 하나 + 까닭입니다. */
    struct LoginSessionRef
    {
        uint64            _accountID{ 0 };
        uint64            _sessionID{ 0 };
        LoginRevokeReason _reason{ LoginRevokeReason::None };
    };
} // namespace sw

namespace sw
{
    /** @brief 저장 부분이 서비스에 넘기는 부작용입니다 — `applyCompletion` 이 이 순서로 적용한다: 밀려남 → 오프라인 → 끊김 → 온라인 → 사건. */
    struct LoginStoreOutcome
    {
        vector<LoginSessionRef> _listRevoked{};      ///< 이 프로세스에 붙어 있으면 `Revoked` 사건 + 접속 표에서 뺀다
        vector<LoginSessionRef> _listOffline{};      ///< 접속 표의 세션이 이것이면 뺀다(사건은 `_listEvent` 가 갖는다)
        vector<LoginSessionRef> _listDisconnected{}; ///< 접속 표의 세션이 이것이면 빼고 `Disconnected` 사건
        vector<LoginSessionRef> _listOnline{};       ///< 접속 표에 넣는다(같은 계정의 다른 세션이 있으면 그것은 Revoked)
        vector<LoginEvent>      _listEvent{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class LoginStoreLogic
     * @brief 로그인 흐름의 저장 부분입니다(일 하나 동안 사는 객체).
     */
    class LoginStoreLogic
    {
    public:
        LoginStoreLogic( IServiceStoreConnection& connection, ILoginCrypto& crypto, const LoginSettings& settings, const LoginTicketAuthority& ticketAuthority,
                         LoginStoreOutcome& outOutcome );

        LoginResult registerAccount( const LoginCredential& credential, uint64* pOutAccountID );
        LoginResult login( const LoginCredential& credential, int64 nowMs, LoginGrant& outGrant );
        /** @brief 장치 비밀로 들어옵니다 — 그 장치의 게스트 계정이 없으면 만든다(`_bCreated`). 비밀은 저장하지 않는다(다이제스트만). */
        LoginResult guestLogin( const uint8 ( &arrDeviceSecret )[LoginConstant::kDeviceSecretSize], int64 nowMs, LoginGrant& outGrant );
        /** @brief 제공자가 확인한 주체로 들어옵니다 — 연결된 계정이 없으면 만든다(`_bCreated`, 표시 이름은 @p displayNameHint 에서). */
        LoginResult platformLogin( string_view provider, string_view subject, string_view displayNameHint, int64 nowMs, LoginGrant& outGrant );
        LoginResult resumeSession( const LoginSessionToken& token, int64 nowMs, LoginGrant& outGrant );
        LoginResult validateSession( const LoginSessionToken& token, int64 nowMs, AccountIdentity& outIdentity, LoginRevokeReason* pOutRevokeReason = nullptr );
        LoginResult logout( const LoginSessionToken& token, int64 nowMs );
        void        markDisconnected( uint64 sessionID, int64 nowMs );
        LoginResult revokeAccountSessions( uint64 accountID, LoginRevokeReason reason, int64 nowMs );
        LoginResult issueGameTicket( const LoginSessionToken& token, const hashed_string& serverID, int64 nowMs, NetGameTicket& outTicket );

        /** @brief 세션의 계정에 이름 · 비밀번호를 붙입니다(게스트 → 정식). 이 계정에 이미 이름이 있거나 이름이 다른 계정 것이면 `AlreadyLinked`. */
        LoginResult linkCredential( const LoginSessionToken& token, const LoginCredential& credential, int64 nowMs, AccountIdentity& outIdentity );
        /** @brief 세션의 계정에 외부 계정을 붙입니다. 그 주체가 다른 계정 것이거나 이 계정에 그 제공자가 이미 있으면 `AlreadyLinked`. */
        LoginResult linkPlatform( const LoginSessionToken& token, string_view provider, string_view subject, int64 nowMs, AccountIdentity& outIdentity );
        /** @brief 외부 계정 연결을 풉니다. 마지막 로그인 수단(이름 + 외부 계정 수가 1)이면 `LastLoginMethod`. */
        LoginResult unlinkPlatform( const LoginSessionToken& token, string_view provider, int64 nowMs );
        LoginResult listLinks( const LoginSessionToken& token, int64 nowMs, AccountLinkSummary& outSummary );
        /** @brief 탈퇴를 예약합니다(유예 `_deletionGraceMs`) — 세션을 끊는다. 유예 안에 다시 로그인해 `cancelDeletion` 할 수 있다. 이미 예약돼 있으면 그 시각입니다. */
        LoginResult requestDeletion( const LoginSessionToken& token, int64 nowMs, int64& outDueMs );
        LoginResult cancelDeletion( const LoginSessionToken& token, int64 nowMs );
        /**
         * @brief 예약 시각이 지난 계정을 @p maxCount 개까지 지웁니다 — 이름 · 프로필 · 게스트 · 외부 연결 · 세션. 원장 · 감사 줄은 남긴다(계정 id 만 있다 — 개인정보 없음).
         * @return 지운 계정 수(저장소가 아프면 그 자리에서 멈춘다)
         */
        int32 purgeDueDeletions( int64 nowMs, int32 maxCount );

        /** @brief 서비스가 고른 세션을 다시 읽어, 다른 프로세스가 밀어낸 · 끝난 세션을 `_listRevoked` 에 넣습니다. 저장소가 아프면 아무도 끊지 않는다. */
        void refreshSessions( const vector<LoginSessionRef>& listOnline, int64 nowMs );
        /** @brief 계정 id 의 공개 신원입니다(이름 색인 `AccountNameIndex` 가 다른 키트의 저장소 일 안에서 부른다). */
        [[nodiscard]] static ServiceStoreResult readIdentity( IServiceStoreConnection& connection, uint64 accountID, AccountIdentity& outIdentity );
        /** @brief 정식 계정(소문자 로그인 이름 = 표시 이름)을 이름으로 찾습니다. 규칙 밖 이름이면 NotFound. */
        [[nodiscard]] static ServiceStoreResult readIdentityByDisplayName( IServiceStoreConnection& connection, string_view displayName, AccountIdentity& outIdentity );

    private:
        LoginResult recordFailure( string_view nameKey, const vector<uint8>& accountBytes, uint64 accountVersion, int64 nowMs, int64& outRetryAfterMs );
        /** @brief 없는 계정에도 해시를 한 번 돌린다(응답 시간으로 계정 유무가 드러나지 않게). */
        void burnPasswordHash( string_view password );
        /** @brief 로그인을 막는 제재(정지 · 영구 정지)가 있으면 `AccountSuspended` 와 끝 시각 · 사유를 @p outGrant 에. 읽지 못하면 `StoreUnavailable`. */
        LoginResult evaluateSanction( uint64 accountID, int64 nowMs, LoginGrant& outGrant );
        /** @brief 옛 세션을 정책대로 거절하거나 묘비로 바꾸고 새 세션 · 계정 → 세션 연결을 @p inoutTransaction 에 붙입니다. */
        LoginResult stageSessionOpen( uint64 accountID, int64 nowMs, ServiceTransaction& inoutTransaction, LoginSessionToken& outToken, int64& outExpiresAtMs,
                                      uint64& outReplacedSessionID );
        /** @brief 커밋이 된 새 세션을 결과(밀려남 · 온라인 · 사건)에 적습니다. */
        void recordSessionOpened( uint64 accountID, const LoginSessionToken& token, uint64 replacedSessionID );
        /** @brief 계정의 지금 세션을 @p reason 의 묘비로 바꾸고 연결을 지우는 쓰기를 붙입니다. 세션이 없으면 아무것도 붙이지 않고 @p outSessionID 는 0. */
        LoginResult stageSessionRevoke( uint64 accountID, LoginRevokeReason reason, int64 nowMs, ServiceTransaction& inoutTransaction, uint64& outSessionID );
        LoginResult purgeAccount( uint64 accountID, string_view deletionKey, int64 nowMs );

        IServiceStoreConnection&    _connection;
        ILoginCrypto&               _crypto;
        const LoginSettings&        _settings;
        const LoginTicketAuthority& _ticketAuthority;
        LoginStoreOutcome&          _outcome;
    };
} // namespace sw
