#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/RestaurantSim/RestaurantCatalog.h"
#include "GameFramework/Runtime/Foundation/GameFeature/GameFeatureRegistry.h"
#include "GameFramework/Runtime/Foundation/GameFeature/GameFeatureScope.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Crafting.h"

namespace sw
{
    namespace
    {
        /** @brief 메뉴 · 손님(`RestaurantCatalog`, 팩의 `data/restaurant.xml`)과 레시피(`RecipeCatalog`, `data/recipes.xml`)를 게임 기능 범위에 겁니다 — 미니게임 정의의 catalogs 에 둘 다 있어야 한다. */
        [[nodiscard]] bool activateRestaurantSim( GameFeatureScope& scope )
        {
            return scope.loadCatalog<RestaurantCatalog>() && scope.loadCatalog<RecipeCatalog>();
        }
    } // namespace

    SW_GAME_FEATURE( RestaurantSim, "RestaurantSim", &activateRestaurantSim );
} // namespace sw
