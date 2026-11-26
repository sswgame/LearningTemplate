/**
 * @file KillcamWatches.cpp
 * @brief 킬캠(`KillcamComponent`)의 자동화 감시 값입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/Log/Logger.h"
#include "Core/Math/Math.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Runtime/Character/Combat/Killcam/KillcamComponent.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"

namespace sw
{
    SW_AUTOMATION_GETTER_WATCH( killcamPlaying, "Killcam.Playing", "1 while the first killcam replays a shot", KillcamComponent, &KillcamComponent::isPlaying );
    SW_AUTOMATION_GETTER_WATCH( killcamShotCount, "Killcam.ShotCount", "Shots the first killcam keeps", KillcamComponent, &KillcamComponent::getShotCount );
    SW_AUTOMATION_GETTER_WATCH( killcamLastHitCount, "Killcam.LastHitCount", "Hits of the most recent recorded shot (2 or more for a piercing shot)", KillcamComponent, &KillcamComponent::getLastHitCount );
    SW_AUTOMATION_GETTER_WATCH( killcamPhase, "Killcam.Phase", "Phase of the first killcam replay: 0 follow, 1 hit hold, 2 done", KillcamComponent, &KillcamComponent::getPhase );
} // namespace sw
