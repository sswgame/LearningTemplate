#include "pch.h"

#include "GameFramework/Kits/Genre/Casual/CardGame/CardBattle.h"
#include "GameFramework/Runtime/Foundation/GameFeature/GameFeatureRegistry.h"
#include "GameFramework/Runtime/Foundation/GameFeature/GameFeatureScope.h"

namespace sw
{
    namespace
    {
        /** @brief 덱 전투 카드(`CardBattleCatalog`, 팩의 `data/deckbattle.xml`)를 게임 기능 범위에 겁니다 — 미니게임 정의의 catalogs 에 `CardBattleCatalog` 가 있어야 한다. */
        [[nodiscard]] bool activateCardGame( GameFeatureScope& scope )
        {
            return scope.loadCatalog<CardBattleCatalog>();
        }
    } // namespace

    SW_GAME_FEATURE( CardGame, "CardGame", &activateCardGame );
} // namespace sw
