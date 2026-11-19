#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Chat/Runtime/ChatProtocol.h"

#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Runtime/Online/Store/ServiceRecordUtil.h"

namespace sw
{
    bool ChatProtocol::encodeRecord( const ChatMessage& message, vector<uint8>& outBytes )
    {
        return ServiceRecordUtil::encode( message, outBytes );
    }

    bool ChatProtocol::decodeRecord( const string_view key, const vector<uint8>& bytes, ChatMessage& outMessage )
    {
        return ServiceRecordUtil::readDurable( key, bytes, outMessage );
    }

    string ChatProtocol::makeChannelTopic( string_view channelID )
    {
        return ChatBus::kChannelTopic.makeTopic( channelID );
    }

    string ChatProtocol::makeServerTopic( uint64 serverID )
    {
        return ChatBus::kServerTopic.makeTopic( ServiceKeyUtil::makeHex64( serverID ) );
    }
} // namespace sw

namespace sw
{
    const OnlineProtocolEntry ChatProtocol::kOnlineEntry{ "Chat", ChatProtocol::Table{} };
} // namespace sw
