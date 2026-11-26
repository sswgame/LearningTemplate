/**
 * @file CardBattleGameModeComponentDevTools.cpp
 * @brief `CardBattleGameModeComponent` 의 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/FormatDesc.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Casual/CardGame/Component/CardBattleGameModeComponent.h"

namespace sw
{
    SW_AUTOMATION_GETTER_WATCH( cardBattlePhase, "CardBattle.Phase", "Deck battle phase (0 player turn, 1 won, 2 lost)", CardBattleGameModeComponent, &CardBattleGameModeComponent::getPhase );
    SW_AUTOMATION_GETTER_WATCH( cardBattleTurn, "CardBattle.Turn", "Deck battle turn (1 = first)", CardBattleGameModeComponent, &CardBattleGameModeComponent::getTurn );
    SW_AUTOMATION_GETTER_WATCH( cardBattlePlayerHP, "CardBattle.PlayerHP", "Player HP", CardBattleGameModeComponent, &CardBattleGameModeComponent::getPlayerHP );
    SW_AUTOMATION_GETTER_WATCH( cardBattleEnemyHP, "CardBattle.EnemyHP", "Enemy HP", CardBattleGameModeComponent, &CardBattleGameModeComponent::getEnemyHP );
    SW_AUTOMATION_GETTER_WATCH( cardBattleHand, "CardBattle.HandCount", "Cards in hand", CardBattleGameModeComponent, &CardBattleGameModeComponent::getHandCount );
    SW_AUTOMATION_GETTER_WATCH( cardBattleEnergy, "CardBattle.Energy", "Energy left this turn", CardBattleGameModeComponent, &CardBattleGameModeComponent::getEnergy );
} // namespace sw
