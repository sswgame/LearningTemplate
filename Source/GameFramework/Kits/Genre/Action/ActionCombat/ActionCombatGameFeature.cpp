#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionCombat/MonsterCatalog.h"
#include "GameFramework/Runtime/Foundation/GameFeature/GameFeatureRegistry.h"
#include "GameFramework/Runtime/Foundation/GameFeature/GameFeatureScope.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilitySystemComponent.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 액션 룸이 적 수치를 찾는 몬스터 정의(`MonsterCatalog`, 팩의 `data/monsters.xml`)를 게임 기능 범위에 겁니다 — 미니게임 정의의 catalogs 에 `MonsterCatalog` 가 있어야 한다.
         *        받는 이름 `Combatant` 의 오브젝트(허수아비 · 부서지는 소품)는 어빌리티 시스템과 어빌리티 세트 `ActionCombat.Combatant`(게임의 어빌리티 카탈로그)를 받는다 —
         *        피해를 받는 것은 모두 어빌리티 시스템이 체력을 쥔다(언리얼 Lyra 의 AddComponents + AddAbilities).
         */
        /** @brief 받는 쪽에 붙인 어빌리티 시스템에 허수아비의 전투 세트를 줍니다(카탈로그에 없으면 시스템이 알린다). */
        void grantCombatantSet( AbilitySystemComponent& abilities )
        {
            (void)abilities.grantAbilitySet( hashed_string( "ActionCombat.Combatant" ) );
        }

        [[nodiscard]] bool activateActionCombat( GameFeatureScope& scope )
        {
            return scope.loadCatalog<MonsterCatalog>() && scope.addComponent<AbilitySystemComponent, &grantCombatantSet>( "Combatant" );
        }
    } // namespace

    SW_GAME_FEATURE( ActionCombat, "ActionCombat", &activateActionCombat );
} // namespace sw
