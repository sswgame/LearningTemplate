/**
 * @file MatchmakingTestPolicy.h
 * @brief 매칭 키트 테스트가 쓰는 운영 값입니다(Tests 폴더 — 배포에 없다). 서버 설정 `Config/Server/Online/Playground.online.json` 의 값과 같게 둔다.
 */
#pragma once
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/MatchQueueService.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/MatchServerAgent.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/PartyLobbyService.h"

namespace sw::test
{
    struct MatchmakingTestPolicy
    {
        static constexpr int32 kMaxPartySize = 4; ///< 서버 설정 `_maxPartySize` 와 같은 값

        static PartyLobbyPolicy makePartyLobby()
        {
            PartyLobbyPolicy policy;
            policy._partyTTLMs  = 1800000;
            policy._lobbyTTLMs  = 7200000;
            policy._inviteTTLMs = 300000;
            return policy;
        }

        static MatchQueuePolicy makeQueue()
        {
            MatchQueuePolicy policy;
            policy._leaseTTLMs       = 10000;
            policy._leaseRenewMs     = 3000;
            policy._noServerGiveUpMs = 30000;
            policy._resultGraceMs    = 10000;
            policy._ratingWaitMs     = 3000;
            return policy;
        }

        static MatchServerAgentPolicy makeAgent()
        {
            MatchServerAgentPolicy policy;
            policy._expectTTLMs     = 120000;
            policy._recordRefreshMs = 1000;
            policy._maxRecordCount  = 256;
            return policy;
        }
    };
} // namespace sw::test
