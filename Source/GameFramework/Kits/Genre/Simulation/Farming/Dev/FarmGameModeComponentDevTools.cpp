/**
 * @file FarmGameModeComponentDevTools.cpp
 * @brief `FarmGameModeComponent` 의 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/Container/EnumArray.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/FormatDef.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Simulation/Farming/Component/FarmAutoFarmerAIComponent.h"
#include "GameFramework/Kits/Genre/Simulation/Farming/Component/FarmGameModeComponent.h"
#include "GameFramework/Runtime/Character/Control/ControlSystem.h"
#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"
#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"
#include "GameFramework/Runtime/Gameplay/GameState/PlayerComponentUtil.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/InventoryComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"
#include "GameFramework/Runtime/Gameplay/Inventory/WalletComponent.h"
#include "GameFramework/Runtime/World/Environment/WorldClock.h"
#include "GameFramework/Runtime/World/Query/WalkSweep.h"

namespace sw
{
    namespace
    {
        [[nodiscard]] bool readFarmerX( const FarmGameModeComponent& gameMode, float64& outValue )
        {
            outValue = static_cast<float64>( gameMode.getPlayerPosition()._value._x );
            return true;
        }

        [[nodiscard]] bool readTilledCount( const FarmGameModeComponent& gameMode, float64& outValue )
        {
            const FarmField& field       = gameMode.getField();
            int32            tilledCount = 0;
            for ( int32 y = 0; y < field.getHeight(); ++y )
            {
                for ( int32 x = 0; x < field.getWidth(); ++x )
                {
                    const FarmTile* pTile = field.findTile( x, y );
                    tilledCount += pTile != nullptr && pTile->_bTilled != SW_FALSE ? 1 : 0;
                }
            }
            outValue = tilledCount;
            return true;
        }

        [[nodiscard]] bool readCropCount( const FarmGameModeComponent& gameMode, float64& outValue )
        {
            outValue = gameMode.getField().getCropCount();
            return true;
        }
    } // namespace

    SW_AUTOMATION_COMPONENT_WATCH( farmFarmerX, "Farm.FarmerX", "World X of the farmer", FarmGameModeComponent, &readFarmerX );
    SW_AUTOMATION_GETTER_WATCH( farmFarmerTool, "Farm.FarmerTool", "Tool in the farmer's hand (0 hoe, 1 watering can, 2 seeds, 3 hand)", FarmGameModeComponent, &FarmGameModeComponent::getTool );
    SW_AUTOMATION_GETTER_WATCH( farmFarmerWalking, "Farm.FarmerWalking", "1 while the farmer walked last tick (0 once the move input ends)", FarmGameModeComponent, &FarmGameModeComponent::isFarmerWalking );
    SW_AUTOMATION_COMPONENT_WATCH( farmTilledCount, "Farm.TilledCount", "Tilled field tiles", FarmGameModeComponent, &readTilledCount );
    SW_AUTOMATION_COMPONENT_WATCH( farmCropCount, "Farm.CropCount", "Field tiles with a crop", FarmGameModeComponent, &readCropCount );
    SW_AUTOMATION_GETTER_WATCH( farmRaining, "Farm.Raining", "1 while the shared session weather is rain (its wetness is half or more)", FarmGameModeComponent, &FarmGameModeComponent::isRaining );
    SW_AUTOMATION_GETTER_WATCH( farmFarmerControllerKind, "Farm.FarmerControllerKind", "0 = the player controller holds the farmer, 1 = the auto farmer AI", FarmGameModeComponent, &FarmGameModeComponent::isFarmerDrivenByAI );
} // namespace sw
