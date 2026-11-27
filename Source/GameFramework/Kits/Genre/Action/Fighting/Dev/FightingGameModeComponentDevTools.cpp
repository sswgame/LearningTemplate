/**
 * @file FightingGameModeComponentDevTools.cpp
 * @brief `FightingGameModeComponent` 의 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/Foundation/Constants.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Serialization/Format/FormatDesc.h"
#include "Engine/Serialization/Serializer/StateIO.h"

#include "GameFramework/Kits/Genre/Action/Fighting/Component/FightingGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameStack.h"

namespace sw
{
    SW_AUTOMATION_GETTER_WATCH( fightingMatchWinner, "Fighting.MatchWinner", "Fighting match winner (0 player, 1 opponent, 2 draw, -1 not over)", FightingGameModeComponent,
                                &FightingGameModeComponent::getMatchWinner );
    SW_AUTOMATION_GETTER_WATCH( fightingRound, "Fighting.Round", "Fighting round number (1 = first)", FightingGameModeComponent, &FightingGameModeComponent::getRound );
    SW_AUTOMATION_GETTER_WATCH( fightingOpponentHealth, "Fighting.OpponentHealth", "Health of the opponent fighter", FightingGameModeComponent, &FightingGameModeComponent::getOpponentHealth );
    SW_AUTOMATION_GETTER_WATCH( fightingPlayerState, "Fighting.PlayerState", "FighterState name of the player fighter", FightingGameModeComponent,
                                &FightingGameModeComponent::getPlayerState );
} // namespace sw
