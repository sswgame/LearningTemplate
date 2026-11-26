/**
 * @file ScavengerGameModeComponentDevTools.cpp
 * @brief `ScavengerGameModeComponent` 의 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/FormatDesc.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Horror/CoopScavenger/Component/ScavengerGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameStack.h"
#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"

namespace sw
{
    SW_AUTOMATION_GETTER_WATCH( scavengerResult, "Scavenger.Result", "Expedition result (0 ongoing, 1 win, 2 lose)", ScavengerGameModeComponent, &ScavengerGameModeComponent::getResult );
    SW_AUTOMATION_GETTER_WATCH( scavengerPhase, "Scavenger.Phase", "Expedition phase (0 in orbit, 1 landed, 2 game over)", ScavengerGameModeComponent, &ScavengerGameModeComponent::getPhase );
    SW_AUTOMATION_GETTER_WATCH( scavengerQuota, "Scavenger.Quota", "Quota of this cycle", ScavengerGameModeComponent, &ScavengerGameModeComponent::getQuota );
    SW_AUTOMATION_GETTER_WATCH( scavengerFulfilled, "Scavenger.Fulfilled", "Credits sold towards the quota", ScavengerGameModeComponent, &ScavengerGameModeComponent::getFulfilled );
    SW_AUTOMATION_GETTER_WATCH( scavengerDaysLeft, "Scavenger.DaysLeft", "Days before the deadline day", ScavengerGameModeComponent, &ScavengerGameModeComponent::getDaysLeft );
    SW_AUTOMATION_GETTER_WATCH( scavengerShipValue, "Scavenger.ShipValue", "Value of the scrap on the ship", ScavengerGameModeComponent, &ScavengerGameModeComponent::getShipValue );
    SW_AUTOMATION_GETTER_WATCH( scavengerCredits, "Scavenger.Credits", "Credits of the crew", ScavengerGameModeComponent, &ScavengerGameModeComponent::getCredits );
    SW_AUTOMATION_GETTER_WATCH( scavengerGround, "Scavenger.GroundScrap", "Scrap left on the moon", ScavengerGameModeComponent, &ScavengerGameModeComponent::getGroundScrapCount );
    SW_AUTOMATION_GETTER_WATCH( scavengerCarried, "Scavenger.Carried", "Scrap in the hands of the crew", ScavengerGameModeComponent, &ScavengerGameModeComponent::countCarried );
    SW_AUTOMATION_GETTER_WATCH( scavengerThreats, "Scavenger.Threats", "Threats on the moon", ScavengerGameModeComponent, &ScavengerGameModeComponent::getThreatCount );
    SW_AUTOMATION_GETTER_WATCH( scavengerPlayerInShip, "Scavenger.PlayerInShip", "1 while crew member 0 is in the ship", ScavengerGameModeComponent, &ScavengerGameModeComponent::isPlayerInShip );
    SW_AUTOMATION_GETTER_WATCH( scavengerCanUnlock, "Scavenger.CanUnlock", "1 while the ship holds a lockpick", ScavengerGameModeComponent, &ScavengerGameModeComponent::canUnlockDoor );
} // namespace sw
