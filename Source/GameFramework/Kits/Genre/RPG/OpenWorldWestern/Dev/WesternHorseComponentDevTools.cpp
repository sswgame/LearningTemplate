/**
 * @file WesternHorseComponentDevTools.cpp
 * @brief `WesternHorseComponent` 의 시나리오 단계 · 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Automation/Scenario/IAutomationStepContext.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Component/WesternGameModeComponent.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Component/WesternHorseComponent.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Component/WesternHorseMountComponent.h"
#include "GameFramework/Runtime/Foundation/Save/StatefulComponentRegistrar.h"

namespace sw
{
    namespace
    {
        /** @brief 감시 값 `Western.Stamina` 입니다. 대상 말의 스태미나 비율(0..1)이고, 말을 열지 않았으면 값이 없습니다. */
        [[nodiscard]] bool readStamina( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            const WesternHorseComponent* pHorseComponent = query.findTargetObject() != nullptr ? query.findTargetObject()->getComponent<WesternHorseComponent>() : nullptr;
            const WesternHorse*          pHorse          = pHorseComponent != nullptr ? pHorseComponent->findHorse() : nullptr;
            if ( pHorse == nullptr )
            {
                outValue.setText( query.describeMissing( "opened Western Horse" ) );
                return false;
            }
            outValue.setNumber( static_cast<float64>( pHorse->getStamina().getRatio() ) );
            return true;
        }

        struct WesternHorseStepsDevInternal
        {
            static expected<void> validateFeedHorse( const AutomationStep& step ) { return step.hasRequiredAttributes( { "horse", "food" }, { "horse", "food" } ); }

            static bool runFeedHorse( IAutomationStepContext& context, const AutomationStep& step )
            {
                const string&               horseName = *step.findAttribute( "horse" );
                const string&               foodID    = *step.findAttribute( "food" );
                const expected<GameObject*> found     = AutomationTarget::find( context.findActiveObjectManager(), horseName );
                WesternHorseComponent*      pHorse    = found.has_value() ? ( *found )->getComponent<WesternHorseComponent>() : nullptr;
                if ( found.has_value() == false )
                    context.recordFailure( step, AutomationTarget::describe( found.error() ) );
                else if ( pHorse == nullptr )
                {
                    fixed_string<constant::buffer::kMax256> message;
                    formatstring( message.data(), message.capacity(), "object '%#' has no Western Horse", horseName );
                    context.recordFailure( step, message.view() );
                }
                else if ( pHorse->feed( hashed_string{ foodID.c_str() } ) == false )
                {
                    fixed_string<constant::buffer::kMax256> message;
                    formatstring( message.data(), message.capacity(), "'%#' did not take '%#' (horse not opened or food not in the western catalog)", horseName, foodID );
                    context.recordFailure( step, message.view() );
                }
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( westernStamina, "Western.Stamina", "Stamina ratio (0..1) of the western horse on target", &readStamina, true, false );
    // 키트 단계도 엔진 단계와 같은 레지스트리에 제 .cpp 가 올린다(언리얼 Functional Test 의 게임 쪽 액션과 같은 자리). 씬 틱 뒤에 돈다 — 다음 틱의 탈것이 회복한 스태미나를 본다.
    SW_AUTOMATION_STEP( westernFeedHorse, "FeedHorse", &WesternHorseStepsDevInternal::runFeedHorse, &WesternHorseStepsDevInternal::validateFeedHorse, AutomationStepOptions{} );
} // namespace sw
