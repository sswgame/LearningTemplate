/**
 * @file PartyGameModeComponentDevTools.cpp
 * @brief `PartyGameModeComponent` 의 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/FormatDesc.h"
#include "Engine/Serialization/Serializer/Archive.h"
#include "Engine/Serialization/Serializer/StateIO.h"

#include "GameFramework/Kits/Genre/Casual/PartyArena/Component/PartyGameModeComponent.h"
#include "GameFramework/Kits/Genre/Casual/PartyArena/TrampolineArena.h"
#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"

namespace sw
{
    SW_AUTOMATION_GETTER_WATCH( partyArenaPhase, "PartyArena.Phase", "PartyGamePhase name of the party game", PartyGameModeComponent, &PartyGameModeComponent::getPhase );
    SW_AUTOMATION_GETTER_WATCH( partyArenaRound, "PartyArena.Round", "Round number (0 = first)", PartyGameModeComponent, &PartyGameModeComponent::getRound );
    SW_AUTOMATION_GETTER_WATCH( partyArenaTotal, "PartyArena.PlayerTotal", "Placement points of the local player", PartyGameModeComponent, &PartyGameModeComponent::getPlayerTotal );
    SW_AUTOMATION_GETTER_WATCH( partyArenaRingOuts, "PartyArena.PlayerRingOuts", "Times the local player fell off the arena", PartyGameModeComponent, &PartyGameModeComponent::getPlayerRingOutCount );
} // namespace sw
