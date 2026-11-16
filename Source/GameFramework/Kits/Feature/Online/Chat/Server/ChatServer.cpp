#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Chat/Server/ChatServer.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Serialization/BitStream.h"

#include "GameFramework/Kits/Feature/Online/Chat/Runtime/ChatProtocol.h"
#include "GameFramework/Runtime/Online/Bus/ServerBus.h"

namespace sw
{
    ChatServer::ChatServer()
        : _listCompletionScratch{}
        , _listDeliveryScratch{}
        , _listTopicScratch{}
        , _mapTopicSubscription{}
        , _pService{ nullptr }
    {
        setReady( false );
    }

    void ChatServer::initialize( ChatService* pService, MetricRegistry* pMetricRegistry )
    {
        _pService = pService;
        initializeCalls( pMetricRegistry, "chat" );
        setReady( pService != nullptr );
    }

    void ChatServer::shutdown()
    {
        _mapTopicSubscription.clear(); // 구독이 사라지며 버스 구독을 끊는다
        _calls.clear();
        _pService = nullptr;
        setReady( false );
    }

    void ChatServer::onHostShutdown( OnlineServiceHost& host )
    {
        (void)host;
        _mapTopicSubscription.clear();
    }

    void ChatServer::handleCall( OnlineServerCall<ChatMethod::Join> call, const OnlineCallContext& context, const ChatChannelRequest& request )
    {
        _pService->joinChannel( context._accountID, request._channelID, context._nowMs, call._callID );
    }

    void ChatServer::handleCall( OnlineServerCall<ChatMethod::Leave> call, const OnlineCallContext& context, const ChatChannelRequest& request )
    {
        _pService->leaveChannel( context._accountID, request._channelID, call._callID );
    }

    void ChatServer::handleCall( OnlineServerCall<ChatMethod::Send> call, const OnlineCallContext& context, const ChatSendRequest& request )
    {
        _pService->sendMessage( context._accountID, request._channelID, request._text, context._nowMs, call._callID );
    }

    void ChatServer::handleCall( OnlineServerCall<ChatMethod::Whisper> call, const OnlineCallContext& context, const ChatWhisperRequest& request )
    {
        _pService->sendWhisper( context._accountID, request._recipientID, request._text, context._nowMs, call._callID );
    }

    void ChatServer::handleCall( OnlineServerCall<ChatMethod::History> call, const OnlineCallContext& context, const ChatHistoryRequest& request )
    {
        _pService->readHistory( context._accountID, request._channelID, request._cursor, request._maxCount, call._callID );
    }

    void ChatServer::handleCall( OnlineServerCall<ChatMethod::Report> call, const OnlineCallContext& context, const ChatReportRequest& request )
    {
        _pService->reportMessage( context._accountID, request, context._nowMs, call._callID );
    }

    void ChatServer::onServiceTick( OnlineServiceHost& host, UnixMilliseconds nowMs )
    {
        if ( _pService == nullptr )
            return;
        _pService->update( nowMs );
        _calls.publishPendingCount();
        flushServiceOutput( host );
    }

    void ChatServer::flushServiceOutput( OnlineServiceHost& host )
    {
        _listTopicScratch.clear();
        _pService->pollBusTopicChanges( _listTopicScratch );
        for ( const ChatBusTopicChange& change : _listTopicScratch )
        {
            const bool bShouldListen = change._bSubscribe == SW_TRUE && host.getServerBus() != nullptr; // 서버가 한 대면 들을 버스가 없다
            if ( bShouldListen && _mapTopicSubscription.contains( change._topic ) == false )
                _mapTopicSubscription.emplace( change._topic, host.subscribeServerBus( change._topic, this ) );
            else if ( change._bSubscribe == SW_FALSE )
                _mapTopicSubscription.erase( change._topic ); // 구독이 사라지며 끊는다
        }

        _listCompletionScratch.clear();
        _pService->takeCompletions( _listCompletionScratch );
        for ( ChatCompletion& completion : _listCompletionScratch )
        {
            respondCompletion( completion ); // 연결이 그사이 닫혔으면 호스트가 버린다
        }

        _listDeliveryScratch.clear();
        _pService->pollDeliveries( _listDeliveryScratch );
        for ( const ChatDelivery& delivery : _listDeliveryScratch )
        {
            BitWriter body;
            if ( ChatProtocol::MessageTypes::write( body, delivery._message ) == false )
                continue;                                                                   // write 가 범위 밖 값을 로그에 남겼다
            (void)host.sendPush( delivery._recipientID, ChatMethod::Message::kKind, body ); // 그사이 떠난 계정이면 버린다
        }
    }

    void ChatServer::respondCompletion( ChatCompletion& completion )
    {
        const uint64             callID = completion._requestTag;
        const ChatServiceResult& result = completion._result;
        if ( completion._method == ChatMethod::Send::kID || completion._method == ChatMethod::Whisper::kID )
        {
            ChatSendReply reply;
            reply._message      = std::move( completion._message );
            reply._retryAfterMs = completion._retryAfterMs;
            reply._result       = result._result;
            (void)finishCall( callID, result, reply );
            return;
        }
        if ( completion._method == ChatMethod::History::kID )
        {
            ChatHistoryReply reply;
            reply._listHistory = std::move( completion._listHistory );
            reply._nextCursor  = std::move( completion._nextCursor );
            reply._result      = result._result;
            (void)finishCall( callID, result, reply );
            return;
        }
        ChatResultReply reply; // Join · Leave · Report
        reply._retryAfterMs = completion._retryAfterMs;
        reply._result       = result._result;
        (void)finishCall( callID, result, reply );
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
