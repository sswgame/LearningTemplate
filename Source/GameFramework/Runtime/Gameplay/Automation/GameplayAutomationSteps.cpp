/**
 * @file GameplayAutomationSteps.cpp
 * @brief 자동화 시나리오의 영역을 넘는 게임 로직 단계입니다. 등록은 이 .cpp 의 정적 등록자가 합니다(Shipping 은 GameFramework 를 통째로 링크한다 — `sw_linkWholeArchive`).
 * @details - 게임 시간: `AdvanceGameTime`(minutes · hours · toHour) — 활성 씬의 판 상태(`SessionStateComponent`) 시계를 감는다.
 *          - 빙의 · 탑승: 단계 `Mount` · `Dismount`(`VehicleSeatComponent`). 빙의 단계 `Possess` 는 조종 층(`ControlAutomationSteps`)에 있다.
 *          - 오브젝트 대상(`rider` · `vehicle`)은 `AutomationTarget` 규칙으로 고른다 — 같은 이름이 여럿이면 `Name[N]`.
 *          감시 값은 각 영역 폴더의 자기 .cpp 가 등록한다: `Clock.*` · `Session.Money`(`SessionStateProbes.cpp`), `Quest.Status`(`QuestProbes.cpp`), `Schedule.*`(`ScheduleProbes.cpp`),
 *          `AI.*`(`AIControllerProbes.cpp`), `Pawn.Controller`(`PawnProbes.cpp`), `Pawn.Seat`(`VehicleProbes.cpp`). 이 파일에는 영역을 넘는 단계만 둔다.
 */
#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/StringUtil.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepContext.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/EnumName.h"

#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Gameplay/Automation/GameplayAutomationParams.h"
#include "GameFramework/Runtime/Gameplay/SessionState/SessionStateComponent.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/VehicleSeatComponent.h"
#include "GameFramework/Runtime/World/Environment/WorldClock.h"

namespace sw
{
    namespace
    {
        struct GameplayAutomationStepsInternal
        {
            /** @brief `AdvanceGameTime` 은 `minutes` · `hours` · `toHour`(다음 그 시각, 0..24) 중 정확히 하나를 받습니다. */
            static expected<void> validateAdvanceGameTime( const AutomationStep& step )
            {
                AdvanceGameTimeParams params;
                const expected<void>  read = step.readFields( params );
                if ( read.has_value() == false )
                    return read;
                const uint32 count = ( params._minutes >= 0.0f ? 1u : 0u ) + ( params._hours >= 0.0f ? 1u : 0u ) + ( params._toHour >= 0.0f ? 1u : 0u );
                if ( count == 1 )
                    return {};
                return unexpected( Error{ ErrorKind::InvalidArgument, "needs exactly one of minutes · hours · toHour" } );
            }

            /**
             * @brief 활성 씬의 판 상태마다 시계를 감는다. 지나간 시 · 날 경계는 알림으로 남아 다음 틱의 디렉터 · 일정이 받는다(잠 · 빨리 감기와 같은 길).
             * @details 씬 틱 뒤에 돌므로 같은 프레임의 틱은 감기 전 시각을 봤다. 감시 값은 감은 시각을 바로 보지만 몸(일정을 따르는 오브젝트)은 다음 틱에 옮겨진다.
             */
            static bool runAdvanceGameTime( AutomationStepContext& runner, const AutomationStep& step )
            {
                GameObjectManager*             pManager = runner.findActiveObjectManager();
                uint32                         count    = 0;
                const AdvanceGameTimeParams    params   = step.readValidatedFields<AdvanceGameTimeParams>();
                vector<SessionStateComponent*> listState;
                SessionStateComponent::collectOpen( pManager, listState );
                for ( SessionStateComponent* pState : listState )
                {
                    WorldClock& clock = pState->getClock();
                    if ( params._toHour >= 0.0f )
                        clock.advanceToHour( params._toHour );
                    else
                        clock.advanceMinutes( params._hours >= 0.0f ? params._hours * static_cast<float32>( constant::time::kSecondsPerHour / constant::time::kSecondsPerMinute ) : params._minutes );
                    ++count;
                }
                if ( count == 0 )
                    runner.recordFailure( step, "the active scene has no open SessionStateComponent (game clock)" );
                return true;
            }

            /** @brief 대상 고르기 규칙(`AutomationTarget` — `Name` · `Name[N]`)으로 찾은 오브젝트입니다. 없으면 실패를 적고 nullptr 입니다. */
            static GameObject* findObject( AutomationStepContext& runner, const AutomationStep& step, const string& objectName )
            {
                const expected<GameObject*> found = AutomationTarget::find( runner.findActiveObjectManager(), objectName );
                if ( found.has_value() == false )
                {
                    runner.recordFailure( step, AutomationTarget::describe( found.error() ) );
                    return nullptr;
                }
                return *found;
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
                const MountStepParams params      = step.readValidatedFields<MountStepParams>();
                const string&         riderName   = params._rider;
                const string&         vehicleName = params._vehicle;
                PawnComponent*        pRider      = findPawn( runner, step, riderName );
                GameObject*           pVehicle    = pRider != nullptr ? findObject( runner, step, vehicleName ) : nullptr;
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
                const MountResult result = VehicleSeatComponent::mount( *pRider, *pSeat );
                if ( result != MountResult::Mounted )
                    runner.recordFailure( step, "'" + riderName + "' could not mount '" + vehicleName + "': " + getEnumName( result ) );
                return true;
            }

            static bool runDismountStep( AutomationStepContext& runner, const AutomationStep& step )
            {
                const string   riderName = step.readValidatedFields<DismountStepParams>()._rider;
                PawnComponent* pRider    = findPawn( runner, step, riderName );
                if ( pRider != nullptr && VehicleSeatComponent::dismount( *pRider, false ) == false )
                    runner.recordFailure( step, "'" + riderName + "' is not seated" );
                return true;
            }
        };
    } // namespace

    // 시간 감기는 씬 틱 뒤에 돈다 — 감은 시각의 알림은 다음 틱의 디렉터 · 일정이 받는다.
    SW_AUTOMATION_STEP( worldAdvanceGameTime, "AdvanceGameTime", &GameplayAutomationStepsInternal::runAdvanceGameTime, &GameplayAutomationStepsInternal::validateAdvanceGameTime, AutomationStepOptions{} );

    // 탈것 단계는 씬 틱 뒤(틱 밖)에 돈다 — `VehicleSeatComponent` 은 틱 안에서 부르지 않는다.
    SW_AUTOMATION_STEP( controlMount, "Mount", &GameplayAutomationStepsInternal::runMountStep, &AutomationStep::validateFields<MountStepParams>, AutomationStepOptions{} );
    SW_AUTOMATION_STEP( controlDismount, "Dismount", &GameplayAutomationStepsInternal::runDismountStep, &AutomationStep::validateFields<DismountStepParams>, AutomationStepOptions{} );
} // namespace sw
