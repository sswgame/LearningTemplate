/**
 * @file ParkGameModeComponentDevTools.cpp
 * @brief `ParkGameModeComponent` 의 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/OrientationUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Serialization/Format/FormatDesc.h"
#include "Engine/Serialization/Serializer/Archive.h"
#include "Engine/Serialization/Serializer/StateIO.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/Component/ParkGameModeComponent.h"
#include "GameFramework/Runtime/Character/Navigation/NavGridObstacle.h"

namespace sw
{
    namespace
    {
        struct ParkGameModeComponentDevInternal
        {
            /** @brief 시나리오 프로브입니다. 첫 공원 디렉터의 손님 수를 반환하며, 그림자 장면이 비어 있지 않다는 전제를 확인합니다. */
            [[nodiscard]] static bool readGuestCount( const ParkGameModeComponent& gameMode, float64& outValue )
            {
                outValue = static_cast<float64>( gameMode.getSimulation().getGuestCount() );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_COMPONENT_WATCH( parkGuestCount, "ThemePark.GuestCount", "Guests in the park of the first park game mode", ParkGameModeComponent, &ParkGameModeComponentDevInternal::readGuestCount );
} // namespace sw
