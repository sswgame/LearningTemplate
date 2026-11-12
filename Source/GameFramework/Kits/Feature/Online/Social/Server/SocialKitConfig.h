/**
 * @file SocialKitConfig.h
 * @brief 친구 키트의 서버 설정입니다(`_listKit[]._settings`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Runtime/Online/Server/OnlineServerConfig.h"

namespace sw
{
    /** @brief 친구 키트의 서버 설정입니다. */
    REFLECT()
    struct SocialKitConfig
    {
        REFLECT_BODY();
        /** @brief 레벨 2 부터의 누적 경험치입니다(레벨 순서). 늘어나야 하며 없으면 모든 길드가 레벨 1 입니다. */
        PROPERTY()
        vector<int64> _listGuildLevelExperience{};
        /** @brief 만료된 길드 초대의 TTL 입니다. 만료 뒤 이 기간이 지나면 TTL 쓸기가 지웁니다. */
        PROPERTY( Required )
        ServiceTTLConfig _expiredInviteTTL{};
    };
} // namespace sw
