/**
 * @file ControlAutomationSteps.cpp
 * @brief 자동화 시나리오의 행동 층 단계 `<Intent>` · `<Possess>` — 입력 맵 없이 폰에 의도를 넣고 빙의를 옮깁니다(형식은 `Source/GameFramework/README.md` 의 "조종 — 폰, 조종자, 의도").
 * @details 등록은 이 .cpp 의 정적 등록자가 한다(Shipping 은 GameFramework 를 통째로 링크한다 — `sw_linkWholeArchive`).
 */
#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"
#include "Core/Name/hashed_string.h"
#include "Core/String/StringUtil.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepContext.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Character/Control/ControlAutomationParams.h"
#include "GameFramework/Runtime/Character/Control/Controller/ControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Controller/IntentPlayback.h"
#include "GameFramework/Runtime/Character/Control/Controller/IntentTrackControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"

namespace sw
{
    namespace
    {
        struct ControlAutomationStepsInternal
        {
            /** @brief 있으면 수로 읽습니다. @p outbPresent 는 속성이 있었는가입니다. 형식이 틀리면 false 와 이유. */
            [[nodiscard]] static expected<void> readFloat( const AutomationStep& step, string_view name, float32& outValue, bool& outbPresent )
            {
                const string* pValue = step.findAttribute( name );
                outbPresent          = pValue != nullptr;
                outValue             = 0.0f;
                if ( pValue == nullptr || StringUtil::parseFloat( StringUtil::trim( string_view{ *pValue } ), outValue ) )
                    return {};
                Error error{ ErrorKind::InvalidArgument, "the attribute must be a number", name };
                error.appendDetail( " = '" ).appendDetail( *pValue ).appendDetail( "'" );
                return unexpected( error );
            }

            /** @brief 이름의 오브젝트입니다. 없으면 실패를 적고 nullptr 입니다. */
            static GameObject* findObject( AutomationStepContext& runner, const AutomationStep& step, const string& objectName )
            {
                GameObjectManager* pManager = runner.findActiveObjectManager();
                GameObject*        pObject  = pManager != nullptr ? pManager->findGameObjectByName( hashed_string( objectName ) ) : nullptr;
                if ( pObject == nullptr )
                    runner.recordFailure( step, pManager == nullptr ? string( "there is no active scene" ) : "no object named '" + objectName + "'" );
                return pObject;
            }

            // 검사 함수의 모양(bool + outError)은 엔진의 `AutomationStepValidateFunction` 이 정한다 — 그 형식이 옮겨지면 함께 옮긴다.
            static bool validateIntentStep( const AutomationStep& step, string& outError )
            {
                ControlIntentStepParams params;
                if ( step.readFields( params, outError, { "yaw", "pitch" } ) == false )
                    return false;
                float32              value    = 0.0f;
                bool                 bPresent = false;
                const expected<void> numbers  = readFloat( step, "yaw", value, bPresent ).and_then( [&]
                { return readFloat( step, "pitch", value, bPresent ); } );
                if ( numbers.has_value() == false )
                {
                    outError = step.describe() + ": " + string( numbers.error().message() ) + " (" + string( numbers.error().detail() ) + ")";
                    return false;
                }
                // 이동 길이는 1 이하다(`ControlIntent::_move`) — 선 · 파일은 넘는 값을 거절하므로 시나리오도 받지 않는다.
                if ( params._move._x * params._move._x + params._move._y * params._move._y <= 1.0f + MathUtil::kEpsilon )
                    return true;
                outError = step.describe() + ": move must have length at most 1";
                return false;
            }

