/**
 * @file MatchmakingServer.h
 * @brief 매칭 서비스의 스트림 바인딩입니다. `IOnlineHostService`(영역 `kMatchmaking`)를 구현합니다.
 * @details 파티와 로비 요청은 `PartyLobbyService` 에, 대기열 요청은 `MatchQueueService` 에 요청 태그와 함께 맡긴다.
 *          완료는 응답(본문의 첫 값이 결과)으로, 알림은 멤버에게 보낸다. 이 프로세스에 접속하지 않은 멤버에게는 접속 상태 관리자(`IAccountPresence`)로 넘긴다.
 *          - 서비스 틱은 대기열 틱과 게임 서버 에이전트 틱을 부른 뒤, 완료, 알림, 로비 시작(배정으로 이어짐), 깨진 파티 티켓(대기열에서 뺌), 버스 주제 변경(구독으로 이어짐)을 처리한다.
 *          - 버스 구독은 첫 서비스 틱부터 하며 이때는 호스트가 버스를 받은 뒤이다. 계정이 떠나면 대기열, 파티, 이 서버가 들여보낸 로비에서 뺀다. 재접속 유예는 아직 구현하지 않았다.
 *          - 호스트에 등록하는 일은 호출하는 쪽이 한다(`host.registerHostService( &server )`, 호스트 `initialize` 전에).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Base/Online/Service/ServicePendingTable.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchQueueService.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/PartyLobbyService.h"

namespace sw
{
    class IAccountPresence;
    class MatchServerAgent;

    /**
     * @class MatchmakingServer
     * @brief 매칭 바인딩입니다. 호스트 `tick` 스레드에서 씁니다.
     */
    class SW_KIT_SERVER_API MatchmakingServer final : public IOnlineHostService
    {
    public:
        MatchmakingServer();

        /** @brief 넘긴 객체는 빌려 씁니다. @p pAgent 는 게임 서버일 때만, @p pPresence 는 서버가 여러 대일 때만 넘기고 없으면 nullptr 입니다. */
        void initialize( PartyLobbyService* pPartyLobby, MatchQueueService* pQueue, MatchServerAgent* pAgent, IAccountPresence* pPresence );
        /** @brief 버스 구독을 해제합니다. 로직은 호출하는 쪽이 종료합니다. */
        void shutdown();

        // IOnlineHostService
        uint16 getMethodRange() const override { return OnlineMethodRange::kMatchmaking; }
        uint32 getProtocolVersion() const override { return MatchmakingProtocol::kVersion; }
        void   onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) override;
        void   onServiceTick( OnlineServiceHost& host, int64 nowMs ) override;
        void   onHostShutdown( OnlineServiceHost& host ) override;
        void   onAccountLeft( OnlineServiceHost& host, AccountID accountID ) override;
        void   onServerBusMessage( OnlineServiceHost& host, const ServerBusMessage& message ) override;

    private:
        void attachHost( OnlineServiceHost& host );
        void respond( OnlineServiceHost& host, uint64 requestTag, const MatchmakingReply& reply );
        void push( OnlineServiceHost& host, AccountID accountID, uint16 kind, const BitWriter& body );

        ServicePendingTable            _pendingTable;
        vector<PartyLobbyCompletion>   _listPartyCompletionScratch;
        vector<PartyLobbyNotification> _listPartyNotificationScratch;
        vector<LobbyStartRequest>      _listLobbyStartScratch;
        vector<uint64>                 _listBrokenTicketScratch;
        vector<MatchQueueCompletion>   _listQueueCompletionScratch;
        vector<MatchQueueNotification> _listQueueNotificationScratch;
        vector<MatchQueueTopicChange>  _listTopicChangeScratch;
        vector<string>                 _listSubscribedTopic;
        PartyLobbyService*             _pPartyLobby;
        MatchQueueService*             _pQueue;
        MatchServerAgent*              _pAgent;
        IAccountPresence*              _pPresence;
        OnlineServiceHost*             _pHost; ///< 버스를 구독한 호스트이다. 구독을 해제할 때 쓴다
        int64                          _nowMs;
    };
} // namespace sw
