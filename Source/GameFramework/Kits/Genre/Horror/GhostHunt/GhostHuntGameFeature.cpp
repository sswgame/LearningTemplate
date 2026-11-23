#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/GhostHunt/GhostCatalog.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureRegistry.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureScope.h"

namespace sw
{
    namespace
    {
        /** @brief 손전등 · 청소기 · 유령(`GhostCatalog`, 팩의 `data/ghosts.xml`)을 게임 기능 범위에 겁니다 — 미니게임 정의의 catalogs 에 `GhostCatalog` 가 있어야 한다. */
        [[nodiscard]] bool activateGhostHunt( GameFeatureScope& scope )
        {
            return scope.loadCatalog<GhostCatalog>();
        }
    } // namespace

    SW_GAME_FEATURE( GhostHunt, "GhostHunt", &activateGhostHunt );
} // namespace sw
