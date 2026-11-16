#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchmakingServer.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Serialization/BitStream.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchServerAgent.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Identity/AccountPresence.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Identity/GroupChannelMembership.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Identity/OnlinePushRelay.h"
#include "GameFramework/Runtime/Online/Bus/ServerBus.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct MatchmakingServerInternal
        {
            static constexpr int64 kDefaultReconnectGraceMs = 60000; ///< 60초이다. 모바일 앱 전환과 짧은 끊김을 넘길 만큼이다
        };
    } // namespace
} // namespace sw

namespace sw
{
    MatchmakingServer::MatchmakingServer()
        : _listPartyCompletionScratch{}
        , _listPartyNotificationScratch{}
        , _listLobbyStartScratch{}
        , _listBrokenTicketScratch{}
        , _listQueueCompletionScratch{}
        , _listQueueNotificationScratch{}
        , _listTopicChangeScratch{}
        , _mapTopicSubscription{}
        , _pPartyLobby{ nullptr }
        , _pQueue{ nullptr }
        , _pAgent{ nullptr }
        , _mapAccountToGraceEnd{}
        , _presenceFinds{}
        , _pGroupChannels{ nullptr }
        , _pHost{ nullptr }
        , _nowMs{ 0 }
        , _reconnectGraceMs{ MatchmakingServerInternal::kDefaultReconnectGraceMs }
    {
        setReady( false );
    }

    void MatchmakingServer::initialize( PartyLobbyService* pPartyLobby, MatchQueueService* pQueue, MatchServerAgent* pAgent, IAccountPresence* pPresence, MetricRegistry* pMetricRegistry )
    {
        _pPartyLobby = pPartyLobby;
        _pQueue      = pQueue;
        _pAgent      = pAgent;
        _presenceFinds.bind( pPresence );
        initializeCalls( pMetricRegistry, "matchmaking" );
        setReady( pPartyLobby != nullptr && pQueue != nullptr );
    }

    void MatchmakingServer::shutdown()
    {
        _presenceFinds.unbind(); // 기다리던 찾기가 이 객체를 부르지 않게 한다. 답을 못 받은 계정은 취소 완료가 유예를 지금 끝낸다(`handleGracePresenceResult`)
        if ( _pPartyLobby != nullptr )
        {
            for ( const auto& [accountID, graceEndMs] : _mapAccountToGraceEnd )
            {
                (void)graceEndMs;
                _pPartyLobby->removeAccount( accountID ); // 종료하는 서버는 유예를 지킬 수 없다. 남기면 파티 기록의 시한(30분)까지 빈 슬롯이 된다
            }
        }
        _mapAccountToGraceEnd.clear();
        _mapTopicSubscription.clear(); // 구독이 사라지며 버스 구독을 끊는다
        _calls.clear();
        setReady( false );
        _pHost          = nullptr;
        _pPartyLobby    = nullptr;
        _pQueue         = nullptr;
        _pAgent         = nullptr;
        _pGroupChannels = nullptr;
    }

    void MatchmakingServer::onHostShutdown( OnlineServiceHost& host )
    {
        (void)host;
        _mapTopicSubscription.clear();
        _pHost = nullptr;
    }

    void MatchmakingServer::handleCall( OnlineServerCall<MatchmakingMethod::PartyCreate> call, const OnlineCallContext& context, const OnlineNoBody& request )
    {
        (void)request;
        _pPartyLobby->createParty( context._accountID, call._callID );
    }

    void MatchmakingServer::handleCall( OnlineServerCall<MatchmakingMethod::PartyInvite> call, const OnlineCallContext& context, const MatchmakingIDRequest& request )
    {
        _pPartyLobby->inviteToParty( context._accountID, request._id, call._callID );
    }

    void MatchmakingServer::handleCall( OnlineServerCall<MatchmakingMethod::PartyAccept> call, const OnlineCallContext& context, const MatchmakingIDRequest& request )
    {
        _pPartyLobby->acceptPartyInvite( context._accountID, request._id, call._callID );
    }

    void MatchmakingServer::handleCall( OnlineServerCall<MatchmakingMethod::PartyLeave> call, const OnlineCallContext& context, const OnlineNoBody& request )
    {
        (void)request;
        _pPartyLobby->leaveParty( context._accountID, call._callID );
    }

    void MatchmakingServer::handleCall( OnlineServerCall<MatchmakingMethod::PartyKick> call, const OnlineCallContext& context, const MatchmakingIDRequest& request )
    {
        _pPartyLobby->kickFromParty( context._accountID, request._id, call._callID );
    }

