#include "pch.h"

#include "GameFramework/Kits/Genre/Action/Metroidvania/MetroidvaniaCatalog.h"
#include "GameFramework/Runtime/Foundation/GameFeature/GameFeatureRegistry.h"
#include "GameFramework/Runtime/Foundation/GameFeature/GameFeatureScope.h"

namespace sw
{
    namespace
    {
        /** @brief 규칙 · 장소 · 적(`MetroidvaniaCatalog`, 팩의 `data/metroidvania.xml`)을 게임 기능 범위에 겁니다 — 미니게임 정의의 catalogs 에 `MetroidvaniaCatalog` 가 있어야 한다. */
        [[nodiscard]] bool activateMetroidvania( GameFeatureScope& scope )
        {
            return scope.loadCatalog<MetroidvaniaCatalog>();
        }
    } // namespace

    SW_GAME_FEATURE( Metroidvania, "Metroidvania", &activateMetroidvania );
} // namespace sw
