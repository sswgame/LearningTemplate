#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Chat/Shared/ChatClient.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    ChatClient::ChatClient()
        : _callTable{}
        , _messageBuffer{}
        , _pClient{ nullptr }
    {
    }

    void ChatClient::initialize( OnlineServiceClient* pClient ) { _pClient = pClient; }

    uint64 ChatClient::join( string_view channelId, const ReplyDelegate& onReply )
    {
        BitWriter body;
        ServiceKeyUtil::writeString( body, channelId );
        return sendCall( ChatMethod::kJoin, body, onReply );
    }

    uint64 ChatClient::leave( string_view channelId, const ReplyDelegate& onReply )
    {
        BitWriter body;
        ServiceKeyUtil::writeString( body, channelId );
        return sendCall( ChatMethod::kLeave, body, onReply );
    }

    uint64 ChatClient::send( string_view channelId, string_view text, const ReplyDelegate& onReply )
    {
        BitWriter body;
        ServiceKeyUtil::writeString( body, channelId );
        ServiceKeyUtil::writeString( body, text );
        return sendCall( ChatMethod::kSend, body, onReply );
    }

    uint64 ChatClient::whisper( AccountId recipientId, string_view text, const ReplyDelegate& onReply )
    {
        BitWriter body;
        body.writeVarUint( recipientId );
        ServiceKeyUtil::writeString( body, text );
        return sendCall( ChatMethod::kWhisper, body, onReply );
    }

    uint64 ChatClient::requestHistory( string_view channelId, string_view cursor, int32 maxCount, const ReplyDelegate& onReply )
    {
        BitWriter body;
        ServiceKeyUtil::writeString( body, channelId );
        ServiceKeyUtil::writeString( body, cursor );
        body.writeVarUint( static_cast<uint64>( maxCount > 0 ? maxCount : 0 ) );
        return sendCall( ChatMethod::kHistory, body, onReply );
    }

    void ChatClient::onServicePush( uint16 kind, BitReader& body )
    {
        if ( kind != ChatMethod::kPushMessage )
            return;
        ChatMessage message;
        if ( ChatProtocol::readMessage( body, message ) )
            _messageBuffer.push( std::move( message ) );
    }

    uint64 ChatClient::sendCall( uint16 method, const BitWriter& body, const ReplyDelegate& onReply )
    {
        if ( _pClient == nullptr )
        {
            ChatClientReply reply;
            reply._method        = method;
            reply._errorCode     = OnlineError::kUnavailable;
            reply._reply._result = ChatResult::Unavailable;
            if ( onReply.isBound() )
                onReply( reply );
            return 0;
        }
        return _callTable.send( *_pClient, method, body, NetRequestOptions{}, OnlineResponseDelegate::create<&ChatClient::onResponse>( this ), PendingCall{ onReply, method } );
    }

    void ChatClient::onResponse( const OnlineResponse& response )
    {
        PendingCall call;
        if ( _callTable.take( response._requestId, call ) == false )
            return;
        ChatClientReply reply;
        reply._requestId = response._requestId;
        reply._method    = call._method;
        reply._errorCode = response._errorCode;
        if ( response.isOk() == false )
        {
            reply._reply._result = ChatProtocol::fromErrorCode( response._errorCode );
        }
        else
        {
            BitReader body( response._pBody, response._bodySize );
            if ( ChatProtocol::readReply( body, call._method, reply._reply ) == false )
            {
                reply._reply         = ChatReply{};
                reply._reply._result = ChatResult::Unavailable;
            }
        }
        if ( call._onReply.isBound() )
            call._onReply( reply );
    }
} // namespace sw
