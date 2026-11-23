#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/MonsterHuntCatalog.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureRegistry.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureScope.h"

namespace sw
{
    namespace
    {
        /** @brief 몬스터 · 계약(`MonsterHuntCatalog`, 팩의 `data/monsterhunt.xml`)을 게임 기능 범위에 겁니다 — 미니게임 정의의 catalogs 에 `MonsterHuntCatalog` 가 있어야 한다. */
        [[nodiscard]] bool activateMonsterHuntRPG( GameFeatureScope& scope )
        {
            return scope.loadCatalog<MonsterHuntCatalog>();
        }
    } // namespace

    SW_GAME_FEATURE( MonsterHuntRPG, "MonsterHuntRPG", &activateMonsterHuntRPG );
} // namespace sw
