/**
 * @file AccountServer.h
 * @brief 계정 서비스의 스트림 바인딩입니다. 프로토콜 표(`OnlineProtocolService`, 영역 `kAccount`)로 요청을 받아 `LoginService` 에 맡기고, 완료를 응답으로 돌려줍니다.
 * @details 로그인이나 재접속에 성공하면 호스트에 계정을 연결한다. 세션이 밀려나거나 운영 조치로 끊기면 알림(`AccountMethod::Revoked`)을 보낸 뒤 연결을 닫고, 연결이 닫히면 재접속 유예를 시작한다.
 *          - 연결의 세션 토큰은 이 객체가 메모리에 보관한다. 로그인 응답으로 클라이언트에 준 토큰이다. 로그아웃, 연동, 티켓 발급은 연결의 주체로 처리하므로 클라이언트가 비밀을 다시 보내지 않는다.
 *          - 이미 로그인했거나 로그인을 맡겨 둔 연결이 로그인, 게스트 로그인, 외부 로그인, 재접속을 요청하면 `kInvalidRequest` 로 거절한다. 한 연결에는 계정 하나만 쓰고, 다른 계정은 새 연결로 접속해야 한다.
 *          - 로그인 응답 몸을 쓰지 못하면 `kInternal` 로 답하고, 저장소 작업이 이미 만든 그 세션은 로그아웃 작업으로 지운다. 응답 몸이 그 세션의 토큰을 실으므로
 *            저장소 작업 전에 쓸 수 없다.
 *          - 저장소 완료는 호스트 `tick` 이 회수한다. 탈퇴 정리와 세션 다시 읽기는 이 객체의 틱이 주기적으로 작업을 맡긴다.
 *          - `IAccountSessionControl` 을 멤버로 든다(`getSessionControl`). GM 과 제재가 계정의 세션을 끊을 수 있고, 사유 코드를 알림에 싣는다.
 *          - 서버가 여러 대일 때(호스트에 캐시와 버스가 있으면) 접속 상태(`OnlinePresence`)를 소유한다. 다른 키트는 `getPresence` 로 접근한다.
 *            다른 서버에 접속한 세션을 끊으면 버스 `account.revoke` 로 알리고, 받은 서버가 그 연결을 닫는다. 버스를 놓쳐도 세션 다시 읽기가 찾아낸다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Transport/StreamTypes.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Account/Runtime/Protocol/AccountProtocol.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginService.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/OnlinePresence.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Service/OnlineProtocolService.h"
#include "GameFramework/Runtime/Online/Bus/ServerBusTopic.h"
#include "GameFramework/Runtime/Online/Identity/Server/AccountDirectory.h"
#include "GameFramework/Runtime/Online/Service/Server/OnlineServiceHost.h"

namespace sw
{
    class MetricRegistry;

    /** @brief 계정 바인딩 설정입니다. */
    struct AccountServerConfig
    {
        MetricRegistry*      _pMetricRegistry{ nullptr }; ///< 없으면 요청 지표를 세지 않는다
        int64                _purgeIntervalMs{ 60000 };   ///< 탈퇴 계정을 정리하는 간격이다
        int64                _refreshIntervalMs{ 10000 }; ///< 접속해 있는 세션을 다시 읽는 간격이다. 서버가 여러 대일 때 버스 알림을 놓친 변경을 찾아낸다
        int32                _refreshCount{ 64 };
        int32                _purgeCount{ 32 };
        OnlinePresenceConfig _presence{};
    };
} // namespace sw

namespace sw
{
    /** @brief 계정 키트의 서버 버스 주제입니다. 내보내는 클래스의 정적 멤버로 두지 않습니다(지연 로드 모듈은 데이터를 가져올 수 없습니다). */
    struct AccountBus
    {
        static constexpr ServerBusTopic<AccountRevoked> kRevokeTopic{ "account.revoke" }; ///< 다른 서버에서 끊긴 세션이다
    };
} // namespace sw

namespace sw
{
    /**
     * @class AccountServer
     * @brief 계정 서비스 바인딩입니다. 호스트 `tick` 스레드에서 씁니다.
     */
    class SW_KIT_SERVER_API AccountServer final : public OnlineProtocolService<AccountServer, AccountProtocol>
    {
    public:
        AccountServer();

