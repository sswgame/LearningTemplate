/**
 * @file MatchmakingServer.h
 * @brief 매칭 서비스의 스트림 바인딩입니다. `IOnlineServerService`(영역 `kMatchmaking`)를 구현합니다.
 * @details 파티와 로비 요청은 `PartyLobbyService` 에, 대기열 요청은 `MatchQueueService` 에 요청 태그와 함께 맡긴다.
 *          완료는 응답(본문의 첫 값이 결과)으로, 알림은 멤버에게 보낸다. 이 프로세스에 접속하지 않은 멤버에게는 접속 상태 관리자(`IAccountPresence`)로 넘긴다.
 *          - 서비스 틱은 대기열 틱과 게임 서버 에이전트 틱을 부른 뒤, 완료, 알림, 로비 시작(배정으로 이어짐), 깨진 파티 티켓(대기열에서 뺌), 버스 주제 변경(구독으로 이어짐)을 처리한다.
 *          - 버스 구독은 첫 서비스 틱부터 하며 이때는 호스트가 버스를 받은 뒤이다. 계정이 떠나면 대기열에서는 바로 빼고, 파티와 이 서버가 들여보낸 로비에서는
 *            재접속 유예(`setReconnectGrace`, 기본 60초)가 지난 뒤에 뺀다. 유예 안에 이 서버로 돌아오면 취소하고, 시한에 접속 상태로 물어 다른 서버에 붙어 있으면 남긴다.
 *            다른 서버로 돌아온 계정이 그 서버에서 떠나면 그 서버는 이 서버가 들여보낸 로비를 모르므로 로비 기록의 시한(2시간)까지 남는다.
 *          - 채팅 키트가 있으면(`setGroupChannels`) 파티 알림을 받는 이 서버의 계정을 파티 채널에 넣고 뺀다. 접속하면 파티를 찾아 다시 넣는다.
 *            다른 서버에 있는 멤버가 내보내지면 그 서버의 채널에는 다음 접속까지 남는다(중계된 알림은 그 서버의 바인딩을 거치지 않는다).
 *          - 호스트에 등록하는 일은 호출하는 쪽이 한다(`host.registerServerService( &server )`, 호스트 `initialize` 전에).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Base/Online/Service/ServicePendingTable.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchQueueService.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/PartyLobbyService.h"

namespace sw
{
    struct AccountPresenceResult;

    class IAccountPresence;
    class IGroupChannelMembership;
    class MatchServerAgent;

    /**
     * @class MatchmakingServer
     * @brief 매칭 바인딩입니다. 호스트 `tick` 스레드에서 씁니다.
     */
    class SW_KIT_SERVER_API MatchmakingServer final : public IOnlineServerService
    {
    public:
        MatchmakingServer();

        /** @brief 넘긴 객체는 빌려 씁니다. @p pAgent 는 게임 서버일 때만, @p pPresence 는 서버가 여러 대일 때만 넘기고 없으면 nullptr 입니다. */
        void initialize( PartyLobbyService* pPartyLobby, MatchQueueService* pQueue, MatchServerAgent* pAgent, IAccountPresence* pPresence );
        /** @brief 버스 구독을 해제하고 유예 중인 계정을 파티와 로비에서 뺍니다. 로직은 호출하는 쪽이 종료합니다. */
        void shutdown();
        /** @brief 떠난 계정을 파티와 로비에 남겨 둘 시간입니다. 0 이면 바로 뺍니다. */
        void setReconnectGrace( int64 graceMs ) { _reconnectGraceMs = graceMs; }
        /** @brief 파티 채널 멤버 창입니다(채팅 키트, 빌려 쓴다). 없으면 파티 채널을 쓰지 않습니다. */
        void setGroupChannels( IGroupChannelMembership* pGroupChannels ) { _pGroupChannels = pGroupChannels; }

        // IOnlineServerService
        const OnlineProtocolEntry& getProtocol() const override { return MatchmakingProtocol::kOnlineEntry; }
        void                       onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) override;
        void                       onServiceTick( OnlineServiceHost& host, UnixMilliseconds nowMs ) override;
        void                       onHostShutdown( OnlineServiceHost& host ) override;
        void                       onAccountJoined( OnlineServiceHost& host, AccountID accountID ) override;
        void                       onAccountLeft( OnlineServiceHost& host, AccountID accountID ) override;
        int32                      getGraceCount() const { return static_cast<int32>( _mapAccountToGraceEnd.size() ); }
        void                       onServerBusMessage( OnlineServiceHost& host, const ServerBusMessage& message ) override;

    private:
        void attachHost( OnlineServiceHost& host );
        /** @brief 유예가 끝난 계정을 접속 상태로 묻거나(서버 여러 대) 바로 뺍니다. */
        void expireGrace( UnixMilliseconds nowMs );
        void onGracePresenceFound( const AccountPresenceResult& result );
        void onPartyFound( uint64 lookupTag, MatchmakingResult result, const PartySnapshot& party );
        /** @brief 이 서버 계정 @p recipientID 가 받은 파티 스냅숏으로 파티 채널 멤버를 맞춥니다. 멤버가 비었으면 빠진 것이다. */
        void syncPartyChannel( AccountID recipientID, const PartySnapshot& party );
        void respond( OnlineServiceHost& host, uint64 requestTag, const MatchmakingReply& reply );
        /** @brief 알림 @p kind 를 이 서버의 계정에게 B 로 보내고, 없으면 다른 서버로 중계합니다(`IAccountPresence::sendRemotePush`). 정의는 `.cpp` 에만 있습니다. */
        template <typename TBody>
        void push( OnlineServiceHost& host, AccountID accountID, uint16 kind, const TBody& message );

        ServicePendingTable                 _pendingTable;
        vector<PartyLobbyCompletion>        _listPartyCompletionScratch;
        vector<PartyLobbyNotification>      _listPartyNotificationScratch;
        vector<LobbyStartRequest>           _listLobbyStartScratch;
        vector<uint64>                      _listBrokenTicketScratch;
        vector<MatchQueueCompletion>        _listQueueCompletionScratch;
        vector<MatchQueueNotification>      _listQueueNotificationScratch;
        vector<MatchQueueTopicChange>       _listTopicChangeScratch;
        unordered_map<string, Subscription> _mapTopicSubscription; ///< 버스 주제 → 그 구독(배정 주제와 큐가 고른 주제). 서버가 한 대면 비어 있다
        PartyLobbyService*                  _pPartyLobby;
        MatchQueueService*                  _pQueue;
        MatchServerAgent*                   _pAgent;
        unordered_map<AccountID, int64>     _mapAccountToGraceEnd;        ///< 떠났지만 파티와 로비에 남겨 둔 계정과 유예 끝 시각이다
        unordered_map<uint64, AccountID>    _mapPresenceRequestToAccount; ///< 유예가 끝나 접속 상태를 묻는 중인 계정이다
        IAccountPresence*                   _pPresence;
        Subscription                        _findOwner; ///< 접속 상태 찾기의 주인 몫(`initialize` 가 열고 `shutdown` 이 놓는다)
        IGroupChannelMembership*            _pGroupChannels;
        OnlineServiceHost*                  _pHost; ///< 붙은 호스트이다(첫 서비스 틱에 붙는다). 붙었는지만 본다
        UnixMilliseconds                    _nowMs;
        int64                               _reconnectGraceMs;
    };
} // namespace sw
