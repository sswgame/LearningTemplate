#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Chat/Shared/ChatClient.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    ChatClient::ChatClient()
        : _messageBuffer{}
    {
    }

    uint64 ChatClient::join( string_view channelID, const ReplyDelegate& onReply )
    {
        BitWriter body;
        ServiceKeyUtil::writeString( body, channelID );
        return sendCall( ChatMethod::kJoin, body, NetIdempotencyKey{}, onReply );
    }

    uint64 ChatClient::leave( string_view channelID, const ReplyDelegate& onReply )
    {
        BitWriter body;
        ServiceKeyUtil::writeString( body, channelID );
        return sendCall( ChatMethod::kLeave, body, NetIdempotencyKey{}, onReply );
    }

    uint64 ChatClient::send( string_view channelID, string_view text, const ReplyDelegate& onReply )
    {
        BitWriter body;
        ServiceKeyUtil::writeString( body, channelID );
        ServiceKeyUtil::writeString( body, text );
        return sendCall( ChatMethod::kSend, body, NetIdempotencyKey{}, onReply );
    }

    uint64 ChatClient::whisper( AccountID recipientID, string_view text, const ReplyDelegate& onReply )
    {
        BitWriter body;
        body.writeVarUint( recipientID );
        ServiceKeyUtil::writeString( body, text );
        return sendCall( ChatMethod::kWhisper, body, NetIdempotencyKey{}, onReply );
    }

    uint64 ChatClient::requestHistory( string_view channelID, string_view cursor, int32 maxCount, const ReplyDelegate& onReply )
    {
        BitWriter body;
        ServiceKeyUtil::writeString( body, channelID );
        ServiceKeyUtil::writeString( body, cursor );
        body.writeVarUint( static_cast<uint64>( maxCount > 0 ? maxCount : 0 ) );
        return sendCall( ChatMethod::kHistory, body, NetIdempotencyKey{}, onReply );
    }

    void ChatClient::onServicePush( uint16 kind, BitReader& body )
    {
        if ( kind != ChatMethod::kPushMessage )
            return;
        ChatMessage message;
        if ( ChatProtocol::readMessage( body, message ) )
            _messageBuffer.push( std::move( message ) );
    }
} // namespace sw
