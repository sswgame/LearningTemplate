/**
 * @file AsymmetricHorrorGameModeComponentDevTools.cpp
 * @brief `AsymmetricHorrorGameModeComponent` 의 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/FormatDesc.h"
#include "Engine/Serialization/Serializer/StateIO.h"

#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/Component/AsymmetricHorrorGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameStack.h"

namespace sw
{
    SW_AUTOMATION_GETTER_WATCH( asymmetricHorrorResult, "AsymmetricHorror.Result", "AsymmetricHorrorResult name of the match for the survivors", AsymmetricHorrorGameModeComponent,
                                &AsymmetricHorrorGameModeComponent::getResult );
    SW_AUTOMATION_GETTER_WATCH( asymmetricHorrorGenerators, "AsymmetricHorror.Generators", "Generators repaired", AsymmetricHorrorGameModeComponent,
                                &AsymmetricHorrorGameModeComponent::getCompletedGenerators );
    SW_AUTOMATION_GETTER_WATCH( asymmetricHorrorPowered, "AsymmetricHorror.GatesPowered", "1 when the exit gates have power", AsymmetricHorrorGameModeComponent,
                                &AsymmetricHorrorGameModeComponent::areGatesPowered );
    SW_AUTOMATION_GETTER_WATCH( asymmetricHorrorOpenGates, "AsymmetricHorror.OpenGates", "Exit gates that stand open", AsymmetricHorrorGameModeComponent,
                                &AsymmetricHorrorGameModeComponent::countOpenGates );
    SW_AUTOMATION_GETTER_WATCH( asymmetricHorrorEscaped, "AsymmetricHorror.Escaped", "Survivors who escaped", AsymmetricHorrorGameModeComponent, &AsymmetricHorrorGameModeComponent::countEscaped );
    SW_AUTOMATION_GETTER_WATCH( asymmetricHorrorSacrificed, "AsymmetricHorror.Sacrificed", "Survivors who were sacrificed", AsymmetricHorrorGameModeComponent,
                                &AsymmetricHorrorGameModeComponent::countSacrificed );
    SW_AUTOMATION_GETTER_WATCH( asymmetricHorrorPlayerX, "AsymmetricHorror.PlayerX", "Absolute X of survivor 0 (m)", AsymmetricHorrorGameModeComponent, &AsymmetricHorrorGameModeComponent::getPlayerX );
    SW_AUTOMATION_GETTER_WATCH( asymmetricHorrorActivity, "AsymmetricHorror.PlayerActivity", "SurvivorActivity name of what the first survivor does",
                                AsymmetricHorrorGameModeComponent, &AsymmetricHorrorGameModeComponent::getPlayerActivity );
} // namespace sw
