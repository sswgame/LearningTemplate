/**
 * @file VehicleProbes.cpp
 * @brief 탈것의 자동화 탐침 `Pawn.Seat` 입니다. 폰이 어느 탈것에 앉았는지는 탈것 층(`MountUtil`)이 알고, 조종 층(폰)은 탈것을 보지 않으므로 이 폴더에 둔다.
 */
#include "pch.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Probe/AutomationProbe.h"

#include "GameFramework/Base/Actor/Control/Pawn/PawnComponent.h"
#include "GameFramework/Base/Gameplay/Vehicle/MountUtil.h"
#include "GameFramework/Base/Gameplay/Vehicle/VehicleSeatComponent.h"

namespace sw
{
    namespace
    {
        /** @brief 탐침 `Pawn.Seat` 입니다. 폰이 앉은 좌석의 탈것 오브젝트 이름이고, 앉지 않았으면 `none` 입니다. */
        [[nodiscard]] bool readPawnSeat( const AutomationProbeQuery& query, AutomationProbeValue& outValue )
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
    } // namespace

    SW_AUTOMATION_QUERY_PROBE( pawnSeat, "Pawn.Seat", "Name of the vehicle the pawn on target sits in, none when on foot", &readPawnSeat, true, false );
} // namespace sw
