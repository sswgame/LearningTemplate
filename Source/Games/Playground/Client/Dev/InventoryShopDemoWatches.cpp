/**
 * @file InventoryShopDemoWatches.cpp
 * @brief 인벤토리 상점 시연(`InventoryShopDemoComponent`)의 자동화 감시 값 `Demo.Gold` 입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"

#include "Games/Playground/Client/InventoryShopDemoComponent.h"

namespace sw
{
    namespace
    {
        struct InventoryShopDemoWatchInternal
        {
            /** @brief 감시 값 `Demo.Gold` — 시연 지갑의 가게 통화 잔액입니다. */
            [[nodiscard]] static bool readGold( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const InventoryShopDemoComponent* pDemo = query.findTargetComponent<InventoryShopDemoComponent>( "InventoryShopDemoComponent", outValue );
                if ( pDemo == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pDemo->getWallet().getBalance( pDemo->getCurrency() ) ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( demoGold, "Demo.Gold", "Shop currency balance of the inventory shop demo wallet", &InventoryShopDemoWatchInternal::readGold, true, false );
} // namespace sw
