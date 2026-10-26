#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Chat/Server/ChatServer.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Serialization/BitStream.h"

#include "GameFramework/Base/Online/Bus/ServerBus.h"
#include "GameFramework/Kits/Feature/Online/Chat/Shared/ChatProtocol.h"

namespace sw
{
    ChatServer::ChatServer()
        : _pendingTable{}
        , _listCompletionScratch{}
        , _listDeliveryScratch{}
        , _listTopicScratch{}
        , _mapTopicSubscription{}
        , _pService{ nullptr }
    {
    }

    void ChatServer::initialize( ChatService* pService )
    {
        _pService = pService;
    }

    void ChatServer::shutdown()
    {
        _mapTopicSubscription.clear(); // 구독이 사라지며 버스 구독을 끊는다
        _pendingTable.clear();
        _pService = nullptr;
    }

    void ChatServer::onHostShutdown( OnlineServiceHost& host )
    {
        (void)host;
        _mapTopicSubscription.clear();
    }

    void ChatServer::onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body )
    {
        if ( _pService == nullptr )
        {
            (void)host.respondError( context._token, OnlineError::kUnavailable );
            return;
        }
        switch ( context._method )
        {
            case ChatMethod::kJoin:
            case ChatMethod::kLeave:
            {
                ChatChannelRequest request;
                if ( ChatProtocol::MessageTypes::read( body, request ) != FixedReadResult::Ok )
                    break;
                const uint64 requestTag = _pendingTable.add( context._token );
                if ( context._method == ChatMethod::kJoin )
                    _pService->joinChannel( context._accountID, request._channelID, context._nowMs, requestTag );
                else
                    _pService->leaveChannel( context._accountID, request._channelID, requestTag );
                return;
            }
            case ChatMethod::kSend:
            {
                ChatSendRequest request;
                if ( ChatProtocol::MessageTypes::read( body, request ) != FixedReadResult::Ok )
                    break;
                _pService->sendMessage( context._accountID, request._channelID, request._text, context._nowMs, _pendingTable.add( context._token ) );
                return;
            }
            case ChatMethod::kWhisper:
            {
                ChatWhisperRequest request;
                if ( ChatProtocol::MessageTypes::read( body, request ) != FixedReadResult::Ok )
                    break;
                _pService->sendWhisper( context._accountID, request._recipientID, request._text, context._nowMs, _pendingTable.add( context._token ) );
                return;
            }
            case ChatMethod::kHistory:
            {
                ChatHistoryRequest request;
                if ( ChatProtocol::MessageTypes::read( body, request ) != FixedReadResult::Ok )
                    break;
                _pService->readHistory( context._accountID, request._channelID, request._cursor, request._maxCount, _pendingTable.add( context._token ) );
                return;
            }
            case ChatMethod::kReport:
            {
                ChatReportRequest request;
                if ( ChatProtocol::MessageTypes::read( body, request ) != FixedReadResult::Ok )
                    break;
                _pService->reportMessage( context._accountID, request, context._nowMs, _pendingTable.add( context._token ) );
                return;
            }
            default:
            {
                (void)host.respondError( context._token, OnlineError::kNotFound );
                return;
            }
        }
        (void)host.respondError( context._token, OnlineError::kInvalidRequest );
    }

    void ChatServer::onServiceTick( OnlineServiceHost& host, int64 nowMs )
    {
        if ( _pService == nullptr )
            return;
        _pService->update( nowMs );
        flushServiceOutput( host );
    }

    void ChatServer::flushServiceOutput( OnlineServiceHost& host )
    {
        _listTopicScratch.clear();
        _pService->drainBusTopicChanges( _listTopicScratch );
        for ( const ChatBusTopicChange& change : _listTopicScratch )
        {
            const bool bShouldListen = change._bSubscribe == SW_TRUE && host.getServerBus() != nullptr; // 서버가 한 대면 들을 버스가 없다
            if ( bShouldListen && _mapTopicSubscription.contains( change._topic ) == false )
                _mapTopicSubscription.emplace( change._topic, host.subscribeServerBus( change._topic, this ) );
            else if ( change._bSubscribe == SW_FALSE )
                _mapTopicSubscription.erase( change._topic ); // 구독이 사라지며 끊는다
        }

        _listCompletionScratch.clear();
        _pService->drainCompletions( _listCompletionScratch );
        for ( const ChatCompletion& completion : _listCompletionScratch )
        {
            NetRequestToken token;
            if ( _pendingTable.take( completion._requestTag, token ) == false )
                continue;
            (void)host.respondMessage<ChatProtocol::MessageTypes>( token, completion._reply ); // 연결이 그사이 닫혔으면 호스트가 버린다
        }

        _listDeliveryScratch.clear();
        _pService->drainDeliveries( _listDeliveryScratch );
        for ( const ChatDelivery& delivery : _listDeliveryScratch )
        {
            BitWriter body;
            if ( ChatProtocol::MessageTypes::write( body, delivery._message ) == false )
                continue;                                                                 // write 가 범위 밖 값을 로그에 남겼다
            (void)host.sendPush( delivery._recipientID, ChatMethod::kPushMessage, body ); // 그사이 떠난 계정이면 버린다
        }
    }

    void ChatServer::onAccountLeft( OnlineServiceHost& host, AccountID accountID )
    {
        if ( _pService == nullptr )
            return;
        _pService->removeAccount( accountID );
        flushServiceOutput( host );
    }

    void ChatServer::onServerBusMessage( OnlineServiceHost& host, const ServerBusMessage& message )
    {
        (void)host;
        if ( _pService != nullptr )
            _pService->handleBusMessage( message );
    }
} // namespace sw
