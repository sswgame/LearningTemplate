#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Chat/Shared/ChatProtocol.h"

#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Base/Online/Store/ServiceRecordUtil.h"

namespace sw
{
    namespace
    {
        constexpr uint32 kChatRecordVersion = 1; ///< 기록 레코드(A)의 판이다. 필드를 빼거나 뜻을 바꿀 때 올리고 데이터를 다시 쓴다
    } // namespace
} // namespace sw

namespace sw
{
    bool ChatProtocol::encodeRecord( const ChatMessage& message, vector<uint8>& outBytes )
    {
        return ServiceRecordUtil::encode( kChatRecordVersion, message, outBytes );
    }

    bool ChatProtocol::decodeRecord( const string_view key, const vector<uint8>& bytes, ChatMessage& outMessage )
    {
        return ServiceRecordUtil::readDurable( key, bytes, kChatRecordVersion, outMessage );
    }

    ChatResult ChatProtocol::fromErrorCode( uint16 errorCode )
    {
        static constexpr OnlineErrorResult<ChatResult> kArrErrorResult[] = {
            {             OnlineError::kOk,          ChatResult::Ok},
            {OnlineError::kUnauthenticated, ChatResult::NotSignedIn},
            { OnlineError::kInvalidRequest,     ChatResult::Invalid},
            {    OnlineError::kRateLimited, ChatResult::RateLimited},
        };
        return OnlineError::toResult( errorCode, kArrErrorResult, ChatResult::Unavailable );
    }

    string ChatProtocol::makeChannelTopic( string_view channelID )
    {
        string topic( ChatBus::kChannelTopicPrefix );
        topic += channelID;
        return topic;
    }

    string ChatProtocol::makeServerTopic( uint64 serverID )
    {
        string topic( ChatBus::kServerTopicPrefix );
        ServiceKeyUtil::appendHex64( topic, serverID );
        return topic;
    }
} // namespace sw

namespace sw
{
    const OnlineProtocolEntry ChatProtocol::kOnlineEntry{ "Chat", ChatMethod{}, ChatProtocol::MessageTypes{} };
} // namespace sw
