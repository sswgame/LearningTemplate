/**
 * @file ActionCombatRoomHUDComponentDevTools.cpp
 * @brief `ActionCombatRoomHUDComponent` 의 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/Event/EventDispatcher.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Object/GameObject/Registry/CameraShakeChannel.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"

#include "GameFramework/Kits/Genre/Action/ActionCombat/ActionCombatRoom.h"
#include "GameFramework/Kits/Genre/Action/ActionCombat/Client/ActionCombatRoomHUDComponent.h"
#include "GameFramework/Kits/Genre/Action/ActionCombat/Client/ActionCombatRoomHUDViewModel.h"
#include "GameFramework/Runtime/Character/Combat/Damage/DamageEvents.h"
#include "GameFramework/Runtime/Foundation/Presentation/IHUDViewModelBinder.h"
#include "GameFramework/Runtime/Foundation/Service/GameEvents.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    SW_AUTOMATION_GETTER_WATCH( actionRoomHUDNotification, "ActionCombat.HUDNotification", "Room notification on the HUD: 0 none, 1 cleared, 2 defeated", ActionCombatRoomHUDComponent, &ActionCombatRoomHUDComponent::getNotificationValue );
    SW_AUTOMATION_GETTER_WATCH( actionRoomHUDGate, "ActionCombat.HUDGateLocked", "1 while the HUD shows the locked clear gate", ActionCombatRoomHUDComponent, &ActionCombatRoomHUDComponent::getGateLockedValue );
} // namespace sw
