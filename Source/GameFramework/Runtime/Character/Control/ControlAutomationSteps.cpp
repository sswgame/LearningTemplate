/**
 * @file ControlAutomationSteps.cpp
 * @brief 자동화 시나리오의 행동 층 단계 `<Intent>` · `<Possess>` — 입력 맵 없이 폰에 의도를 넣고 빙의를 옮깁니다.
 * @details 등록은 이 .cpp 의 정적 등록자가 한다(Shipping 은 GameFramework 를 통째로 링크한다 — `sw_link_whole_archive`).
 */
#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/Radians.h"
#include "Core/String/Name/hashed_string.h"
#include "Core/String/StringUtil.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Automation/Scenario/IAutomationStepContext.h"
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
            /** @brief 있으면 각(글은 도 — 데이터 파일의 `Radians` 칸과 같다)으로 읽습니다. 속성이 없으면 빈 값, 형식이 틀리면 그 글이 대상인 오류입니다. */
            [[nodiscard]] static expected<optional<Radians>> readAngle( const AutomationStep& step, string_view name )
            {
                const string* pValue = step.findAttribute( name );
                if ( pValue == nullptr )
                    return optional<Radians>{};
                float32 value = 0.0f;
                if ( StringUtil::parseFloat( StringUtil::trim( string_view{ *pValue } ), value ) )
                    return optional<Radians>{ Radians::fromDegrees( value ) };
                return unexpected( Error{ ErrorKind::InvalidArgument, "yaw and pitch must be numbers", *pValue } );
            }

            /** @brief 이름의 오브젝트입니다. 없으면 실패를 적고 nullptr 입니다. */
            static GameObject* findObject( IAutomationStepContext& runner, const AutomationStep& step, const string& objectName )
            {
                GameObjectManager* pManager = runner.findActiveObjectManager();
                GameObject*        pObject  = pManager != nullptr ? pManager->findGameObjectByName( hashed_string( objectName ) ) : nullptr;
                if ( pObject == nullptr && pManager == nullptr )
                    runner.recordFailure( step, "there is no active scene" );
                else if ( pObject == nullptr )
                {
                    fixed_string<constant::buffer::kMax256> message;
                    formatstring( message.data(), message.capacity(), "no object named '%#'", objectName );
                    runner.recordFailure( step, message.view() );
                }
                return pObject;
            }

            static expected<void> validateIntentStep( const AutomationStep& step )
            {
                ControlIntentStepParams params;
                const expected<void>    read = step.readProperties( params, { "yaw", "pitch" } );
                if ( read.has_value() == false )
                    return read;
                for ( const string_view name : { string_view{ "yaw" }, string_view{ "pitch" } } )
                {
                    const expected<optional<Radians>> angle = readAngle( step, name );
                    if ( angle.has_value() == false )
                        return unexpected( angle.error() );
                }
                // 이동 길이는 1 이하다(`ControlIntent::_move`) — 선 · 파일은 넘는 값을 거절하므로 시나리오도 받지 않는다.
                if ( params._move._x * params._move._x + params._move._y * params._move._y <= 1.0f + MathUtil::kEpsilon )
                    return {};
                return unexpected( Error{ ErrorKind::InvalidArgument, "move must have length at most 1" } );
            }

            /** @brief 폰을 기록 조종자로 `frames` 틱 동안 몰고 원래 조종자에게 돌려준다. 버튼은 첫 틱에 발동 + 내내 누름. */
            static bool runIntentStep( IAutomationStepContext& runner, const AutomationStep& step )
            {
                const ControlIntentStepParams params  = step.readValidatedProperties<ControlIntentStepParams>( { "yaw", "pitch" } );
                GameObject*                   pObject = findObject( runner, step, params._pawn );
                if ( pObject == nullptr )
                    return true;
                PawnComponent* pPawn = pObject->getComponent<PawnComponent>();
                if ( pPawn == nullptr )
                {
                    fixed_string<constant::buffer::kMax256> message;
                    formatstring( message.data(), message.capacity(), "object '%#' has no pawn", params._pawn );
                    runner.recordFailure( step, message.view() );
                    return true;
                }
                ControlIntent                     intent{};
                const expected<optional<Radians>> yaw   = readAngle( step, "yaw" );
                const expected<optional<Radians>> pitch = readAngle( step, "pitch" );
                if ( yaw.has_value() == false || pitch.has_value() == false )
                    return false;
                intent._move         = params._move;
                intent._moveUp       = params._up;
                intent._controlYaw   = yaw->value_or( Radians::fromRadians( pPawn->getIntent()._controlYaw ) ).radians(); // 적지 않으면 지금 바라보는 쪽을 둔다
                intent._controlPitch = pitch->value_or( Radians::fromRadians( pPawn->getIntent()._controlPitch ) ).radians();
                string_view rest     = params._buttons;
                while ( rest.empty() == false )
                {
                    const size_t      comma      = rest.find( ',' );
                    const string_view buttonName = StringUtil::trim( rest.substr( 0, comma ) );
                    rest                         = comma == string_view::npos ? string_view{} : rest.substr( comma + 1 );
                    if ( buttonName.empty() )
                        continue;
                    const optional<int32> buttonIndex = pPawn->findButton( hashed_string( buttonName ) );
                    if ( buttonIndex.has_value() == false )
                    {
                        fixed_string<constant::buffer::kMax256> message;
                        formatstring( message.data(), message.capacity(), "pawn '%#' has no button '%#'", params._pawn, buttonName );
                        runner.recordFailure( step, message.view() );
                    }
                    else
                        intent.setButton( buttonIndex, true );
                }

                vector<ControlIntent> listIntent( params._frames, intent );
                for ( int32 buttonIndex = 0; buttonIndex < ControlIntent::kButtonCount; ++buttonIndex )
                {
                    if ( intent.isDown( buttonIndex ) )
                        listIntent[0].addTrigger( buttonIndex, intent._controlYaw, intent._controlPitch, ControlViewTick::makePresent() ); // 기록 조종자가 낼 때의 시각으로 바꾼다
                }

                IntentTrackControllerComponent* pTrack = IntentPlayback::getOrCreateTrackController( *pObject->getMutableManager(), params._pawn );
                if ( pTrack == nullptr )
                    return false;
                pTrack->play( *pPawn, listIntent, true );
                return true;
            }

            /** @brief 조종자 오브젝트가 폰을 쥐게 한다(`pawn` 이 없으면 놓게). */
            static bool runPossessStep( IAutomationStepContext& runner, const AutomationStep& step )
            {
                const ControlPossessStepParams params  = step.readValidatedProperties<ControlPossessStepParams>();
                GameObject*                    pObject = findObject( runner, step, params._controller );
                if ( pObject == nullptr )
                    return true;
                ControllerComponent* pController = pObject->getComponent<ControllerComponent>();
                if ( pController == nullptr )
                {
                    fixed_string<constant::buffer::kMax256> message;
                    formatstring( message.data(), message.capacity(), "object '%#' has no controller", params._controller );
                    runner.recordFailure( step, message.view() );
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
                    {
                        fixed_string<constant::buffer::kMax256> message;
                        formatstring( message.data(), message.capacity(), "object '%#' has no pawn", params._pawn );
                        runner.recordFailure( step, message.view() );
                    }
                    return true;
                }
                pController->possess( *pPawn );
                return true;
            }
        };
    } // namespace

    // 행동 층 단계는 그 프레임의 입력 재생 · 조종 시스템 전에 돈다 — 그 프레임의 의도부터 바뀐다.
    SW_AUTOMATION_STEP( controlIntent, "Intent", &ControlAutomationStepsInternal::runIntentStep, &ControlAutomationStepsInternal::validateIntentStep, AutomationStepOptions{}.withBeforeInput() );
    SW_AUTOMATION_STEP( controlPossess, "Possess", &ControlAutomationStepsInternal::runPossessStep, &AutomationStep::validateProperties<ControlPossessStepParams>, AutomationStepOptions{}.withBeforeInput() );
} // namespace sw
