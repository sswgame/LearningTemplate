#include "pch.h"

#include "GameFramework/Kits/Genre/Action/MechArena/MechCatalog.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureRegistry.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureScope.h"

namespace sw
{
    namespace
    {
        /** @brief 기체 · 비용 · 체력(`MechCatalog`, 팩의 `data/mechs.xml`)을 게임 기능 범위에 겁니다 — 미니게임 정의의 catalogs 에 `MechCatalog` 가 있어야 한다. */
        [[nodiscard]] bool activateMechArena( GameFeatureScope& scope )
        {
            return scope.loadCatalog<MechCatalog>();
        }
    } // namespace

    SW_GAME_FEATURE( MechArena, "MechArena", &activateMechArena );
} // namespace sw
