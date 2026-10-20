#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Chat/Shared/ChatClient.h"

#include "Core/Network/BitStream.h"

namespace sw
{
    ChatClient::ChatClient()
        : _messageBuffer{}
    {
    }

    uint64 ChatClient::join( string_view channelID, const ReplyDelegate& onReply )
    {
        ChatChannelRequest request;
        request._channelID = string( channelID );
        return sendRequest( ChatMethod::kJoin, request, NetIdempotencyKey{}, onReply );
    }

    uint64 ChatClient::leave( string_view channelID, const ReplyDelegate& onReply )
    {
        ChatChannelRequest request;
        request._channelID = string( channelID );
        return sendRequest( ChatMethod::kLeave, request, NetIdempotencyKey{}, onReply );
    }

    uint64 ChatClient::send( string_view channelID, string_view text, const ReplyDelegate& onReply )
    {
        ChatSendRequest request;
        request._channelID = string( channelID );
        request._text      = string( text );
        return sendRequest( ChatMethod::kSend, request, NetIdempotencyKey{}, onReply );
    }

    uint64 ChatClient::whisper( AccountID recipientID, string_view text, const ReplyDelegate& onReply )
    {
        ChatWhisperRequest request;
        request._recipientID = recipientID;
        request._text        = string( text );
        return sendRequest( ChatMethod::kWhisper, request, NetIdempotencyKey{}, onReply );
    }

    uint64 ChatClient::requestHistory( string_view channelID, string_view cursor, int32 maxCount, const ReplyDelegate& onReply )
    {
        ChatHistoryRequest request;
        request._channelID = string( channelID );
        request._cursor    = string( cursor );
        request._maxCount  = maxCount;
        return sendRequest( ChatMethod::kHistory, request, NetIdempotencyKey{}, onReply );
    }

    uint64 ChatClient::report( const ChatMessage& message, string_view reason, const ReplyDelegate& onReply )
    {
        ChatReportRequest request;
        request._channelID = message._channelID;
        request._reason    = string( reason );
        request._sentMs    = message._sentMs;
        request._serverID  = message._serverID;
        request._sequence  = message._sequence;
        return sendRequest( ChatMethod::kReport, request, NetIdempotencyKey{}, onReply );
    }

    void ChatClient::onServicePush( uint16 kind, BitReader& body )
    {
        if ( kind != ChatMethod::kPushMessage )
            return;
        ChatMessage message;
        if ( ChatProtocol::MessageTypes::read( body, message ) == FixedReadResult::Ok )
            _messageBuffer.push( std::move( message ) );
    }
} // namespace sw
