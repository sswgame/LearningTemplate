/**
 * @file KartGameModeComponentDevTools.cpp
 * @brief `KartGameModeComponent` 의 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/FormatDef.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Casual/KartRacing/Component/KartGameModeComponent.h"

namespace sw
{
    SW_AUTOMATION_GETTER_WATCH( kartRacingPhase, "KartRacing.Phase", "Race phase (0 waiting, 1 countdown, 2 racing, 3 ended)", KartGameModeComponent, &KartGameModeComponent::getPhase );
    SW_AUTOMATION_GETTER_WATCH( kartRacingFinishPlace, "KartRacing.PlayerFinishPlace", "Final place of the player kart (0 = not decided)", KartGameModeComponent,
                                &KartGameModeComponent::getPlayerFinishPlace );
    SW_AUTOMATION_GETTER_WATCH( kartRacingSpeed, "KartRacing.PlayerSpeed", "Forward speed of the player kart (m/s)", KartGameModeComponent, &KartGameModeComponent::getPlayerSpeed );
    SW_AUTOMATION_GETTER_WATCH( kartRacingFinished, "KartRacing.FinishedCount", "Karts with a final place", KartGameModeComponent, &KartGameModeComponent::getFinishedCount );
} // namespace sw
