#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/SRPGCatalog.h"
#include "GameFramework/Runtime/Foundation/GameFeature/GameFeatureRegistry.h"
#include "GameFramework/Runtime/Foundation/GameFeature/GameFeatureScope.h"

namespace sw
{
    namespace
    {
        /** @brief 지형 · 무기 · 유닛 · 파일럿(`SRPGCatalog`, 팩의 `data/srpg.xml`)을 게임 기능 범위에 겁니다 — 미니게임 정의의 catalogs 에 `SRPGCatalog` 가 있어야 한다. */
        [[nodiscard]] bool activateTacticsSRPG( GameFeatureScope& scope )
        {
            return scope.loadCatalog<SRPGCatalog>();
        }
    } // namespace

    SW_GAME_FEATURE( TacticsSRPG, "TacticsSRPG", &activateTacticsSRPG );
} // namespace sw
