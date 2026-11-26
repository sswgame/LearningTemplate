/**
 * @file CardBattleViewComponentDevTools.cpp
 * @brief `CardBattleViewComponent` 의 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Kits/Genre/Casual/CardGame/Client/CardBattleHUDViewModel.h"
#include "GameFramework/Kits/Genre/Casual/CardGame/Client/CardBattleViewComponent.h"
#include "GameFramework/Kits/Genre/Casual/CardGame/Component/CardBattleGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Presentation/IHUDViewModelBinder.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    SW_AUTOMATION_GETTER_WATCH( cardBattleSelected, "CardBattle.SelectedCard", "Picked hand slot (0 = leftmost)", CardBattleViewComponent, &CardBattleViewComponent::getSelectedCard );
} // namespace sw
