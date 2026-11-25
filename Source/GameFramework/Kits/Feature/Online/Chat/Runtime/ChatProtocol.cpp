#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Chat/Runtime/ChatProtocol.h"

#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

namespace sw
{
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
