/**
 * @file GimmickSensorComponentDevTools.cpp
 * @brief `GimmickSensorComponent` 의 자동화 감시 값입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectEvents.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Physics/Component/RigidBody2DComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Trigger/GimmickSensorComponent.h"
#include "GameFramework/Runtime/Gameplay/Interaction/InteractableComponent.h"

namespace sw
{
    namespace
    {
        struct GimmickSensorWatchesDevInternal
        {
            /** @brief 감시 값 `Gimmick.OccupantWeight` 입니다. 대상 센서에 겹친 것들의 무게 합(kg — `SensorWeightOverrideComponent`, 강체 질량, 기본 무게 순)입니다. 눌림판이 읽는 값과 같다. */
            [[nodiscard]] static bool readOccupantWeight( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GimmickSensorComponent* pSensor = query.findTargetComponent<GimmickSensorComponent>( "gimmick sensor", outValue );
                if ( pSensor == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pSensor->computeOccupantWeight() ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( gimmickOccupantWeight, "Gimmick.OccupantWeight", "Weight of what overlaps the target's gimmick sensor (kg, the pressure plate's reading)",
                               &GimmickSensorWatchesDevInternal::readOccupantWeight, true, false );
} // namespace sw
