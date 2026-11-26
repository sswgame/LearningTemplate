/**
 * @file MetroidvaniaGameModeComponentDevTools.cpp
 * @brief `MetroidvaniaGameModeComponent` 의 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/FormatDef.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Action/Metroidvania/Component/MetroidvaniaGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/GameData/StatBlock.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameStack.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemStackList.h"

namespace sw
{
    SW_AUTOMATION_GETTER_WATCH( metroidvaniaPhase, "Metroidvania.Phase", "Metroidvania phase (0 explore, 1 dead, 2 won)", MetroidvaniaGameModeComponent, &MetroidvaniaGameModeComponent::getPhase );
    SW_AUTOMATION_GETTER_WATCH( metroidvaniaPlayerX, "Metroidvania.PlayerX", "Player body X (m)", MetroidvaniaGameModeComponent, &MetroidvaniaGameModeComponent::getPlayerX );
    SW_AUTOMATION_GETTER_WATCH( metroidvaniaAbilities, "Metroidvania.Abilities", "Abilities gained", MetroidvaniaGameModeComponent, &MetroidvaniaGameModeComponent::getAbilityCount );
    SW_AUTOMATION_GETTER_WATCH( metroidvaniaCurrency, "Metroidvania.Currency", "Currency carried", MetroidvaniaGameModeComponent, &MetroidvaniaGameModeComponent::getCurrency );
    SW_AUTOMATION_GETTER_WATCH( metroidvaniaVisited, "Metroidvania.VisitedAreas", "Rooms visited", MetroidvaniaGameModeComponent, &MetroidvaniaGameModeComponent::getVisitedAreaCount );
} // namespace sw
