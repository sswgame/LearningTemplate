/**
 * @file CityBuildInputComponentDevTools.cpp
 * @brief `CityBuildInputComponent` 의 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Serialization/Serializer/Archive.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/Client/CityBuildInputComponent.h"
#include "GameFramework/Kits/Genre/Strategy/CityBuilder/Component/CityBuilderGameModeComponent.h"
#include "GameFramework/Runtime/Character/Camera/CameraDirectorComponent.h"
#include "GameFramework/Runtime/Character/Input/InputActionCheckUtil.h"
#include "GameFramework/Runtime/Foundation/Presentation/GameSound.h"
#include "GameFramework/Runtime/Foundation/Save/StatefulComponentRegistrar.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Wallet.h"

namespace sw
{
    namespace
    {
        struct CityBuildInputComponentDevInternal
        {
            /** @brief 감시 값 `City.CursorCellX` · `City.CursorCellZ` 입니다. 커서가 땅 밖이면 값이 없습니다. */
            [[nodiscard]] static bool readCursorCellX( const CityBuildInputComponent& buildInput, float64& outValue )
            {
                int2  tile{};
                int32 size = 1;
                if ( buildInput.findCursor( tile, size ) == false )
                    return false;
                outValue = static_cast<float64>( tile._x );
                return true;
            }

            [[nodiscard]] static bool readCursorCellZ( const CityBuildInputComponent& buildInput, float64& outValue )
            {
                int2  tile{};
                int32 size = 1;
                if ( buildInput.findCursor( tile, size ) == false )
                    return false;
                outValue = static_cast<float64>( tile._y );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( cityTool, "City.Tool", "Selected build tool (0 road, then the catalog buildings in order)", CityBuildInputComponent, &CityBuildInputComponent::getSelectedTool );
    SW_AUTOMATION_COMPONENT_WATCH( cityCursorCellX, "City.CursorCellX", "Ground tile X under the pointer; absent while the pointer is off the ground", CityBuildInputComponent,
                                   &CityBuildInputComponentDevInternal::readCursorCellX );
    SW_AUTOMATION_COMPONENT_WATCH( cityCursorCellZ, "City.CursorCellZ", "Ground tile Z (city row) under the pointer; absent while the pointer is off the ground", CityBuildInputComponent,
                                   &CityBuildInputComponentDevInternal::readCursorCellZ );
} // namespace sw
