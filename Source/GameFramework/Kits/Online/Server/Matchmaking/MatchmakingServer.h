/**
 * @file MatchmakingServer.h
 * @brief 매칭 서비스의 스트림 바인딩 — `IOnlineService`(영역 `kMatchmaking`). 파티 · 로비 요청은 `PartyLobbyService`, 대기열은 `MatchQueueService` 에 꼬리표로 맡기고
 *        완료를 응답(몸 첫 값이 결과)으로, 알림을 회원에게 보냅니다. 이 프로세스에 없는 회원에게는 접속 상태 창구(`IAccountPresence`)로 넘깁니다.
 * @details - 서비스 틱: 대기열 틱 · 게임 서버 에이전트 틱 → 완료 · 알림 · 로비 시작(→ 배정) · 깨진 파티 표(→ 대기열에서 빼기) · 버스 주제 바뀜(→ 구독).
 *          - 버스 구독은 첫 서비스 틱부터(호스트가 버스를 받은 뒤). 계정이 떠나면 대기열 · 파티 · 이 서버가 들여보낸 로비에서 뺀다(재접속 유예는 백로그).
 *          - 호스트에 올리는 것은 부르는 쪽이(`host.registerService( &server )` — 호스트 `initialize` 전에).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Base/Online/Service/ServicePendingTable.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Online/Server/Matchmaking/MatchQueueService.h"
#include "GameFramework/Kits/Online/Server/Matchmaking/PartyLobbyService.h"

namespace sw
{
    class IAccountPresence;
    class MatchServerAgent;

    /**
     * @class MatchmakingServer
     * @brief 매칭 바인딩입니다(호스트 `tick` 스레드).
     */
    class SW_GF_API MatchmakingServer final : public IOnlineService
    {
    public:
        MatchmakingServer();

        /** @brief 넘긴 것은 빌려 쓴다. @p pAgent 는 게임 서버일 때만, @p pPresence 는 서버 여럿일 때만(없으면 nullptr). */
        void initialize( PartyLobbyService* pPartyLobby, MatchQueueService* pQueue, MatchServerAgent* pAgent, IAccountPresence* pPresence );
        /** @brief 버스 구독을 풉니다. 로직은 부르는 쪽이 내린다. */
        void shutdown();

        // IOnlineService
        uint16 getMethodRange() const override { return OnlineMethodRange::kMatchmaking; }
        uint32 getProtocolVersion() const override { return MatchmakingProtocol::kVersion; }
        void   onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) override;
        void   onServiceTick( OnlineServiceHost& host, int64 nowMs ) override;
        void   onHostShutdown( OnlineServiceHost& host ) override;
        void   onAccountLeft( OnlineServiceHost& host, AccountId accountId ) override;
        void   onServerBusMessage( OnlineServiceHost& host, const ServerBusMessage& message ) override;

    private:
        void attachHost( OnlineServiceHost& host );
        void respond( OnlineServiceHost& host, uint64 requestTag, const MatchmakingReply& reply );
        void push( OnlineServiceHost& host, AccountId accountId, uint16 kind, const BitWriter& body );

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
        OnlineServiceHost*             _pHost; ///< 버스를 구독한 호스트(풀 때)
        int64                          _nowMs;
    };
} // namespace sw
