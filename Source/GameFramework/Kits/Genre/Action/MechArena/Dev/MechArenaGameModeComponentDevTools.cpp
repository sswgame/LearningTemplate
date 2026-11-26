/**
 * @file MechArenaGameModeComponentDevTools.cpp
 * @brief `MechArenaGameModeComponent` 의 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/FormatDesc.h"
#include "Engine/Serialization/Serializer/StateIO.h"

#include "GameFramework/Kits/Genre/Action/MechArena/Component/MechArenaGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameStack.h"

namespace sw
{
    SW_AUTOMATION_GETTER_WATCH( mechArenaEnded, "MechArena.Ended", "1 when the mech arena match is over", MechArenaGameModeComponent, &MechArenaGameModeComponent::getEnded );
    SW_AUTOMATION_GETTER_WATCH( mechArenaPlayerState, "MechArena.PlayerState", "Player pilot state (0 waiting, 1 active, 2 down, 3 destroyed, 4 retired)", MechArenaGameModeComponent,
                                &MechArenaGameModeComponent::getPlayerState );
    SW_AUTOMATION_GETTER_WATCH( mechArenaPlayerAmmo, "MechArena.PlayerAmmo", "Magazine of the player's main weapon", MechArenaGameModeComponent, &MechArenaGameModeComponent::getPlayerAmmo );
    SW_AUTOMATION_GETTER_WATCH( mechArenaEnemyGauge, "MechArena.EnemyGauge", "Cost gauge left for the enemy team", MechArenaGameModeComponent, &MechArenaGameModeComponent::getEnemyGauge );
} // namespace sw
