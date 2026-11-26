/**
 * @file SurvivalHorrorGameModeComponentDevTools.cpp
 * @brief `SurvivalHorrorGameModeComponent` 의 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/FormatDesc.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Horror/SurvivalHorror/Component/SurvivalHorrorGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameStack.h"

namespace sw
{
    SW_AUTOMATION_GETTER_WATCH( survivalHorrorResult, "SurvivalHorror.Result", "Result (0 ongoing, 1 win, 2 lose)", SurvivalHorrorGameModeComponent, &SurvivalHorrorGameModeComponent::getResult );
    SW_AUTOMATION_GETTER_WATCH( survivalHorrorPlayerX, "SurvivalHorror.PlayerX", "Absolute X of the player (m)", SurvivalHorrorGameModeComponent, &SurvivalHorrorGameModeComponent::getPlayerX );
    SW_AUTOMATION_GETTER_WATCH( survivalHorrorItems, "SurvivalHorror.Items", "Stacks in the bag", SurvivalHorrorGameModeComponent, &SurvivalHorrorGameModeComponent::getItemCount );
    SW_AUTOMATION_GETTER_WATCH( survivalHorrorSanity, "SurvivalHorror.Sanity", "Sanity of the player", SurvivalHorrorGameModeComponent, &SurvivalHorrorGameModeComponent::getSanity );
    SW_AUTOMATION_GETTER_WATCH( survivalHorrorHealth, "SurvivalHorror.Health", "Health of the player", SurvivalHorrorGameModeComponent, &SurvivalHorrorGameModeComponent::getHealth );
    SW_AUTOMATION_GETTER_WATCH( survivalHorrorFlashlight, "SurvivalHorror.Flashlight", "1 while the flashlight is on", SurvivalHorrorGameModeComponent,
                                &SurvivalHorrorGameModeComponent::isFlashlightOn );
    SW_AUTOMATION_GETTER_WATCH( survivalHorrorDark, "SurvivalHorror.Dark", "1 while the player is in a dark room", SurvivalHorrorGameModeComponent, &SurvivalHorrorGameModeComponent::isInDarkness );
    SW_AUTOMATION_GETTER_WATCH( survivalHorrorFighting, "SurvivalHorror.Fighting", "1 during a fight", SurvivalHorrorGameModeComponent, &SurvivalHorrorGameModeComponent::isFighting );
    SW_AUTOMATION_GETTER_WATCH( survivalHorrorMonsters, "SurvivalHorror.Monsters", "Monsters left", SurvivalHorrorGameModeComponent, &SurvivalHorrorGameModeComponent::countMonsters );
} // namespace sw
