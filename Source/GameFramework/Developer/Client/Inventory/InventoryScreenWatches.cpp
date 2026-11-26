/**
 * @file InventoryScreenWatches.cpp
 * @brief 인벤토리 화면(`InventoryScreen`)의 자동화 감시 값입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/Foundation/Constants.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/UI/Menu/QuantityDialogScreen.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/WidgetTree/PanelWidget.h"
#include "Engine/UI/WidgetTree/UIEvents.h"
#include "Engine/UI/WidgetTree/UIFocusManager.h"
#include "Engine/UI/WidgetTree/WidgetTree.h"
#include "Engine/UI/Widgets/ButtonWidget.h"
#include "Engine/UI/Widgets/TextWidget.h"

#include "GameFramework/Client/Inventory/InventoryScreen.h"
#include "GameFramework/Client/Inventory/InventorySlotWidget.h"
#include "GameFramework/Runtime/Foundation/GameData/StatBlock.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterEquipmentComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Runtime/Gameplay/Inventory/PickupComponent.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct InventoryScreenWatchInternal
        {
            static const InventoryScreen* findTopInventory( AutomationWatchValue& outValue )
            {
                const UISystem* pUI = game::findSubsystem<UISystem>();
                if ( pUI != nullptr )
                {
                    const vector<unique_ptr<UIScreen>>& listScreen = pUI->getScreens();
                    for ( auto iter = listScreen.rbegin(); iter != listScreen.rend(); ++iter )
                    {
                        const InventoryScreen* pScreen = castTo<InventoryScreen>( iter->get() );
                        if ( pScreen != nullptr && pScreen->isClosing() == false )
                            return pScreen;
                    }
                }
                outValue.setText( "no inventory screen is open" );
                return nullptr;
            }

            /** @brief 감시 값 `Inventory.HeldSlot` — 집은 칸 번호(없으면 -1)입니다. */
            [[nodiscard]] static bool readHeldSlot( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                (void)query;
                const InventoryScreen* pScreen = findTopInventory( outValue );
                if ( pScreen == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pScreen->getHeldSlot() ) );
                return true;
            }

            /** @brief 감시 값 `Inventory.MenuSlot` — 동작 메뉴가 열린 칸 번호(닫혔으면 -1)입니다. */
            [[nodiscard]] static bool readMenuSlot( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                (void)query;
                const InventoryScreen* pScreen = findTopInventory( outValue );
                if ( pScreen == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pScreen->getMenuSlot() ) );
                return true;
            }

            /** @brief 감시 값 `Inventory.LastResult` — 마지막 명령 결과 이름(`InventoryCommandResult`)이고, 명령 전이면 `none` 입니다. */
            [[nodiscard]] static bool readLastResult( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                (void)query;
                const InventoryScreen* pScreen = findTopInventory( outValue );
                if ( pScreen == nullptr )
                    return false;
                outValue.setText( pScreen->getCommandCount().get() == 0 ? "none" : getEnumName( pScreen->getLastResult() ) );
                return true;
            }

            /** @brief 감시 값 `Inventory.CommandCount` — 화면이 낸 명령 수입니다(성공 · 실패 모두). */
            [[nodiscard]] static bool readCommandCount( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                (void)query;
                const InventoryScreen* pScreen = findTopInventory( outValue );
                if ( pScreen == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pScreen->getCommandCount().get() ) );
                return true;
            }

            /** @brief 감시 값 `Inventory.Filter` — 거르는 분류이고, 모두면 `all` 입니다. */
            [[nodiscard]] static bool readFilter( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                (void)query;
                const InventoryScreen* pScreen = findTopInventory( outValue );
                if ( pScreen == nullptr )
                    return false;
                outValue.setText( pScreen->getFilter().empty() ? "all" : pScreen->getFilter().c_str() );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( inventoryHeldSlot, "Inventory.HeldSlot", "Bag slot the open inventory screen holds (picked, waiting for a place); -1 without one",
                               &InventoryScreenWatchInternal::readHeldSlot, false, false );
    SW_AUTOMATION_QUERY_WATCH( inventoryMenuSlot, "Inventory.MenuSlot", "Bag slot whose action menu the open inventory screen shows; -1 when closed", &InventoryScreenWatchInternal::readMenuSlot, false,
                               false );
    SW_AUTOMATION_QUERY_WATCH( inventoryLastResult, "Inventory.LastResult", "Result name of the last command of the open inventory screen (InventoryCommandResult); none before the first",
                               &InventoryScreenWatchInternal::readLastResult, false, false );
    SW_AUTOMATION_QUERY_WATCH( inventoryCommandCount, "Inventory.CommandCount", "Commands the open inventory screen ran (succeeded or failed)", &InventoryScreenWatchInternal::readCommandCount, false,
                               false );
    SW_AUTOMATION_QUERY_WATCH( inventoryFilter, "Inventory.Filter", "Item category the open inventory screen filters by; all when none", &InventoryScreenWatchInternal::readFilter, false, false );
} // namespace sw
