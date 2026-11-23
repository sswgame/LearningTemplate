#include "pch.h"

#include "GameFramework/Kits/Genre/Action/Fighting/FighterCatalog.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureRegistry.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureScope.h"

namespace sw
{
    namespace
    {
        /** @brief 대전 캐릭터(`FighterCatalog`, 팩의 `data/fighters.xml`)를 게임 기능 범위에 겁니다 — 미니게임 정의의 catalogs 에 `FighterCatalog` 가 있어야 한다. */
        [[nodiscard]] bool activateFighting( GameFeatureScope& scope )
        {
            return scope.loadCatalog<FighterCatalog>();
        }
    } // namespace

    SW_GAME_FEATURE( Fighting, "Fighting", &activateFighting );
} // namespace sw
