#include "pch.h"

#include "GameFramework/Base/Actor/Control/ControlAutomationSteps.h"

#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/StringUtil.h"
#include "Core/String/hashed_string.h"

#include "Engine/Automation/AutomationRunner.h"
#include "Engine/Automation/AutomationScenario.h"
#include "Engine/Automation/AutomationStepRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Actor/Control/Controller/ControllerComponent.h"
#include "GameFramework/Base/Actor/Control/Controller/IntentTrackControllerComponent.h"
#include "GameFramework/Base/Actor/Control/Intent/ControlIntent.h"
#include "GameFramework/Base/Actor/Control/Pawn/PawnComponent.h"

namespace sw
{
    namespace
    {
        struct ControlAutomationStepsInternal
        {
            /** @brief `<Intent>` 이 폰마다 세우는 기록 조종자 오브젝트 이름의 앞머리입니다(뒤에 폰 이름). */
            static constexpr string_view kTrackControllerPrefix = "IntentTrack.";

            static bool checkAttributeNames( const AutomationStep& step, std::initializer_list<string_view> listAllowed, string& outError )
            {
                for ( const AutomationAttribute& attribute : step._listAttribute )
                {
                    bool bKnown = false;
                    for ( const string_view allowed : listAllowed )
                    {
                        bKnown = bKnown || string_view{ attribute._name } == allowed;
                    }
                    if ( bKnown == false )
                    {
                        outError = step.describe() + ": unknown attribute '" + attribute._name + "'";
                        return false;
                    }
                }
                return true;
            }

            static const string* requireAttribute( const AutomationStep& step, string_view name, string& outError )
            {
                const string* pValue = step.findAttribute( name );
                if ( pValue == nullptr )
                    outError = step.describe() + ": needs " + string( name ) + "=\"…\"";
                return pValue;
            }

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

            /** @brief `move="x,y"` 를 읽습니다(없으면 0,0). */
            [[nodiscard]] static bool readMove( const AutomationStep& step, float2& outMove, string& outError )
            {
                outMove              = float2{};
                const string* pValue = step.findAttribute( "move" );
                if ( pValue == nullptr )
                    return true;
                const string_view text  = *pValue;
                const size_t      comma = text.find( ',' );
                const bool        bParsed =
                    comma != string_view::npos && StringUtil::parseFloat( StringUtil::trim( text.substr( 0, comma ) ), outMove._x ) &&
                    StringUtil::parseFloat( StringUtil::trim( text.substr( comma + 1 ) ), outMove._y );
                if ( bParsed )
                    return true;
                outError = step.describe() + ": move must be \"x,y\", got '" + *pValue + "'";
                return false;
            }

            /** @brief `frames` 를 읽습니다(없으면 1, 1 이상). */
            [[nodiscard]] static bool readFrameCount( const AutomationStep& step, uint32& outFrameCount, string& outError )
            {
                outFrameCount        = 1;
                const string* pValue = step.findAttribute( "frames" );
                if ( pValue == nullptr )
                    return true;
                int32 value = 0;
                if ( StringUtil::parseInt( StringUtil::trim( string_view{ *pValue } ), value ) && value >= 1 )
                {
                    outFrameCount = static_cast<uint32>( value );
                    return true;
                }
                outError = step.describe() + ": frames must be a positive integer, got '" + *pValue + "'";
                return false;
            }

            /** @brief 이름의 오브젝트입니다. 없으면 실패를 적고 nullptr 입니다. */
            static GameObject* findObject( AutomationRunner& runner, const AutomationStep& step, const string& objectName )
            {
                GameObjectManager* pManager = runner.findActiveObjectManager();
                GameObject*        pObject  = pManager != nullptr ? pManager->findGameObjectByName( hashed_string( objectName ) ) : nullptr;
                if ( pObject == nullptr )
                    runner.recordFailure( step, pManager == nullptr ? string( "there is no active scene" ) : "no object named '" + objectName + "'" );
                return pObject;
            }

            static bool validateIntentStep( const AutomationStep& step, string& outError )
            {
                float32 value    = 0.0f;
                bool    bPresent = false;
                float2  move{};
                uint32  frameCount = 0;
                return checkAttributeNames( step, { "pawn", "move", "up", "yaw", "pitch", "buttons", "frames" }, outError ) &&
                       requireAttribute( step, "pawn", outError ) != nullptr && readMove( step, move, outError ) &&
                       readFloat( step, "up", value, bPresent, outError ) && readFloat( step, "yaw", value, bPresent, outError ) &&
                       readFloat( step, "pitch", value, bPresent, outError ) && readFrameCount( step, frameCount, outError );
            }

