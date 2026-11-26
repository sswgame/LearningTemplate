/**
 * @file VelmoraEncounterGameModeComponentDevTools.cpp
 * @brief `VelmoraEncounterGameModeComponent` 의 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/Log/Logger.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Action/ActionAdventure/AdventureRegionCatalog.h"
#include "GameFramework/Kits/Genre/Action/ActionAdventure/Component/AdventureGameModeComponent.h"
#include "GameFramework/Kits/Genre/Action/ActionCombat/MonsterCatalog.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"
#include "GameFramework/Runtime/Gameplay/GameState/PlayerComponentUtil.h"

#include "Games/Playground/Velmora/VelmoraEncounterGameModeComponent.h"

namespace sw
{
    SW_AUTOMATION_GETTER_WATCH( velmoraPhase, "Velmora.Phase", "Velmora region fight phase (0 explore, 1 hall, 2 boss wait, 3 boss)", VelmoraEncounterGameModeComponent,
                                &VelmoraEncounterGameModeComponent::getPhase );
    SW_AUTOMATION_GETTER_WATCH( velmoraAlive, "Velmora.Alive", "Living enemies in the Velmora region fight", VelmoraEncounterGameModeComponent,
                                &VelmoraEncounterGameModeComponent::getAliveEnemyCount );
    SW_AUTOMATION_GETTER_WATCH( velmoraAttackReady, "Velmora.AttackReady", "1 once the Velmora player's attack cooldown has run out (the next attack press lands)",
                                VelmoraEncounterGameModeComponent, &VelmoraEncounterGameModeComponent::isAttackReady );
} // namespace sw
