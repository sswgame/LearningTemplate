#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/ClassicJRPG/JRPGCatalog.h"
#include "GameFramework/Runtime/Foundation/GameFeature/GameFeatureRegistry.h"
#include "GameFramework/Runtime/Foundation/GameFeature/GameFeatureScope.h"

namespace sw
{
    namespace
    {
        /** @brief 경험치 곡선 · 직업 · 적(`JRPGCatalog`, 팩의 `data/jrpg.xml`)을 게임 기능 범위에 겁니다 — 미니게임 정의의 catalogs 에 `JRPGCatalog` 가 있어야 한다. */
        [[nodiscard]] bool activateClassicJRPG( GameFeatureScope& scope )
        {
            return scope.loadCatalog<JRPGCatalog>();
        }
    } // namespace

    SW_GAME_FEATURE( ClassicJRPG, "ClassicJRPG", &activateClassicJRPG );
} // namespace sw
