/**
 * @file LoginService.h
 * @brief 로그인 서비스 — 가입 · 로그인 · 게스트 · 외부 로그인 · 재접속 · 검증 · 로그아웃 · 중복 로그인 · 연동 · 탈퇴 · 게임 접속 표. 전송을 모른다(스트림 바인딩은 `AccountServer`).
 * @details - 상태는 모두 저장소(`IServiceStore`)에 있다. 이 객체가 메모리에 드는 것은 "이 프로세스에 붙어 있는 계정" 과 시도 버킷 · 외부 확인 대기뿐이라, 서버를 다시
 *            띄워도 세션 · 계정은 그대로이고 클라이언트는 토큰으로 돌아온다(`resumeSession`).
 *          - 요청은 저장소에 일로 맡기고(해시 · 저장은 저장소 워커에서), 결과는 `drainCompletions` 로 꼬리표와 함께 거둔다. 서비스 스레드는 DB 를 기다리지 않는다.
 *            저장소의 `pollCompletions` 를 이 서비스의 스레드에서 부른다. 외부 로그인은 제공자 확인(`IPlatformLoginProvider`)을 거쳐 일을 맡긴다 — `tick` 이 거둔다.
 *          - 서버 프로세스 여럿이 한 저장소를 쓸 수 있다. 같은 계정의 동시 로그인은 마지막 로그인이 이기고(새 로그인이 옛 세션을 밀어낸다 — 기본 정책),
 *            다른 프로세스가 밀어낸 세션은 `refreshOnlineSessions`(또는 버스 알림 — 바인딩)가 알아챈다.
 *          - 로그인 · 게스트 · 외부 · 재접속은 클라이언트 빌드 판을 원격 설정(`account.minimum_build.<플랫폼>` · `account.recommended_build.<플랫폼>` ·
 *            `account.store_url.<플랫폼>`)과 견주고(낮으면 일을 맡기지 않고 `UpdateRequired`), 성공 직전에 제재(`Online/Sanction`)를 본다.
 *          - 시각은 부르는 쪽이 넘기는 벽시계 밀리초다(세션 시한이 저장소에 남아 재시작을 넘기 때문).
 *          - `IAccountDirectory` 를 구현한다 — 채팅 · 거래가 표시 이름 · 접속 여부를 여기서 본다(이 프로세스에 붙어 있는 계정만).
 *          - 내리는 순서: 저장소 `shutdown` → 저장소 `pollCompletions`(남은 완료가 이 서비스를 부른다) → 서비스 `shutdown`.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Online/Guard/TokenBucketMap.h"
#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/Base/Utility/EventBuffer.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Online/Server/Account/LoginTicketAuthority.h"
#include "GameFramework/Kits/Online/Server/Account/LoginTypes.h"
#include "GameFramework/Kits/Online/Server/Account/Platform/PlatformLoginProvider.h"

namespace sw
{
    struct LoginStoreOutcome;

    class IPlatformLoginProvider;
    class IServiceStore;
    class RemoteConfig;

    /** @brief 로그인 서비스의 작업 종류입니다. */
    enum class LoginOperation : uint8
    {
        Register = 0,
        Login,
        GuestLogin,
        PlatformLogin,
        Resume,
        Validate,
        Logout,
        IssueGameTicket,
        Revoke,
        LinkCredential,
        LinkPlatform,
        UnlinkPlatform,
        ListLinks,
        RequestDeletion,
        CancelDeletion
    };

    /** @brief 끝난 요청 하나입니다 — 바인딩이 꼬리표로 응답을 찾는다. */
    struct LoginCompletion
    {
        LoginGrant         _grant{};       ///< Login · GuestLogin · PlatformLogin · Resume(Validate 는 `_revokeReason` 만, RequestDeletion 은 `_deletionDueMs`)
        NetGameTicket      _ticket{};      ///< IssueGameTicket
        AccountIdentity    _identity{};    ///< Validate · Register(계정 id) · Link*
        AccountLinkSummary _linkSummary{}; ///< ListLinks
        uint64             _requestTag{ 0 };
        LoginResult        _result{ LoginResult::Ok };
        LoginOperation     _operation{ LoginOperation::Login };
    };
} // namespace sw

namespace sw
{
    /**
     * @class LoginService
     * @brief 로그인 서비스 하나입니다(서버 프로세스마다 하나, 서비스 스레드 하나에서 부른다).
     */
    class SW_GF_API LoginService final : public IAccountDirectory
    {
    public:
        static constexpr size_t kMaxTrackedClientCount = 4096; ///< 시도 버킷 상한 — 넘으면 가득 찬(= 새것과 같은) 버킷을 지운다

        LoginService();

        /** @brief @p pStore · @p pCrypto 는 빌려 쓴다. @p pCrypto 는 스레드 안전이어야 한다(저장소 워커에서 해시를 돌린다). @p arrTicketMasterKey 는 게임 서버와 나눠 가진 표 주 키. */
        void initialize( IServiceStore* pStore, ILoginCrypto* pCrypto, const LoginSettings& settings,
                         const uint8 ( &arrTicketMasterKey )[LoginTicketAuthority::kMasterKeySize] );
        void shutdown();

