#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionAdventure/AdventureRegionCatalog.h"
#include "GameFramework/Runtime/Foundation/GameFeature/GameFeatureRegistry.h"
#include "GameFramework/Runtime/Foundation/GameFeature/GameFeatureScope.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestCatalog.h"

namespace sw
{
    namespace
    {
        /** @brief 디렉터가 찾는 지역 표(`AdventureRegionCatalog`, 팩의 `data/regions.xml`) · 퀘스트(`QuestCatalog`, 팩의 `data/quests.xml`)를 게임 기능 범위에 겁니다 — 미니게임 정의의 catalogs 에 `adventureregions` · `quests` 가 있어야 한다. */
        [[nodiscard]] bool activateActionAdventure( GameFeatureScope& scope )
        {
            return scope.loadCatalog<AdventureRegionCatalog>() && scope.loadCatalog<QuestCatalog>();
        }
    } // namespace

    SW_GAME_FEATURE( ActionAdventure, "ActionAdventure", &activateActionAdventure );
} // namespace sw