    void MatchmakingServer::handleCall( OnlineServerCall<MatchmakingMethod::LobbyCreate> call, const OnlineCallContext& context, const LobbySnapshot& request )
    {
        _pPartyLobby->createLobby( context._accountID, request, context._nowMs, call._callID );
    }

    void MatchmakingServer::handleCall( OnlineServerCall<MatchmakingMethod::LobbyList> call, const OnlineCallContext& context, const MatchmakingModeRequest& request )
    {
        (void)context; // 목록은 누가 묻든 같다
        _pPartyLobby->listLobbies( request._modeID, call._callID );
    }

    void MatchmakingServer::handleCall( OnlineServerCall<MatchmakingMethod::LobbyJoin> call, const OnlineCallContext& context, const MatchmakingIDRequest& request )
    {
        _pPartyLobby->joinLobby( context._accountID, request._id, call._callID );
    }

    void MatchmakingServer::handleCall( OnlineServerCall<MatchmakingMethod::LobbyLeave> call, const OnlineCallContext& context, const MatchmakingIDRequest& request )
    {
        _pPartyLobby->leaveLobby( context._accountID, request._id, call._callID );
    }

    void MatchmakingServer::handleCall( OnlineServerCall<MatchmakingMethod::LobbyReady> call, const OnlineCallContext& context, const MatchmakingReadyRequest& request )
    {
        _pPartyLobby->setLobbyReady( context._accountID, request._lobbyID, request._bReady == SW_TRUE, call._callID );
    }

    void MatchmakingServer::handleCall( OnlineServerCall<MatchmakingMethod::LobbyStart> call, const OnlineCallContext& context, const MatchmakingIDRequest& request )
    {
        _pPartyLobby->startLobby( context._accountID, request._id, call._callID );
    }

    void MatchmakingServer::handleCall( OnlineServerCall<MatchmakingMethod::QueueJoin> call, const OnlineCallContext& context, const MatchmakingModeRequest& request )
    {
        _pQueue->joinQueue( context._accountID, request._modeID, request._region, context._nowMs, call._callID );
    }

    void MatchmakingServer::handleCall( OnlineServerCall<MatchmakingMethod::QueueLeave> call, const OnlineCallContext& context, const OnlineNoBody& request )
    {
        (void)request;
        _pQueue->leaveQueue( context._accountID, call._callID );
    }

    void MatchmakingServer::onServiceTick( OnlineServiceHost& host, UnixMilliseconds nowMs )
    {
        _nowMs = nowMs;
        if ( _pHost == nullptr )
            attachHost( host );
        if ( _pPartyLobby == nullptr || _pQueue == nullptr )
            return;
        _pQueue->update( nowMs );
        _calls.publishPendingCount();
        if ( _pAgent != nullptr )
            _pAgent->update( nowMs );
        expireGrace( nowMs );

        _listLobbyStartScratch.clear();
        _pPartyLobby->pollLobbyStarts( _listLobbyStartScratch );
        for ( const LobbyStartRequest& start : _listLobbyStartScratch )
        {
            _pQueue->placeLobby( start._lobby, nowMs );
        }
        _listBrokenTicketScratch.clear();
        _pPartyLobby->pollBrokenTickets( _listBrokenTicketScratch );
        for ( const uint64 ticketID : _listBrokenTicketScratch )
        {
            _pQueue->cancelTicket( ticketID );
        }

        _listPartyCompletionScratch.clear();
        _pPartyLobby->takeCompletions( _listPartyCompletionScratch );
        for ( PartyLobbyCompletion& completion : _listPartyCompletionScratch )
        {
            (void)completeByMethod( completion._requestTag, completion ); // 응답 타입은 호출의 메서드가 정한다(`fillReply`). 요청 태그가 0이면 서버가 낸 요청이다
        }
        _listQueueCompletionScratch.clear();
        _pQueue->takeCompletions( _listQueueCompletionScratch );
        for ( MatchQueueCompletion& completion : _listQueueCompletionScratch )
        {
            (void)completeByMethod( completion._requestTag, completion ); // 요청 태그가 0이면 서버가 낸 요청이다
        }

        _listPartyNotificationScratch.clear();
        _pPartyLobby->pollNotifications( _listPartyNotificationScratch );
        for ( const PartyLobbyNotification& notification : _listPartyNotificationScratch )
        {
            if ( notification._pushKind == MatchmakingMethod::PartyInvited::kKind )
                push( host, notification._recipientID, notification._pushKind, notification._invite );
            else if ( notification._pushKind == MatchmakingMethod::Lobby::kKind )
                push( host, notification._recipientID, notification._pushKind, notification._lobby );
            else
            {
                push( host, notification._recipientID, notification._pushKind, notification._party );
                syncPartyChannel( notification._recipientID, notification._party );
            }
        }
        _listQueueNotificationScratch.clear();
        _pQueue->pollNotifications( _listQueueNotificationScratch );
        for ( const MatchQueueNotification& notification : _listQueueNotificationScratch )
        {
            push( host, notification._recipientID, MatchmakingMethod::Match::kKind, notification._assignment );
        }

        _listTopicChangeScratch.clear();
        _pQueue->pollTopicChanges( _listTopicChangeScratch );
        for ( const MatchQueueTopicChange& change : _listTopicChangeScratch )
        {
            const bool bSubscribed   = _mapTopicSubscription.contains( change._topic );
            const bool bShouldListen = change._bSubscribe != SW_FALSE && host.getServerBus() != nullptr; // 서버가 한 대면 들을 버스가 없다
            const bool bNewSubscribe = bShouldListen && bSubscribed == false;
            if ( bNewSubscribe )
                _mapTopicSubscription.emplace( change._topic, host.subscribeServerBus( change._topic, this ) );
            else if ( change._bSubscribe == SW_FALSE && bSubscribed )
                _mapTopicSubscription.erase( change._topic ); // 구독이 사라지며 끊는다
        }
    }

