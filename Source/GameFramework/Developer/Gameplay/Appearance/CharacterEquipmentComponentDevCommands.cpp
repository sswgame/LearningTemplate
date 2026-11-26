/**
 * @file CharacterEquipmentComponentDevCommands.cpp
 * @brief 장비(`CharacterEquipmentComponent`)의 개발 명령입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
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
        struct CharacterEquipmentComponentDevCommandsInternal
        {
            /** @brief 활성 씬에서 이름 @p objectName 의 시작한 장비 컴포넌트입니다. 없으면 까닭을 @p outReply 에 적고 nullptr 입니다. */
            static CharacterEquipmentComponent* findEquipment( const string& objectName, string& outReply )
            {
                GameObject*                  pObject    = engine::findActiveObject( hashed_string( objectName ) );
                CharacterEquipmentComponent* pEquipment = pObject != nullptr ? pObject->getComponent<CharacterEquipmentComponent>() : nullptr;
                if ( pEquipment == nullptr || pEquipment->isStarted() == false )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "no object named '%#' with a started CharacterEquipmentComponent in the active scene", objectName );
                    outReply = string( replyBuilder.view() );
                }
                return pEquipment != nullptr && pEquipment->isStarted() ? pEquipment : nullptr;
            }

            /** @brief `equipment.give <object> <item>` — 가방에 새 아이템 인스턴스 하나를 넣는다. */
            static bool runGive( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 2 )
                    return false;
                CharacterEquipmentComponent* pEquipment = findEquipment( listArgument[0], outReply );
                if ( pEquipment == nullptr )
                    return false;
                InventorySlot item;
                item._itemID = hashed_string( listArgument[1] );
                item._count  = 1;
                if ( pEquipment->addToBag( item ) == false )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "the bag cannot take '%#' (unknown item or no room)", listArgument[1] );
                    outReply = string( replyBuilder.view() );
                    return false;
                }
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "gave %#", listArgument[1] );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief `equipment.equip <object> <bagSlot> [slot]` — 가방 칸의 인스턴스를 낀다. 벗겨진 것은 가방으로 간다. */
            static bool runEquip( const vector<string>& listArgument, string& outReply )
            {
                int32 bagSlot = 0;
                if ( ( listArgument.size() != 2 && listArgument.size() != 3 ) || StringUtil::parseInt( listArgument[1], bagSlot ) == false )
                    return false;
                CharacterEquipmentComponent* pEquipment = findEquipment( listArgument[0], outReply );
                if ( pEquipment == nullptr )
                    return false;
                const hashed_string slot   = listArgument.size() == 3 ? hashed_string( listArgument[2] ) : hashed_string{};
                const EquipResult   result = pEquipment->equipFromBag( bagSlot, slot );
                if ( result != EquipResult::Ok )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "could not equip bag slot %# (EquipResult %#)", listArgument[1], static_cast<uint32>( result ) );
                    outReply = string( replyBuilder.view() );
                    return false;
                }
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "equipped bag slot %#", listArgument[1] );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief `item.pickup <character> <worldItem>` — 월드 아이템의 조각을 캐릭터 가방에 모두 넣고 그 오브젝트를 없앤다. 다 들어가지 않으면 그대로 둔다(가방 가득). */
            static bool runPickUp( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 2 )
                    return false;
                CharacterEquipmentComponent* pEquipment = findEquipment( listArgument[0], outReply );
                if ( pEquipment == nullptr )
                    return false;
                GameObject*      pObject = engine::findActiveObject( hashed_string( listArgument[1] ) );
                PickupComponent* pItem   = pObject != nullptr ? pObject->getComponent<PickupComponent>() : nullptr;
                if ( pItem == nullptr )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "no object named '%#' with a PickupComponent", listArgument[1] );
                    outReply = string( replyBuilder.view() );
                    return false;
                }
                if ( pItem->pickUp( pEquipment->getBagForTrade() ) == false )
                {
                    SW_LOG_INFO_ID( "Pickup.PickUpRefused", "The bag of %# has no room for %# - it stays on the ground", listArgument[0].c_str(), listArgument[1].c_str() );
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "the bag has no room for %#", listArgument[1] );
                    outReply = string( replyBuilder.view() );
                    return true; // 거절도 답이다 — 시나리오는 로그와 감시 값으로 본다
                }
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "picked up %#", listArgument[1] );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /**
             * @brief `equipment.dye <object> <slot> <parameter> <value…>` — 낀 아이템 인스턴스의 꾸미기 값 하나를 바꾼다(염색). 값의 종류는 그 아이템 외형의 스키마가 정한다
             *        (슬라이더: 수 하나, 색: `r g b [a]`, 고르기: 항목 이름).
             */
            static bool runDye( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() < 4 )
                    return false;
                CharacterEquipmentComponent* pEquipment = findEquipment( listArgument[0], outReply );
                if ( pEquipment == nullptr )
                    return false;
                const hashed_string           slot      = hashed_string( listArgument[1] );
                const InventorySlot*          pWorn     = pEquipment->getEquipment().findEquipped( slot );
                const AppearanceDatabase*     pDatabase = game::findSubsystem<AppearanceDatabase>();
                const CustomizationSchemaDef* pSchema   = pWorn != nullptr && pDatabase != nullptr ? CharacterEquipmentComponent::findItemSchema( *pDatabase, pWorn->_itemID ) : nullptr;
                const CustomizationParamDef*  pParam    = pSchema != nullptr ? pSchema->findParameter( hashed_string( listArgument[2] ) ) : nullptr;
                if ( pParam == nullptr )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "the item in '%#' has no customization parameter '%#'", listArgument[1], listArgument[2] );
                    outReply = string( replyBuilder.view() );
                    return false;
                }
                CustomizationValue value;
                value._parameter       = pParam->_name;
                const size_t valueSize = listArgument.size() - 3;
                bool         bRead     = false;
                if ( pParam->_kind == CustomizationKind::Slider )
                    bRead = valueSize == 1 && StringUtil::parseFloat( listArgument[3], value._number._x );
                else if ( pParam->_kind == CustomizationKind::Color )
                {
                    value._number._w = 1.0f;
                    bRead            = ( valueSize == 3 || valueSize == 4 ) && StringUtil::parseFloat( listArgument[3], value._number._x ) &&
                            StringUtil::parseFloat( listArgument[4], value._number._y ) && StringUtil::parseFloat( listArgument[5], value._number._z ) &&
                            ( valueSize == 3 || StringUtil::parseFloat( listArgument[6], value._number._w ) );
                }
                else
                {
                    value._option = hashed_string( listArgument[3] );
                    bRead         = valueSize == 1 && pParam->findOption( value._option ) != nullptr;
                }
                if ( bRead == false || pEquipment->setEquippedValue( slot, value ) == false )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "parameter '%#' does not take that value (slider: one number, color: r g b [a], choice: an option name)", listArgument[2] );
                    outReply = string( replyBuilder.view() );
                    return false;
                }
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "%# dyed", listArgument[2] );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief `equipment.sort <object>`: 가방을 정렬한다(인벤토리 화면의 정렬과 같은 명령 길, `applyCommand`). */
            static bool runSort( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                CharacterEquipmentComponent* pEquipment = findEquipment( listArgument[0], outReply );
                if ( pEquipment == nullptr )
                    return false;
                InventoryCommand command{};
                command._kind = InventoryCommandKind::Sort;
                vector<InventorySlot> listDropped;
                (void)pEquipment->applyCommand( command, listDropped );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "sorted the bag of %#", listArgument[0] );
                outReply = string( replyBuilder.view() );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( EquipmentGive, "equipment.give", "equipment.give <object> <item>", "Put a new item instance in a character's equipment bag", &CharacterEquipmentComponentDevCommandsInternal::runGive );
    SW_DEV_COMMAND( EquipmentEquip, "equipment.equip", "equipment.equip <object> <bagSlot> [slot]", "Wear the item instance of a bag slot (what it displaces goes to the bag)",
                    &CharacterEquipmentComponentDevCommandsInternal::runEquip );
    SW_DEV_COMMAND( ItemPickUp, "item.pickup", "item.pickup <character> <worldItem>", "Pick a world item stack up into a character's equipment bag (refused when it does not all fit)",
                    &CharacterEquipmentComponentDevCommandsInternal::runPickUp );
    SW_DEV_COMMAND( EquipmentDye, "equipment.dye", "equipment.dye <object> <slot> <parameter> <number | r g b [a] | option>",
                    "Change one customization value (dye) of the item instance worn in a slot", &CharacterEquipmentComponentDevCommandsInternal::runDye );
    SW_DEV_COMMAND( EquipmentSort, "equipment.sort", "equipment.sort <object>", "Sort a character's equipment bag (stacks merge)", &CharacterEquipmentComponentDevCommandsInternal::runSort );
} // namespace sw
