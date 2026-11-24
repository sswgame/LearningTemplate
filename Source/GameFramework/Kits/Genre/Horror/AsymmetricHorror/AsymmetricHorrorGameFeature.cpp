#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/AsymmetricHorrorRules.h"
#include "GameFramework/Runtime/Foundation/GameFeature/GameFeatureRegistry.h"
#include "GameFramework/Runtime/Foundation/GameFeature/GameFeatureScope.h"

namespace sw
{
    namespace
    {
        /** @brief 발전기 · 종반 · 살인마 규칙(`AsymmetricHorrorRulesCatalog`, 팩의 `data/asymhorror.xml`)을 게임 기능 범위에 겁니다 — 미니게임 정의의 catalogs 에 `AsymmetricHorrorRulesCatalog` 가 있어야 한다. */
        [[nodiscard]] bool activateAsymmetricHorror( GameFeatureScope& scope )
        {
            return scope.loadCatalog<AsymmetricHorrorRulesCatalog>();
        }
    } // namespace

    SW_GAME_FEATURE( AsymmetricHorror, "AsymmetricHorror", &activateAsymmetricHorror );
} // namespace sw