    void MatchmakingServer::onAccountJoined( OnlineServiceHost& host, AccountID accountID )
    {
        (void)host;
        _mapAccountToGraceEnd.erase( accountID ); // 유예 안에 이 서버로 돌아왔다
        if ( _pPartyLobby != nullptr && _pGroupChannels != nullptr )
            _pPartyLobby->findPartyOfAccount( accountID, accountID, PartyLobbyService::PartyFoundDelegate::create<&MatchmakingServer::onPartyFound>( this ) );
    }

    void MatchmakingServer::onAccountLeft( OnlineServiceHost& host, AccountID accountID )
    {
        (void)host;
        if ( _pQueue != nullptr )
            _pQueue->removeAccount( accountID ); // 떠난 사람과 경기를 만들 수 없으므로 대기열에서는 바로 뺀다
        if ( _pPartyLobby == nullptr )
            return;
        if ( _reconnectGraceMs <= 0 )
        {
            _pPartyLobby->removeAccount( accountID );
            return;
        }
        _mapAccountToGraceEnd[accountID] = _nowMs.count() + _reconnectGraceMs;
    }

    void MatchmakingServer::expireGrace( UnixMilliseconds nowMs )
    {
        for ( auto graceIt = _mapAccountToGraceEnd.begin(); graceIt != _mapAccountToGraceEnd.end(); )
        {
            if ( nowMs.count() < graceIt->second )
            {
                ++graceIt;
                continue;
            }
            const AccountID accountID = graceIt->first;
            graceIt                   = _mapAccountToGraceEnd.erase( graceIt );
            const bool bFinding       = _presenceFinds.findByAccount( accountID, accountID, [this, accountID]( const AccountPresenceResult& result, PendingRequestEnd end )
                  { handleGracePresenceResult( accountID, result, end ); } );
            if ( bFinding == false )
                _pPartyLobby->removeAccount( accountID ); // 서버가 한 대이고 돌아오지 않았다
        }
    }

    void MatchmakingServer::handleGracePresenceResult( AccountID accountID, const AccountPresenceResult& result, PendingRequestEnd end )
    {
        if ( end == PendingRequestEnd::Cancelled )
        {
            _mapAccountToGraceEnd[accountID] = _nowMs.count(); // 내려가는 서버는 유예를 지킬 수 없다 — 아래 정리가 뺀다
            return;
        }
        if ( result.isOnline() || _pPartyLobby == nullptr )
            return; // 다른 서버(또는 이 서버)로 돌아왔다. 파티와 로비에 그대로 둔다
        _pPartyLobby->removeAccount( accountID );
    }

    void MatchmakingServer::fillReply( MatchmakingPartyReply& reply, PartyLobbyCompletion& completion )
    {
        reply._party  = std::move( completion._party );
        reply._result = completion._result._result;
    }

    void MatchmakingServer::fillReply( MatchmakingLobbyReply& reply, PartyLobbyCompletion& completion )
    {
        reply._lobby  = std::move( completion._lobby );
        reply._result = completion._result._result;
    }

