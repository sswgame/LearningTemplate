#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Ability/AbilityActions.h"

#include "Engine/Behavior/ActionRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Runtime/Foundation/Condition/GameConditions.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityCatalog.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityComponent.h"

namespace sw
{
    namespace
    {
        struct AbilityActionsInternal
        {
            /** @brief 대상의 어빌리티 컴포넌트입니다. 대상 칸이 없거나 컴포넌트가 없으면 nullptr 입니다(동작은 실패다). */
            static AbilityComponent* findTargetAbility( const ActionContext& context )
            {
                GameObject* const pTarget = context._world.find<ActionTargetSlot>();
                return pTarget != nullptr ? pTarget->getComponent<AbilityComponent>() : nullptr;
            }

            /** @brief 이 호출의 출처 이름입니다(없으면 비어 있다 — 어빌리티 · 코드가 건 것과 같다). */
            static hashed_string findSourceName( const ActionContext& context )
            {
                const ActionSource* const pSource = context._world.find<ActionSourceSlot>();
                return pSource != nullptr ? pSource->_name : hashed_string{};
            }

            static ActionResult runApplyEffect( ActionContext& context, const AbilityEffectActionParams& params )
            {
                AbilityComponent* const pAbility = findTargetAbility( context );
                if ( pAbility == nullptr )
                    return ActionResult::Failed;
                AbilityEffectSpec spec = pAbility->makeOutgoingSpecByID( params._effect, params._level );
                if ( spec.isValid() == false )
                    return ActionResult::Failed;
                if ( const ActionSource* const pSource = context._world.find<ActionSourceSlot>() )
                {
                    spec._context._sourceName = pSource->_name;
                    if ( pSource->_object.isValid() )
                        spec._context._effectCauser = pSource->_object;
                }
                if ( params._setByCaller.empty() == false )
                {
                    // 값은 실행기가 그 순간 계산한 수다. 이름을 적었는데 값이 없으면 데이터가 틀렸다 — 0 을 지어내지 않는다.
                    const ActionValues* const pValues = context._world.find<ActionValuesSlot>();
                    const optional<float32>   value   = pValues != nullptr ? pValues->find( params._value ) : nullopt;
                    if ( value.has_value() == false )
                    {
                        SW_LOG_ERROR_ID( "AbilityActions.ValueMissing", "ability.applyEffect '%#': value '%#' for SetByCaller '%#' was not given by the caller", params._effect.c_str(),
                                         params._value.c_str(), params._setByCaller.c_str() );
                        return ActionResult::Failed;
                    }
                    spec.setSetByCallerMagnitude( params._setByCaller, *value );
                }
                return pAbility->applyGameplayEffectSpecToTarget( spec, pAbility ).isValid() ? ActionResult::Succeeded : ActionResult::Failed;
            }

            static ActionResult runRemoveEffect( ActionContext& context, const AbilityEffectRemoveParams& params )
            {
                AbilityComponent* const pAbility = findTargetAbility( context );
                if ( pAbility == nullptr )
                    return ActionResult::Failed;
                if ( params._effect.empty() )
                {
                    (void)pAbility->removeActiveEffectsFromSource( findSourceName( context ) ); // 지운 수는 쓰지 않는다 — 없으면 할 일이 없었다
                    return ActionResult::Succeeded;
                }
                const ActiveEffectHandle handle = pAbility->findActiveEffectByID( params._effect );
                if ( handle.isValid() == false )
                    return ActionResult::Succeeded; // 이미 없다 — 지우기는 목표 상태가 같다
                return pAbility->removeActiveEffect( handle ) ? ActionResult::Succeeded : ActionResult::Failed;
            }

            static ActionResult runRemoveStacks( ActionContext& context, const AbilityStackRemoveParams& params )
            {
                AbilityComponent* const pAbility = findTargetAbility( context );
                if ( pAbility == nullptr )
                    return ActionResult::Failed;
                const ActiveEffectHandle handle = pAbility->findActiveEffectByID( params._effect );
                if ( handle.isValid() == false )
                    return ActionResult::Failed; // 줄일 스택이 없다 — 탈출할 것이 없었다
                return pAbility->removeActiveEffect( handle, params._stacks ) ? ActionResult::Succeeded : ActionResult::Failed;
            }

            static ActionResult runGrant( ActionContext& context, const AbilityGrantParams& params )
            {
                AbilityComponent* const pAbility = findTargetAbility( context );
                if ( pAbility == nullptr )
                    return ActionResult::Failed;
                if ( pAbility->findAbilitySpecHandle( params._ability ).isValid() )
                    return ActionResult::Succeeded; // 이미 가졌다
                return pAbility->giveAbilityByID( params._ability ).isValid() ? ActionResult::Succeeded : ActionResult::Failed;
            }

