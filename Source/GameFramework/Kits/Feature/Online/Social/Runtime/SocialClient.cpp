#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Social/Runtime/SocialClient.h"

#include "GameFramework/Runtime/Online/Service/OnlineClientRegistry.h"

namespace sw
{
    SocialClient::SocialClient()
        : _notificationConsumerLock{}
        , _notificationBuffer{}
    {
    }

    void SocialClient::onPush( SocialMethod::Notification, const SocialNotification& body )
    {
        SocialNotification notification = body;
        _notificationBuffer.push( std::move( notification ) );
    }
} // namespace sw

namespace sw
{
    SW_REGISTER_ONLINE_CLIENT( SocialClient );
} // namespace sw
