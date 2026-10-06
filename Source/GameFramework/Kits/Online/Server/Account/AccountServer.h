/**
 * @file AccountServer.h
 * @brief 계정 서비스의 스트림 바인딩 — `IOnlineService`(영역 `kAccount`)로 요청을 받아 `LoginService` 에 맡기고, 완료를 응답으로 돌려주며, 로그인 · 재접속 성공이면
 *        호스트에 계정을 붙입니다. 밀려남 · 운영 끊기는 알림(`kPushRevoked`) 뒤 연결을 닫고, 연결이 닫히면 재접속 유예를 시작합니다.
 * @details - 연결의 세션 토큰은 이 객체가 메모리에 든다(로그인 응답으로 클라이언트에 준 것) — 로그아웃 · 연동 · 표 발급은 연결의 주체로 하고 클라이언트가 비밀을 다시 보내지 않는다.
 *          - 이미 로그인한 연결의 로그인 · 게스트 · 외부 · 재접속은 `kInvalidRequest`(한 연결에 계정 하나 — 다른 계정은 새 연결로).
 *          - 저장소 완료는 호스트 `tick` 이 거둔다. 탈퇴 쓸기 · 세션 다시 읽기는 이 객체의 틱이 주기로 맡긴다.
 *          - `IAccountSessionControl` 구현 — GM · 제재가 계정의 세션을 끊는다(사유 코드를 알림에 싣는다).
 *          - 서버 여럿(호스트에 캐시 · 버스가 있으면): 접속 상태(`OnlinePresence` — 다른 키트는 `getPresence`)를 소유하고, 다른 서버에 붙은 세션을 끊으면
 *            버스 `account.revoke` 로 알린다(받은 서버가 그 연결을 닫는다). 버스를 놓쳐도 세션 다시 읽기가 줍는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Network/Message/NetRequest.h"
#include "Core/Network/Transport/StreamTypes.h"

#include "GameFramework/Base/Online/Identity/AccountSessionControl.h"
#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Online/Server/Account/LoginService.h"
#include "GameFramework/Kits/Online/Server/Account/OnlinePresence.h"

namespace sw
{
    /** @brief 계정 바인딩 설정입니다. */
    struct AccountServerSettings
    {
        int64                  _purgeIntervalMs{ 60000 };   ///< 탈퇴 쓸기 간격
        int64                  _refreshIntervalMs{ 10000 }; ///< 붙어 있는 세션 다시 읽기 간격(서버 여럿 — 버스 알림을 놓친 것을 줍는다)
        int32                  _refreshCount{ 64 };
        int32                  _purgeCount{ 32 };
        OnlinePresenceSettings _presence{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class AccountServer
     * @brief 계정 서비스 바인딩입니다(호스트 `tick` 스레드).
     */
    class SW_GF_API AccountServer final : public IOnlineService, public IAccountSessionControl
    {
    public:
        AccountServer();

        /** @brief @p pLoginService 는 빌려 쓴다(이 객체보다 오래 산다). 호스트에 올리기 전에. */
        void initialize( LoginService* pLoginService, const AccountServerSettings& settings );
        void shutdown();

        // IOnlineService
        uint16 getMethodRange() const override { return OnlineMethodRange::kAccount; }
        uint32 getProtocolVersion() const override;
        bool   isAnonymousMethod( uint16 method ) const override;
        void   onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) override;
        void   onServiceTick( OnlineServiceHost& host, int64 nowMs ) override;
        void   onAccountLeft( OnlineServiceHost& host, AccountId accountId ) override;
        void   onServerBusMessage( OnlineServiceHost& host, const ServerBusMessage& message ) override;

        // IAccountSessionControl
        void revokeAccountSessions( AccountId accountId, string_view reasonCode, int64 nowMs ) override;

        /** @brief 이 연결 계정의 지금 세션 id 입니다(시험 · 진단). 없으면 0. */
        uint64 findSessionId( AccountId accountId ) const;
        /** @brief 접속 상태 창구입니다(거래 · 채팅이 빌려 쓴다 — 서버 한 대면 찾기는 "없음"). 이 객체와 같이 산다. */
        IAccountPresence* getPresence() { return &_presence; }

        static constexpr const utf8* kRevokeTopic = "account.revoke";

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
        /** @brief 업무 결과가 공통 오류(시도 제한 · 저장소 · 빌드 판)면 그것으로 답하고 true 입니다. */
        bool respondCommonError( OnlineServiceHost& host, const NetRequestToken& token, LoginResult result, const LoginGrant& grant );
        /** @brief 세션의 연결에 알림을 보내고 끊습니다(세션 표에서 먼저 빼 `onAccountLeft` 가 유예를 걸지 않게). */
        void revokeBound( OnlineServiceHost& host, AccountId accountId, LoginRevokeReason reason );
        /** @brief 처음 틱 — 호스트의 캐시 · 버스를 붙이고 주제를 구독합니다. */
        void attachHost( OnlineServiceHost& host );
        /** @brief 다른 서버에 붙은 세션을 끊은 기록을 버스로 알립니다. */
        void publishRemoteRevocations( OnlineServiceHost& host );

        unordered_map<uint64, PendingCall>     _mapTagToCall;
        unordered_map<AccountId, BoundSession> _mapAccountToSession;
        unordered_map<AccountId, string>       _mapAccountToReasonCode;
        vector<LoginCompletion>                _listCompletionScratch;
        vector<LoginEvent>                     _listEventScratch;
        OnlinePresence                         _presence;
        AccountServerSettings                  _settings;
        LoginService*                          _pLoginService;
        uint64                                 _nextTag;
        int64                                  _nowMs;
        int64                                  _nextPurgeMs;
        int64                                  _nextRefreshMs;
        uint8                                  _bHostAttached;
    };
} // namespace sw
