/**
 * @file LoginService.h
 * @brief 로그인 서비스 — 가입 · 로그인 · 재접속 · 검증 · 로그아웃 · 중복 로그인 · 게임 접속 표. 전송을 모른다(스트림 바인딩은 다음 단위).
 * @details - 상태는 모두 저장소(`IServiceStore`)에 있다. 이 객체가 메모리에 드는 것은 "이 프로세스에 붙어 있는 계정" 과 시도 버킷뿐이라, 서버를 다시 띄워도
 *            세션 · 계정은 그대로이고 클라이언트는 토큰으로 돌아온다(`resumeSession`).
 *          - 요청은 저장소에 일로 맡기고(해시 · 저장은 저장소 워커에서), 결과는 `drainCompletions` 로 꼬리표와 함께 거둔다. 서비스 스레드는 DB 를 기다리지 않는다.
 *            저장소의 `pollCompletions` 를 이 서비스의 스레드에서 부른다.
 *          - 서버 프로세스 여럿이 한 저장소를 쓸 수 있다. 같은 계정의 동시 로그인은 마지막 로그인이 이기고(새 로그인이 옛 세션을 밀어낸다 — 기본 정책),
 *            다른 프로세스가 밀어낸 세션은 `refreshOnlineSessions` 가 알아챈다.
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

namespace sw
{
    struct LoginStoreOutcome;

    class IServiceStore;

    /** @brief 로그인 서비스의 작업 종류입니다. */
    enum class LoginOperation : uint8
    {
        Register = 0,
        Login,
        Resume,
        Validate,
        Logout,
        IssueGameTicket,
        Revoke
    };

    /** @brief 끝난 요청 하나입니다 — 바인딩이 꼬리표로 응답을 찾는다. */
    struct LoginCompletion
    {
        LoginGrant      _grant{};    ///< Login · Resume(Validate 는 `_revokeReason` 만)
        NetGameTicket   _ticket{};   ///< IssueGameTicket
        AccountIdentity _identity{}; ///< Validate · Register(계정 id)
        uint64          _requestTag{ 0 };
        LoginResult     _result{ LoginResult::Ok };
        LoginOperation  _operation{ LoginOperation::Login };
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

        // 요청 — 결과는 `drainCompletions` 로(같은 @p requestTag). 시도 제한에 걸리면 맡기지 않고 바로 완료(RateLimited)를 쌓는다.
        void registerAccount( const LoginCredential& credential, int64 nowMs, uint64 requestTag );
        /** @brief 비밀번호로 로그인합니다. @p clientKey 는 시도 제한의 단위(원격 주소 해시 — 바인딩이 정한다). */
        void login( const LoginCredential& credential, uint64 clientKey, int64 nowMs, uint64 requestTag );
        /** @brief 토큰으로 돌아옵니다 — 같은 세션, 새 비밀(옛 토큰은 무효). */
        void resumeSession( const LoginSessionToken& token, uint64 clientKey, int64 nowMs, uint64 requestTag );
        /** @brief 토큰을 확인합니다(바꾸지 않는다). */
        void validateSession( const LoginSessionToken& token, int64 nowMs, uint64 requestTag );
        void logout( const LoginSessionToken& token, int64 nowMs, uint64 requestTag );
        /** @brief 게임 서버 @p serverId 로 가는 UDP 접속 표를 냅니다. 세션이 살아 있어야 한다. */
        void issueGameTicket( const LoginSessionToken& token, const hashed_string& serverId, int64 nowMs, uint64 requestTag );
        /** @brief 계정의 세션을 끊습니다(운영 · 게임 규칙). 이 프로세스에 붙어 있으면 `Revoked` 사건이 납니다. */
        void revokeAccountSessions( uint64 accountId, int64 nowMs, uint64 requestTag );
        /** @brief 세션의 연결이 끊겼습니다 — 재접속 유예를 시작합니다(완료 없음 — 사건 `Disconnected` 만). */
        void markDisconnected( uint64 sessionId, int64 nowMs );
        /** @brief 붙어 있는 세션 @p maxCount 개를 돌아가며 다시 읽습니다(서버 여럿일 때 몇 초마다 — 하나면 부를 필요 없다). */
        void refreshOnlineSessions( int64 nowMs, int32 maxCount );

        void drainCompletions( vector<LoginCompletion>& outListCompletion ) { _completionBuffer.drainTo( outListCompletion ); }
        void drainEvents( vector<LoginEvent>& outListEvent ) { _eventBuffer.drainTo( outListEvent ); }

        // IAccountDirectory — 이 프로세스에 붙어 있는 계정만(메모리, 저장소를 읽지 않는다)
        bool findIdentity( AccountId accountId, AccountIdentity& outIdentity ) const override;
        bool findIdentityByDisplayName( string_view displayName, AccountIdentity& outIdentity ) const override;
        bool isAccountOnline( AccountId accountId ) const override { return _mapAccountToSession.find( accountId ) != _mapAccountToSession.end(); }

        /** @brief 맡겨 두고 아직 끝나지 않은 요청 수입니다. */
        int32                       getPendingCount() const { return _pendingCount; }
        const LoginTicketAuthority& getTicketAuthority() const { return _ticketAuthority; }
        const LoginSettings&        getSettings() const { return _settings; }
        int32                       getOnlineCount() const { return static_cast<int32>( _mapAccountToSession.size() ); }

        /** @brief 일의 `complete` 가 부른다(키트 안 — 바인딩 · 게임은 부르지 않는다). */
        void applyCompletion( LoginCompletion&& completion, const LoginStoreOutcome& outcome );

    private:
        [[nodiscard]] bool consumeAttempt( uint64 clientKey, int64 nowMs, int64& outRetryAfterMs );
        void               pushRateLimited( LoginOperation operation, uint64 requestTag, int64 retryAfterMs );
        void               removeOfflineIdentities();

        EventBuffer<LoginEvent>                _eventBuffer;
        EventBuffer<LoginCompletion>           _completionBuffer;
        LoginTicketAuthority                   _ticketAuthority;
        LoginSettings                          _settings;
        unordered_map<uint64, uint64>          _mapAccountToSession;      ///< 이 프로세스에 붙어 있는 계정 → 세션
        unordered_map<uint64, AccountIdentity> _mapAccountToIdentity;     ///< 붙어 있는 계정의 신원(디렉터리)
        unordered_map<string, uint64>          _mapNameKeyToAccount;      ///< 소문자 이름 → 계정(붙어 있는 것만)
        TokenBucketMap                         _mapClientToAttemptBucket; ///< 시도 제한(원격 주소 해시마다)
        IServiceStore*                         _pStore;
        ILoginCrypto*                          _pCrypto;
        size_t                                 _refreshCursor;
        int32                                  _pendingCount;
    };
} // namespace sw
