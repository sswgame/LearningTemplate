/**
 * @file AbilityDebugSections.cpp
 * @brief 런타임 Ability 디버거(HUD 섹션 `ability`)입니다 — 게임 창에서 에디터 Ability Debugger 와 같은 것을 읽는다.
 * @details 걸린 이펙트마다 남은 시간 · 스택 · 건 길(영역 · 상호작용 · 기믹 — `AbilityEffectContext::_sourceName`), 멈춤(걸린 동안의 태그 조건) 표시,
 *          소유 태그, 그리고 최근 적용 · 거부(까닭) · 제거(원인) 기록. 읽기만 하고 꺼져 있으면 불리지 않는다.
 */
#include "pch.h"

#include "Core/String/StringUtil.h"

#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Reflection/EnumName.h"

#include "GameFramework/Runtime/Foundation/Framework/GameplayDebugTarget.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityComponent.h"

namespace sw
{
    namespace
    {
        struct AbilityDebugSectionsInternal
        {
            static string formatSource( const hashed_string& sourceName ) { return sourceName.size() > 0 ? string( sourceName.c_str() ) : string( "ability/code" ); }

            static void writeAbility( DebugHUDSectionWriter& writer )
            {
                const GameObjectManager* const pManager = GameplayDebugTarget::findActiveObjectManager();
                const AbilityComponent* const  pAbility = pManager != nullptr ? GameplayDebugTarget::findTargetOrLowestIDComponent<AbilityComponent>( writer.getSceneIterationKey(), *pManager ) : nullptr;
                if ( pAbility == nullptr )
                {
                    writer.addLine( "target", "none (no ability component in the active scene)" );
                    return;
                }
                writer.addLine( "target", pAbility->getOwner() != nullptr ? string( pAbility->getOwner()->getName().c_str() ) : string( "?" ) );
                uint32 lineCount = 1;
                for ( const unique_ptr<ActiveGameplayEffect>& pEffect : pAbility->getActiveEffects() )
                {
                    if ( pEffect == nullptr || pEffect->_bPendingRemove == SW_TRUE || pEffect->_spec._pDef == nullptr )
                        continue;
                    if ( ++lineCount > GameplayDebugTarget::kMaxLines / 2 )
                        break;
                    StringBuilder<constant::buffer::kMax256> value;
                    if ( pEffect->_duration > 0.0f )
                        value.appendFormat( "%#s", pEffect->_remainingTime );
                    else
                        value.append( "infinite" );
                    value.appendFormat( " x%# from %#", pEffect->_stackCount, formatSource( pEffect->_spec._context._sourceName ) );
                    if ( pEffect->_bInhibited == SW_TRUE )
                        value.append( " (inhibited)" );
                    fixed_string<constant::buffer::kMax128> label;
                    formatstring( label.data(), label.capacity(), "effect %#", pEffect->_spec._pDef->_id.c_str() );
                    writer.addLine( label.view(), value.view() );
                }
                StringBuilder<constant::buffer::kMax512> tags;
                for ( const TagID& tag : pAbility->getOwnedTagCounts().getTags().getTags() )
                {
                    const utf8* pName = tag.getString();
                    if ( tags.size() > 0 )
                        tags.append( ", " );
                    tags.append( pName != nullptr ? pName : "?" );
                }
                writer.addLine( "tags", tags.size() == 0 ? string_view( "-" ) : tags.view() );
                vector<AbilityEffectLogEntry> listLog;
                pAbility->collectEffectLog( listLog );
                for ( auto it = listLog.rbegin(); it != listLog.rend() && ++lineCount <= GameplayDebugTarget::kMaxLines; ++it )
                {
                    fixed_string<constant::buffer::kMax32> label;
                    formatstring( label.data(), label.capacity(), "t %#", it->_time );
                    fixed_string<constant::buffer::kMax256> value;
                    formatstring( value.data(), value.capacity(), "%# %# from %#", getEnumName( it->_kind ), it->_effectID.c_str(), formatSource( it->_sourceName ) );
                    writer.addLine( label.view(), value.view() );
                }
            }
        };

        /** @brief 시험 조작(읽기 전용 디버거와 나눈다) — 이펙트를 대상에 건다. 건 길은 `devcommand` 로 적혀 디버거가 구별한다. */
        struct AbilityTestCommandsInternal
        {
            /** @brief `ability.apply <이펙트 id>` — `debug.target` 의 AbilityComponent(없으면 첫 것)에 건다. 거부되면 까닭이 디버거 기록에 남는다. */
            static bool runApply( SceneIterationKey key, const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                const GameObjectManager* const pManager = GameplayDebugTarget::findActiveObjectManager();
                AbilityComponent* const        pAbility = pManager != nullptr ? GameplayDebugTarget::findTargetOrLowestIDComponent<AbilityComponent>( key, *pManager ) : nullptr;
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
    SW_DEBUG_HUD_SECTION( ability, "ability", "Ability (effects, tags, log)", 270, DebugHUDSectionFlag::kNone, &AbilityDebugSectionsInternal::writeAbility );
} // namespace sw
