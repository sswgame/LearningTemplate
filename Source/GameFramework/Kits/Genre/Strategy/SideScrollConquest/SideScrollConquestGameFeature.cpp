#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/SideScrollConquest/ConquestCatalog.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureRegistry.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureScope.h"

namespace sw
{
    namespace
    {
        /** @brief 규칙 · 병종 · 거점(`ConquestCatalog`, 팩의 `data/conquest.xml`)을 게임 기능 범위에 겁니다 — 미니게임 정의의 catalogs 에 `ConquestCatalog` 가 있어야 한다. */
        [[nodiscard]] bool activateSideScrollConquest( GameFeatureScope& scope )
        {
            return scope.loadCatalog<ConquestCatalog>();
        }
    } // namespace

    SW_GAME_FEATURE( SideScrollConquest, "SideScrollConquest", &activateSideScrollConquest );
} // namespace sw
