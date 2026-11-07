/**
 * @file VehicleWatches.cpp
 * @brief 탈것의 자동화 감시 값입니다. `Pawn.Seat` — 폰이 어느 탈것에 앉았는지는 탈것 층(`VehicleSeatComponent`)이 알고, 조종 층(폰)은 탈것을 보지 않으므로 이 폴더에 둔다.
 *        `Mount.*` 는 걷는 탈것, `Vehicle.*` 는 바퀴 탈것(물리 차 · 키네마틱 차(바디 없는 차))의 값이고, `Vehicle.Ride*` · `Vehicle.RiderLean` 은 어느 탈것이든 좌석이 잰 탑승 가속감이다.
 */
#include "pch.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Physics/Component/WheeledVehicleComponent.h"

#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/KinematicVehicleComponent.h"
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

        /** @brief 감시 값 `Vehicle.ForwardSpeed` 입니다. 대상 차가 앞으로 가는 속도(m/s)입니다. 물리 차면 마지막 물리 프레임의 값, 키네마틱 차(바디 없는 차)면 모터의 값입니다. */
        [[nodiscard]] bool readVehicleForwardSpeed( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            const GameObject*                pTarget    = query.findTargetObject();
            const WheeledVehicleComponent*   pWheels    = pTarget != nullptr ? pTarget->getComponent<WheeledVehicleComponent>() : nullptr;
            const KinematicVehicleComponent* pKinematic = pTarget != nullptr ? pTarget->getComponent<KinematicVehicleComponent>() : nullptr;
            if ( pWheels != nullptr )
            {
                outValue.setNumber( static_cast<float64>( pWheels->getVehicleState()._forwardSpeed ) );
                return true;
            }
            if ( pKinematic != nullptr )
            {
                outValue.setNumber( static_cast<float64>( pKinematic->getMotor().getForwardSpeed() ) );
                return true;
            }
            outValue.setText( pTarget == nullptr ? query.getTargetProblem() : query.describeMissing( "Wheeled Vehicle or Kinematic Vehicle" ) );
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

        /** @brief 대상 탈것에서 누가 앉은 첫 좌석입니다. 없으면 까닭을 적고 nullptr 입니다. */
        const VehicleSeatComponent* findOccupiedSeat( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            const GameObject*           pTarget = query.findTargetObject();
            const VehicleSeatComponent* pSeat   = nullptr;
            if ( pTarget != nullptr )
            {
                pTarget->forEachComponentOfType<VehicleSeatComponent>( [&pSeat]( const VehicleSeatComponent* pCandidate )
                {
                    if ( pSeat == nullptr && pCandidate->isFree() == false )
                        pSeat = pCandidate;
                } );
            }
            if ( pSeat == nullptr )
                outValue.setText( pTarget == nullptr ? query.getTargetProblem() : query.describeMissing( "occupied Vehicle Seat" ) );
            return pSeat;
        }

        /** @brief 감시 값 `Vehicle.RideAcceleration` 입니다. 앉은 좌석이 잰 앞 가속도(m/s², 탈것 기준, 매끄럽게 한 값 — 급정거면 음수)입니다. */
        [[nodiscard]] bool readRideAcceleration( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            const VehicleSeatComponent* pSeat = findOccupiedSeat( query, outValue );
            if ( pSeat == nullptr )
                return false;
            outValue.setNumber( static_cast<float64>( pSeat->getRideAcceleration()._z ) );
            return true;
        }

        /** @brief 감시 값 `Vehicle.RiderLean` 입니다. 앉은 탑승자의 앞으로 숙인 각(rad, + 앞 — 급정거, − 뒤 — 가속)입니다. */
        [[nodiscard]] bool readRiderLean( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            const VehicleSeatComponent* pSeat = findOccupiedSeat( query, outValue );
            if ( pSeat == nullptr )
                return false;
            outValue.setNumber( static_cast<float64>( pSeat->getRiderLean()._x ) );
            return true;
        }

        /** @brief 감시 값 `Vehicle.RailDistance` 입니다. 바디 없는 탈것이 레일 위에서 간 거리(m)입니다. */
        [[nodiscard]] bool readRailDistance( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            const GameObject*                pTarget    = query.findTargetObject();
            const KinematicVehicleComponent* pKinematic = pTarget != nullptr ? pTarget->getComponent<KinematicVehicleComponent>() : nullptr;
            if ( pKinematic == nullptr )
            {
                outValue.setText( pTarget == nullptr ? query.getTargetProblem() : query.describeMissing( "Kinematic Vehicle" ) );
                return false;
            }
            outValue.setNumber( static_cast<float64>( pKinematic->getMotor().getRailDistance() ) );
            return true;
        }
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( mountGait, "Mount.Gait", "Gait of the mount on target (0 standing, 1 walk, 2 trot, 3 canter, 4 gallop)", &readGait, true, false );
    SW_AUTOMATION_QUERY_WATCH( mountSpeed, "Mount.Speed", "Forward speed (m/s) of the mount on target", &readSpeed, true, false );
    SW_AUTOMATION_QUERY_WATCH( vehicleForwardSpeed, "Vehicle.ForwardSpeed", "Forward speed (m/s) of the physics or kinematic car on target", &readVehicleForwardSpeed, true, false );
    SW_AUTOMATION_QUERY_WATCH( vehicleGroundedWheels, "Vehicle.GroundedWheels", "Wheels of the physics car on target that touch the ground", &readVehicleGroundedWheels, true, false );
    SW_AUTOMATION_QUERY_WATCH( vehicleRideAcceleration, "Vehicle.RideAcceleration", "Smoothed forward acceleration (m/s2) the occupied seat of the vehicle on target measures", &readRideAcceleration, true, false );
    SW_AUTOMATION_QUERY_WATCH( vehicleRiderLean, "Vehicle.RiderLean", "Forward lean (rad) of the rider in the occupied seat of the vehicle on target (+ forward on braking)", &readRiderLean, true, false );
    SW_AUTOMATION_QUERY_WATCH( vehicleRailDistance, "Vehicle.RailDistance", "Distance (m) the kinematic vehicle on target has run along its rail", &readRailDistance, true, false );
    SW_AUTOMATION_QUERY_WATCH( pawnSeat, "Pawn.Seat", "Name of the vehicle the pawn on target sits in, none when on foot", &readPawnSeat, true, false );
} // namespace sw
