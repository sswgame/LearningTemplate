#include "pch.h"

#include "GameFramework/Kits/Online/Server/Chat/ChatServer.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Bus/ServerBus.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Kits/Online/Chat/ChatProtocol.h"

#include <algorithm>

namespace sw
{
    ChatServer::ChatServer()
        : _pendingTable{}
        , _listCompletionScratch{}
        , _listDeliveryScratch{}
        , _listTopicScratch{}
        , _listSubscribedTopic{}
        , _pService{ nullptr }
        , _pHost{ nullptr }
    {
    }

    void ChatServer::initialize( ChatService* pService ) { _pService = pService; }

    void ChatServer::shutdown()
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
        _pHost    = nullptr;
        _pService = nullptr;
    }

    void ChatServer::onHostShutdown( OnlineServiceHost& host )
    {
        (void)host;
        _listSubscribedTopic.clear(); // 구독은 호스트가 이어서 모두 푼다
        _pHost = nullptr;
    }

    void ChatServer::onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body )
    {
        if ( _pService == nullptr )
        {
            (void)host.respondError( context._token, OnlineError::kUnavailable );
            return;
        }
        string channelId;
        string text;
        switch ( context._method )
        {
            case ChatMethod::kJoin:
            case ChatMethod::kLeave:
            {
                if ( ServiceKeyUtil::readString( body, ChatLimit::kMaxChannelIdSize, channelId ) == false || body.hasOverflowed() )
                    break;
                const uint64 requestTag = _pendingTable.add( context._token );
                if ( context._method == ChatMethod::kJoin )
                    _pService->joinChannel( context._accountId, channelId, context._nowMs, requestTag );
                else
                    _pService->leaveChannel( context._accountId, channelId, requestTag );
                return;
            }
            case ChatMethod::kSend:
            {
                if ( ServiceKeyUtil::readString( body, ChatLimit::kMaxChannelIdSize, channelId ) == false ||
                     ServiceKeyUtil::readString( body, ChatLimit::kMaxTextSize, text ) == false || body.hasOverflowed() )
                    break;
                _pService->sendMessage( context._accountId, channelId, text, context._nowMs, _pendingTable.add( context._token ) );
                return;
            }
            case ChatMethod::kWhisper:
            {
                const AccountId recipientId = body.readVarUint();
                if ( ServiceKeyUtil::readString( body, ChatLimit::kMaxTextSize, text ) == false || body.hasOverflowed() )
                    break;
                _pService->sendWhisper( context._accountId, recipientId, text, context._nowMs, _pendingTable.add( context._token ) );
                return;
            }
            case ChatMethod::kHistory:
            {
                string cursor;
                if ( ServiceKeyUtil::readString( body, ChatLimit::kMaxChannelIdSize, channelId ) == false ||
                     ServiceKeyUtil::readString( body, ChatProtocol::kMaxCursorSize, cursor ) == false )
                    break;
                const uint64 maxCount = body.readVarUint();
                if ( body.hasOverflowed() || maxCount > static_cast<uint64>( ChatLimit::kMaxHistoryPage ) )
                    break;
                _pService->readHistory( context._accountId, channelId, cursor, static_cast<int32>( maxCount ), _pendingTable.add( context._token ) );
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
        _pHost = &host;
        if ( _pService == nullptr )
            return;
        _pService->tick( nowMs );
        flushServiceOutput( host );
    }

    void ChatServer::flushServiceOutput( OnlineServiceHost& host )
    {
        _listTopicScratch.clear();
        _pService->drainBusTopicChanges( _listTopicScratch );
        for ( const ChatBusTopicChange& change : _listTopicScratch )
        {
            if ( change._bSubscribe == SW_TRUE )
            {
                host.subscribeServerBus( change._topic, this );
                _listSubscribedTopic.push_back( change._topic );
            }
            else
            {
                host.unsubscribeServerBus( change._topic, this );
                _listSubscribedTopic.erase( std::remove( _listSubscribedTopic.begin(), _listSubscribedTopic.end(), change._topic ), _listSubscribedTopic.end() );
            }
        }

        _listCompletionScratch.clear();
        _pService->drainCompletions( _listCompletionScratch );
        for ( const ChatCompletion& completion : _listCompletionScratch )
        {
            NetRequestToken token;
            if ( _pendingTable.take( completion._requestTag, token ) == false )
                continue;
            BitWriter reply;
            ChatProtocol::writeReply( reply, completion._method, completion._reply );
            (void)host.respondOk( token, reply ); // 연결이 그새 닫혔으면 호스트가 버린다
        }

        _listDeliveryScratch.clear();
        _pService->drainDeliveries( _listDeliveryScratch );
        for ( const ChatDelivery& delivery : _listDeliveryScratch )
        {
            BitWriter body;
            ChatProtocol::writeMessage( body, delivery._message );
            (void)host.sendPush( delivery._recipientId, ChatMethod::kPushMessage, body ); // 그새 떠난 계정이면 버린다
        }
    }

    void ChatServer::onAccountLeft( OnlineServiceHost& host, AccountId accountId )
    {
        if ( _pService == nullptr )
            return;
        _pService->removeAccount( accountId );
        flushServiceOutput( host );
    }

    void ChatServer::onServerBusMessage( OnlineServiceHost& host, const ServerBusMessage& message )
    {
        (void)host;
        if ( _pService != nullptr )
            _pService->handleBusMessage( message._topic, message._bytes );
    }
} // namespace sw
