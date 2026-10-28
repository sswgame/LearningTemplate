/**
 * @file PawnWatches.cpp
 * @brief 폰의 자동화 감시 값 `Pawn.Controller` 입니다. 좌석 쪽 `Pawn.Seat` 는 탈것 층(`Gameplay/Vehicle/VehicleWatches.cpp`)에 있다 — 조종 층이 탈것 층을 보지 않는다.
 */
#include "pch.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Character/Control/Controller/ControllerComponent.h"
#include "GameFramework/Base/Character/Control/Pawn/PawnComponent.h"

namespace sw
{
    namespace
    {
        /** @brief 감시 값 `Pawn.Controller` 입니다. 폰을 쥔 조종자 오브젝트의 이름이고, 아무도 쥐지 않았으면 `none` 입니다. */
        [[nodiscard]] bool readPawnController( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            const GameObject*    pTarget = query.findTargetObject();
            const PawnComponent* pPawn   = pTarget != nullptr ? pTarget->getComponent<PawnComponent>() : nullptr;
            if ( pPawn == nullptr )
            {
                outValue.setText( pTarget == nullptr ? query.getTargetProblem() : query.describeMissing( "pawn" ) );
                return false;
            }
            const Component* pController = query._pManager != nullptr ? query._pManager->resolveComponent( pPawn->getController() ) : nullptr;
            outValue.setText( pController != nullptr ? string_view{ pController->getOwner()->getName().c_str() } : string_view{ "none" } );
            return true;
        }
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( pawnController, "Pawn.Controller", "Name of the object whose controller possesses the pawn on target, none when free", &readPawnController, true,
                               false );
} // namespace sw
