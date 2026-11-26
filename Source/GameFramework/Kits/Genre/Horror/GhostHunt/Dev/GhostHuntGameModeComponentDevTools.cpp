/**
 * @file GhostHuntGameModeComponentDevTools.cpp
 * @brief `GhostHuntGameModeComponent` 의 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/FormatDesc.h"
#include "Engine/Serialization/Serializer/StateIO.h"

#include "GameFramework/Kits/Genre/Horror/GhostHunt/Component/GhostHuntGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/GameData/DepletingValue.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameStack.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemStackList.h"

namespace sw
{
    SW_AUTOMATION_GETTER_WATCH( ghostHuntResult, "GhostHunt.Result", "Hunt result (0 ongoing, 1 win, 2 lose)", GhostHuntGameModeComponent, &GhostHuntGameModeComponent::getResult );
    SW_AUTOMATION_GETTER_WATCH( ghostHuntLitRooms, "GhostHunt.LitRooms", "Rooms whose light is on", GhostHuntGameModeComponent, &GhostHuntGameModeComponent::countLitRooms );
    SW_AUTOMATION_GETTER_WATCH( ghostHuntRemaining, "GhostHunt.Remaining", "Ghosts left in the current room", GhostHuntGameModeComponent, &GhostHuntGameModeComponent::getRemainingGhosts );
    SW_AUTOMATION_GETTER_WATCH( ghostHuntCoins, "GhostHunt.Coins", "Coins collected", GhostHuntGameModeComponent, &GhostHuntGameModeComponent::getCoins );
    SW_AUTOMATION_GETTER_WATCH( ghostHuntKeys, "GhostHunt.Keys", "Keys in the bag", GhostHuntGameModeComponent, &GhostHuntGameModeComponent::getKeyCount );
    SW_AUTOMATION_GETTER_WATCH( ghostHuntHealth, "GhostHunt.Health", "Player health", GhostHuntGameModeComponent, &GhostHuntGameModeComponent::getHealth );
    SW_AUTOMATION_GETTER_WATCH( ghostHuntPlayerX, "GhostHunt.PlayerX", "Absolute X of the player (m)", GhostHuntGameModeComponent, &GhostHuntGameModeComponent::getPlayerX );
    SW_AUTOMATION_GETTER_WATCH( ghostHuntPlayerZ, "GhostHunt.PlayerZ", "Absolute Z of the player (m)", GhostHuntGameModeComponent, &GhostHuntGameModeComponent::getPlayerZ );
    SW_AUTOMATION_GETTER_WATCH( ghostHuntSucking, "GhostHunt.Sucking", "1 while a ghost is in the vacuum", GhostHuntGameModeComponent, &GhostHuntGameModeComponent::isSucking );
} // namespace sw
