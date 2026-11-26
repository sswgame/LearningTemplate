/**
 * @file InteractableWatches.cpp
 * @brief 상호작용 대상이 쥔 정의의 자동화 감시 값 `Interaction.HeldMaxDistance` · `Interaction.HeldDefinition` 과 하는 쪽의 `Interaction.Focus` · `Interaction.StepMode` · `Interaction.ReactionOpen` 입니다.
 * @details 캐시에서 표를 찾지 않고 쓰는 쪽이 쥔 객체를 읽는다 — 캐시를 새로 읽는 감시는 다시 읽기가 쥔 쪽에 닿지 않아도 새 값을 보여 거짓 통과가 된다.
 *          다시 읽기 · 연쇄가 쓰는 쪽에 닿는지(시나리오 `editor/catalogshared` · `editor/catalogcascade`)와 같은 경로를 쥔 둘이 한 벌을 나누는지를 본다.
 */
#include "pch.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/EnumName.h"

#include "GameFramework/Runtime/Gameplay/Interaction/InteractableComponent.h"
#include "GameFramework/Runtime/Gameplay/Interaction/InteractionCatalog.h"
#include "GameFramework/Runtime/Gameplay/Interaction/InteractorComponent.h"

namespace sw
{
    namespace
    {
        struct InteractableWatchesInternal
        {
            /** @brief 대상(target = 오브젝트 이름)의 상호작용 대상이 쥔 정의입니다. 없으면 까닭을 @p outValue 에 적고 nullptr 입니다. */
            [[nodiscard]] static const InteractionDef* findHeldDefinition( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const InteractableComponent* pInteractable = query.findTargetComponent<InteractableComponent>( "interactable", outValue );
                if ( pInteractable == nullptr )
                    return nullptr;
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

            /** @brief 감시 값 `Interaction.Focus` — 하는 쪽(target)이 고른 대상 오브젝트의 이름이고, 없으면 `none` 입니다. */
            [[nodiscard]] static bool readFocus( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const InteractorComponent* pInteractor = query.findTargetComponent<InteractorComponent>( "interactor", outValue );
                if ( pInteractor == nullptr )
                    return false;
                const GameObject* pFocus = query._pManager != nullptr ? query._pManager->resolveGameObject( pInteractor->getFocus() ) : nullptr;
                outValue.setText( pFocus != nullptr ? string_view{ pFocus->getName().c_str() } : string_view{ "none" } );
                return true;
            }

            /** @brief 감시 값 `Interaction.StepMode` — 하는 쪽(target)이 진행 중인 단계의 입력 방식이고, 진행 중이 아니면 `none` 입니다. */
            [[nodiscard]] static bool readStepMode( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const InteractorComponent* pInteractor = query.findTargetComponent<InteractorComponent>( "interactor", outValue );
                if ( pInteractor == nullptr )
                    return false;
                const InteractionStepDef* pStep = pInteractor->getSession().getStep();
                outValue.setText( pStep != nullptr ? string_view{ getEnumName( pStep->_mode ) } : string_view{ "none" } );
                return true;
            }

            /** @brief 감시 값 `Interaction.ReactionOpen` — 하는 쪽(target)의 반응 단계가 열려 있으면 1 입니다. */
            [[nodiscard]] static bool readReactionOpen( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const InteractorComponent* pInteractor = query.findTargetComponent<InteractorComponent>( "interactor", outValue );
                if ( pInteractor == nullptr )
                    return false;
                outValue.setNumber( pInteractor->getSession().isReactionOpen() ? 1.0 : 0.0 );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( interactionFocus, "Interaction.Focus", "Name of the object the interactor on target focuses, none without one", &InteractableWatchesInternal::readFocus, true, false );
    SW_AUTOMATION_QUERY_WATCH( interactionStepMode, "Interaction.StepMode", "Input mode of the interaction step the interactor on target is in, none when idle", &InteractableWatchesInternal::readStepMode, true, false );
    SW_AUTOMATION_QUERY_WATCH( interactionReactionOpen, "Interaction.ReactionOpen", "1 while the reaction step of the interactor on target is open (press now)",
                               &InteractableWatchesInternal::readReactionOpen, true, false );
    SW_AUTOMATION_QUERY_WATCH( interactionHeldMaxDistance, "Interaction.HeldMaxDistance", "Max distance (m) of the interaction definition the interactable on target holds",
                               &InteractableWatchesInternal::readHeldMaxDistance, true, false );
    SW_AUTOMATION_QUERY_WATCH( interactionHeldDefinition, "Interaction.HeldDefinition", "Address of the interaction definition the interactable on target holds (equal on two holders = one shared copy)",
                               &InteractableWatchesInternal::readHeldDefinition, true, false );
} // namespace sw
