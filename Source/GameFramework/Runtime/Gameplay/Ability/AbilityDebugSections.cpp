/**
 * @file AbilityDebugSections.cpp
 * @brief 런타임 Ability 디버거(HUD 섹션 `ability`)입니다 — 게임 창에서 에디터 Ability Debugger 와 같은 것을 읽는다.
 * @details 걸린 이펙트마다 남은 시간 · 스택 · 건 길(영역 · 상호작용 · 기믹 — `AbilityEffectContext::_sourceName`), 멈춤(걸린 동안의 태그 조건) 표시,
 *          소유 태그, 그리고 최근 적용 · 거부(까닭) · 제거(원인) 기록. 읽기만 하고 꺼져 있으면 불리지 않는다.
 */
#include "pch.h"

#include "Core/String/StringUtil.h"

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
                const AbilityComponent* const  pAbility = pManager != nullptr ? GameplayDebugTarget::findTargetComponent<AbilityComponent>( writer.getSceneIterationKey(), *pManager ) : nullptr;
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
                    string value = pEffect->_duration > 0.0f ? to_string( pEffect->_remainingTime ) + "s" : string( "infinite" );
                    value += " x" + to_string( pEffect->_stackCount ) + " from " + formatSource( pEffect->_spec._context._sourceName );
                    if ( pEffect->_bInhibited == SW_TRUE )
                        value += " (inhibited)";
                    writer.addLine( string( "effect " ) + pEffect->_spec._pDef->_id.c_str(), value );
                }
                string tags;
                for ( const TagID& tag : pAbility->getOwnedTagCounts().getTags().getTags() )
                {
                    const utf8* pName = tag.getString();
                    if ( tags.empty() == false )
                        tags += ", ";
                    tags += pName != nullptr ? pName : "?";
                }
                writer.addLine( "tags", tags.empty() ? string( "-" ) : tags );
                vector<AbilityEffectLogEntry> listLog;
                pAbility->collectEffectLog( listLog );
                for ( auto it = listLog.rbegin(); it != listLog.rend() && ++lineCount <= GameplayDebugTarget::kMaxLines; ++it )
                {
                    writer.addLine( "t " + to_string( it->_time ), string( getEnumName( it->_kind ) ) + " " + it->_effectID.c_str() + " from " + formatSource( it->_sourceName ) );
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
                AbilityComponent* const        pAbility = pManager != nullptr ? GameplayDebugTarget::findTargetComponent<AbilityComponent>( key, *pManager ) : nullptr;
                if ( pAbility == nullptr )
                {
                    outReply = "ability.apply: no ability component in the active scene";
                    return false;
                }
                AbilityEffectSpec spec = pAbility->makeOutgoingSpecByID( hashed_string( listArgument[0] ) );
                if ( spec.isValid() == false )
                {
                    outReply = "ability.apply: no effect '" + listArgument[0] + "' in the catalog";
                    return false;
                }
                spec._context._sourceName       = hashed_string( "devcommand" );
                const ActiveEffectHandle handle = pAbility->applyGameplayEffectSpecToSelf( spec );
                outReply                        = handle.isValid() ? "ability.apply -> applied " + listArgument[0] : "ability.apply -> rejected " + listArgument[0] + " (see hud.print ability)";
                return true;
            }
        };
    } // namespace

    SW_DEV_SCENE_COMMAND( AbilityApply, "ability.apply", "ability.apply <effect id>",
                          "Apply an effect from the catalog to the debug target's ability component (test control; the ability debugger shows it as from devcommand)",
                          &AbilityTestCommandsInternal::runApply );
    SW_DEBUG_HUD_SECTION( ability, "ability", "Ability (effects, tags, log)", 270, DebugHUDSectionFlag::kNone, &AbilityDebugSectionsInternal::writeAbility );
} // namespace sw
