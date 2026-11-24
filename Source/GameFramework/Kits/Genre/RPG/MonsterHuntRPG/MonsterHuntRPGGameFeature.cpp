#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/MonsterHuntCatalog.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureRegistry.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureScope.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Crafting.h"

namespace sw
{
    namespace
    {
        /** @brief 몬스터 · 계약(`MonsterHuntCatalog`, 팩의 `data/monsterhunt.xml`)과 레시피(`RecipeCatalog`, `data/recipes.xml`)를 게임 기능 범위에 겁니다 — 미니게임 정의의 catalogs 에 둘 다 있어야 한다. */
        [[nodiscard]] bool activateMonsterHuntRPG( GameFeatureScope& scope )
        {
            return scope.loadCatalog<MonsterHuntCatalog>() && scope.loadCatalog<RecipeCatalog>();
        }
    } // namespace

    SW_GAME_FEATURE( MonsterHuntRPG, "MonsterHuntRPG", &activateMonsterHuntRPG );
} // namespace sw
