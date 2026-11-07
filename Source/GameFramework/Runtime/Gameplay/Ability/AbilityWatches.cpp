/**
 * @file AbilityWatches.cpp
 * @brief 어빌리티 감시 값 — 대상의 속성(current), 태그(효과가 준 것 포함), 이펙트 스택 수. 값 시나리오가 판정한다(지역 효과의 가라앉음 깊이 · 체력 · 화상 · 버프).
 * @details 읽기만 한다. 속성이 없는 대상은 실패 줄에 까닭을 싣는다(0 을 지어내지 않는다).
 */
#include "pch.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Runtime/Gameplay/Ability/AbilityComponent.h"

namespace sw
{
    namespace
    {
        struct AbilityWatchesInternal
        {
            /** @brief 대상의 어빌리티 컴포넌트입니다. 없으면 까닭을 적고 nullptr 입니다. */
            static const AbilityComponent* findAbility( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject*       pTarget  = query.findTargetObject();
                const AbilityComponent* pAbility = pTarget != nullptr ? pTarget->getComponent<AbilityComponent>() : nullptr;
                if ( pAbility == nullptr )
                    outValue.setText( pTarget == nullptr ? query.getTargetProblem() : query.describeMissing( "AbilityComponent" ) );
                return pAbility;
            }

            /** @brief 감시 값 `Ability.Attribute` 입니다. 대상 속성(key)의 current 값입니다. */
            [[nodiscard]] static bool readAttribute( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const AbilityComponent* pAbility = findAbility( query, outValue );
                if ( pAbility == nullptr )
                    return false;
                const std::optional<float32> value = pAbility->getAttributeValue( hashed_string( string( query._key ).c_str() ) );
                if ( value.has_value() == false )
                {
                    outValue.setText( string( "the target has no attribute '" ) + string( query._key ) + "'" );
                    return false;
                }
                outValue.setNumber( static_cast<float64>( *value ) );
                return true;
            }

            /** @brief 감시 값 `Ability.HasTag` 입니다. 대상이 태그(key, 하위 태그 포함)를 가졌으면 1, 아니면 0 입니다. */
            [[nodiscard]] static bool readHasTag( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const AbilityComponent* pAbility = findAbility( query, outValue );
                if ( pAbility == nullptr )
                    return false;
                outValue.setNumber( pAbility->getOwnedTagCounts().hasMatchingTag( TagID::request( query._key ) ) ? 1.0 : 0.0 );
                return true;
            }

            /** @brief 감시 값 `Ability.StackCount` 입니다. 대상에 걸린 이펙트(key = 정의 id)의 스택 수이고, 걸려 있지 않으면 0 입니다. */
            [[nodiscard]] static bool readStackCount( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const AbilityComponent* pAbility = findAbility( query, outValue );
                if ( pAbility == nullptr )
                    return false;
                const ActiveEffectHandle handle = pAbility->findActiveEffectByID( hashed_string( string( query._key ).c_str() ) );
                outValue.setNumber( handle.isValid() ? static_cast<float64>( pAbility->getActiveEffectStackCount( handle ) ) : 0.0 );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( abilityAttribute, "Ability.Attribute", "Current value of the target's ability attribute (key = attribute name)", &AbilityWatchesInternal::readAttribute, true, true );
    SW_AUTOMATION_QUERY_WATCH( abilityHasTag, "Ability.HasTag", "1 when the target owns the tag (key, child tags match), else 0", &AbilityWatchesInternal::readHasTag, true, true );
    SW_AUTOMATION_QUERY_WATCH( abilityStackCount, "Ability.StackCount", "Stacks of the active effect (key = effect id) on the target; 0 when it is not applied", &AbilityWatchesInternal::readStackCount,
                               true, true );
} // namespace sw
