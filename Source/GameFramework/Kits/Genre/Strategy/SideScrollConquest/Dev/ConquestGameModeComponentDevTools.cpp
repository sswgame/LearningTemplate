/**
 * @file ConquestGameModeComponentDevTools.cpp
 * @brief `ConquestGameModeComponent` 의 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/FormatDef.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Strategy/SideScrollConquest/Component/ConquestGameModeComponent.h"

namespace sw
{
    SW_AUTOMATION_GETTER_WATCH( conquestVictory, "Conquest.Victory", "1 when the enemy holds no site", ConquestGameModeComponent, &ConquestGameModeComponent::getVictory );
    SW_AUTOMATION_GETTER_WATCH( conquestDefeat, "Conquest.Defeat", "1 when the player holds no site", ConquestGameModeComponent, &ConquestGameModeComponent::getDefeat );
    SW_AUTOMATION_GETTER_WATCH( conquestTerritory, "Conquest.Territory", "Sites the player owns", ConquestGameModeComponent, &ConquestGameModeComponent::getTerritory );
    SW_AUTOMATION_GETTER_WATCH( conquestSquad, "Conquest.Squad", "Soldiers in the commander's squad", ConquestGameModeComponent, &ConquestGameModeComponent::getSquadSize );
    SW_AUTOMATION_GETTER_WATCH( conquestCommanderX, "Conquest.CommanderX", "Commander X (m)", ConquestGameModeComponent, &ConquestGameModeComponent::getCommanderX );
} // namespace sw