            /** @brief 폰을 기록 조종자로 `frames` 틱 동안 몰고 원래 조종자에게 돌려준다. 버튼은 첫 틱에 발동 + 내내 누름. */
            static bool runIntentStep( AutomationStepContext& runner, const AutomationStep& step )
            {
                const ControlIntentStepParams params  = step.readValidatedFields<ControlIntentStepParams>( { "yaw", "pitch" } );
                GameObject*                   pObject = findObject( runner, step, params._pawn );
                if ( pObject == nullptr )
                    return true;
                PawnComponent* pPawn = pObject->getComponent<PawnComponent>();
                if ( pPawn == nullptr )
                {
                    runner.recordFailure( step, "object '" + params._pawn + "' has no pawn" );
                    return true;
                }
                ControlIntent intent{};
                float32       yaw       = 0.0f;
                float32       pitch     = 0.0f;
                bool          bHasYaw   = false;
                bool          bHasPitch = false;
                if ( readFloat( step, "yaw", yaw, bHasYaw ).and_then( [&]
                { return readFloat( step, "pitch", pitch, bHasPitch ); } )
                         .has_value() == false )
                    return false; // 검사(`validateIntentStep`)가 같은 읽기를 이미 통과시켰다
                intent._move         = params._move;
                intent._moveUp       = params._up;
                intent._controlYaw   = bHasYaw ? yaw : pPawn->getIntent()._controlYaw;
                intent._controlPitch = bHasPitch ? pitch : pPawn->getIntent()._controlPitch;
                string_view rest     = params._buttons;
                while ( rest.empty() == false )
                {
                    const size_t      comma      = rest.find( ',' );
                    const string_view buttonName = StringUtil::trim( rest.substr( 0, comma ) );
                    rest                         = comma == string_view::npos ? string_view{} : rest.substr( comma + 1 );
                    if ( buttonName.empty() )
                        continue;
                    const int32 buttonIndex = pPawn->findButton( hashed_string( buttonName ) );
                    if ( buttonIndex < 0 )
                        runner.recordFailure( step, "pawn '" + params._pawn + "' has no button '" + string( buttonName ) + "'" );
                    else
                        intent.setButton( buttonIndex, true, false );
                }

                vector<ControlIntent> listIntent( params._frames, intent );
                listIntent[0]._buttonTriggered = intent._buttonDown;

                GameObjectManager& manager        = *pObject->getManager();
                const string       controllerName = string( IntentPlayback::kControllerPrefix ) + params._pawn;
                GameObject*        pTrackObject   = manager.findGameObjectByName( hashed_string( controllerName ) );
                if ( pTrackObject == nullptr )
                    pTrackObject = manager.createGameObject( hashed_string( controllerName ) );
                IntentTrackControllerComponent* pTrack = pTrackObject != nullptr ? pTrackObject->getComponent<IntentTrackControllerComponent>() : nullptr;
                if ( pTrack == nullptr && pTrackObject != nullptr )
                    pTrack = pTrackObject->addComponent<IntentTrackControllerComponent>();
                if ( pTrack == nullptr )
                    return false;
                pTrack->play( *pPawn, listIntent, true );
                return true;
            }

            /** @brief 조종자 오브젝트가 폰을 쥐게 한다(`pawn` 이 없으면 놓게). */
            static bool runPossessStep( AutomationStepContext& runner, const AutomationStep& step )
            {
                const ControlPossessStepParams params  = step.readValidatedFields<ControlPossessStepParams>();
                GameObject*                    pObject = findObject( runner, step, params._controller );
                if ( pObject == nullptr )
                    return true;
                ControllerComponent* pController = pObject->getComponent<ControllerComponent>();
                if ( pController == nullptr )
                {
                    runner.recordFailure( step, "object '" + params._controller + "' has no controller" );
                    return true;
                }
                if ( params._pawn.empty() )
                {
                    pController->unpossess();
                    return true;
                }
                GameObject*    pPawnObject = findObject( runner, step, params._pawn );
                PawnComponent* pPawn       = pPawnObject != nullptr ? pPawnObject->getComponent<PawnComponent>() : nullptr;
                if ( pPawn == nullptr )
                {
                    if ( pPawnObject != nullptr )
                        runner.recordFailure( step, "object '" + params._pawn + "' has no pawn" );
                    return true;
                }
                pController->possess( *pPawn );
                return true;
            }
        };
    } // namespace

    // 행동 층 단계는 그 프레임의 입력 재생 · 조종 시스템 전에 돈다 — 그 프레임의 의도부터 바뀐다.
    SW_AUTOMATION_STEP( controlIntent, "Intent", &ControlAutomationStepsInternal::runIntentStep, &ControlAutomationStepsInternal::validateIntentStep, AutomationStepOptions{}.withBeforeInput() );
    SW_AUTOMATION_STEP( controlPossess, "Possess", &ControlAutomationStepsInternal::runPossessStep, &AutomationStep::validateFields<ControlPossessStepParams>, AutomationStepOptions{}.withBeforeInput() );
} // namespace sw
