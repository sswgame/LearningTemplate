/**
 * @file AbilityEffectDevCommands.cpp
 * @brief 어빌리티 이펙트 시험 조작 `ability.apply` 입니다(읽기 전용 디버거 `AbilityDebugSections` 와 나눈다). 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/String/StringUtil.h"

#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Reflection/EnumName.h"

#include "GameFramework/Runtime/Foundation/Debug/GameplayDebugTarget.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilitySystemComponent.h"

namespace sw
{
    namespace
    {
        /** @brief 시험 조작(읽기 전용 디버거와 나눈다) — 이펙트를 대상에 건다. 건 길은 `devcommand` 로 적혀 디버거가 구별한다. */
        struct AbilityTestCommandsInternal
        {
            /** @brief `ability.apply <이펙트 id>` — `debug.target` 의 AbilitySystemComponent(없으면 첫 것)에 건다. 거부되면 까닭이 디버거 기록에 남는다. */
            static bool runApply( SceneIterationKey key, const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                const GameObjectManager* const pManager = GameplayDebugTarget::findActiveObjectManager();
                AbilitySystemComponent* const  pAbility = pManager != nullptr ? GameplayDebugTarget::findTargetOrLowestIDComponent<AbilitySystemComponent>( key, *pManager ) : nullptr;
                if ( pAbility == nullptr )
                {
                    outReply = "ability.apply: no ability component in the active scene";
                    return false;
                }
                AbilityEffectSpec spec = pAbility->makeOutgoingSpecByID( hashed_string( listArgument[0] ) );
                if ( spec.isValid() == false )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "ability.apply: no effect '%#' in the catalog", listArgument[0] );
                    outReply = string( replyBuilder.view() );
                    return false;
                }
                spec._context._sourceName                       = hashed_string( "devcommand" );
                const ActiveEffectHandle                 handle = pAbility->applyGameplayEffectSpecToSelf( spec );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                if ( handle.isValid() )
                    replyBuilder.appendFormat( "ability.apply -> applied %#", listArgument[0] );
                else
                    replyBuilder.appendFormat( "ability.apply -> rejected %# (see hud.print ability)", listArgument[0] );
                outReply = string( replyBuilder.view() );
                return true;
            }
        };
    } // namespace

    SW_DEV_SCENE_COMMAND( AbilityApply, "ability.apply", "ability.apply <effect id>",
                          "Apply an effect from the catalog to the debug target's ability component (test control; the ability debugger shows it as from devcommand)",
                          &AbilityTestCommandsInternal::runApply );
} // namespace sw
