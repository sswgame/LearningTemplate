#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/SurvivalHorror/SurvivalHorrorCatalog.h"
#include "GameFramework/Runtime/Foundation/GameFeature/GameFeatureRegistry.h"
#include "GameFramework/Runtime/Foundation/GameFeature/GameFeatureScope.h"

namespace sw
{
    namespace
    {
        /** @brief 규칙 · 아이템 · 몬스터(`SurvivalHorrorCatalog`, 팩의 `data/horror.xml`)를 게임 기능 범위에 겁니다 — 미니게임 정의의 catalogs 에 `SurvivalHorrorCatalog` 가 있어야 한다. */
        [[nodiscard]] bool activateSurvivalHorror( GameFeatureScope& scope )
        {
            return scope.loadCatalog<SurvivalHorrorCatalog>();
        }
    } // namespace

    SW_GAME_FEATURE( SurvivalHorror, "SurvivalHorror", &activateSurvivalHorror );
} // namespace sw