    void MatchmakingServer::fillReply( MatchmakingLobbyListReply& reply, PartyLobbyCompletion& completion )
    {
        reply._listLobby = std::move( completion._listLobby );
        reply._result    = completion._result._result;
    }

    void MatchmakingServer::fillReply( MatchmakingTicketReply& reply, MatchQueueCompletion& completion )
    {
        reply._ticketID = completion._ticketID;
        reply._result   = completion._result._result;
    }

    void MatchmakingServer::onPartyFound( uint64 lookupTag, MatchmakingServiceResult result, const PartySnapshot& party )
    {
        if ( result != MatchmakingResult::Ok || _pGroupChannels == nullptr )
            return; // 파티가 없다
        syncPartyChannel( static_cast<AccountID>( lookupTag ), party );
    }

    void MatchmakingServer::syncPartyChannel( AccountID recipientID, const PartySnapshot& party )
    {
        StreamConnectionHandle connection;
        const bool             bLocal    = _pHost != nullptr && _pHost->findConnection( recipientID, connection );
        const bool             bSyncHere = _pGroupChannels != nullptr && bLocal && party._partyID != 0;
        if ( bSyncHere == false )
            return; // 다른 서버의 멤버는 그 서버가 맞춘다
        const bool bMember = VectorUtil::contains( party._listMemberID, recipientID );
        if ( bMember )
            _pGroupChannels->addGroupMember( GroupChannelKind::Party, party._partyID, recipientID, _nowMs );
        else
            _pGroupChannels->removeGroupMember( GroupChannelKind::Party, party._partyID, recipientID );
    }

    void MatchmakingServer::onServerBusMessage( OnlineServiceHost& host, const ServerBusMessage& message )
    {
        (void)host;
        if ( _pAgent != nullptr && message.getTopic() == _pAgent->getAssignTopic() )
        {
            (void)_pAgent->handleAssign( message, _nowMs ); // 읽지 못한 배정은 읽기가 로그와 카운터를 남겼다
            return;
        }
        if ( _pQueue != nullptr )
            _pQueue->handleBusMessage( message, _nowMs );
    }

    void MatchmakingServer::attachHost( OnlineServiceHost& host )
    {
        _pHost = &host;
        if ( _pAgent == nullptr || host.getServerBus() == nullptr )
            return; // 배정 받을 서버가 아니거나 서버가 한 대이다
        const string topic = _pAgent->getAssignTopic();
        _mapTopicSubscription.emplace( topic, host.subscribeServerBus( topic, this ) );
    }

    template <typename TBody>
    void MatchmakingServer::push( OnlineServiceHost& host, AccountID accountID, uint16 kind, const TBody& message )
    {
        BitWriter body;
        if ( MatchmakingProtocol::MessageTypes::write( body, message ) == false )
            return; // write 가 범위 밖 값을 로그에 남겼다
        if ( host.sendPush( accountID, kind, body ) )
            return;
        if ( _presenceFinds.hasPresence() ) // 다른 서버에 접속한 멤버이다. 받는 서버가 자기 빌드의 번호와 B 로 다시 쓴다
            (void)_presenceFinds.getPresence()->sendRemotePush( accountID, static_cast<MatchmakingPushName>( kind - MatchmakingMethod::kFirstPush ), message );
    }
} // namespace sw

namespace sw
{
    // 다른 서버에 접속한 계정에게 가는 알림이다. 받는 서버가 이 항목으로 자기 빌드의 번호와 B 를 만든다(OnlinePushRelay.h).
    const OnlinePushRelayEntry kMatchmakingPartyRelay{ "Matchmaking.Party", MatchmakingMethod{}, MatchmakingPushName::Party, MatchmakingProtocol::MessageTypes{}, OnlinePushBody<PartySnapshot>{} };
    const OnlinePushRelayEntry kMatchmakingPartyInviteRelay{ "Matchmaking.PartyInvite", MatchmakingMethod{}, MatchmakingPushName::PartyInvite, MatchmakingProtocol::MessageTypes{}, OnlinePushBody<PartyInvite>{} };
    const OnlinePushRelayEntry kMatchmakingLobbyRelay{ "Matchmaking.Lobby", MatchmakingMethod{}, MatchmakingPushName::Lobby, MatchmakingProtocol::MessageTypes{}, OnlinePushBody<LobbySnapshot>{} };
    const OnlinePushRelayEntry kMatchmakingMatchRelay{ "Matchmaking.Match", MatchmakingMethod{}, MatchmakingPushName::Match, MatchmakingProtocol::MessageTypes{}, OnlinePushBody<MatchAssignment>{} };
} // namespace sw