        /** @brief @p pLoginService 는 빌려 씁니다. 이 객체보다 오래 살아야 하며 호스트에 등록하기 전에 지정합니다. */
        void initialize( LoginService* pLoginService, const AccountServerConfig& settings );
        void shutdown();

        // 프로토콜 표의 메서드 - 결과는 호출 번호로 `takeCompletions` 에서 받는다. 응답은 계정을 연결에 묶은 뒤에 보낸다.
        void handleCall( OnlineServerCall<AccountMethod::Register> call, const OnlineCallContext& context, const AccountCredentialRequest& request );
        void handleCall( OnlineServerCall<AccountMethod::Login> call, const OnlineCallContext& context, const AccountLoginRequest& request );
        void handleCall( OnlineServerCall<AccountMethod::GuestLogin> call, const OnlineCallContext& context, const AccountGuestLoginRequest& request );
        void handleCall( OnlineServerCall<AccountMethod::PlatformLogin> call, const OnlineCallContext& context, const AccountPlatformRequest& request );
        void handleCall( OnlineServerCall<AccountMethod::Resume> call, const OnlineCallContext& context, const AccountResumeRequest& request );
        void handleCall( OnlineServerCall<AccountMethod::Logout> call, const OnlineCallContext& context, const OnlineNoBody& request );
        void handleCall( OnlineServerCall<AccountMethod::LinkCredential> call, const OnlineCallContext& context, const AccountCredentialRequest& request );
        void handleCall( OnlineServerCall<AccountMethod::LinkPlatform> call, const OnlineCallContext& context, const AccountPlatformRequest& request );
        void handleCall( OnlineServerCall<AccountMethod::IssueGameTicket> call, const OnlineCallContext& context, const AccountGameTicketRequest& request );
        void handleCall( OnlineServerCall<AccountMethod::UnlinkPlatform> call, const OnlineCallContext& context, const AccountProviderRequest& request );
        void handleCall( OnlineServerCall<AccountMethod::ListLinks> call, const OnlineCallContext& context, const OnlineNoBody& request );
        void handleCall( OnlineServerCall<AccountMethod::RequestDeletion> call, const OnlineCallContext& context, const OnlineNoBody& request );
        void handleCall( OnlineServerCall<AccountMethod::CancelDeletion> call, const OnlineCallContext& context, const OnlineNoBody& request );

        // IOnlineServerService
        /** @brief 두 리슨 모두에서 받습니다 — GM 도구도 GM 포트에서 로그인합니다. */
        OnlineServiceExposure getExposure() const override { return OnlineServiceExposure::PlayerAndAdmin; }
        void                  onServiceTick( OnlineServiceHost& host, UnixMilliseconds nowMs ) override;
        void                  onHostShutdown( OnlineServiceHost& host ) override;
        void                  onAccountLeft( OnlineServiceHost& host, AccountID accountID ) override;
        void                  onServerBusMessage( OnlineServiceHost& host, const ServerBusMessage& message ) override;

        /** @brief 계정의 세션을 모두 끊습니다. @p reasonCode 는 끊김 알림에 싣습니다. `getSessionControl` 이 이 함수로 넘깁니다. */
        void revokeAccountSessions( AccountID accountID, string_view reasonCode, UnixMilliseconds nowMs );
        /** @brief 다른 키트(운영 · 제재)에 거는 세션 끊기 창구입니다. 이 객체와 수명이 같습니다. */
        IAccountSessionControl& getSessionControl() { return _sessionControl; }

        /** @brief 이 연결 계정의 현재 세션 id 입니다. 테스트와 진단에 씁니다. 없으면 0입니다. */
        uint64 findSessionID( AccountID accountID ) const;
        /** @brief 접속 상태 관리자입니다. 거래와 채팅이 빌려 쓰며, 서버 한 대이면 찾기는 "없음" 으로 끝납니다. 이 객체와 수명이 같습니다. */
        IAccountPresence* getPresence() { return &_presence; }

    protected:
        uint16 checkRequest( OnlineServiceHost& host, const OnlineCallContext& context ) override;

