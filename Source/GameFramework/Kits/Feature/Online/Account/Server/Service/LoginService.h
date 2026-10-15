/**
 * @file LoginService.h
 * @brief 로그인 서비스입니다. 가입, 로그인, 게스트 로그인, 외부 로그인, 재접속, 검증, 로그아웃, 중복 로그인, 연동, 탈퇴, 게임 접속 티켓 발급을 처리합니다. 전송은 모르며 스트림 바인딩은 `AccountServer` 가 맡습니다.
 * @details - 상태는 모두 저장소(`IServiceStore`)에 있다. 이 객체가 메모리에 가지는 것은 "이 프로세스에 접속해 있는 계정", 시도 버킷, 외부 확인 대기뿐이다.
 *            그래서 서버를 다시 시작해도 세션과 계정은 그대로이고 클라이언트는 토큰으로 돌아온다(`resumeSession`).
 *          - 요청은 저장소에 작업으로 맡기고(해시와 저장은 저장소 워커에서 실행) 결과는 `drainCompletions` 로 요청 태그와 함께 회수한다. 서비스 스레드는 DB 를 기다리지 않는다.
 *            저장소의 `pollCompletions` 는 이 서비스의 스레드에서 부른다. 외부 로그인은 제공자 확인(`IPlatformLoginProvider`)을 거친 뒤 작업을 맡기며, `tick` 이 그 결과를 회수한다.
 *          - 서버 프로세스 여러 개가 한 저장소를 쓸 수 있다. 같은 계정의 동시 로그인은 마지막 로그인이 이기며, 기본 정책에서는 새 로그인이 이전 세션을 밀어낸다.
 *            다른 프로세스가 밀어낸 세션은 `refreshOnlineSessions`(또는 바인딩이 받는 버스 알림)로 알아챈다.
 *          - 로그인, 게스트 로그인, 외부 로그인, 재접속은 클라이언트 빌드 버전을 원격 설정과 비교한다.
 *            원격 설정 키는 `account.minimum_build.<플랫폼>`, `account.recommended_build.<플랫폼>`, `account.store_url.<플랫폼>` 이다.
 *            최소 버전보다 낮으면 작업을 맡기지 않고 `UpdateRequired` 로 끝낸다. 성공 직전에는 제재(`Online/Sanction`)를 확인한다.
 *          - 시각은 호출하는 쪽이 넘기는 벽시계 밀리초이다. 세션 시한이 저장소에 남아 재시작을 넘어 유지되기 때문이다.
 *          - `IAccountDirectory` 를 구현한다. 채팅과 거래가 표시 이름과 접속 여부를 여기서 조회하며, 이 프로세스에 접속해 있는 계정만 볼 수 있다.
 *          - 종료 순서는 저장소 `shutdown`, 저장소 `pollCompletions`(남은 완료가 이 서비스를 호출한다), 서비스 `shutdown` 이다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/EventBuffer.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Online/Guard/TokenBucketMap.h"
#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/LoginTypes.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/PlatformLoginProvider.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginTicketAuthority.h"

namespace sw
{
    struct LoginStoreOutcome;

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

    /** @brief 끝난 요청 하나입니다. 바인딩이 요청 태그로 응답 대상을 찾습니다. */
    struct LoginCompletion
    {
        LoginGrant         _grant{};       ///< Login, GuestLogin, PlatformLogin, Resume 의 결과이다. Validate 는 `_revokeReason` 만, RequestDeletion 은 `_deletionDueMs` 만 쓴다
        NetGameTicket      _ticket{};      ///< IssueGameTicket
        AccountIdentity    _identity{};    ///< Validate, Register(계정 id), Link* 의 결과이다
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
     * @brief 로그인 서비스입니다. 서버 프로세스마다 하나이며 서비스 스레드 하나에서만 호출합니다.
     */
    class SW_KIT_SERVER_API LoginService final : public IAccountDirectory
    {
    public:
        static constexpr size_t kMaxTrackedClientCount = 4096; ///< 시도 버킷의 상한이다. 넘으면 가득 찬 버킷(새 버킷과 같은 상태)을 지운다

        LoginService();

        /** @brief @p pStore 와 @p pCrypto 는 빌려 씁니다. @p pCrypto 는 스레드 안전해야 합니다. 저장소 워커에서 해시를 돌리기 때문입니다. @p arrTicketMasterKey 는 게임 서버와 나눠 가진 티켓 마스터 키입니다. */
        void initialize( IServiceStore* pStore, ILoginCrypto* pCrypto, const LoginSettings& settings,
                         const uint8 ( &arrTicketMasterKey )[LoginTicketAuthority::kMasterKeySize] );
        void shutdown();

        /** @brief 외부 로그인 제공자를 등록합니다. 빌려 쓰는 포인터입니다. 같은 이름이 있거나 이름 규칙(`[a-z0-9_]`)을 어기면 false 를 반환합니다. */
        [[nodiscard]] bool registerPlatformProvider( IPlatformLoginProvider* pProvider );
        /** @brief 빌드 버전 확인에 쓸 원격 설정을 지정합니다. 빌려 쓰는 포인터이며, 없으면 확인하지 않습니다. */
        void setRemoteConfig( const RemoteConfig* pRemoteConfig ) { _pRemoteConfig = pRemoteConfig; }
        /** @brief 외부 확인 결과를 회수합니다. 제공자의 `tick` 과 `pollVerifications` 를 부르며, 서비스 스레드의 틱마다 호출합니다. */
        void update( int64 nowMs );

        // 요청 함수들이다. 결과는 같은 @p requestTag 로 `drainCompletions` 에서 받는다. 시도 제한이나 빌드 버전에 걸리면 작업을 맡기지 않고 바로 완료를 쌓는다.
        void registerAccount( const LoginCredential& credential, int64 nowMs, uint64 requestTag );
        /** @brief 비밀번호로 로그인합니다. @p clientKey 는 시도 제한의 단위이며 원격 주소 해시로, 바인딩이 정합니다. */
        void login( const LoginCredential& credential, const AccountClientInfo& clientInfo, uint64 clientKey, int64 nowMs, uint64 requestTag );
        /** @brief 장치 비밀로 들어옵니다. 장치 비밀은 32 B 이며 클라이언트가 로컬에 봉인해 저장합니다. 그 장치의 게스트 계정이 없으면 만듭니다. */
        void guestLogin( const uint8 ( &arrDeviceSecret )[LoginConstant::kDeviceSecretSize], const AccountClientInfo& clientInfo, uint64 clientKey, int64 nowMs,
                         uint64 requestTag );
        /** @brief 외부 제공자 티켓(ID 토큰, 액세스 토큰)으로 들어옵니다. 없는 제공자는 `ProviderUnavailable` 입니다. */
        void platformLogin( string_view provider, const vector<uint8>& ticketBytes, const AccountClientInfo& clientInfo, uint64 clientKey, int64 nowMs, uint64 requestTag );
        /** @brief 토큰으로 돌아옵니다. 같은 세션을 쓰고 비밀은 새로 받으며, 이전 토큰은 무효가 됩니다. */
        void resumeSession( const LoginSessionToken& token, const AccountClientInfo& clientInfo, uint64 clientKey, int64 nowMs, uint64 requestTag );
        /** @brief 토큰을 확인합니다. 상태는 바꾸지 않습니다. */
        void validateSession( const LoginSessionToken& token, int64 nowMs, uint64 requestTag );
        void logout( const LoginSessionToken& token, int64 nowMs, uint64 requestTag );
        /** @brief 게임 서버 @p serverID 로 가는 UDP 접속 티켓을 발급합니다. 세션이 살아 있어야 합니다. */
        void issueGameTicket( const LoginSessionToken& token, const hashed_string& serverID, int64 nowMs, uint64 requestTag );
        /** @brief 계정의 세션을 끊습니다. 운영, 제재, 게임 규칙에서 부릅니다. 이 프로세스에 접속해 있으면 `Revoked` 이벤트가 발생합니다. */
        void revokeAccountSessions( uint64 accountID, LoginRevokeReason reason, int64 nowMs, uint64 requestTag );
        void linkCredential( const LoginSessionToken& token, const LoginCredential& credential, int64 nowMs, uint64 requestTag );
        void linkPlatform( const LoginSessionToken& token, string_view provider, const vector<uint8>& ticketBytes, int64 nowMs, uint64 requestTag );
        void unlinkPlatform( const LoginSessionToken& token, string_view provider, int64 nowMs, uint64 requestTag );
        void listLinks( const LoginSessionToken& token, int64 nowMs, uint64 requestTag );
        /** @brief 탈퇴를 예약합니다. 유예 시간이 지나면 계정을 지우고 세션을 끊습니다. 유예 안에 다시 로그인해 `cancelDeletion` 으로 취소할 수 있습니다. */
        void requestDeletion( const LoginSessionToken& token, int64 nowMs, uint64 requestTag );
        void cancelDeletion( const LoginSessionToken& token, int64 nowMs, uint64 requestTag );
        /** @brief 예약 시각이 지난 탈퇴 계정을 @p maxCount 개까지 지웁니다. 예약 작업이 분마다 부르며 완료는 없습니다. */
        void purgeDueDeletions( int64 nowMs, int32 maxCount );
        /** @brief 세션의 연결이 끊겼을 때 부릅니다. 재접속 유예를 시작하며 완료는 없고 `Disconnected` 이벤트만 발생합니다. */
        void markDisconnected( uint64 sessionID, int64 nowMs );
        /** @brief 접속해 있는 세션 @p maxCount 개를 돌아가며 다시 읽습니다. 서버가 여러 대일 때 버스 알림을 놓친 변경을 찾아냅니다. */
        void refreshOnlineSessions( int64 nowMs, int32 maxCount );

        void drainCompletions( vector<LoginCompletion>& outListCompletion ) { _completionBuffer.drainTo( outListCompletion ); }
        void drainEvents( vector<LoginEvent>& outListEvent ) { _eventBuffer.drainTo( outListEvent ); }
        /** @brief 이 프로세스에 접속해 있지 않은 세션을 끊은 기록입니다. 밀려남과 운영 조치가 해당하며, 바인딩이 버스로 다른 서버에 알립니다. */
        void drainRemoteRevocations( vector<LoginEvent>& outListEvent ) { _remoteRevokeBuffer.drainTo( outListEvent ); }
        /** @brief 다른 서버가 이 세션을 끊었음을 버스로 받았을 때 부릅니다. 이 프로세스에 접속해 있으면 접속 테이블에서 빼고 `Revoked` 이벤트를 냅니다. */
        void noteRevokedElsewhere( AccountID accountID, uint64 sessionID, LoginRevokeReason reason );

        // IAccountDirectory 구현이다. 이 프로세스에 접속해 있는 계정만 메모리에서 조회하며 저장소는 읽지 않는다
        bool findIdentity( AccountID accountID, AccountIdentity& outIdentity ) const override;
        bool findIdentityByDisplayName( string_view displayName, AccountIdentity& outIdentity ) const override;
        bool isAccountOnline( AccountID accountID ) const override { return _mapAccountToSession.find( accountID ) != _mapAccountToSession.end(); }

        /** @brief 맡겨 두고 아직 끝나지 않은 요청 수입니다. 외부 확인 대기도 포함합니다. */
        int32                       getPendingCount() const { return _pendingCount + static_cast<int32>( _listPendingVerification.size() ); }
        const LoginTicketAuthority& getTicketAuthority() const { return _ticketAuthority; }
        const LoginSettings&        getSettings() const { return _settings; }
        int32                       getOnlineCount() const { return static_cast<int32>( _mapAccountToSession.size() ); }
        /** @brief 이 프로세스에 접속해 있는 계정의 현재 세션 id 입니다. 없으면 0입니다. */
        uint64 findOnlineSessionID( AccountID accountID ) const;

        /** @brief 작업의 `complete` 가 호출합니다. 키트 안에서만 부르며 바인딩과 게임은 부르지 않습니다. */
        void applyCompletion( LoginCompletion&& completion, const LoginStoreOutcome& outcome );

    private:
        /** @brief 외부 제공자의 확인을 기다리는 요청입니다. */
        struct PendingVerification
        {
            LoginSessionToken       _token{}; ///< LinkPlatform
            string                  _provider{};
            string                  _storeURL{};
            IPlatformLoginProvider* _pProvider{ nullptr };
            uint64                  _verificationID{ 0 };
            uint64                  _requestTag{ 0 };
            int64                   _nowMs{ 0 };
            LoginOperation          _operation{ LoginOperation::PlatformLogin };
            uint8                   _bUpdateRecommended{ SW_FALSE };
        };

        [[nodiscard]] bool consumeAttempt( uint64 clientKey, int64 nowMs, int64& outRetryAfterMs );
        void               pushImmediate( LoginOperation operation, uint64 requestTag, LoginResult result, const LoginGrant& grant );
        /** @brief 빌드 버전을 확인합니다. 최소 버전 미만이면 `UpdateRequired` 이고 @p outGrant 에 상점 주소를 채웁니다. 권장 버전 미만이면 Ok 와 `_bUpdateRecommended` 를 반환합니다. */
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
        unordered_map<uint64, uint64>          _mapAccountToSession;      ///< 이 프로세스에 접속해 있는 계정에서 세션으로 가는 맵이다
        unordered_map<uint64, AccountIdentity> _mapAccountToIdentity;     ///< 접속해 있는 계정의 신원이다. 디렉터리가 쓴다
        unordered_map<string, uint64>          _mapNameKeyToAccount;      ///< 소문자 이름에서 계정으로 가는 맵이다. 접속해 있는 계정만 담는다
        TokenBucketMap                         _mapClientToAttemptBucket; ///< 시도 제한이다. 원격 주소 해시마다 버킷을 둔다
        IServiceStore*                         _pStore;
        ILoginCrypto*                          _pCrypto;
        const RemoteConfig*                    _pRemoteConfig;
        size_t                                 _refreshCursor;
        int32                                  _pendingCount;
    };
} // namespace sw
