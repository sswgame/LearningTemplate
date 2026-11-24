#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/CoopScavenger/ScavengerCatalog.h"
#include "GameFramework/Runtime/Foundation/GameFeature/GameFeatureRegistry.h"
#include "GameFramework/Runtime/Foundation/GameFeature/GameFeatureScope.h"

namespace sw
{
    namespace
    {
        /** @brief 할당량 · 시설 · 고철 · 위성(`ScavengerCatalog`, 팩의 `data/scavenger.xml`)을 게임 기능 범위에 겁니다 — 미니게임 정의의 catalogs 에 `ScavengerCatalog` 가 있어야 한다. */
        [[nodiscard]] bool activateCoopScavenger( GameFeatureScope& scope )
        {
            return scope.loadCatalog<ScavengerCatalog>();
        }
    } // namespace

    SW_GAME_FEATURE( CoopScavenger, "CoopScavenger", &activateCoopScavenger );
} // namespace sw
