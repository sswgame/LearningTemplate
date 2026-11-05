/**
 * @file ControlAutomationSteps.cpp
 * @brief 자동화 시나리오의 행동 층 단계 `<Intent>` · `<Possess>` — 입력 맵 없이 폰에 의도를 넣고 빙의를 옮깁니다(형식은 `Source/GameFramework/README.md` 의 "조종 — 폰, 조종자, 의도").
 * @details 등록은 이 .cpp 의 정적 등록자가 한다(Shipping 은 GameFramework 를 통째로 링크한다 — `sw_linkWholeArchive`).
 */
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/hashed_string.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepContext.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Character/Control/ControlAutomationFields.h"
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
            [[nodiscard]] static bool readFloat( const AutomationStep& step, string_view name, float32& outValue, bool& outbPresent, string& outError )
            {
                const string* pValue = step.findAttribute( name );
                outbPresent          = pValue != nullptr;
                outValue             = 0.0f;
                if ( pValue == nullptr || StringUtil::parseFloat( StringUtil::trim( string_view{ *pValue } ), outValue ) )
                    return true;
                outError = step.describe() + ": " + string( name ) + " must be a number, got '" + *pValue + "'";
                return false;
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

            static bool validateIntentStep( const AutomationStep& step, string& outError )
            {
                ControlIntentStepFields fields;
                float32                 value    = 0.0f;
                bool                    bPresent = false;
                if ( step.readFields( fields, outError, { "yaw", "pitch" } ) == false || readFloat( step, "yaw", value, bPresent, outError ) == false ||
                     readFloat( step, "pitch", value, bPresent, outError ) == false )
                    return false;
                // 이동 길이는 1 이하다(`ControlIntent::_move`) — 선 · 파일은 넘는 값을 거절하므로 시나리오도 받지 않는다.
                if ( fields._move._x * fields._move._x + fields._move._y * fields._move._y <= 1.0f + MathUtil::kEpsilon )
                    return true;
                outError = step.describe() + ": move must have length at most 1";
                return false;
            }

            /** @brief 폰을 기록 조종자로 `frames` 틱 동안 몰고 원래 조종자에게 돌려준다. 버튼은 첫 틱에 발동 + 내내 누름. */
            static bool runIntentStep( AutomationStepContext& runner, const AutomationStep& step )
            {
                const ControlIntentStepFields fields  = step.readValidatedFields<ControlIntentStepFields>( { "yaw", "pitch" } );
                GameObject*                   pObject = findObject( runner, step, fields._pawn );
                if ( pObject == nullptr )
                    return true;
                PawnComponent* pPawn = pObject->getComponent<PawnComponent>();
                if ( pPawn == nullptr )
                {
                    runner.recordFailure( step, "object '" + fields._pawn + "' has no pawn" );
                    return true;
                }
                string        error;
                ControlIntent intent{};
                float32       yaw       = 0.0f;
                float32       pitch     = 0.0f;
                bool          bHasYaw   = false;
                bool          bHasPitch = false;
                if ( readFloat( step, "yaw", yaw, bHasYaw, error ) == false || readFloat( step, "pitch", pitch, bHasPitch, error ) == false )
                    return false;
                intent._move         = fields._move;
                intent._moveUp       = fields._up;
                intent._controlYaw   = bHasYaw ? yaw : pPawn->getIntent()._controlYaw;
                intent._controlPitch = bHasPitch ? pitch : pPawn->getIntent()._controlPitch;
                string_view rest     = fields._buttons;
                while ( rest.empty() == false )
                {
                    const size_t      comma      = rest.find( ',' );
                    const string_view buttonName = StringUtil::trim( rest.substr( 0, comma ) );
                    rest                         = comma == string_view::npos ? string_view{} : rest.substr( comma + 1 );
                    if ( buttonName.empty() )
                        continue;
                    const int32 buttonIndex = pPawn->findButton( hashed_string( buttonName ) );
                    if ( buttonIndex < 0 )
                        runner.recordFailure( step, "pawn '" + fields._pawn + "' has no button '" + string( buttonName ) + "'" );
                    else
                        intent.setButton( buttonIndex, true, false );
                }

                vector<ControlIntent> listIntent( fields._frames, intent );
                listIntent[0]._buttonTriggered = intent._buttonDown;

                GameObjectManager& manager        = *pObject->getManager();
                const string       controllerName = string( IntentPlayback::kControllerPrefix ) + fields._pawn;
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

            static bool validatePossessStep( const AutomationStep& step, string& outError )
            {
                ControlPossessStepFields fields;
                return step.readFields( fields, outError );
            }

            /** @brief 조종자 오브젝트가 폰을 쥐게 한다(`pawn` 이 없으면 놓게). */
            static bool runPossessStep( AutomationStepContext& runner, const AutomationStep& step )
            {
                const ControlPossessStepFields fields  = step.readValidatedFields<ControlPossessStepFields>();
                GameObject*                    pObject = findObject( runner, step, fields._controller );
                if ( pObject == nullptr )
                    return true;
                ControllerComponent* pController = pObject->getComponent<ControllerComponent>();
                if ( pController == nullptr )
                {
                    runner.recordFailure( step, "object '" + fields._controller + "' has no controller" );
                    return true;
                }
                if ( fields._pawn.empty() )
                {
                    pController->unpossess();
                    return true;
                }
                GameObject*    pPawnObject = findObject( runner, step, fields._pawn );
                PawnComponent* pPawn       = pPawnObject != nullptr ? pPawnObject->getComponent<PawnComponent>() : nullptr;
                if ( pPawn == nullptr )
                {
                    if ( pPawnObject != nullptr )
                        runner.recordFailure( step, "object '" + fields._pawn + "' has no pawn" );
                    return true;
                }
                pController->possess( *pPawn );
                return true;
            }
        };
    } // namespace

    // 행동 층 단계는 그 프레임의 입력 재생 · 조종 시스템 전에 돈다 — 그 프레임의 의도부터 바뀐다.
    SW_AUTOMATION_STEP( controlIntent, "Intent", &ControlAutomationStepsInternal::runIntentStep, &ControlAutomationStepsInternal::validateIntentStep, true );
    SW_AUTOMATION_STEP( controlPossess, "Possess", &ControlAutomationStepsInternal::runPossessStep, &ControlAutomationStepsInternal::validatePossessStep, true );
} // namespace sw
