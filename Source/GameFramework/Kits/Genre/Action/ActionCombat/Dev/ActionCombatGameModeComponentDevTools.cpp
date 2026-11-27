/**
 * @file ActionCombatGameModeComponentDevTools.cpp
 * @brief `ActionCombatGameModeComponent` 의 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Serialization/Format/FormatDesc.h"
#include "Engine/Serialization/Serializer/StateIO.h"

#include "GameFramework/Kits/Genre/Action/ActionCombat/Component/ActionCombatGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/GameData/DepletingValue.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameStack.h"

namespace sw
{
    SW_AUTOMATION_GETTER_WATCH( actionCombatPhase, "ActionCombat.Phase", "ActionCombatPhase name of the action combat game", ActionCombatGameModeComponent,
                                &ActionCombatGameModeComponent::getPhase );
    SW_AUTOMATION_GETTER_WATCH( actionCombatAlive, "ActionCombat.Alive", "Living enemies in the action combat room", ActionCombatGameModeComponent,
                                &ActionCombatGameModeComponent::getAliveEnemyCount );
    SW_AUTOMATION_GETTER_WATCH( actionCombatPlayerHP, "ActionCombat.PlayerHP", "Player HP in the action combat run", ActionCombatGameModeComponent, &ActionCombatGameModeComponent::getPlayerHP );
    SW_AUTOMATION_GETTER_WATCH( actionCombatPlayerX, "ActionCombat.PlayerX", "Player room-plane X (m) in the action combat run", ActionCombatGameModeComponent,
                                &ActionCombatGameModeComponent::getPlayerX );
} // namespace sw
