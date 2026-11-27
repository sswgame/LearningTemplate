/**
 * @file BattleRoyaleGameModeComponentDevTools.cpp
 * @brief `BattleRoyaleGameModeComponent` 의 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/FormatDesc.h"
#include "Engine/Serialization/Serializer/StateIO.h"

#include "GameFramework/Kits/Genre/Action/BattleRoyale/Component/BattleRoyaleGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameStack.h"

namespace sw
{
    SW_AUTOMATION_GETTER_WATCH( battleRoyalePhase, "BattleRoyale.Phase", "BattleRoyalePhase name of the battle royale match", BattleRoyaleGameModeComponent, &BattleRoyaleGameModeComponent::getPhase );
    SW_AUTOMATION_GETTER_WATCH( battleRoyaleRemaining, "BattleRoyale.Remaining", "People not dead (downed included)", BattleRoyaleGameModeComponent, &BattleRoyaleGameModeComponent::getRemaining );
    SW_AUTOMATION_GETTER_WATCH( battleRoyaleZoneRadius, "BattleRoyale.ZoneRadius", "Radius of the safe zone (m)", BattleRoyaleGameModeComponent, &BattleRoyaleGameModeComponent::getZoneRadius );
    SW_AUTOMATION_GETTER_WATCH( battleRoyalePlayerMagazine, "BattleRoyale.PlayerMagazine", "Rounds in the player weapon", BattleRoyaleGameModeComponent, &BattleRoyaleGameModeComponent::getPlayerMagazine );
    SW_AUTOMATION_GETTER_WATCH( battleRoyalePlayerDropStage, "BattleRoyale.PlayerDropStage", "BattleRoyaleDropStage name of the player", BattleRoyaleGameModeComponent,
                                &BattleRoyaleGameModeComponent::getPlayerDropStage );
} // namespace sw
