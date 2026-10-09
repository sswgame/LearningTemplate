#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Chat/ChatProtocol.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Guard/RequestLimits.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    namespace
    {
        struct ChatProtocolInternal
        {
            static constexpr uint8 kRecordVersion = 1;
            static constexpr int32 kResultBits    = 8;
        };
    } // namespace
} // namespace sw

namespace sw
{
    void ChatProtocol::writeMessage( BitWriter& outWriter, const ChatMessage& message )
    {
        ServiceKeyUtil::writeString( outWriter, message._channelId );
        outWriter.writeBits( static_cast<uint32>( message._kind ), 8 );
        outWriter.writeVarUint( message._senderId );
        ServiceKeyUtil::writeString( outWriter, message._senderName );
        outWriter.writeVarUint( message._recipientId );
        ServiceKeyUtil::writeString( outWriter, message._text );
        outWriter.writeVarInt( message._sentMs );
        outWriter.writeVarUint( message._serverId );
        outWriter.writeVarUint( message._sequence );
    }

    bool ChatProtocol::readMessage( BitReader& reader, ChatMessage& outMessage )
    {
        if ( ServiceKeyUtil::readString( reader, ChatLimit::kMaxChannelIdSize, outMessage._channelId ) == false )
            return false;
        const uint32 kind    = reader.readBits( 8 );
        outMessage._senderId = reader.readVarUint();
        if ( kind >= static_cast<uint32>( ChatChannelKind::Count ) ||
             ServiceKeyUtil::readString( reader, RequestLimits::kMaxDisplayNameSize, outMessage._senderName ) == false )
            return false;
        outMessage._kind        = static_cast<ChatChannelKind>( kind );
        outMessage._recipientId = reader.readVarUint();
        if ( ServiceKeyUtil::readString( reader, ChatLimit::kMaxTextSize, outMessage._text ) == false ) // 가린 글은 코드 포인트마다 '*' 하나라 원문보다 길지 않다
            return false;
        outMessage._sentMs    = reader.readVarInt();
        outMessage._serverId  = reader.readVarUint();
        const uint64 sequence = reader.readVarUint();
        outMessage._sequence  = static_cast<uint32>( sequence );
        return reader.hasOverflowed() == false && static_cast<uint64>( outMessage._sequence ) == sequence;
    }

    vector<uint8> ChatProtocol::encodeRecord( const ChatMessage& message )
    {
        BitWriter writer;
        writer.writeBits( ChatProtocolInternal::kRecordVersion, 8 );
        writeMessage( writer, message );
        vector<uint8> bytes = writer.getBytes();
        bytes.resize( static_cast<size_t>( writer.getByteCount() ) );
        return bytes;
    }

    bool ChatProtocol::decodeRecord( const vector<uint8>& bytes, ChatMessage& outMessage )
    {
        BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
        if ( reader.readBits( 8 ) != ChatProtocolInternal::kRecordVersion )
            return false;
        return readMessage( reader, outMessage );
    }

    void ChatProtocol::writeReply( BitWriter& outWriter, uint16 method, const ChatReply& reply )
    {
        outWriter.writeBits( static_cast<uint32>( reply._result ), ChatProtocolInternal::kResultBits );
        outWriter.writeVarUint( static_cast<uint64>( reply._retryAfterMs > 0 ? reply._retryAfterMs : 0 ) );
        if ( reply._result != ChatResult::Ok )
            return;
        if ( method == ChatMethod::kSend || method == ChatMethod::kWhisper )
        {
            writeMessage( outWriter, reply._message );
        }
        else if ( method == ChatMethod::kHistory )
        {
            outWriter.writeVarUint( reply._listHistory.size() );
            for ( const ChatMessage& message : reply._listHistory )
            {
                writeMessage( outWriter, message );
            }
            ServiceKeyUtil::writeString( outWriter, reply._nextCursor );
        }
    }

    bool ChatProtocol::readReply( BitReader& reader, uint16 method, ChatReply& outReply )
    {
        const uint32 result     = reader.readBits( ChatProtocolInternal::kResultBits );
        const uint64 retryAfter = reader.readVarUint();
        if ( reader.hasOverflowed() || result > static_cast<uint32>( ChatResult::NotSignedIn ) )
            return false;
        outReply._result       = static_cast<ChatResult>( result );
        outReply._retryAfterMs = static_cast<int64>( retryAfter );
        if ( outReply._result != ChatResult::Ok )
            return true;
        if ( method == ChatMethod::kSend || method == ChatMethod::kWhisper )
            return readMessage( reader, outReply._message );
        if ( method == ChatMethod::kHistory )
        {
            const uint64 count = reader.readVarUint();
            if ( reader.hasOverflowed() || count > static_cast<uint64>( ChatLimit::kMaxHistoryPage ) )
                return false;
            outReply._listHistory.resize( static_cast<size_t>( count ) );
            for ( ChatMessage& message : outReply._listHistory )
            {
                if ( readMessage( reader, message ) == false )
                    return false;
            }
            return ServiceKeyUtil::readString( reader, kMaxCursorSize, outReply._nextCursor );
        }
        return reader.hasOverflowed() == false;
    }

    ChatResult ChatProtocol::fromErrorCode( uint16 errorCode )
    {
        switch ( errorCode )
        {
            case OnlineError::kOk:
                return ChatResult::Ok;
            case OnlineError::kUnauthenticated:
                return ChatResult::NotSignedIn;
            case OnlineError::kInvalidRequest:
                return ChatResult::Invalid;
            case OnlineError::kRateLimited:
                return ChatResult::RateLimited;
            default:
                return ChatResult::Unavailable;
        }
    }

    string ChatProtocol::makeChannelTopic( string_view channelId )
    {
        string topic( ChatBus::kChannelTopicPrefix );
        topic += channelId;
        return topic;
    }

    string ChatProtocol::makeServerTopic( uint64 serverId )
    {
        string topic( ChatBus::kServerTopicPrefix );
        ServiceKeyUtil::appendHex64( topic, serverId );
        return topic;
    }
} // namespace sw