        /** @brief 외부 로그인 제공자를 올립니다(빌려 쓴다). 같은 이름이 있거나 이름 규칙(`[a-z0-9_]`)을 어기면 false. */
        [[nodiscard]] bool registerPlatformProvider( IPlatformLoginProvider* pProvider );
        /** @brief 빌드 판 확인에 쓸 원격 설정입니다(빌려 쓴다 — 없으면 확인하지 않는다). */
        void setRemoteConfig( const RemoteConfig* pRemoteConfig ) { _pRemoteConfig = pRemoteConfig; }
        /** @brief 외부 확인을 거둡니다(제공자 `tick` · `pollVerifications`). 서비스 스레드 틱마다. */
        void tick( int64 nowMs );

        // 요청 — 결과는 `drainCompletions` 로(같은 @p requestTag). 시도 제한 · 빌드 판에 걸리면 맡기지 않고 바로 완료를 쌓는다.
        void registerAccount( const LoginCredential& credential, int64 nowMs, uint64 requestTag );
        /** @brief 비밀번호로 로그인합니다. @p clientKey 는 시도 제한의 단위(원격 주소 해시 — 바인딩이 정한다). */
        void login( const LoginCredential& credential, const AccountClientInfo& clientInfo, uint64 clientKey, int64 nowMs, uint64 requestTag );
        /** @brief 장치 비밀(32 B — 클라이언트 로컬 저장, 봉인)로 들어옵니다. 그 장치의 게스트 계정이 없으면 만든다. */
        void guestLogin( const uint8 ( &arrDeviceSecret )[LoginConstant::kDeviceSecretSize], const AccountClientInfo& clientInfo, uint64 clientKey, int64 nowMs,
                         uint64 requestTag );
        /** @brief 외부 제공자 표(ID 토큰 · 액세스 토큰)로 들어옵니다. 없는 제공자는 `ProviderUnavailable`. */
        void platformLogin( string_view provider, const vector<uint8>& ticketBytes, const AccountClientInfo& clientInfo, uint64 clientKey, int64 nowMs, uint64 requestTag );
        /** @brief 토큰으로 돌아옵니다 — 같은 세션, 새 비밀(옛 토큰은 무효). */
        void resumeSession( const LoginSessionToken& token, const AccountClientInfo& clientInfo, uint64 clientKey, int64 nowMs, uint64 requestTag );
        /** @brief 토큰을 확인합니다(바꾸지 않는다). */
        void validateSession( const LoginSessionToken& token, int64 nowMs, uint64 requestTag );
        void logout( const LoginSessionToken& token, int64 nowMs, uint64 requestTag );
        /** @brief 게임 서버 @p serverId 로 가는 UDP 접속 표를 냅니다. 세션이 살아 있어야 한다. */
        void issueGameTicket( const LoginSessionToken& token, const hashed_string& serverId, int64 nowMs, uint64 requestTag );
        /** @brief 계정의 세션을 끊습니다(운영 · 제재 · 게임 규칙). 이 프로세스에 붙어 있으면 `Revoked` 사건이 납니다. */
        void revokeAccountSessions( uint64 accountId, LoginRevokeReason reason, int64 nowMs, uint64 requestTag );
        void linkCredential( const LoginSessionToken& token, const LoginCredential& credential, int64 nowMs, uint64 requestTag );
        void linkPlatform( const LoginSessionToken& token, string_view provider, const vector<uint8>& ticketBytes, int64 nowMs, uint64 requestTag );
        void unlinkPlatform( const LoginSessionToken& token, string_view provider, int64 nowMs, uint64 requestTag );
        void listLinks( const LoginSessionToken& token, int64 nowMs, uint64 requestTag );
        /** @brief 탈퇴를 예약합니다(유예 뒤 지움, 세션을 끊는다). 유예 안에 다시 로그인해 `cancelDeletion` 할 수 있다. */
        void requestDeletion( const LoginSessionToken& token, int64 nowMs, uint64 requestTag );
        void cancelDeletion( const LoginSessionToken& token, int64 nowMs, uint64 requestTag );
        /** @brief 예약 시각이 지난 탈퇴 계정을 @p maxCount 개까지 지웁니다(예약 작업이 분마다 — 완료 없음). */
        void purgeDueDeletions( int64 nowMs, int32 maxCount );
        /** @brief 세션의 연결이 끊겼습니다 — 재접속 유예를 시작합니다(완료 없음 — 사건 `Disconnected` 만). */
        void markDisconnected( uint64 sessionId, int64 nowMs );
        /** @brief 붙어 있는 세션 @p maxCount 개를 돌아가며 다시 읽습니다(서버 여럿일 때 — 버스 알림을 놓친 것을 줍는다). */
        void refreshOnlineSessions( int64 nowMs, int32 maxCount );

