#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchmakingServer.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Bus/ServerBus.h"
#include "GameFramework/Base/Online/Identity/AccountPresence.h"
#include "GameFramework/Base/Online/Identity/GroupChannelMembership.h"
#include "GameFramework/Base/Online/Identity/OnlinePushRelay.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchServerAgent.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct MatchmakingServerInternal
        {
            static constexpr int64 kDefaultReconnectGraceMs = 60000; ///< 60초이다. 모바일 앱 전환과 짧은 끊김을 넘길 만큼이다

            /** @brief 본문을 읽은 결과이다. */
            enum class ReadStatus : uint8
            {
                Ok,
                UnknownMethod,
                Malformed,
            };

            /** @brief 요청 본문에서 읽은 값이다. 메서드마다 쓰는 필드가 달라 한 구조체에 모은다. */
            struct Request
            {
                MatchmakingIDRequest    _idRequest;
                MatchmakingReadyRequest _readyRequest;
                MatchmakingModeRequest  _modeRequest;
                LobbySnapshot           _lobby;
            };

            /** @brief 메서드에 맞는 본문을 `outRequest` 로 읽는다. 본문이 없는 메서드는 읽지 않고 Ok 이다. */
            static ReadStatus readRequest( uint16 method, BitReader& body, Request& outRequest )
            {
                FixedReadResult readResult = FixedReadResult::Ok;
                switch ( method )
                {
                    case MatchmakingMethod::kPartyInvite:
                    case MatchmakingMethod::kPartyAccept:
                    case MatchmakingMethod::kPartyKick:
                    case MatchmakingMethod::kLobbyJoin:
                    case MatchmakingMethod::kLobbyLeave:
                    case MatchmakingMethod::kLobbyStart:
                    {
                        readResult = MatchmakingProtocol::MessageTypes::read( body, outRequest._idRequest );
                        break;
                    }
                    case MatchmakingMethod::kLobbyReady:
                    {
                        readResult = MatchmakingProtocol::MessageTypes::read( body, outRequest._readyRequest );
                        break;
                    }
                    case MatchmakingMethod::kLobbyCreate:
                    {
                        readResult = MatchmakingProtocol::MessageTypes::read( body, outRequest._lobby );
                        break;
                    }
                    case MatchmakingMethod::kLobbyList:
                    case MatchmakingMethod::kQueueJoin:
                    {
                        readResult = MatchmakingProtocol::MessageTypes::read( body, outRequest._modeRequest );
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
                        return ReadStatus::UnknownMethod;
                    }
                }
                return readResult == FixedReadResult::Ok ? ReadStatus::Ok : ReadStatus::Malformed;
            }

            /** @brief 읽은 요청을 파티 · 로비 서비스나 큐 서비스로 보낸다. `tag` 는 응답을 되찾는 대기 테이블 번호이다. */
            static void dispatch( PartyLobbyService& partyLobby, MatchQueueService& queue, const OnlineCallContext& context, const Request& request, uint64 tag )
            {
                const AccountID accountID = context._accountID;
                switch ( context._method )
                {
                    case MatchmakingMethod::kPartyCreate:
                    {
                        partyLobby.createParty( accountID, tag );
                        break;
                    }
                    case MatchmakingMethod::kPartyInvite:
                    {
                        partyLobby.inviteToParty( accountID, request._idRequest._id, tag );
                        break;
                    }
                    case MatchmakingMethod::kPartyAccept:
                    {
                        partyLobby.acceptPartyInvite( accountID, request._idRequest._id, tag );
                        break;
                    }
                    case MatchmakingMethod::kPartyLeave:
                    {
                        partyLobby.leaveParty( accountID, tag );
                        break;
                    }
                    case MatchmakingMethod::kPartyKick:
                    {
                        partyLobby.kickFromParty( accountID, request._idRequest._id, tag );
                        break;
                    }
                    case MatchmakingMethod::kLobbyCreate:
                    {
                        partyLobby.createLobby( accountID, request._lobby, context._nowMs, tag );
                        break;
                    }
                    case MatchmakingMethod::kLobbyList:
                    {
                        partyLobby.listLobbies( request._modeRequest._modeID, tag );
                        break;
                    }
                    case MatchmakingMethod::kLobbyJoin:
                    {
                        partyLobby.joinLobby( accountID, request._idRequest._id, tag );
                        break;
                    }
                    case MatchmakingMethod::kLobbyLeave:
                    {
                        partyLobby.leaveLobby( accountID, request._idRequest._id, tag );
                        break;
                    }
                    case MatchmakingMethod::kLobbyReady:
                    {
                        partyLobby.setLobbyReady( accountID, request._readyRequest._lobbyID, request._readyRequest._bReady == SW_TRUE, tag );
                        break;
                    }
                    case MatchmakingMethod::kLobbyStart:
                    {
                        partyLobby.startLobby( accountID, request._idRequest._id, tag );
                        break;
                    }
                    case MatchmakingMethod::kQueueJoin:
                    {
                        queue.joinQueue( accountID, request._modeRequest._modeID, request._modeRequest._region, context._nowMs, tag );
                        break;
                    }
                    case MatchmakingMethod::kQueueLeave:
                    {
                        queue.leaveQueue( accountID, tag );
                        break;
                    }
                    default:
                    {
                        break;
                    }
                }
            }
        };
    } // namespace
} // namespace sw

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
        , _mapAccountToGraceEnd{}
        , _mapPresenceRequestToAccount{}
        , _pPresence{ nullptr }
        , _pGroupChannels{ nullptr }
        , _pHost{ nullptr }
        , _nowMs{ 0 }
        , _reconnectGraceMs{ MatchmakingServerInternal::kDefaultReconnectGraceMs }
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
        if ( _pPresence != nullptr )
        {
            for ( const auto& [requestID, accountID] : _mapPresenceRequestToAccount )
            {
                _pPresence->cancel( requestID );
                _mapAccountToGraceEnd[accountID] = _nowMs;
            }
        }
        _mapPresenceRequestToAccount.clear();
        if ( _pPartyLobby != nullptr )
        {
            for ( const auto& [accountID, graceEndMs] : _mapAccountToGraceEnd )
            {
                (void)graceEndMs;
                _pPartyLobby->removeAccount( accountID ); // 종료하는 서버는 유예를 지킬 수 없다. 남기면 파티 기록의 시한(30분)까지 빈 슬롯이 된다
            }
        }
        _mapAccountToGraceEnd.clear();
        if ( _pHost != nullptr )
        {
            for ( const string& topic : _listSubscribedTopic )
            {
                _pHost->unsubscribeServerBus( topic, this );
            }
        }
        _listSubscribedTopic.clear();
        _pendingTable.clear();
        _pHost          = nullptr;
        _pPartyLobby    = nullptr;
        _pQueue         = nullptr;
        _pAgent         = nullptr;
        _pPresence      = nullptr;
        _pGroupChannels = nullptr;
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
        MatchmakingServerInternal::Request request;
        switch ( MatchmakingServerInternal::readRequest( context._method, body, request ) )
        {
            case MatchmakingServerInternal::ReadStatus::UnknownMethod:
            {
                (void)host.respondError( context._token, OnlineError::kNotFound );
                return;
            }
            case MatchmakingServerInternal::ReadStatus::Malformed:
            {
                (void)host.respondError( context._token, OnlineError::kInvalidRequest );
                return;
            }
            case MatchmakingServerInternal::ReadStatus::Ok:
            {
                break;
            }
        }
        MatchmakingServerInternal::dispatch( *_pPartyLobby, *_pQueue, context, request, _pendingTable.add( context._token ) );
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
        expireGrace( nowMs );

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
            {
                push( host, notification._recipientID, notification._pushKind, notification._party );
                syncPartyChannel( notification._recipientID, notification._party );
            }
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
        _mapAccountToGraceEnd[accountID] = _nowMs + _reconnectGraceMs;
    }

    void MatchmakingServer::expireGrace( int64 nowMs )
    {
        for ( auto graceIt = _mapAccountToGraceEnd.begin(); graceIt != _mapAccountToGraceEnd.end(); )
        {
            if ( nowMs < graceIt->second )
            {
                ++graceIt;
                continue;
            }
            const AccountID accountID = graceIt->first;
            graceIt                   = _mapAccountToGraceEnd.erase( graceIt );
            if ( _pPresence == nullptr )
            {
                _pPartyLobby->removeAccount( accountID ); // 서버가 한 대이고 돌아오지 않았다
                continue;
            }
            const uint64 requestID                  = _pPresence->submitFindByAccount( accountID, AccountPresenceDelegate::create<&MatchmakingServer::onGracePresenceFound>( this ) );
            _mapPresenceRequestToAccount[requestID] = accountID;
        }
    }

    void MatchmakingServer::onGracePresenceFound( const AccountPresenceResult& result )
    {
        const auto requestIt = _mapPresenceRequestToAccount.find( result._requestID );
        if ( requestIt == _mapPresenceRequestToAccount.end() )
            return;
        const AccountID accountID = requestIt->second;
        _mapPresenceRequestToAccount.erase( requestIt );
        if ( result.isOnline() || _pPartyLobby == nullptr )
            return; // 다른 서버(또는 이 서버)로 돌아왔다. 파티와 로비에 그대로 둔다
        _pPartyLobby->removeAccount( accountID );
    }

    void MatchmakingServer::onPartyFound( uint64 lookupTag, MatchmakingResult result, const PartySnapshot& party )
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
