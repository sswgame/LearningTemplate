#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchmakingServer.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Bus/ServerBus.h"
#include "GameFramework/Base/Online/Identity/AccountPresence.h"
#include "GameFramework/Base/Online/Identity/OnlinePushRelay.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchServerAgent.h"

#include <algorithm>

namespace sw
{
    MatchmakingServer::MatchmakingServer()
        : _pendingTable{}
        , _listPartyCompletionScratch{}
        , _listPartyNotificationScratch{}
        , _listLobbyStartScratch{}
        , _listBrokenTicketScratch{}
        , _listQueueCompletionScratch{}
        , _listQueueNotificationScratch{}
        , _listTopicChangeScratch{}
        , _listSubscribedTopic{}
        , _pPartyLobby{ nullptr }
        , _pQueue{ nullptr }
        , _pAgent{ nullptr }
        , _pPresence{ nullptr }
        , _pHost{ nullptr }
        , _nowMs{ 0 }
    {
    }

    void MatchmakingServer::initialize( PartyLobbyService* pPartyLobby, MatchQueueService* pQueue, MatchServerAgent* pAgent, IAccountPresence* pPresence )
    {
        _pPartyLobby = pPartyLobby;
        _pQueue      = pQueue;
        _pAgent      = pAgent;
        _pPresence   = pPresence;
    }

    void MatchmakingServer::shutdown()
    {
        if ( _pHost != nullptr )
        {
            for ( const string& topic : _listSubscribedTopic )
            {
                _pHost->unsubscribeServerBus( topic, this );
            }
        }
        _listSubscribedTopic.clear();
        _pendingTable.clear();
        _pHost       = nullptr;
        _pPartyLobby = nullptr;
        _pQueue      = nullptr;
        _pAgent      = nullptr;
        _pPresence   = nullptr;
    }

    void MatchmakingServer::onHostShutdown( OnlineServiceHost& host )
    {
        (void)host;
        _listSubscribedTopic.clear(); // 구독은 호스트가 이어서 모두 해제한다
        _pHost = nullptr;
    }