        void drainCompletions( vector<LoginCompletion>& outListCompletion ) { _completionBuffer.drainTo( outListCompletion ); }
        void drainEvents( vector<LoginEvent>& outListEvent ) { _eventBuffer.drainTo( outListEvent ); }
        /** @brief 이 프로세스에 붙어 있지 않은 세션을 끊은 기록(밀려남 · 운영)입니다 — 바인딩이 버스로 다른 서버에 알린다. */
        void drainRemoteRevocations( vector<LoginEvent>& outListEvent ) { _remoteRevokeBuffer.drainTo( outListEvent ); }
        /** @brief 다른 서버가 이 세션을 끊었다(버스) — 이 프로세스에 붙어 있으면 접속 표에서 빼고 `Revoked` 사건을 냅니다. */
        void noteRevokedElsewhere( AccountId accountId, uint64 sessionId, LoginRevokeReason reason );

        // IAccountDirectory — 이 프로세스에 붙어 있는 계정만(메모리, 저장소를 읽지 않는다)
        bool findIdentity( AccountId accountId, AccountIdentity& outIdentity ) const override;
        bool findIdentityByDisplayName( string_view displayName, AccountIdentity& outIdentity ) const override;
        bool isAccountOnline( AccountId accountId ) const override { return _mapAccountToSession.find( accountId ) != _mapAccountToSession.end(); }

        /** @brief 맡겨 두고 아직 끝나지 않은 요청 수입니다(외부 확인 대기 포함). */
        int32                       getPendingCount() const { return _pendingCount + static_cast<int32>( _listPendingVerification.size() ); }
        const LoginTicketAuthority& getTicketAuthority() const { return _ticketAuthority; }
        const LoginSettings&        getSettings() const { return _settings; }
        int32                       getOnlineCount() const { return static_cast<int32>( _mapAccountToSession.size() ); }
        /** @brief 이 프로세스에 붙어 있는 계정의 지금 세션 id 입니다(없으면 0). */
        uint64 findOnlineSessionId( AccountId accountId ) const;

        /** @brief 일의 `complete` 가 부른다(키트 안 — 바인딩 · 게임은 부르지 않는다). */
        void applyCompletion( LoginCompletion&& completion, const LoginStoreOutcome& outcome );

    private:
        /** @brief 외부 제공자의 확인을 기다리는 요청입니다. */
        struct PendingVerification
        {
            LoginSessionToken       _token{}; ///< LinkPlatform
            string                  _provider{};
            string                  _storeUrl{};
            IPlatformLoginProvider* _pProvider{ nullptr };
            uint64                  _verificationId{ 0 };
            uint64                  _requestTag{ 0 };
            int64                   _nowMs{ 0 };
            LoginOperation          _operation{ LoginOperation::PlatformLogin };
            uint8                   _bUpdateRecommended{ SW_FALSE };
        };

        [[nodiscard]] bool consumeAttempt( uint64 clientKey, int64 nowMs, int64& outRetryAfterMs );
        void               pushImmediate( LoginOperation operation, uint64 requestTag, LoginResult result, const LoginGrant& grant );
        /** @brief 빌드 판을 봅니다 — 최소 미만이면 `UpdateRequired`(@p outGrant 에 상점 주소), 권장 미만이면 Ok + `_bUpdateRecommended`. */
        LoginResult             evaluateClientBuild( const AccountClientInfo& clientInfo, LoginGrant& outGrant ) const;
        IPlatformLoginProvider* findPlatformProvider( string_view provider ) const;
        void                    beginVerification( LoginOperation operation, string_view provider, const vector<uint8>& ticketBytes, const LoginSessionToken& token,
                                                   const LoginGrant& buildGrant, int64 nowMs, uint64 requestTag );
        void                    finishVerification( const PendingVerification& pending, const PlatformLoginVerification& verification );
        void                    removeOfflineIdentities();

        EventBuffer<LoginEvent>                _eventBuffer;
        EventBuffer<LoginEvent>                _remoteRevokeBuffer;
        EventBuffer<LoginCompletion>           _completionBuffer;
        LoginTicketAuthority                   _ticketAuthority;
        LoginSettings                          _settings;
        vector<IPlatformLoginProvider*>        _listPlatformProvider;
        vector<PendingVerification>            _listPendingVerification;
        vector<PlatformLoginVerification>      _listVerificationScratch;
        unordered_map<uint64, uint64>          _mapAccountToSession;      ///< 이 프로세스에 붙어 있는 계정 → 세션
        unordered_map<uint64, AccountIdentity> _mapAccountToIdentity;     ///< 붙어 있는 계정의 신원(디렉터리)
        unordered_map<string, uint64>          _mapNameKeyToAccount;      ///< 소문자 이름 → 계정(붙어 있는 것만)
        TokenBucketMap                         _mapClientToAttemptBucket; ///< 시도 제한(원격 주소 해시마다)
        IServiceStore*                         _pStore;
        ILoginCrypto*                          _pCrypto;
        const RemoteConfig*                    _pRemoteConfig;
        size_t                                 _refreshCursor;
        int32                                  _pendingCount;
    };
} // namespace sw
