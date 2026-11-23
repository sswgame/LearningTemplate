#include "pch.h"

#include "GameFramework/Kits/Genre/Action/BattleRoyale/BattleRoyaleCatalog.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureRegistry.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureScope.h"

namespace sw
{
    namespace
    {
        /** @brief 플레이어 · 자기장 · 방어구(`BattleRoyaleCatalog`, 팩의 `data/battleroyale.xml`)를 게임 기능 범위에 겁니다 — 미니게임 정의의 catalogs 에 `BattleRoyaleCatalog` 가 있어야 한다. */
        [[nodiscard]] bool activateBattleRoyale( GameFeatureScope& scope )
        {
            return scope.loadCatalog<BattleRoyaleCatalog>();
        }
    } // namespace

    SW_GAME_FEATURE( BattleRoyale, "BattleRoyale", &activateBattleRoyale );
} // namespace sw
