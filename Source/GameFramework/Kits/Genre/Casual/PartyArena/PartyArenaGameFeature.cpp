#include "pch.h"

#include "GameFramework/Kits/Genre/Casual/PartyArena/PartyRoundSeries.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureRegistry.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureScope.h"

namespace sw
{
    namespace
    {
        /** @brief 라운드 · 순위 점수(`PartyRoundSeries`, 팩의 `data/partyrounds.xml`)를 게임 기능 범위에 겁니다 — 미니게임 정의의 catalogs 에 `PartyRoundSeries` 가 있어야 한다. */
        [[nodiscard]] bool activatePartyArena( GameFeatureScope& scope )
        {
            return scope.loadCatalog<PartyRoundSeries>();
        }
    } // namespace

    SW_GAME_FEATURE( PartyArena, "PartyArena", &activatePartyArena );
} // namespace sw
