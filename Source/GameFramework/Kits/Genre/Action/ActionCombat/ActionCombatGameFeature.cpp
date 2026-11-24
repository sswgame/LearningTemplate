#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionCombat/MonsterCatalog.h"
#include "GameFramework/Runtime/Character/Combat/Health/UnitStatsComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureRegistry.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureScope.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 액션 룸이 적 수치를 찾는 몬스터 정의(`MonsterCatalog`, 팩의 `data/monsters.xml`)를 게임 기능 범위에 겁니다 — 미니게임 정의의 catalogs 에 `MonsterCatalog` 가 있어야 한다.
         *        받는 이름 `Combatant` 의 오브젝트(허수아비 · 부서지는 소품)는 이 키트의 체력 · 피해(`UnitStatsComponent`)를 받는다.
         */
        [[nodiscard]] bool activateActionCombat( GameFeatureScope& scope )
        {
            return scope.loadCatalog<MonsterCatalog>() && scope.addComponent<UnitStatsComponent>( "Combatant" );
        }
    } // namespace

    SW_GAME_FEATURE( ActionCombat, "ActionCombat", &activateActionCombat );
} // namespace sw
