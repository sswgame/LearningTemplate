#include "pch.h"

#include "Core/Log/Logger.h"
#include "Core/Name/hashed_string.h"

#include "GameFramework/Kits/Genre/Action/AbilityArena/ArenaAbilities.h"
#include "GameFramework/Runtime/Character/Combat/Projectile/ProjectileDef.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureRegistry.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureScope.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityCatalog.h"

namespace sw
{
    namespace
    {
        /** @brief 어빌리티 클래스 @p TAbility 를 @p classes 에 올리고 끌 때 내립니다. 같은 이름이 이미 있으면 레지스트리가 알리고 false 입니다. */
        template <typename TAbility>
        [[nodiscard]] bool addAbilityClass( GameFeatureScope& scope, AbilityClassRegistry& classes, const hashed_string& className )
        {
            if ( classes.registerAbilityClass<TAbility>( className ) == false )
                return false;
            scope.addCleanup( SW_DELEGATE_LAMBDA( Delegate<void()>, [pClasses = &classes, className]()
            { pClasses->unregisterAbilityClass( className ); } ) );
            return true;
        }

        /**
         * @brief 키트의 어빌리티 클래스를 게임 범위의 레지스트리(게임 기능 하나가 건 서비스, 이 범위보다 오래 산다)에 올립니다.
         *        어빌리티 데이터(`data/abilities.xml`)와 탄(`data/projectiles.xml`)은 게임이 게임 범위에 이미 건 팩의 카탈로그다 — 탄은 다른 미니게임도 쓴다.
         */
        [[nodiscard]] bool activateAbilityArena( GameFeatureScope& scope )
        {
            AbilityClassRegistry* const pClasses = game::findSubsystem<AbilityClassRegistry>();
            if ( pClasses == nullptr || game::findSubsystem<const ProjectileCatalog>() == nullptr )
            {
                SW_LOG_ERROR_ID( "AbilityArena.NoGameScopeService", "Ability arena: no %# is bound to the game scope - the game must bind one before this feature",
                                 pClasses == nullptr ? "AbilityClassRegistry" : "ProjectileCatalog" );
                return false;
            }
            return addAbilityClass<ArenaMeleeStrikeAbility>( scope, *pClasses, "ArenaMeleeStrike" ) && addAbilityClass<ArenaProjectileAbility>( scope, *pClasses, "ArenaProjectile" ) &&
                   addAbilityClass<ArenaDashAbility>( scope, *pClasses, "ArenaDash" );
        }
    } // namespace

    SW_GAME_FEATURE( AbilityArena, "AbilityArena", &activateAbilityArena );
} // namespace sw