            /** @brief 폰을 기록 조종자로 `frames` 틱 동안 몰고 원래 조종자에게 돌려준다. 버튼은 첫 틱에 발동 + 내내 누름. */
            static bool runIntentStep( AutomationRunner& runner, const AutomationStep& step )
            {
                const string* pPawnName = step.findAttribute( "pawn" );
                GameObject*   pObject   = pPawnName != nullptr ? findObject( runner, step, *pPawnName ) : nullptr;
                if ( pObject == nullptr )
                    return pPawnName != nullptr;
                PawnComponent* pPawn = pObject->getComponent<PawnComponent>();
                if ( pPawn == nullptr )
                {
                    runner.recordFailure( step, "object '" + *pPawnName + "' has no pawn" );
                    return true;
                }
                string        error;
                ControlIntent intent{};
                float32       yaw        = 0.0f;
                float32       pitch      = 0.0f;
                bool          bHasYaw    = false;
                bool          bHasPitch  = false;
                bool          bHasUp     = false;
                uint32        frameCount = 1;
                const bool    bRead      = readMove( step, intent._move, error ) && readFloat( step, "up", intent._moveUp, bHasUp, error ) &&
                                   readFloat( step, "yaw", yaw, bHasYaw, error ) && readFloat( step, "pitch", pitch, bHasPitch, error ) &&
                                   readFrameCount( step, frameCount, error );
                if ( bRead == false )
                    return false;
                intent._controlYaw     = bHasYaw ? yaw : pPawn->getIntent()._controlYaw;
                intent._controlPitch   = bHasPitch ? pitch : pPawn->getIntent()._controlPitch;
                const string* pButtons = step.findAttribute( "buttons" );
                string_view   rest     = pButtons != nullptr ? string_view{ *pButtons } : string_view{};
                while ( rest.empty() == false )
                {
                    const size_t      comma      = rest.find( ',' );
                    const string_view buttonName = StringUtil::trim( rest.substr( 0, comma ) );
                    rest                         = comma == string_view::npos ? string_view{} : rest.substr( comma + 1 );
                    if ( buttonName.empty() )
                        continue;
                    const int32 buttonIndex = pPawn->findButton( hashed_string( buttonName ) );
                    if ( buttonIndex < 0 )
                        runner.recordFailure( step, "pawn '" + *pPawnName + "' has no button '" + string( buttonName ) + "'" );
                    else
                        intent.setButton( buttonIndex, true, false );
                }

                vector<ControlIntent> listIntent( frameCount, intent );
                listIntent[0]._buttonTriggered = intent._buttonDown;

                GameObjectManager& manager        = *pObject->getManager();
                const string       controllerName = string( kTrackControllerPrefix ) + *pPawnName;
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
                return checkAttributeNames( step, { "controller", "pawn" }, outError ) && requireAttribute( step, "controller", outError ) != nullptr;
            }

            /** @brief 조종자 오브젝트가 폰을 쥐게 한다(`pawn` 이 없으면 놓게). */
            static bool runPossessStep( AutomationRunner& runner, const AutomationStep& step )
            {
                const string* pControllerName = step.findAttribute( "controller" );
                GameObject*   pObject         = pControllerName != nullptr ? findObject( runner, step, *pControllerName ) : nullptr;
                if ( pObject == nullptr )
                    return pControllerName != nullptr;
                ControllerComponent* pController = pObject->getComponent<ControllerComponent>();
                if ( pController == nullptr )
                {
                    runner.recordFailure( step, "object '" + *pControllerName + "' has no controller" );
                    return true;
                }
                const string* pPawnName = step.findAttribute( "pawn" );
                if ( pPawnName == nullptr )
                {
                    pController->unpossess();
                    return true;
                }
                GameObject*    pPawnObject = findObject( runner, step, *pPawnName );
                PawnComponent* pPawn       = pPawnObject != nullptr ? pPawnObject->getComponent<PawnComponent>() : nullptr;
                if ( pPawn == nullptr )
                {
                    if ( pPawnObject != nullptr )
                        runner.recordFailure( step, "object '" + *pPawnName + "' has no pawn" );
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

namespace sw
{
    void ControlAutomationSteps::ensureLinked()
    {
    }
} // namespace sw
