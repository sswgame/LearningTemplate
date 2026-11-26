#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Social/Runtime/SocialClient.h"

#include "Engine/Localization/LocalizationManager.h"

#include "GameFramework/Runtime/Online/Service/OnlineClientRegistry.h"

namespace sw
{
    // Social 키트의 글(엔진 현지화 폴더의 `kits/social/`) — 이 모듈이 올라온 동안 조회 사슬에 든다(언리얼 플러그인 현지화 타깃)
    SW_LOCALIZATION_PROJECT( SocialKit, "engine/localization/kits/social/social.locproject.xml" );
} // namespace sw

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
