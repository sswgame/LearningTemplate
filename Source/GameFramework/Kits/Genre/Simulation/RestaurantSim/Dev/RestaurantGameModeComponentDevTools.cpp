/**
 * @file RestaurantGameModeComponentDevTools.cpp
 * @brief `RestaurantGameModeComponent` 의 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Serialization/Format/FormatDesc.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Simulation/RestaurantSim/Component/RestaurantGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameStack.h"

namespace sw
{
    SW_AUTOMATION_GETTER_WATCH( restaurantPhase, "Restaurant.Phase", "Restaurant phase (0 prepare, 1 open, 2 closed, 3 victory, 4 defeat)", RestaurantGameModeComponent,
                                &RestaurantGameModeComponent::getPhase );
    SW_AUTOMATION_GETTER_WATCH( restaurantDay, "Restaurant.Day", "Business day (1 = first)", RestaurantGameModeComponent, &RestaurantGameModeComponent::getDay );
    SW_AUTOMATION_GETTER_WATCH( restaurantGold, "Restaurant.Gold", "Gold of the restaurant", RestaurantGameModeComponent, &RestaurantGameModeComponent::getGold );
    SW_AUTOMATION_GETTER_WATCH( restaurantServed, "Restaurant.Served", "Customers who paid today", RestaurantGameModeComponent, &RestaurantGameModeComponent::getServed );
    SW_AUTOMATION_GETTER_WATCH( restaurantTotalProfit, "Restaurant.TotalProfit", "Profit of the closed days", RestaurantGameModeComponent, &RestaurantGameModeComponent::getTotalProfit );
    SW_AUTOMATION_GETTER_WATCH( restaurantReputation, "Restaurant.Reputation", "Reputation among the guests", RestaurantGameModeComponent, &RestaurantGameModeComponent::getReputation );
    SW_AUTOMATION_GETTER_WATCH( restaurantCookLevel, "Restaurant.BestCookLevel", "Highest level among the cooks", RestaurantGameModeComponent, &RestaurantGameModeComponent::getBestCookLevel );
    SW_AUTOMATION_GETTER_WATCH( restaurantStations, "Restaurant.Stations", "Cooking stations for the first dish", RestaurantGameModeComponent, &RestaurantGameModeComponent::getFirstStationCount );
    SW_AUTOMATION_GETTER_WATCH( restaurantMenuPrice, "Restaurant.MenuPrice", "Menu price of the first dish", RestaurantGameModeComponent, &RestaurantGameModeComponent::getFirstMenuPrice );
} // namespace sw
