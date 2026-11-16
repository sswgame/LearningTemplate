#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Leaderboard/Runtime/LeaderboardClient.h"

#include "GameFramework/Runtime/Online/Service/OnlineClientRegistry.h"

namespace sw
{
    LeaderboardClient::LeaderboardClient()
        : _unlockConsumerLock{}
        , _unlockBuffer{}
    {
    }

    void LeaderboardClient::onPush( LeaderboardMethod::Achievement, const AchievementState& body )
    {
        AchievementState achievement = body;
        _unlockBuffer.push( std::move( achievement ) );
    }
} // namespace sw

namespace sw
{
    SW_REGISTER_ONLINE_CLIENT( LeaderboardClient );
} // namespace sw
