/**
 * @file GameplayAutomationSteps.cpp
 * @brief 자동화 시나리오의 영역을 넘는 게임 로직 단계입니다. 등록은 이 .cpp 의 정적 등록자가 합니다(Shipping 은 GameFramework 를 통째로 링크한다 — `sw_linkWholeArchive`).
 * @details - 게임 시간: `AdvanceGameTime`(minutes · hours · toHour) — 활성 씬의 판 상태(`SessionStateComponent`) 시계를 감는다.
 *          - 빙의 · 탑승: 단계 `Mount` · `Dismount`(`MountUtil`). 빙의 단계 `Possess` 는 조종 층(`ControlAutomationSteps`)에 있다.
 *          - 오브젝트 대상(`rider` · `vehicle`)은 `AutomationTarget` 규칙으로 고른다 — 같은 이름이 여럿이면 `Name[N]`.
 *          탐침은 각 영역 폴더의 자기 .cpp 가 등록한다: `Clock.*` · `Session.Money`(`SessionStateProbes.cpp`), `Quest.Status`(`QuestProbes.cpp`), `Schedule.*`(`ScheduleProbes.cpp`),
 *          `AI.*`(`AIControllerProbes.cpp`), `Pawn.Controller`(`PawnProbes.cpp`), `Pawn.Seat`(`VehicleProbes.cpp`). 이 파일에는 영역을 넘는 단계만 둔다.
 */
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Automation/Probe/AutomationProbe.h"
#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepContext.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Actor/Control/Pawn/PawnComponent.h"
#include "GameFramework/Base/Gameplay/SessionState/SessionStateComponent.h"
#include "GameFramework/Base/Gameplay/Vehicle/MountUtil.h"
#include "GameFramework/Base/Gameplay/Vehicle/VehicleSeatComponent.h"
#include "GameFramework/Base/World/Environment/WorldClock.h"

namespace sw
{
    namespace
    {
        struct GameplayAutomationStepsInternal
        {
            /** @brief 속성 @p name 을 음이 아닌 수로 읽습니다. 없으면 true 이고 @p outbPresent 가 false 입니다. */
            [[nodiscard]] static bool readNonNegative( const AutomationStep& step, string_view name, float32& outValue, bool& outbPresent, string& outError )
            {
                const string* pValue = step.findAttribute( name );
                outbPresent          = pValue != nullptr;
                outValue             = 0.0f;
                if ( pValue == nullptr )
                    return true;
                if ( StringUtil::parseFloat( StringUtil::trim( string_view{ *pValue } ), outValue ) && outValue >= 0.0f )
                    return true;
                outError = step.describe() + ": " + string( name ) + " must be a non-negative number, got '" + *pValue + "'";
                return false;
            }

            /** @brief `AdvanceGameTime` 은 `minutes` · `hours` · `toHour`(다음 그 시각, 0..24) 중 정확히 하나를 받습니다. */
            static bool validateAdvanceGameTime( const AutomationStep& step, string& outError )
            {
                float32    value    = 0.0f;
                bool       bMinutes = false;
                bool       bHours   = false;
                bool       bToHour  = false;
                const bool bRead    = step.hasOnlyAttributes( { "minutes", "hours", "toHour" }, outError ) && readNonNegative( step, "minutes", value, bMinutes, outError ) &&
                                   readNonNegative( step, "hours", value, bHours, outError ) && readNonNegative( step, "toHour", value, bToHour, outError );
                if ( bRead == false )
                    return false;
                const uint32 count = ( bMinutes ? 1u : 0u ) + ( bHours ? 1u : 0u ) + ( bToHour ? 1u : 0u );
                if ( count == 1 )
                    return true;
                outError = step.describe() + ": needs exactly one of minutes · hours · toHour";
                return false;
            }

            /**
             * @brief 활성 씬의 판 상태마다 시계를 감는다. 지나간 시 · 날 경계는 알림으로 남아 다음 틱의 디렉터 · 일정이 받는다(잠 · 빨리 감기와 같은 길).
             * @details 씬 틱 뒤에 돌므로 같은 프레임의 틱은 감기 전 시각을 봤다. 탐침은 감은 시각을 바로 보지만 몸(일정을 따르는 오브젝트)은 다음 틱에 옮겨진다.
             */
            static bool runAdvanceGameTime( AutomationStepContext& runner, const AutomationStep& step )
            {
                GameObjectManager* pManager = runner.findActiveObjectManager();
                uint32             count    = 0;
                string             error;
                float32            minutes  = 0.0f;
                float32            hours    = 0.0f;
                float32            toHour   = 0.0f;
                bool               bMinutes = false;
                bool               bHours   = false;
                bool               bToHour  = false;
                const bool         bRead    = readNonNegative( step, "minutes", minutes, bMinutes, error ) && readNonNegative( step, "hours", hours, bHours, error ) &&
                                   readNonNegative( step, "toHour", toHour, bToHour, error );
                if ( bRead == false )
                {
                    runner.recordFailure( step, error ); // 검사 뒤 단계가 바뀌었다(핫 리로드)
                    return true;
                }
                vector<SessionStateComponent*> listState;
                SessionStateComponent::collectOpen( pManager, listState );
                for ( SessionStateComponent* pState : listState )
                {
                    WorldClock& clock = pState->getClock();
                    if ( bToHour )
                        clock.advanceToHour( toHour );
                    else
                        clock.advanceMinutes( bHours ? hours * static_cast<float32>( constant::kSecondsPerHour / constant::kSecondsPerMinute ) : minutes );
                    ++count;
                }
                if ( count == 0 )
                    runner.recordFailure( step, "the active scene has no open SessionStateComponent (game clock)" );
                return true;
            }