    private:
        /** @brief 세션 끊기 창구입니다. 바인딩의 `revokeAccountSessions` 로 넘깁니다. 바인딩이 멤버로 들어 수명이 같다. */
        class SessionControl final : public IAccountSessionControl
        {
        public:
            explicit SessionControl( AccountServer& server )
                : _server{ server }
            {
            }

            void revokeAccountSessions( AccountID accountID, string_view reasonCode, UnixMilliseconds nowMs ) override { _server.revokeAccountSessions( accountID, reasonCode, nowMs ); }

        private:
            AccountServer& _server;
        };

        struct PendingCall
        {
            StreamConnectionHandle _connection{};
            uint8                  _bLogin{ SW_FALSE }; ///< 로그인 메서드이다. 끝나면 겹친 로그인 표에서 뺀다
        };

        struct BoundSession
        {
            LoginSessionToken      _token{};
            StreamConnectionHandle _connection{};
        };

        /** @brief 로그인 메서드 하나를 받아도 되면 호출을 올리고 true 입니다. 이미 로그인했거나 로그인을 맡겨 둔 연결이면 `kInvalidRequest` 로 끝내고 false 입니다. */
        template <typename TMethod>
        [[nodiscard]] bool beginLogin( OnlineServerCall<TMethod> call, const OnlineCallContext& context );
        /** @brief 세션이 필요한 메서드 하나를 받습니다. 호출을 올리고 세션 토큰을 돌려줍니다. 연결에 세션이 없으면 `kUnauthenticated` 로 끝내고 null 입니다. */
        template <typename TMethod>
        [[nodiscard]] const LoginSessionToken* beginSessionCall( OnlineServerCall<TMethod> call, const OnlineCallContext& context );

        void handleCompletion( OnlineServiceHost& host, const LoginCompletion& completion );
        void handleEvent( OnlineServiceHost& host, const LoginEvent& event );
        /** @brief 업무 결과가 공통 오류(시도 제한, 저장소, 빌드 버전)이면 그 오류로 응답하고 true 를 반환합니다. */
        bool respondCommonError( uint64 callID, LoginServiceResult result, const LoginGrant& grant );
        /** @brief 세션의 연결에 알림을 보내고 끊습니다. 세션 테이블에서 먼저 빼서 `onAccountLeft` 가 유예를 걸지 않게 합니다. */
        void revokeBound( OnlineServiceHost& host, AccountID accountID, LoginRevokeReason reason );
        /** @brief 처음 틱에서 호스트의 캐시와 버스를 연결하고 주제를 구독합니다. */
        void attachHost( OnlineServiceHost& host );
        /** @brief 다른 서버에 접속한 세션을 끊은 기록을 버스로 알립니다. */
        void publishRemoteRevocations( OnlineServiceHost& host );

    private:
        unordered_map<uint64, PendingCall>     _mapCallToPending; ///< 응답을 기다리는 호출(호출 번호 → 연결)이다
        unordered_map<AccountID, BoundSession> _mapAccountToSession;
        unordered_map<AccountID, string>       _mapAccountToReasonCode;
        unordered_set<uint64>                  _uniqueSetLoginConnection; ///< 로그인을 맡겨 두고 완료를 기다리는 연결(packed)이다 — 묶기 전이라 호스트의 연결 주체로는 겹친 로그인을 가리지 못한다
        vector<LoginCompletion>                _listCompletionScratch;
        vector<LoginEvent>                     _listEventScratch;
        vector<Subscription>                   _listBusSubscription; ///< 취소 주제와 이 서버의 알림 주제 구독(`attachHost`). 호스트가 내려가면 `onHostShutdown` 이 푼다
        OnlinePresence                         _presence;
        SessionControl                         _sessionControl; ///< 세션 끊기 창구(`getSessionControl`)
        AccountServerConfig                    _settings;
        LoginService*                          _pLoginService;
        UnixMilliseconds                       _nowMs;
        UnixMilliseconds                       _nextPurgeMs;
        UnixMilliseconds                       _nextRefreshMs;
        uint8                                  _bHostAttached;
    };
} // namespace sw
