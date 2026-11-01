/**
 * @file VehicleWatches.cpp
 * @brief 탈것의 자동화 감시 값 `Pawn.Seat` 입니다. 폰이 어느 탈것에 앉았는지는 탈것 층(`MountUtil`)이 알고, 조종 층(폰)은 탈것을 보지 않으므로 이 폴더에 둔다.
 */
#include "pch.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/MountMovementComponent.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/MountUtil.h"
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
            const VehicleSeatComponent* pSeat = MountUtil::findSeatOf( *pPawn );
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
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( mountGait, "Mount.Gait", "Gait of the mount on target (0 standing, 1 walk, 2 trot, 3 canter, 4 gallop)", &readGait, true, false );
    SW_AUTOMATION_QUERY_WATCH( mountSpeed, "Mount.Speed", "Forward speed (m/s) of the mount on target", &readSpeed, true, false );
    SW_AUTOMATION_QUERY_WATCH( pawnSeat, "Pawn.Seat", "Name of the vehicle the pawn on target sits in, none when on foot", &readPawnSeat, true, false );
} // namespace sw