    void MatchmakingServer::onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body )
    {
        if ( _pPartyLobby == nullptr || _pQueue == nullptr )
        {
            (void)host.respondError( context._token, OnlineError::kUnavailable );
            return;
        }
        const AccountID         accountID = context._accountID;
        MatchmakingIDRequest    idRequest;
        MatchmakingReadyRequest readyRequest;
        MatchmakingModeRequest  modeRequest;
        LobbySnapshot           lobby;
        FixedReadResult         readResult = FixedReadResult::Ok;
        switch ( context._method )
        {
            case MatchmakingMethod::kPartyInvite:
            case MatchmakingMethod::kPartyAccept:
            case MatchmakingMethod::kPartyKick:
            case MatchmakingMethod::kLobbyJoin:
            case MatchmakingMethod::kLobbyLeave:
            case MatchmakingMethod::kLobbyStart:
            {
                readResult = MatchmakingProtocol::MessageTypes::read( body, idRequest );
                break;
            }
            case MatchmakingMethod::kLobbyReady:
            {
                readResult = MatchmakingProtocol::MessageTypes::read( body, readyRequest );
                break;
            }
            case MatchmakingMethod::kLobbyCreate:
            {
                readResult = MatchmakingProtocol::MessageTypes::read( body, lobby );
                break;
            }
            case MatchmakingMethod::kLobbyList:
            case MatchmakingMethod::kQueueJoin:
            {
                readResult = MatchmakingProtocol::MessageTypes::read( body, modeRequest );
                break;
            }
            case MatchmakingMethod::kPartyCreate:
            case MatchmakingMethod::kPartyLeave:
            case MatchmakingMethod::kQueueLeave:
            {
                break;
            }
            default:
            {
                (void)host.respondError( context._token, OnlineError::kNotFound );
                return;
            }
        }
        if ( readResult != FixedReadResult::Ok )
        {
            (void)host.respondError( context._token, OnlineError::kInvalidRequest );
            return;
        }
        const uint64 tag = _pendingTable.add( context._token );
        switch ( context._method )
        {
            case MatchmakingMethod::kPartyCreate:
            {
                _pPartyLobby->createParty( accountID, tag );
                break;
            }
            case MatchmakingMethod::kPartyInvite:
            {
                _pPartyLobby->inviteToParty( accountID, idRequest._id, tag );
                break;
            }
            case MatchmakingMethod::kPartyAccept:
            {
                _pPartyLobby->acceptPartyInvite( accountID, idRequest._id, tag );
                break;
            }
            case MatchmakingMethod::kPartyLeave:
            {
                _pPartyLobby->leaveParty( accountID, tag );
                break;
            }
            case MatchmakingMethod::kPartyKick:
            {
                _pPartyLobby->kickFromParty( accountID, idRequest._id, tag );
                break;
            }
            case MatchmakingMethod::kLobbyCreate:
            {
                _pPartyLobby->createLobby( accountID, lobby, context._nowMs, tag );
                break;
            }
            case MatchmakingMethod::kLobbyList:
            {
                _pPartyLobby->listLobbies( modeRequest._modeID, tag );
                break;
            }
            case MatchmakingMethod::kLobbyJoin:
            {
                _pPartyLobby->joinLobby( accountID, idRequest._id, tag );
                break;
            }
            case MatchmakingMethod::kLobbyLeave:
            {
                _pPartyLobby->leaveLobby( accountID, idRequest._id, tag );
                break;
            }
            case MatchmakingMethod::kLobbyReady:
            {
                _pPartyLobby->setLobbyReady( accountID, readyRequest._lobbyID, readyRequest._bReady == SW_TRUE, tag );
                break;
            }
            case MatchmakingMethod::kLobbyStart:
            {
                _pPartyLobby->startLobby( accountID, idRequest._id, tag );
                break;
            }
            case MatchmakingMethod::kQueueJoin:
            {
                _pQueue->joinQueue( accountID, modeRequest._modeID, modeRequest._region, context._nowMs, tag );
                break;
            }
            case MatchmakingMethod::kQueueLeave:
            {
                _pQueue->leaveQueue( accountID, tag );
                break;
            }
            default:
            {
                break;
            }
        }
    }

    void MatchmakingServer::onServiceTick( OnlineServiceHost& host, int64 nowMs )
    {
        _nowMs = nowMs;
        if ( _pHost == nullptr )
            attachHost( host );
        if ( _pPartyLobby == nullptr || _pQueue == nullptr )
            return;
        _pQueue->update( nowMs );
        if ( _pAgent != nullptr )
            _pAgent->update( nowMs );

        _listLobbyStartScratch.clear();
        _pPartyLobby->drainLobbyStarts( _listLobbyStartScratch );
        for ( const LobbyStartRequest& start : _listLobbyStartScratch )
        {
            _pQueue->placeLobby( start._lobby, nowMs );
        }
        _listBrokenTicketScratch.clear();
        _pPartyLobby->drainBrokenTickets( _listBrokenTicketScratch );
        for ( const uint64 ticketID : _listBrokenTicketScratch )
        {
            _pQueue->cancelTicket( ticketID );
        }

        _listPartyCompletionScratch.clear();
        _pPartyLobby->drainCompletions( _listPartyCompletionScratch );
        for ( PartyLobbyCompletion& completion : _listPartyCompletionScratch )
        {
            MatchmakingReply reply;
            reply._result    = completion._result;
            reply._party     = std::move( completion._party );
            reply._lobby     = std::move( completion._lobby );
            reply._listLobby = std::move( completion._listLobby );
            respond( host, completion._requestTag, reply );
        }
        _listQueueCompletionScratch.clear();
        _pQueue->drainCompletions( _listQueueCompletionScratch );
        for ( const MatchQueueCompletion& completion : _listQueueCompletionScratch )
        {
            MatchmakingReply reply;
            reply._result   = completion._result;
            reply._ticketID = completion._ticketID;
            respond( host, completion._requestTag, reply );
        }

        _listPartyNotificationScratch.clear();
        _pPartyLobby->drainNotifications( _listPartyNotificationScratch );
        for ( const PartyLobbyNotification& notification : _listPartyNotificationScratch )
        {
            if ( notification._pushKind == MatchmakingMethod::kPushPartyInvite )
                push( host, notification._recipientID, notification._pushKind, notification._invite );
            else if ( notification._pushKind == MatchmakingMethod::kPushLobby )
                push( host, notification._recipientID, notification._pushKind, notification._lobby );
            else
                push( host, notification._recipientID, notification._pushKind, notification._party );
        }
        _listQueueNotificationScratch.clear();
        _pQueue->drainNotifications( _listQueueNotificationScratch );
        for ( const MatchQueueNotification& notification : _listQueueNotificationScratch )
        {
            push( host, notification._recipientID, MatchmakingMethod::kPushMatch, notification._assignment );
        }

        _listTopicChangeScratch.clear();
        _pQueue->drainTopicChanges( _listTopicChangeScratch );
        for ( const MatchQueueTopicChange& change : _listTopicChangeScratch )
        {
            const auto topicIt = std::find( _listSubscribedTopic.begin(), _listSubscribedTopic.end(), change._topic );
            if ( change._bSubscribe != SW_FALSE && topicIt == _listSubscribedTopic.end() )
            {
                host.subscribeServerBus( change._topic, this );
                _listSubscribedTopic.push_back( change._topic );
            }
            else if ( change._bSubscribe == SW_FALSE && topicIt != _listSubscribedTopic.end() )
            {
                host.unsubscribeServerBus( change._topic, this );
                _listSubscribedTopic.erase( topicIt );
            }
        }
    }

    void MatchmakingServer::onAccountLeft( OnlineServiceHost& host, AccountID accountID )
    {
        (void)host;
        if ( _pQueue != nullptr )
            _pQueue->removeAccount( accountID );
        if ( _pPartyLobby != nullptr )
            _pPartyLobby->removeAccount( accountID );
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
        if ( _pAgent == nullptr )
            return;
        const string topic = _pAgent->getAssignTopic();
        host.subscribeServerBus( topic, this );
        _listSubscribedTopic.push_back( topic );
    }

    void MatchmakingServer::respond( OnlineServiceHost& host, uint64 requestTag, const MatchmakingReply& reply )
    {
        NetRequestToken token;
        if ( requestTag == 0 || _pendingTable.take( requestTag, token ) == false )
            return; // 서버가 낸 요청이다. 계정이 떠난 경우이다
        (void)host.respondMessage<MatchmakingProtocol::MessageTypes>( token, reply );
    }

    template <typename TBody>
    void MatchmakingServer::push( OnlineServiceHost& host, AccountID accountID, uint16 kind, const TBody& message )
    {
        BitWriter body;
        if ( MatchmakingProtocol::MessageTypes::write( body, message ) == false )
            return; // write 가 범위 밖 값을 로그에 남겼다
        if ( host.sendPush( accountID, kind, body ) )
            return;
        if ( _pPresence != nullptr ) // 다른 서버에 접속한 멤버이다. 받는 서버가 자기 빌드의 번호와 B 로 다시 쓴다
            (void)_pPresence->sendRemotePush( accountID, static_cast<MatchmakingPushName>( kind - MatchmakingMethod::kFirstPush ), message );
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