            static ActionResult runActivate( ActionContext& context, const AbilityGrantParams& params )
            {
                AbilityComponent* const pAbility = findTargetAbility( context );
                if ( pAbility == nullptr )
                    return ActionResult::Failed;
                AbilitySpecHandle handle = pAbility->findAbilitySpecHandle( params._ability );
                if ( handle.isValid() == false )
                    handle = pAbility->giveAbilityByID( params._ability ); // 스킬 영역은 가지지 않은 스킬도 발동한다(주고 발동)
                return handle.isValid() && pAbility->tryActivateAbility( handle ) == AbilityActivationResult::Activated ? ActionResult::Succeeded : ActionResult::Failed;
            }

            static ActionResult runKill( ActionContext& context, const HealthKillParams& params )
            {
                AbilityComponent* const pAbility = findTargetAbility( context );
                if ( pAbility == nullptr )
                    return ActionResult::Failed;
                return pAbility->applyFatalDamage( params._bIgnoreInvulnerable != SW_FALSE ) ? ActionResult::Succeeded : ActionResult::Failed;
            }

            static bool checkEffect( const AbilityCatalog& catalog, const hashed_string& effectID, string_view sourceName )
            {
                if ( effectID.empty() || catalog.findEffect( effectID ) != nullptr )
                    return true;
                SW_LOG_ERROR_ID( "AbilityActions.UnknownEffect", "%#: effect '%#' is not in the ability catalog", sourceName, effectID.c_str() );
                return false;
            }

            static bool checkAbility( const AbilityCatalog& catalog, const hashed_string& abilityID, string_view sourceName )
            {
                if ( catalog.findAbility( abilityID ) != nullptr )
                    return true;
                SW_LOG_ERROR_ID( "AbilityActions.UnknownAbility", "%#: ability '%#' is not in the ability catalog", sourceName, abilityID.c_str() );
                return false;
            }
        };
    } // namespace

    SW_GAME_ACTION( AbilityApplyEffect, "ability.applyEffect", AbilityEffectActionParams, &AbilityActionsInternal::runApplyEffect );
    SW_GAME_ACTION( AbilityRemoveEffect, "ability.removeEffect", AbilityEffectRemoveParams, &AbilityActionsInternal::runRemoveEffect );
    SW_GAME_ACTION( AbilityRemoveStacks, "ability.removeStacks", AbilityStackRemoveParams, &AbilityActionsInternal::runRemoveStacks );
    SW_GAME_ACTION( AbilityGrant, "ability.grant", AbilityGrantParams, &AbilityActionsInternal::runGrant );
    SW_GAME_ACTION( AbilityActivate, "ability.activate", AbilityGrantParams, &AbilityActionsInternal::runActivate );
    SW_GAME_ACTION( HealthKill, "health.kill", HealthKillParams, &AbilityActionsInternal::runKill );
} // namespace sw

namespace sw
{
    HealthKillParams::HealthKillParams()
        : _bIgnoreInvulnerable{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    bool AbilityActions::validateReferences( const GameActionCall& call, const AbilityCatalog* pCatalog, string_view sourceName )
    {
        using Internal                       = AbilityActionsInternal;
        const ActionEntry* const pEntry      = call.getEntry();
        const void* const        pParams     = call.getParams();
        const bool               bEffect     = pEntry != nullptr && pEntry->_pParamsOps == &InstancedStructOps::of<AbilityEffectActionParams>();
        const bool               bRemove     = pEntry != nullptr && pEntry->_pParamsOps == &InstancedStructOps::of<AbilityEffectRemoveParams>();
        const bool               bStacks     = pEntry != nullptr && pEntry->_pParamsOps == &InstancedStructOps::of<AbilityStackRemoveParams>();
        const bool               bAbility    = pEntry != nullptr && pEntry->_pParamsOps == &InstancedStructOps::of<AbilityGrantParams>();
        const bool               bVocabulary = bEffect || bRemove || bStacks || bAbility;
        if ( bVocabulary == false || pParams == nullptr )
            return true;
        if ( pCatalog == nullptr )
        {
            SW_LOG_ERROR_ID( "AbilityActions.NoCatalog", "%#: '%#' needs the game's AbilityCatalog (bind it as a game service)", sourceName, pEntry->_pName );
            return false;
        }
        if ( bEffect )
            return Internal::checkEffect( *pCatalog, static_cast<const AbilityEffectActionParams*>( pParams )->_effect, sourceName );
        if ( bRemove )
            return Internal::checkEffect( *pCatalog, static_cast<const AbilityEffectRemoveParams*>( pParams )->_effect, sourceName );
        if ( bStacks )
            return Internal::checkEffect( *pCatalog, static_cast<const AbilityStackRemoveParams*>( pParams )->_effect, sourceName );
        return Internal::checkAbility( *pCatalog, static_cast<const AbilityGrantParams*>( pParams )->_ability, sourceName );
    }
} // namespace sw
