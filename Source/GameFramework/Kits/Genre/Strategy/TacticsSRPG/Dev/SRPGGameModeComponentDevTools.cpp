/**
 * @file SRPGGameModeComponentDevTools.cpp
 * @brief `SRPGGameModeComponent` 의 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Serialization/Format/FormatDesc.h"
#include "Engine/Serialization/Serializer/StateIO.h"

#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/Component/SRPGGameModeComponent.h"
#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/SRPGCombat.h"

namespace sw
{
    SW_AUTOMATION_GETTER_WATCH( srpgPhaseTeam, "SRPG.PhaseTeam", "Team whose phase it is (0 player, 1 enemy, 2 third)", SRPGGameModeComponent, &SRPGGameModeComponent::getPhaseTeam );
    SW_AUTOMATION_GETTER_WATCH( srpgTurn, "SRPG.Turn", "Battle turn (1 = first)", SRPGGameModeComponent, &SRPGGameModeComponent::getTurn );
    SW_AUTOMATION_GETTER_WATCH( srpgResult, "SRPG.Result", "Battle result (0 ongoing, 1 victory, 2 defeat)", SRPGGameModeComponent, &SRPGGameModeComponent::getResult );
    SW_AUTOMATION_GETTER_WATCH( srpgSelected, "SRPG.Selected", "Selected unit slot (-1 = none)", SRPGGameModeComponent, &SRPGGameModeComponent::getSelectedUnit );
    SW_AUTOMATION_GETTER_WATCH( srpgEnemies, "SRPG.EnemiesAlive", "Living enemy units", SRPGGameModeComponent, &SRPGGameModeComponent::getEnemiesAlive );
    SW_AUTOMATION_GETTER_WATCH( srpgPlayers, "SRPG.PlayersAlive", "Living player units", SRPGGameModeComponent, &SRPGGameModeComponent::getPlayersAlive );
    SW_AUTOMATION_GETTER_WATCH( srpgLevelUps, "SRPG.LevelUps", "Level-ups left in the battle growth log (pilot and unit)", SRPGGameModeComponent, &SRPGGameModeComponent::getLevelUps );
} // namespace sw
