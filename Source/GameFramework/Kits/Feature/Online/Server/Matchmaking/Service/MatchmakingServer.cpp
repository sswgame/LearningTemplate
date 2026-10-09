#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Server/Matchmaking/Service/MatchmakingServer.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Bus/ServerBus.h"
#include "GameFramework/Base/Online/Directory/ServerRecord.h"
#include "GameFramework/Base/Online/Identity/AccountPresence.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Kits/Feature/Online/Server/Matchmaking/Service/MatchServerAgent.h"

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
        _listSubscribedTopic.clear(); // 구독은 호스트가 이어서 모두 푼다
        _pHost = nullptr;
    }

    void MatchmakingServer::onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body )
    {
        if ( _pPartyLobby == nullptr || _pQueue == nullptr )
        {
            (void)host.respondError( context._token, OnlineError::kUnavailable );
            return;
        }
        const AccountId accountId = context._accountId;
        uint64          id        = 0;
        string          modeId;
        string          region;
        LobbySnapshot   lobby;
        bool            bReady  = false;
        bool            bBodyOk = true;
        switch ( context._method )
        {
            case MatchmakingMethod::kPartyInvite:
            case MatchmakingMethod::kPartyAccept:
            case MatchmakingMethod::kPartyKick:
            case MatchmakingMethod::kLobbyJoin:
            case MatchmakingMethod::kLobbyLeave:
            case MatchmakingMethod::kLobbyStart:
            {
                id = body.readVarUint();
                break;
            }
            case MatchmakingMethod::kLobbyReady:
            {
                id     = body.readVarUint();
                bReady = body.readBool();
                break;
            }
            case MatchmakingMethod::kLobbyCreate:
            {
                bBodyOk = MatchmakingProtocol::readLobby( body, lobby );
                break;
            }
            case MatchmakingMethod::kLobbyList:
            {
                bBodyOk = ServiceKeyUtil::readString( body, MatchmakingLimit::kMaxIdSize, modeId );
                break;
            }
            case MatchmakingMethod::kQueueJoin:
            {
                bBodyOk = ServiceKeyUtil::readString( body, MatchmakingLimit::kMaxIdSize, modeId ) &&
                          ServiceKeyUtil::readString( body, ServerRecord::kMaxNameSize, region );
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
        if ( bBodyOk == false || body.hasOverflowed() )
        {
            (void)host.respondError( context._token, OnlineError::kInvalidRequest );
            return;
        }
        const uint64 tag = _pendingTable.add( context._token );
        switch ( context._method )
        {
            case MatchmakingMethod::kPartyCreate:
            {
                _pPartyLobby->createParty( accountId, tag );
                break;
            }
            case MatchmakingMethod::kPartyInvite:
            {
                _pPartyLobby->inviteToParty( accountId, id, tag );
                break;
            }
            case MatchmakingMethod::kPartyAccept:
            {
                _pPartyLobby->acceptPartyInvite( accountId, id, tag );
                break;
            }
            case MatchmakingMethod::kPartyLeave:
            {
                _pPartyLobby->leaveParty( accountId, tag );
                break;
            }
            case MatchmakingMethod::kPartyKick:
            {
                _pPartyLobby->kickFromParty( accountId, id, tag );
                break;
            }
            case MatchmakingMethod::kLobbyCreate:
            {
                _pPartyLobby->createLobby( accountId, lobby, context._nowMs, tag );
                break;
            }
            case MatchmakingMethod::kLobbyList:
            {
                _pPartyLobby->listLobbies( modeId, tag );
                break;
            }
            case MatchmakingMethod::kLobbyJoin:
            {
                _pPartyLobby->joinLobby( accountId, id, tag );
                break;
            }
            case MatchmakingMethod::kLobbyLeave:
            {
                _pPartyLobby->leaveLobby( accountId, id, tag );
                break;
            }
            case MatchmakingMethod::kLobbyReady:
            {
                _pPartyLobby->setLobbyReady( accountId, id, bReady, tag );
                break;
            }
            case MatchmakingMethod::kLobbyStart:
            {
                _pPartyLobby->startLobby( accountId, id, tag );
                break;
            }
            case MatchmakingMethod::kQueueJoin:
            {
                _pQueue->joinQueue( accountId, modeId, region, context._nowMs, tag );
                break;
            }
            case MatchmakingMethod::kQueueLeave:
            {
                _pQueue->leaveQueue( accountId, tag );
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
        _pQueue->tick( nowMs );
        if ( _pAgent != nullptr )
            _pAgent->tick( nowMs );

        _listLobbyStartScratch.clear();
        _pPartyLobby->drainLobbyStarts( _listLobbyStartScratch );
        for ( const LobbyStartRequest& start : _listLobbyStartScratch )
        {
            _pQueue->placeLobby( start._lobby, nowMs );
        }
        _listBrokenTicketScratch.clear();
        _pPartyLobby->drainBrokenTickets( _listBrokenTicketScratch );
        for ( const uint64 ticketId : _listBrokenTicketScratch )
        {
            _pQueue->cancelTicket( ticketId );
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
            reply._ticketId = completion._ticketId;
            respond( host, completion._requestTag, reply );
        }

        _listPartyNotificationScratch.clear();
        _pPartyLobby->drainNotifications( _listPartyNotificationScratch );
        for ( const PartyLobbyNotification& notification : _listPartyNotificationScratch )
        {
            BitWriter body;
            if ( notification._pushKind == MatchmakingMethod::kPushPartyInvite )
                MatchmakingProtocol::writeInvite( body, notification._invite );
            else if ( notification._pushKind == MatchmakingMethod::kPushLobby )
                MatchmakingProtocol::writeLobby( body, notification._lobby );
            else
                MatchmakingProtocol::writeParty( body, notification._party );
            push( host, notification._recipientId, notification._pushKind, body );
        }
        _listQueueNotificationScratch.clear();
        _pQueue->drainNotifications( _listQueueNotificationScratch );
        for ( const MatchQueueNotification& notification : _listQueueNotificationScratch )
        {
            BitWriter body;
            MatchmakingProtocol::writeAssignment( body, notification._assignment );
            push( host, notification._recipientId, MatchmakingMethod::kPushMatch, body );
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

    void MatchmakingServer::onAccountLeft( OnlineServiceHost& host, AccountId accountId )
    {
        (void)host;
        if ( _pQueue != nullptr )
            _pQueue->removeAccount( accountId );
        if ( _pPartyLobby != nullptr )
            _pPartyLobby->removeAccount( accountId );
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
            return; // 서버가 낸 요청(계정이 떠남)
        BitWriter body;
        MatchmakingProtocol::writeReply( body, reply );
        (void)host.respondOk( token, body );
    }

    void MatchmakingServer::push( OnlineServiceHost& host, AccountId accountId, uint16 kind, const BitWriter& body )
    {
        if ( host.sendPush( accountId, kind, body ) )
            return;
        if ( _pPresence != nullptr )
            (void)_pPresence->sendRemotePush( accountId, kind, body ); // 다른 서버에 붙은 회원
    }
} // namespace sw
