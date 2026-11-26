/**
 * @file RTSUnitViewComponentDevTools.cpp
 * @brief `RTSUnitViewComponent` 의 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/Client/RTSBattlefieldViewComponent.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/Client/RTSCommandInputComponent.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/Client/RTSUnitViewComponent.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/Component/RTSGameModeComponent.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 감시 값 `Skirmish.ViewLagMax` 입니다. 모든 유닛 뷰 가운데 메시가 디렉터의 유닛 자리에서 가장 멀리 벗어난 거리입니다.
         *        측정한 뷰가 하나도 없으면(뷰가 없거나 모두 비교 불가) false 를 돌려 시나리오가 빈 측정으로 통과하지 못하게 합니다.
         */
        [[nodiscard]] bool readViewLagMax( const GameObjectManager* pManager, float64& outValue )
        {
            if ( pManager == nullptr )
                return false;
            float32 lagMax        = 0.0f;
            uint32  measuredCount = 0;
            for ( const RTSUnitViewComponent* pView : pManager->getComponentRegistry().getAll<RTSUnitViewComponent>() )
            {
                const float32 lag = pView->measureViewLag();
                if ( lag < 0.0f )
                    continue;
                lagMax = MathUtil::max( lagMax, lag );
                ++measuredCount;
            }
            if ( measuredCount == 0 )
                return false;
            outValue = static_cast<float64>( lagMax );
            return true;
        }
    } // namespace

    SW_AUTOMATION_WATCH( skirmishViewLagMax, "Skirmish.ViewLagMax", "Largest distance between a unit view mesh and the game mode's unit position", &readViewLagMax );
} // namespace sw
