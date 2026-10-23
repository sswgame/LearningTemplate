/**
 * @file AccountServer.h
 * @brief 계정 서비스의 스트림 바인딩입니다. `IOnlineHostService`(영역 `kAccount`)로 요청을 받아 `LoginService` 에 맡기고, 완료를 응답으로 돌려줍니다.
 * @details 로그인이나 재접속에 성공하면 호스트에 계정을 연결한다. 세션이 밀려나거나 운영 조치로 끊기면 알림(`kPushRevoked`)을 보낸 뒤 연결을 닫고, 연결이 닫히면 재접속 유예를 시작한다.
 *          - 연결의 세션 토큰은 이 객체가 메모리에 보관한다. 로그인 응답으로 클라이언트에 준 토큰이다. 로그아웃, 연동, 티켓 발급은 연결의 주체로 처리하므로 클라이언트가 비밀을 다시 보내지 않는다.
 *          - 이미 로그인한 연결이 로그인, 게스트 로그인, 외부 로그인, 재접속을 요청하면 `kInvalidRequest` 로 거절한다. 한 연결에는 계정 하나만 쓰고, 다른 계정은 새 연결로 접속해야 한다.
 *          - 로그인 응답 몸을 쓰지 못하면 `kInternal` 로 답하고, 저장소 작업이 이미 만든 그 세션은 로그아웃 작업으로 지운다. 응답 몸이 그 세션의 토큰을 실으므로
 *            저장소 작업 전에 쓸 수 없다(결정 기록 "로그인 응답 쓰기 실패").
 *          - 저장소 완료는 호스트 `tick` 이 회수한다. 탈퇴 정리와 세션 다시 읽기는 이 객체의 틱이 주기적으로 작업을 맡긴다.
 *          - `IAccountSessionControl` 을 구현한다. GM 과 제재가 계정의 세션을 끊을 수 있고, 사유 코드를 알림에 싣는다.
 *          - 서버가 여러 대일 때(호스트에 캐시와 버스가 있으면) 접속 상태(`OnlinePresence`)를 소유한다. 다른 키트는 `getPresence` 로 접근한다.
 *            다른 서버에 접속한 세션을 끊으면 버스 `account.revoke` 로 알리고, 받은 서버가 그 연결을 닫는다. 버스를 놓쳐도 세션 다시 읽기가 찾아낸다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Network/Message/NetRequest.h"
#include "Core/Network/Transport/StreamTypes.h"

#include "GameFramework/Base/Online/Bus/ServerBusTopic.h"
#include "GameFramework/Base/Online/Identity/AccountSessionControl.h"
#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginService.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/OnlinePresence.h"
#include "GameFramework/Kits/Feature/Online/Account/Shared/Protocol/AccountProtocol.h"

namespace sw
{
    /** @brief 계정 바인딩 설정입니다. */
    struct AccountServerConfig
    {
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
        static constexpr ServerBusTopic<AccountRevokeNotice> kRevokeTopic{ "account.revoke" }; ///< 다른 서버에서 끊긴 세션이다
    };
} // namespace sw

namespace sw
{
    /**
     * @class AccountServer
     * @brief 계정 서비스 바인딩입니다. 호스트 `tick` 스레드에서 씁니다.
     */
    class SW_KIT_SERVER_API AccountServer final : public IOnlineHostService, public IAccountSessionControl
    {
    public:
        AccountServer();

        /** @brief @p pLoginService 는 빌려 씁니다. 이 객체보다 오래 살아야 하며 호스트에 등록하기 전에 지정합니다. */
        void initialize( LoginService* pLoginService, const AccountServerConfig& settings );
        void shutdown();

        // IOnlineHostService
        const OnlineProtocolEntry& getProtocol() const override;
        /** @brief 두 리슨 모두에서 받습니다 — GM 도구도 GM 포트에서 로그인합니다. */
        OnlineServiceExposure getExposure() const override { return OnlineServiceExposure::PlayerAndAdmin; }
        bool                  isAnonymousMethod( uint16 method ) const override;
        void                  onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) override;
        void                  onServiceTick( OnlineServiceHost& host, int64 nowMs ) override;
        void                  onHostShutdown( OnlineServiceHost& host ) override;
        void                  onAccountLeft( OnlineServiceHost& host, AccountID accountID ) override;
        void                  onServerBusMessage( OnlineServiceHost& host, const ServerBusMessage& message ) override;

        // IAccountSessionControl
        void revokeAccountSessions( AccountID accountID, string_view reasonCode, int64 nowMs ) override;

        /** @brief 이 연결 계정의 현재 세션 id 입니다. 테스트와 진단에 씁니다. 없으면 0입니다. */
        uint64 findSessionID( AccountID accountID ) const;
        /** @brief 접속 상태 관리자입니다. 거래와 채팅이 빌려 쓰며, 서버 한 대이면 찾기는 "없음" 으로 끝납니다. 이 객체와 수명이 같습니다. */
        IAccountPresence* getPresence() { return &_presence; }

    private:
        struct PendingCall
        {
            NetRequestToken        _token{};
            StreamConnectionHandle _connection{};
            uint16                 _method{ 0 };
        };

        struct BoundSession
        {
            LoginSessionToken      _token{};
            StreamConnectionHandle _connection{};
        };

        void handleCompletion( OnlineServiceHost& host, const LoginCompletion& completion );
        void handleEvent( OnlineServiceHost& host, const LoginEvent& event );
        /** @brief 업무 결과가 공통 오류(시도 제한, 저장소, 빌드 버전)이면 그 오류로 응답하고 true 를 반환합니다. */
        bool respondCommonError( OnlineServiceHost& host, const NetRequestToken& token, LoginResult result, const LoginGrant& grant );
        /** @brief 세션의 연결에 알림을 보내고 끊습니다. 세션 테이블에서 먼저 빼서 `onAccountLeft` 가 유예를 걸지 않게 합니다. */
        void revokeBound( OnlineServiceHost& host, AccountID accountID, LoginRevokeReason reason );
        /** @brief 처음 틱에서 호스트의 캐시와 버스를 연결하고 주제를 구독합니다. */
        void attachHost( OnlineServiceHost& host );
        /** @brief 다른 서버에 접속한 세션을 끊은 기록을 버스로 알립니다. */
        void publishRemoteRevocations( OnlineServiceHost& host );

        unordered_map<uint64, PendingCall>     _mapTagToCall;
        unordered_map<AccountID, BoundSession> _mapAccountToSession;
        unordered_map<AccountID, string>       _mapAccountToReasonCode;
        vector<LoginCompletion>                _listCompletionScratch;
        vector<LoginEvent>                     _listEventScratch;
        OnlinePresence                         _presence;
        AccountServerConfig                    _settings;
        LoginService*                          _pLoginService;
        uint64                                 _nextTag;
        int64                                  _nowMs;
        int64                                  _nextPurgeMs;
        int64                                  _nextRefreshMs;
        uint8                                  _bHostAttached;
    };
} // namespace sw