            /** @brief 대상 고르기 규칙(`AutomationTarget` — `Name` · `Name[N]`)으로 찾은 오브젝트입니다. 없으면 실패를 적고 nullptr 입니다. */
            static GameObject* findObject( AutomationStepContext& runner, const AutomationStep& step, const string& objectName )
            {
                AutomationTargetLookup lookup = AutomationTargetLookup::NotGiven;
                string                 problem;
                GameObject*            pObject = AutomationTarget::find( runner.findActiveObjectManager(), objectName, lookup, problem );
                if ( pObject == nullptr )
                    runner.recordFailure( step, problem );
                return pObject;
            }

            static bool validateMountStep( const AutomationStep& step, string& outError )
            {
                return step.hasOnlyAttributes( { "rider", "vehicle" }, outError ) && step.requireAttribute( "rider", outError ) != nullptr &&
                       step.requireAttribute( "vehicle", outError ) != nullptr;
            }

            static bool validateDismountStep( const AutomationStep& step, string& outError )
            {
                return step.hasOnlyAttributes( { "rider" }, outError ) && step.requireAttribute( "rider", outError ) != nullptr;
            }

            static const utf8* toText( MountResult result )
            {
                switch ( result )
                {
                    case MountResult::Mounted:
                        return "Mounted";
                    case MountResult::SeatTaken:
                        return "SeatTaken";
                    case MountResult::TooFar:
                        return "TooFar";
                    case MountResult::NoSocket:
                        return "NoSocket";
                    case MountResult::NotPossessed:
                        return "NotPossessed";
                    case MountResult::NoVehiclePawn:
                        return "NoVehiclePawn";
                }
                return "Unknown";
            }

            /** @brief 이름의 오브젝트의 폰입니다. 없으면 실패를 적고 nullptr 입니다. */
            static PawnComponent* findPawn( AutomationStepContext& runner, const AutomationStep& step, const string& objectName )
            {
                GameObject*    pObject = findObject( runner, step, objectName );
                PawnComponent* pPawn   = pObject != nullptr ? pObject->getComponent<PawnComponent>() : nullptr;
                if ( pObject != nullptr && pPawn == nullptr )
                    runner.recordFailure( step, "object '" + objectName + "' has no pawn" );
                return pPawn;
            }

            /** @brief 탑승자 폰을 탈것 오브젝트의 빈 좌석(운전석 먼저)에 태운다. 탈 수 없으면 그 까닭(`TooFar` 등)을 실패로 적는다. */
            static bool runMountStep( AutomationStepContext& runner, const AutomationStep& step )
            {
                const string&  riderName   = *step.findAttribute( "rider" );
                const string&  vehicleName = *step.findAttribute( "vehicle" );
                PawnComponent* pRider      = findPawn( runner, step, riderName );
                GameObject*    pVehicle    = pRider != nullptr ? findObject( runner, step, vehicleName ) : nullptr;
                if ( pVehicle == nullptr )
                    return true;
                VehicleSeatComponent* pSeat = nullptr;
                pVehicle->forEachComponentOfType<VehicleSeatComponent>( [&pSeat]( VehicleSeatComponent* pCandidate )
                {
                    const bool bFree = pCandidate->getOccupant().isValid() == false;
                    if ( bFree && ( pSeat == nullptr || pCandidate->isDriverSeat() ) )
                        pSeat = pCandidate;
                } );
                if ( pSeat == nullptr )
                {
                    runner.recordFailure( step, "vehicle '" + vehicleName + "' has no free seat" );
                    return true;
                }
                const MountResult result = MountUtil::mount( *pRider, *pSeat );
                if ( result != MountResult::Mounted )
                    runner.recordFailure( step, "'" + riderName + "' could not mount '" + vehicleName + "': " + toText( result ) );
                return true;
            }

            static bool runDismountStep( AutomationStepContext& runner, const AutomationStep& step )
            {
                const string&  riderName = *step.findAttribute( "rider" );
                PawnComponent* pRider    = findPawn( runner, step, riderName );
                if ( pRider != nullptr && MountUtil::dismount( *pRider, false ) == false )
                    runner.recordFailure( step, "'" + riderName + "' is not seated" );
                return true;
            }
        };
    } // namespace

    // 시간 감기는 씬 틱 뒤에 돈다 — 감은 시각의 알림은 다음 틱의 디렉터 · 일정이 받는다.
    SW_AUTOMATION_STEP( worldAdvanceGameTime, "AdvanceGameTime", &GameplayAutomationStepsInternal::runAdvanceGameTime, &GameplayAutomationStepsInternal::validateAdvanceGameTime, false );

    // 탈것 단계는 씬 틱 뒤(틱 밖)에 돈다 — `MountUtil` 은 틱 안에서 부르지 않는다.
    SW_AUTOMATION_STEP( controlMount, "Mount", &GameplayAutomationStepsInternal::runMountStep, &GameplayAutomationStepsInternal::validateMountStep, false );
    SW_AUTOMATION_STEP( controlDismount, "Dismount", &GameplayAutomationStepsInternal::runDismountStep, &GameplayAutomationStepsInternal::validateDismountStep, false );
} // namespace sw
