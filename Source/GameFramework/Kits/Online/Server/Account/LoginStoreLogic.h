/**
 * @file LoginStoreLogic.h
 * @brief 로그인의 저장 부분 — 저장소 스레드에서 `IServiceStoreWork::run` 이 만들어 한 번 쓰고 버립니다. 서비스 상태는 만지지 않고 `LoginStoreOutcome` 에 적습니다.
 * @details 저장소 표(키는 ASCII): `login_account`(소문자 이름 → 계정 레코드) · `login_account_id`(계정 id 16 진 → 이름 키) · `login_session`(세션 id 16 진 → 세션 레코드) ·
 *          `login_account_session`(계정 id 16 진 → 지금 세션 id — 중복 로그인 판정과 갈음의 조건부 쓰기 자리). 레코드 첫 바이트는 형식 판(1), 다른 판은 읽지 않는다.
 *          같은 계정의 동시 로그인은 `login_account_session` 의 조건부 쓰기로 한 쪽만 이기고(진 쪽은 다시 읽어 이긴 세션을 밀어낸다 — 마지막 로그인이 이긴다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/Kits/Online/Server/Account/LoginTicketAuthority.h"
#include "GameFramework/Kits/Online/Server/Account/LoginTypes.h"

namespace sw
{
    class IServiceStoreConnection;

    /** @brief (계정, 세션) 하나 + 까닭입니다. */
    struct LoginSessionRef
    {
        uint64            _accountId{ 0 };
        uint64            _sessionId{ 0 };
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

        LoginResult registerAccount( const LoginCredential& credential, uint64* pOutAccountId );
        LoginResult login( const LoginCredential& credential, int64 nowMs, LoginGrant& outGrant );
        LoginResult resumeSession( const LoginSessionToken& token, int64 nowMs, LoginGrant& outGrant );
        LoginResult validateSession( const LoginSessionToken& token, int64 nowMs, AccountIdentity& outIdentity, LoginRevokeReason* pOutRevokeReason = nullptr );
        LoginResult logout( const LoginSessionToken& token, int64 nowMs );
        void        markDisconnected( uint64 sessionId, int64 nowMs );
        LoginResult revokeAccountSessions( uint64 accountId, int64 nowMs );
        LoginResult issueGameTicket( const LoginSessionToken& token, const hashed_string& serverId, int64 nowMs, NetGameTicket& outTicket );
        /** @brief 서비스가 고른 세션을 다시 읽어, 다른 프로세스가 밀어낸 · 끝난 세션을 `_listRevoked` 에 넣습니다. 저장소가 아프면 아무도 끊지 않는다. */
        void               refreshSessions( const vector<LoginSessionRef>& listOnline, int64 nowMs );
        [[nodiscard]] bool readIdentity( uint64 accountId, AccountIdentity& outIdentity );
        [[nodiscard]] bool readIdentityByDisplayName( string_view displayName, AccountIdentity& outIdentity );

    private:
        LoginResult recordFailure( string_view nameKey, const vector<uint8>& accountBytes, uint64 accountVersion, int64 nowMs, int64& outRetryAfterMs );
        /** @brief 없는 계정에도 해시를 한 번 돌린다(응답 시간으로 계정 유무가 드러나지 않게). */
        void burnPasswordHash( string_view password );

        IServiceStoreConnection&    _connection;
        ILoginCrypto&               _crypto;
        const LoginSettings&        _settings;
        const LoginTicketAuthority& _ticketAuthority;
        LoginStoreOutcome&          _outcome;
    };
} // namespace sw
