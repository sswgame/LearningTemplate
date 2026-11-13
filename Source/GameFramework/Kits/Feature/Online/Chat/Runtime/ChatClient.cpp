#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Chat/Runtime/ChatClient.h"

#include "GameFramework/Runtime/Online/Service/OnlineClientRegistry.h"

namespace sw
{
    ChatClient::ChatClient()
        : _messageConsumerLock{}
        , _messageBuffer{}
    {
    }

    void ChatClient::onPush( ChatMethod::Message, const ChatMessage& body )
    {
        ChatMessage message = body;
        _messageBuffer.push( std::move( message ) );
    }
} // namespace sw

namespace sw
{
    SW_REGISTER_ONLINE_CLIENT( ChatClient );
} // namespace sw
