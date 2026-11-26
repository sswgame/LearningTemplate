/**
 * @file InventoryComponentDevTools.cpp
 * @brief `InventoryComponent` 의 자동화 감시 값입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/Foundation/BuildInfo.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"

#include "GameFramework/Runtime/Foundation/GameData/ConfigRangeUtil.h"
#include "GameFramework/Runtime/Foundation/Save/StatefulComponentRegistrar.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"
#include "GameFramework/Runtime/Gameplay/Inventory/InventoryComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"

namespace sw
{
    namespace
    {
        /** @brief 감시 값 `Inventory.ItemCount` — 가방(target = 조종자 오브젝트)에 든 아이템 key 의 수입니다. */
        [[nodiscard]] bool readInventoryItemCount( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            const InventoryComponent* pBag = query.findTargetComponent<InventoryComponent>( "inventory", outValue );
            if ( pBag == nullptr )
                return false;
            outValue.setNumber( static_cast<float64>( pBag->getInventory().getItemCount( hashed_string( query._key ) ) ) );
            return true;
        }
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( inventoryItemCount, "Inventory.ItemCount", "Count of the item (key) in the bag of the InventoryComponent on target", &readInventoryItemCount, true, true );
} // namespace sw
