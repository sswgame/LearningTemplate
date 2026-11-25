/**
 * @file SocialTestPolicy.h
 * @brief 친구 키트 테스트가 쓰는 운영 값입니다(Tests 폴더 — 배포에 없다). 서버 설정 `Config/Server/Online/Playground.online.json` 의 값과 같게 둔다.
 */
#pragma once
#include "GameFramework/Kits/Feature/Online/Social/Server/GuildService.h"
#include "GameFramework/Kits/Feature/Online/Social/Server/SocialCounts.h"

namespace sw::test
{
    struct SocialTestPolicy
    {
        static constexpr int64 kWeekMs = 604800000; ///< 7 일

        static SocialLinkPolicy makeLinks()
        {
            SocialLinkPolicy policy;
            policy._maxFriend   = 200;
            policy._maxIncoming = 100;
            policy._maxOutgoing = 100;
            policy._maxBlocked  = 500;
            return policy;
        }

        static GuildPolicy makeGuild()
        {
            GuildPolicy policy;
            policy._maxMember             = 100;
            policy._maxApplication        = 50;
            policy._maxStorageGrant       = 32;
            policy._inviteLifetimeMs      = kWeekMs;
            policy._applicationLifetimeMs = kWeekMs;
            return policy;
        }
    };
} // namespace sw::test
