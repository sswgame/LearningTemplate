/**
 * @file FightingGameModeComponentDevTools.cpp
 * @brief `FightingGameModeComponent` 의 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/Foundation/Constants.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Serialization/Format/FormatDef.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Action/Fighting/Component/FightingGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameStack.h"

namespace sw
{
    SW_AUTOMATION_GETTER_WATCH( fightingMatchWinner, "Fighting.MatchWinner", "Fighting match winner (0 player, 1 opponent, 2 draw, -1 not over)", FightingGameModeComponent,
                                &FightingGameModeComponent::getMatchWinner );
    SW_AUTOMATION_GETTER_WATCH( fightingRound, "Fighting.Round", "Fighting round number (1 = first)", FightingGameModeComponent, &FightingGameModeComponent::getRound );
    SW_AUTOMATION_GETTER_WATCH( fightingOpponentHealth, "Fighting.OpponentHealth", "Health of the opponent fighter", FightingGameModeComponent, &FightingGameModeComponent::getOpponentHealth );
    SW_AUTOMATION_GETTER_WATCH( fightingPlayerState, "Fighting.PlayerState", "State of the player fighter (FighterState: 0 neutral, 1 attacking, 2 sidestep, 3 jump ...)", FightingGameModeComponent,
                                &FightingGameModeComponent::getPlayerState );
} // namespace sw
