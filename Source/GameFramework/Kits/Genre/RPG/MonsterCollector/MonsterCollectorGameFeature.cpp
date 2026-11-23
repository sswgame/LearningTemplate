#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/MonsterCollector/MonsterCollectorCatalog.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureRegistry.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureScope.h"

namespace sw
{
    namespace
    {
        /** @brief 기술 · 종(`MonsterCollectorCatalog`, 팩의 `data/monstercollector.xml`)을 게임 기능 범위에 겁니다 — 미니게임 정의의 catalogs 에 `MonsterCollectorCatalog` 가 있어야 한다. */
        [[nodiscard]] bool activateMonsterCollector( GameFeatureScope& scope )
        {
            return scope.loadCatalog<MonsterCollectorCatalog>();
        }
    } // namespace

    SW_GAME_FEATURE( MonsterCollector, "MonsterCollector", &activateMonsterCollector );
} // namespace sw
