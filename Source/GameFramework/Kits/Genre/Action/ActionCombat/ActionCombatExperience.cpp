#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionCombat/MonsterCatalog.h"
#include "GameFramework/Runtime/Foundation/Framework/Experience/ExperienceKitRegistry.h"
#include "GameFramework/Runtime/Foundation/Framework/Experience/ExperienceScope.h"

namespace sw
{
    namespace
    {
        /** @brief 액션 룸이 적 수치를 찾는 몬스터 정의(`MonsterCatalog`, 팩의 `data/monsters.xml`)를 Experience 범위에 겁니다 — Experience 의 catalogs 에 `MonsterCatalog` 가 있어야 한다. */
        [[nodiscard]] bool activateActionCombat( ExperienceScope& scope )
        {
            return scope.loadCatalog<MonsterCatalog>();
        }
    } // namespace

    SW_EXPERIENCE_KIT( ActionCombat, "ActionCombat", &activateActionCombat );
} // namespace sw
