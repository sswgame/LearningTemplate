/**
 * @file VehicleWatches.cpp
 * @brief 탈것의 자동화 감시 값입니다. `Pawn.Seat` — 폰이 어느 탈것에 앉았는지는 탈것 층(`VehicleSeatComponent`)이 알고, 조종 층(폰)은 탈것을 보지 않으므로 이 폴더에 둔다.
 *        `Mount.*` 는 걷는 탈것, `Vehicle.*` 는 바퀴 탈것(물리 차 · 아케이드 차)의 값이다.
 */
#include "pch.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Physics/Component/WheeledVehicleComponent.h"

#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/ArcadeVehicleComponent.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/MountMovementComponent.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/VehicleSeatComponent.h"

namespace sw
{
    namespace
    {
        /** @brief 감시 값 `Pawn.Seat` 입니다. 폰이 앉은 좌석의 탈것 오브젝트 이름이고, 앉지 않았으면 `none` 입니다. */
        [[nodiscard]] bool readPawnSeat( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            const GameObject*    pTarget = query.findTargetObject();
            const PawnComponent* pPawn   = pTarget != nullptr ? pTarget->getComponent<PawnComponent>() : nullptr;
            if ( pPawn == nullptr )
            {
                outValue.setText( pTarget == nullptr ? query.getTargetProblem() : query.describeMissing( "pawn" ) );
                return false;
            }
            const VehicleSeatComponent* pSeat = VehicleSeatComponent::findSeatOf( *pPawn );
            outValue.setText( pSeat != nullptr ? string_view{ pSeat->getOwner()->getName().c_str() } : string_view{ "none" } );
            return true;
        }

        /** @brief 질의 대상 탈것의 탈것 이동입니다. 없으면 까닭을 적고 nullptr 입니다. */
        const MountMovementComponent* findTargetMovement( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            const MountMovementComponent* pMovement = query.findTargetObject() != nullptr ? query.findTargetObject()->getComponent<MountMovementComponent>() : nullptr;
            if ( pMovement == nullptr )
                outValue.setText( query.findTargetObject() == nullptr ? query.getTargetProblem() : query.describeMissing( "Mount Movement" ) );
            return pMovement;
        }

        /** @brief 감시 값 `Mount.Gait` 입니다. 대상 탈것의 걸음새 번호(0 서기 .. 4 질주)입니다. */
        [[nodiscard]] bool readGait( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            const MountMovementComponent* pMovement = findTargetMovement( query, outValue );
            if ( pMovement == nullptr )
                return false;
            outValue.setNumber( static_cast<float64>( pMovement->getGait() ) );
            return true;
        }

        /** @brief 감시 값 `Mount.Speed` 입니다. 대상 탈것이 몸 방향으로 가는 속도(m/s)입니다. */
        [[nodiscard]] bool readSpeed( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            const MountMovementComponent* pMovement = findTargetMovement( query, outValue );
            if ( pMovement == nullptr )
                return false;
            outValue.setNumber( static_cast<float64>( pMovement->getForwardSpeed() ) );
            return true;
        }

        /** @brief 감시 값 `Vehicle.ForwardSpeed` 입니다. 대상 차가 앞으로 가는 속도(m/s)입니다. 물리 차면 마지막 물리 프레임의 값, 아케이드 차면 모터의 값입니다. */
        [[nodiscard]] bool readVehicleForwardSpeed( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            const GameObject*              pTarget = query.findTargetObject();
            const WheeledVehicleComponent* pWheels = pTarget != nullptr ? pTarget->getComponent<WheeledVehicleComponent>() : nullptr;
            const ArcadeVehicleComponent*  pArcade = pTarget != nullptr ? pTarget->getComponent<ArcadeVehicleComponent>() : nullptr;
            if ( pWheels != nullptr )
            {
                outValue.setNumber( static_cast<float64>( pWheels->getVehicleState()._forwardSpeed ) );
                return true;
            }
            if ( pArcade != nullptr )
            {
                outValue.setNumber( static_cast<float64>( pArcade->getMotor().getForwardSpeed() ) );
                return true;
            }
            outValue.setText( pTarget == nullptr ? query.getTargetProblem() : query.describeMissing( "Wheeled Vehicle or Arcade Vehicle" ) );
            return false;
        }

        /** @brief 감시 값 `Vehicle.GroundedWheels` 입니다. 대상 물리 차의 바퀴 가운데 땅에 닿은 수입니다. 차가 아직 서지 않았으면 0 입니다. */
        [[nodiscard]] bool readVehicleGroundedWheels( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            const GameObject*              pTarget = query.findTargetObject();
            const WheeledVehicleComponent* pWheels = pTarget != nullptr ? pTarget->getComponent<WheeledVehicleComponent>() : nullptr;
            if ( pWheels == nullptr )
            {
                outValue.setText( pTarget == nullptr ? query.getTargetProblem() : query.describeMissing( "Wheeled Vehicle" ) );
                return false;
            }
            outValue.setNumber( static_cast<float64>( pWheels->getVehicleState()._groundedWheelCount ) );
            return true;
        }
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( mountGait, "Mount.Gait", "Gait of the mount on target (0 standing, 1 walk, 2 trot, 3 canter, 4 gallop)", &readGait, true, false );
    SW_AUTOMATION_QUERY_WATCH( mountSpeed, "Mount.Speed", "Forward speed (m/s) of the mount on target", &readSpeed, true, false );
    SW_AUTOMATION_QUERY_WATCH( vehicleForwardSpeed, "Vehicle.ForwardSpeed", "Forward speed (m/s) of the physics or arcade car on target", &readVehicleForwardSpeed, true, false );
    SW_AUTOMATION_QUERY_WATCH( vehicleGroundedWheels, "Vehicle.GroundedWheels", "Wheels of the physics car on target that touch the ground", &readVehicleGroundedWheels, true, false );
    SW_AUTOMATION_QUERY_WATCH( pawnSeat, "Pawn.Seat", "Name of the vehicle the pawn on target sits in, none when on foot", &readPawnSeat, true, false );
} // namespace sw
