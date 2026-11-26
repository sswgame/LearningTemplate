/**
 * @file CityBuilderGameModeComponentDevTools.cpp
 * @brief `CityBuilderGameModeComponent` 의 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/Component/CityBuilderGameModeComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Wallet.h"

namespace sw
{
    namespace
    {
        struct CityBuilderGameModeComponentDevInternal
        {
            /** @brief 감시 값 `City.Money` 입니다. 금고가 없으면 값이 없습니다. */
            [[nodiscard]] static bool readMoney( const CityBuilderGameModeComponent& gameMode, float64& outValue )
            {
                const Wallet* const pCityWallet = gameMode.findCityWallet();
                if ( pCityWallet == nullptr )
                    return false;
                outValue = static_cast<float64>( pCityWallet->getBalance( gameMode.getCity().getCurrency() ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( cityPopulation, "City.Population", "Population of the city", CityBuilderGameModeComponent, &CityBuilderGameModeComponent::getPopulation );
    SW_AUTOMATION_COMPONENT_WATCH( cityMoney, "City.Money", "Money in the city treasury", CityBuilderGameModeComponent, &CityBuilderGameModeComponentDevInternal::readMoney );
    SW_AUTOMATION_GETTER_WATCH( cityBuildCount, "City.BuildCount", "Roads laid and buildings placed by commands this game (successes only)", CityBuilderGameModeComponent,
                                &CityBuilderGameModeComponent::getBuildCount );
    SW_AUTOMATION_GETTER_WATCH( cityDemolishCount, "City.DemolishCount", "Tiles demolished by commands this game", CityBuilderGameModeComponent, &CityBuilderGameModeComponent::getDemolishCount );
} // namespace sw
