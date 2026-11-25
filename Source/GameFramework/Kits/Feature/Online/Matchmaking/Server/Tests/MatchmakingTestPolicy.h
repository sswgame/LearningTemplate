/**
 * @file MatchmakingTestPolicy.h
 * @brief 매칭 키트 테스트가 쓰는 운영 값입니다(Tests 폴더 — 배포에 없다). 서버 설정 `Config/Server/Online/Playground.online.json` 의 값과 같게 둔다.
 */
#pragma once
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchQueueService.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchServerAgent.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/PartyLobbyService.h"

namespace sw::test
{
    struct MatchmakingTestPolicy
    {
        static constexpr int64 kPartyTTLMs       = 1800000;
        static constexpr int64 kLobbyTTLMs       = 7200000;
        static constexpr int64 kInviteTTLMs      = 300000;
        static constexpr int64 kLeaseTTLMs       = 10000;
        static constexpr int64 kLeaseRenewMs     = 3000;
        static constexpr int64 kNoServerGiveUpMs = 30000;
        static constexpr int64 kResultGraceMs    = 10000;
        static constexpr int64 kRatingWaitMs     = 3000;
        static constexpr int64 kExpectTTLMs      = 120000;
        static constexpr int64 kRecordRefreshMs  = 1000;
        static constexpr int32 kMaxRecordCount   = 256;

        static PartyLobbyPolicy makePartyLobby()
        {
            PartyLobbyPolicy policy;
            policy._partyTTLMs  = kPartyTTLMs;
            policy._lobbyTTLMs  = kLobbyTTLMs;
            policy._inviteTTLMs = kInviteTTLMs;
            return policy;
        }

        static MatchQueuePolicy makeQueue()
        {
            MatchQueuePolicy policy;
            policy._leaseTTLMs       = kLeaseTTLMs;
            policy._leaseRenewMs     = kLeaseRenewMs;
            policy._noServerGiveUpMs = kNoServerGiveUpMs;
            policy._resultGraceMs    = kResultGraceMs;
            policy._ratingWaitMs     = kRatingWaitMs;
            return policy;
        }

        static MatchServerAgentPolicy makeAgent()
        {
            MatchServerAgentPolicy policy;
            policy._expectTTLMs     = kExpectTTLMs;
            policy._recordRefreshMs = kRecordRefreshMs;
            policy._maxRecordCount  = kMaxRecordCount;
            return policy;
        }
    };
} // namespace sw::test
