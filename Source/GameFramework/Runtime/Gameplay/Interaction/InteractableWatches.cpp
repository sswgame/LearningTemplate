/**
 * @file InteractableWatches.cpp
 * @brief 상호작용 대상이 **쥔** 정의의 자동화 감시 값 `Interaction.HeldMaxDistance` · `Interaction.HeldDefinition` 입니다.
 * @details 공유 캐시가 든 표(`Interaction.MaxDistance` 처럼 캐시에서 찾는 값)가 아니라 쓰는 쪽이 쥔 객체를 읽는다 — 다시 읽기가 쓰는 쪽에 닿는지(시나리오 `editor/catalogshared`)와
 *          같은 경로를 쥔 둘이 한 벌을 나누는지를 본다.
 */
#include "pch.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Runtime/Gameplay/Interaction/InteractableComponent.h"
#include "GameFramework/Runtime/Gameplay/Interaction/InteractionCatalog.h"

namespace sw
{
    namespace
    {
        struct InteractableWatchesInternal
        {
            /** @brief 대상(target = 오브젝트 이름)의 상호작용 대상이 쥔 정의입니다. 없으면 까닭을 @p outValue 에 적고 nullptr 입니다. */
            [[nodiscard]] static const InteractionDef* findHeldDefinition( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject*            pTarget       = query.findTargetObject();
                const InteractableComponent* pInteractable = pTarget != nullptr ? pTarget->getComponent<InteractableComponent>() : nullptr;
                if ( pInteractable == nullptr )
                {
                    outValue.setText( pTarget == nullptr ? query.getTargetProblem() : query.describeMissing( "interactable" ) );
                    return nullptr;
                }
                const InteractionDef* pDef = pInteractable->getDefinition();
                if ( pDef == nullptr )
                    outValue.setText( "the interactable holds no definition" );
                return pDef;
            }

            /** @brief 감시 값 `Interaction.HeldMaxDistance` — 쥔 정의의 사거리(m)입니다. */
            [[nodiscard]] static bool readHeldMaxDistance( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const InteractionDef* pDef = findHeldDefinition( query, outValue );
                if ( pDef == nullptr )
                    return false;
                outValue.setNumber( pDef->_maxDistance );
                return true;
            }

            /** @brief 감시 값 `Interaction.HeldDefinition` — 쥔 정의 객체의 주소입니다. 같은 경로를 쥔 둘이 같은 값이면 한 벌을 나눈다(사본이면 다르다). */
            [[nodiscard]] static bool readHeldDefinition( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const InteractionDef* pDef = findHeldDefinition( query, outValue );
                if ( pDef == nullptr )
                    return false;
                // 사용자 공간 주소는 48 비트 안이라 배정밀도 수에 그대로 든다 — 같고 다름만 본다.
                outValue.setNumber( static_cast<float64>( reinterpret_cast<uintptr_t>( pDef ) ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( interactionHeldMaxDistance, "Interaction.HeldMaxDistance", "Max distance (m) of the interaction definition the interactable on target holds",
                               &InteractableWatchesInternal::readHeldMaxDistance, true, false );
    SW_AUTOMATION_QUERY_WATCH( interactionHeldDefinition, "Interaction.HeldDefinition", "Address of the interaction definition the interactable on target holds (equal on two holders = one shared copy)",
                               &InteractableWatchesInternal::readHeldDefinition, true, false );
} // namespace sw
