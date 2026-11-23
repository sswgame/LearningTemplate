#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionPlatformer/ActionPlatformerCatalog.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureRegistry.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureScope.h"

namespace sw
{
    namespace
    {
        /** @brief 스테이지 · 채점 · 몸 규칙(`ActionPlatformerCatalog`, 팩의 `data/actionplatformer.xml`)을 게임 기능 범위에 겁니다 — 미니게임 정의의 catalogs 에 `ActionPlatformerCatalog` 가 있어야 한다. */
        [[nodiscard]] bool activateActionPlatformer( GameFeatureScope& scope )
        {
            return scope.loadCatalog<ActionPlatformerCatalog>();
        }
    } // namespace

    SW_GAME_FEATURE( ActionPlatformer, "ActionPlatformer", &activateActionPlatformer );
} // namespace sw
