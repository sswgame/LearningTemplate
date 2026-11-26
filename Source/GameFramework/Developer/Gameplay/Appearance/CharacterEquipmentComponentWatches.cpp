/**
 * @file CharacterEquipmentComponentWatches.cpp
 * @brief 장비(`CharacterEquipmentComponent`)의 자동화 감시 값입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectEvents.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/FormatUpgrade.h"
#include "Engine/Serialization/Serializer/Archive.h"

#include "GameFramework/Runtime/Foundation/GameData/StatBlock.h"
#include "GameFramework/Runtime/Foundation/Save/StatefulComponentRegistrar.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearancePart.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearance.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearanceComponent.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterEquipmentComponent.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CustomizationSchema.h"
#include "GameFramework/Runtime/Gameplay/Appearance/ItemVisual.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Runtime/Gameplay/Inventory/PickupComponent.h"

namespace sw
{
    namespace
    {
        struct CharacterEquipmentComponentWatchesInternal
        {
            static const CharacterEquipmentComponent* findTarget( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                return query.findTargetComponent<CharacterEquipmentComponent>( "CharacterEquipmentComponent", outValue );
            }

            /** @brief 감시 값 `Equipment.Worn` — 칸(key)에 낀 아이템 id 입니다. 비었으면 `(empty)` 입니다. */
            [[nodiscard]] static bool readWorn( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CharacterEquipmentComponent* pEquipment = findTarget( query, outValue );
                if ( pEquipment == nullptr )
                    return false;
                const InventorySlot* pWorn = pEquipment->getEquipment().findEquipped( hashed_string( query._key ) );
                outValue.setText( pWorn != nullptr && pWorn->isEmpty() == false ? pWorn->_itemID.c_str() : "(empty)" );
                return true;
            }

            /** @brief 감시 값 `Equipment.BagItem` — 가방 칸(key, 0 부터)의 아이템 id 와 인스턴스 값 수(`<id>` · `<id> +N`)입니다. 비었으면 `(empty)` 입니다. */
            [[nodiscard]] static bool readBagItem( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CharacterEquipmentComponent* pEquipment = findTarget( query, outValue );
                int32                              bagSlot    = 0;
                if ( pEquipment == nullptr )
                    return false;
                if ( StringUtil::parseInt( query._key, bagSlot ) == false || bagSlot < 0 || bagSlot >= pEquipment->getBag().getSlotCount() )
                {
                    outValue.setText( "key must be a bag slot index" );
                    return false;
                }
                const InventorySlot& item = pEquipment->getBag().getSlot( bagSlot );
                if ( item.isEmpty() )
                {
                    outValue.setText( "(empty)" );
                    return true;
                }
                const size_t                            valueCount = item.getCustomization().getCount();
                fixed_string<constant::buffer::kMax256> text;
                if ( valueCount == 0 )
                    formatstring( text.data(), text.capacity(), "%#", item._itemID.c_str() );
                else
                    formatstring( text.data(), text.capacity(), "%# +%#", item._itemID.c_str(), static_cast<uint32>( valueCount ) );
                outValue.setText( text.view() );
                return true;
            }

            /** @brief 감시 값 `Equipment.BagUsedSlots` — 가방에서 아이템이 든 칸 수입니다. */
            [[nodiscard]] static bool readBagUsedSlots( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CharacterEquipmentComponent* pEquipment = findTarget( query, outValue );
                if ( pEquipment == nullptr )
                    return false;
                const Inventory& bag = pEquipment->getBag();
                outValue.setNumber( static_cast<float64>( bag.getSlotCount() - bag.countEmptySlots() ) );
                return true;
            }

            /** @brief 감시 값 `Equipment.BagCount` — 가방 칸(key, 0 부터)에 든 개수입니다(빈 칸은 0). */
            [[nodiscard]] static bool readBagCount( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CharacterEquipmentComponent* pEquipment = findTarget( query, outValue );
                int32                              bagSlot    = 0;
                if ( pEquipment == nullptr )
                    return false;
                if ( StringUtil::parseInt( query._key, bagSlot ) == false || bagSlot < 0 || bagSlot >= pEquipment->getBag().getSlotCount() )
                {
                    outValue.setText( "key must be a bag slot index" );
                    return false;
                }
                outValue.setNumber( static_cast<float64>( pEquipment->getBag().getSlot( bagSlot )._count ) );
                return true;
            }

            /** @brief 감시 값 `Equipment.ItemCount` — 가방에 든 아이템(key) 개수의 합입니다. */
            [[nodiscard]] static bool readItemCount( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CharacterEquipmentComponent* pEquipment = findTarget( query, outValue );
                if ( pEquipment == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pEquipment->getBag().getItemCount( hashed_string( query._key ) ) ) );
                return true;
            }

            /** @brief 감시 값 `Equipment.Stat`: 낀 장비 능력치(key, 예: armor)의 합입니다(숨김 칸 제외, `Equipment::computeStats`). */
            [[nodiscard]] static bool readStat( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CharacterEquipmentComponent* pEquipment = findTarget( query, outValue );
                if ( pEquipment == nullptr )
                    return false;
                StatBlock stats;
                pEquipment->getEquipment().computeStats( stats );
                outValue.setNumber( static_cast<float64>( stats.getValue( hashed_string( query._key ) ) ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( equipmentWorn, "Equipment.Worn", "Item the character's equipment wears in the slot named by key ('(empty)' when empty)",
                               &CharacterEquipmentComponentWatchesInternal::readWorn, true, true );
    SW_AUTOMATION_QUERY_WATCH( equipmentBagItem, "Equipment.BagItem", "Item in the equipment bag slot key ('<id>' or '<id> +N' with N instance values, '(empty)')",
                               &CharacterEquipmentComponentWatchesInternal::readBagItem, true, true );
    SW_AUTOMATION_QUERY_WATCH( equipmentBagUsedSlots, "Equipment.BagUsedSlots", "Equipment bag slots that hold an item", &CharacterEquipmentComponentWatchesInternal::readBagUsedSlots, true, false );
    SW_AUTOMATION_QUERY_WATCH( equipmentBagCount, "Equipment.BagCount", "How many items the equipment bag slot key holds (0 when empty)", &CharacterEquipmentComponentWatchesInternal::readBagCount, true, true );
    SW_AUTOMATION_QUERY_WATCH( equipmentItemCount, "Equipment.ItemCount", "How many of the item key the equipment bag holds", &CharacterEquipmentComponentWatchesInternal::readItemCount, true, true );
    SW_AUTOMATION_QUERY_WATCH( equipmentStat, "Equipment.Stat", "Sum of the stat key over the worn equipment (suppressed slots excluded)", &CharacterEquipmentComponentWatchesInternal::readStat, true, true );
} // namespace sw
