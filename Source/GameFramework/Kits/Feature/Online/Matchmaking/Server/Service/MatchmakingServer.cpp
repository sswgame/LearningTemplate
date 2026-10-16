#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchmakingServer.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Bus/ServerBus.h"
#include "GameFramework/Base/Online/Identity/AccountPresence.h"
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
            BitWriter body;
            bool      bWritten = false;
            if ( notification._pushKind == MatchmakingMethod::kPushPartyInvite )
                bWritten = MatchmakingProtocol::MessageTypes::write( body, notification._invite );
            else if ( notification._pushKind == MatchmakingMethod::kPushLobby )
                bWritten = MatchmakingProtocol::MessageTypes::write( body, notification._lobby );
            else
                bWritten = MatchmakingProtocol::MessageTypes::write( body, notification._party );
            if ( bWritten ) // 쓰지 못했으면 write 가 범위 밖 값을 로그에 남겼다
                push( host, notification._recipientID, notification._pushKind, body );
        }
        _listQueueNotificationScratch.clear();
        _pQueue->drainNotifications( _listQueueNotificationScratch );
        for ( const MatchQueueNotification& notification : _listQueueNotificationScratch )
        {
            BitWriter body;
            if ( MatchmakingProtocol::MessageTypes::write( body, notification._assignment ) ) // 쓰지 못했으면 write 가 범위 밖 값을 로그에 남겼다
                push( host, notification._recipientID, MatchmakingMethod::kPushMatch, body );
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
        if ( _pAgent != nullptr && message._topic == _pAgent->getAssignTopic() )
        {
            (void)_pAgent->handleAssign( message._bytes, _nowMs );
            return;
        }
        if ( _pQueue != nullptr )
            _pQueue->handleBusMessage( message._topic, message._bytes, _nowMs );
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
        BitWriter body;
        if ( MatchmakingProtocol::MessageTypes::write( body, reply ) )
            (void)host.respondOk( token, body );
        else
            (void)host.respondError( token, OnlineError::kInternal ); // write 가 범위 밖 값을 로그에 남겼다
    }

    void MatchmakingServer::push( OnlineServiceHost& host, AccountID accountID, uint16 kind, const BitWriter& body )
    {
        if ( host.sendPush( accountID, kind, body ) )
            return;
        if ( _pPresence != nullptr )
            (void)_pPresence->sendRemotePush( accountID, kind, body ); // 다른 서버에 접속한 멤버이다
    }
} // namespace sw
