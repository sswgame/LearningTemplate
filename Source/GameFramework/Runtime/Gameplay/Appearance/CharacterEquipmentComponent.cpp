#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Appearance/CharacterEquipmentComponent.h"

#include "Core/Encoding/Base64Util.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/FormatUpgrade.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/ComponentStateStore.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearanceComponent.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CustomizationSchema.h"
#include "GameFramework/Runtime/Gameplay/Appearance/ItemVisual.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"

namespace sw
{
    namespace
    {
        /** @brief 장비 칸과 가방을 게임 세이브에 싣습니다(모든 게임 — 게임마다 올리지 않는다). */
        const StatefulComponentRegistrar<CharacterEquipmentComponent> s_stateRegistration{ "CharacterEquipmentComponent" };

        struct CharacterEquipmentStateInternal
        {
            /** @brief 판 1 → 2: 낀 칸(칸 이름 + 슬롯)과 가방(인벤토리 판 2 본문)의 슬롯마다 꾸미기 스키마 판(1)을 넣는다. */
            [[nodiscard]] static bool upgradeStateFrom1( Archive& inOldBody, Archive& outNewBody )
            {
                StateIO inOld( inOldBody );
                StateIO outNew( outNewBody );
                uint32  wornCount = 0;
                inOld.count( 4 + Inventory::kUnversionedSlotMinBytes, wornCount );
                outNew.count( 4 + Inventory::kSlotStateMinBytes, wornCount );
                for ( uint32 index = 0; index < wornCount && inOld.isOk(); ++index )
                {
                    hashed_string slotName;
                    inOld.value( slotName );
                    outNew.value( slotName );
                    Inventory::upgradeSlotFromUnversioned( inOld, outNew );
                }
                Inventory::upgradeStateFrom2( inOld, outNew );
                return inOld.isOk() && outNew.isOk();
            }
        };
    } // namespace

    SW_STATE_FORMAT_UPGRADE( characterEquipment1To2, "CEQP", 1, 2, &CharacterEquipmentStateInternal::upgradeStateFrom1 );

    CharacterEquipmentComponent::CharacterEquipmentComponent()
        : _equipment{}
        , _bag{}
        , _pendingStateBytes{}
        , _bagSlotCount{ 16 }
        , _bStarted{ SW_FALSE }
    {
    }

    CharacterEquipmentComponent::~CharacterEquipmentComponent() = default;

    void CharacterEquipmentComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        const AppearanceDatabase* pDatabase = game::findSubsystem<AppearanceDatabase>();
        const ItemCatalog*        pCatalog  = pDatabase != nullptr ? pDatabase->getItemCatalog() : nullptr;
        if ( pCatalog == nullptr )
        {
            SW_LOG_ERROR_ID( "CharacterEquipment.NoItemCatalog", "'%#' has equipment but the AppearanceDatabase game service has no item catalog - nothing can be worn",
                             getOwner() != nullptr ? getOwner()->getName().c_str() : "?" );
            return;
        }
        _equipment.initialize( pCatalog, pDatabase->getSlotCatalog().makeEquipmentLayout() );
        if ( _bag.initialize( pCatalog, _bagSlotCount ) == false )
        {
            SW_LOG_ERROR_ID( "CharacterEquipment.BagInvalid", "'%#' bag of %# slots could not be made", getOwner() != nullptr ? getOwner()->getName().c_str() : "?", _bagSlotCount );
            return;
        }
        _bStarted = SW_TRUE;
        (void)applyPendingState();
        updateAppearance();
    }

    void CharacterEquipmentComponent::onEndPlay()
    {
        // 외형이 빌린 장비를 놓게 한다 — 이 컴포넌트가 먼저 사라지면 외형이 해제된 장비를 본다.
        CharacterAppearanceComponent* pAppearance = getOwner() != nullptr ? getOwner()->getComponent<CharacterAppearanceComponent>() : nullptr;
        if ( pAppearance != nullptr )
            pAppearance->setEquipment( nullptr );
        _bStarted = SW_FALSE;
        Component::onEndPlay();
    }

    bool CharacterEquipmentComponent::addToBag( const InventorySlot& item )
    {
        return _bStarted == SW_TRUE && _bag.addStack( item );
    }

    EquipResult CharacterEquipmentComponent::equipFromBag( int32 bagSlot, const hashed_string& slot )
    {
        if ( _bStarted == SW_FALSE || bagSlot < 0 || bagSlot >= _bag.getSlotCount() )
            return EquipResult::EmptySlot;
        const EquipResult result = _equipment.equipFromInventory( _bag, bagSlot, slot );
        updateAppearance();
        return result;
    }

    bool CharacterEquipmentComponent::unequipToBag( const hashed_string& slot )
    {
        if ( _bStarted == SW_FALSE || _equipment.unequipToInventory( slot, _bag ) == false )
            return false;
        updateAppearance();
        return true;
    }

    bool CharacterEquipmentComponent::setEquippedValue( const hashed_string& slot, const CustomizationValue& value )
    {
        const InventorySlot* pWorn = _equipment.findEquipped( slot );
        if ( _bStarted == SW_FALSE || pWorn == nullptr || pWorn->isEmpty() )
            return false;
        const AppearanceDatabase*     pDatabase = game::findSubsystem<AppearanceDatabase>();
        const CustomizationSchemaDef* pSchema   = pDatabase != nullptr ? findItemSchema( *pDatabase, pWorn->_itemID ) : nullptr;
        InventorySlot                 dyed      = *pWorn;
        CustomizationValueSet         values    = dyed.getCustomization();
        if ( pSchema == nullptr || pSchema->applyValue( value, false, values ) == false || dyed.setCustomization( values ) == false )
            return false;
        if ( _equipment.setEquippedInstance( slot, dyed ) == false )
            return false;
        updateAppearance();
        return true;
    }

    const CustomizationSchemaDef* CharacterEquipmentComponent::findItemSchema( const AppearanceDatabase& database, const hashed_string& itemID )
    {
        const ItemDef*       pItem   = database.getItemCatalog() != nullptr ? database.getItemCatalog()->find( itemID ) : nullptr;
        const ItemVisualDef* pVisual = pItem != nullptr ? database.getVisuals().findVisual( pItem->_visualID ) : nullptr;
        return pVisual != nullptr ? database.getSchemas().findSchema( pVisual->_customization ) : nullptr;
    }

    bool CharacterEquipmentComponent::upgradeSavedItem( const AppearanceDatabase& database, InventorySlot& inoutItem )
    {
        const CustomizationSchemaDef* pSchema = findItemSchema( database, inoutItem._itemID );
        if ( pSchema == nullptr || inoutItem.getCustomization().isEmpty() )
            return true; // 스키마가 없는 아이템의 값은 그릴 때도 쓰이지 않는다
        CustomizationValueSet upgraded;
        vector<hashed_string> listDropped;
        const expected<void>  result = pSchema->migrate( inoutItem.getCustomization(), upgraded, &listDropped );
        if ( result.has_value() == false )
        {
            SW_LOG_WARNING_ID( "CharacterEquipment.SavedItemFromNewerSchema", "Saved '%#' has customization of schema '%#' version %# newer than this content (%#) - the equipment state is not loaded", inoutItem._itemID.c_str(),
                               pSchema->_id.c_str(), inoutItem.getCustomization().getSchemaVersion(), pSchema->getVersion() );
            return false;
        }
        for ( const hashed_string& dropped : listDropped )
        {
            SW_LOG_ERROR_ID( "CharacterEquipment.SavedValueDropped", "Saved '%#' value '%#' has no meaning in schema '%#' version %# - only that value is dropped (a schema change without a migration step)",
                             inoutItem._itemID.c_str(), dropped.c_str(), pSchema->_id.c_str(), pSchema->getVersion() );
        }
        return inoutItem.setCustomization( upgraded );
    }

    void CharacterEquipmentComponent::updateAppearance()
    {
        CharacterAppearanceComponent* pAppearance = getOwner() != nullptr ? getOwner()->getComponent<CharacterAppearanceComponent>() : nullptr;
        if ( pAppearance != nullptr )
            pAppearance->setEquipment( _bStarted == SW_TRUE ? &_equipment : nullptr );
    }

    void CharacterEquipmentComponent::serializeState( StateIO& io )
    {
        io.header( kStateTag, kStateVersion );
        // 낀 칸마다 칸 이름 + 인스턴스(`Inventory::serializeSlot` — 가방 · 판 상태의 인벤토리와 같은 바이트)
        constexpr uint32 kWornMinBytes = 4 + Inventory::kSlotStateMinBytes;
        if ( io.isWriting() )
        {
            vector<EquipmentSlot> listWorn;
            for ( const EquipmentSlot& slot : _equipment.getSlots() )
            {
                if ( slot._item.isEmpty() == false )
                    listWorn.push_back( slot );
            }
            uint32 count = static_cast<uint32>( listWorn.size() );
            io.count( kWornMinBytes, count );
            for ( EquipmentSlot& slot : listWorn )
            {
                io.value( slot._name );
                Inventory::serializeSlot( io, slot._item );
            }
            io.value( _bag );
            return;
        }
        // 지역 사본에 읽고 끝까지 맞으면 바꾼다(반쯤 읽은 장비를 남기지 않는다). 카탈로그가 선 뒤에만 읽는다(`applyPendingState`).
        if ( io.check( _bStarted == SW_TRUE ) == false )
            return;
        uint32 count = 0;
        io.count( kWornMinBytes, count );
        vector<EquipmentSlot> listWorn( count );
        for ( EquipmentSlot& slot : listWorn )
        {
            io.value( slot._name );
            Inventory::serializeSlot( io, slot._item );
        }
        Inventory bag = _bag;
        io.value( bag );
        if ( io.isOk() == false )
            return;
        // 읽은 인스턴스 값을 그 아이템 스키마의 지금 판으로 올린다(패치 뒤 첫 불러오기) — 뜻이 없는 값은 그 값만 빼고 알린다. 더 새 판(다운그레이드)이면 읽지 않는다.
        if ( const AppearanceDatabase* pDatabase = game::findSubsystem<AppearanceDatabase>(); pDatabase != nullptr )
        {
            for ( EquipmentSlot& slot : listWorn )
            {
                if ( io.check( upgradeSavedItem( *pDatabase, slot._item ) ) == false )
                    return;
            }
            for ( int32 bagSlot = 0; bagSlot < bag.getSlotCount(); ++bagSlot )
            {
                InventorySlot item = bag.getSlot( bagSlot );
                if ( item.getCustomization().isEmpty() )
                    continue;
                if ( io.check( upgradeSavedItem( *pDatabase, item ) && bag.setSlotCustomization( bagSlot, item.getCustomization() ) ) == false )
                    return;
            }
        }
        Equipment equipment = _equipment;
        for ( const EquipmentSlot& slot : _equipment.getSlots() )
        {
            vector<InventorySlot> listRemoved;
            (void)equipment.unequip( slot._name, listRemoved ); // 빈 칸은 EmptySlot — 지금 차림을 모두 벗긴다
        }
        for ( const EquipmentSlot& slot : listWorn )
        {
            vector<InventorySlot> listRemoved;
            const EquipResult     result = equipment.equip( slot._name, slot._item, listRemoved );
            if ( result != EquipResult::Ok )
            {
                SW_LOG_WARNING_ID( "CharacterEquipment.SavedItemNotWorn", "Saved '%#' in slot '%#' cannot be worn now (%#) - it goes to the bag", slot._item._itemID.c_str(), slot._name.c_str(),
                                   static_cast<uint32>( result ) );
                (void)bag.addStack( slot._item );
            }
        }
        _equipment = equipment;
        _bag       = std::move( bag );
    }

    void CharacterEquipmentComponent::restoreState( vector<uint8>&& bytes )
    {
        _pendingStateBytes = std::move( bytes );
        if ( _bStarted == SW_TRUE && applyPendingState() )
            updateAppearance();
    }

    bool CharacterEquipmentComponent::applyPendingState()
    {
        if ( _pendingStateBytes.empty() )
            return false;
        Archive    archive( _pendingStateBytes.data(), static_cast<uint64>( _pendingStateBytes.size() ) );
        const bool bRead = StateIO::readInPlace( *this, archive );
        _pendingStateBytes.clear();
        if ( bRead == false )
            SW_LOG_WARNING_ID( "CharacterEquipment.StateNotRead", "Equipment state of '%#' did not read - keeping the current equipment", getOwner() != nullptr ? getOwner()->getName().c_str() : "?" );
        return bRead;
    }
} // namespace sw

namespace sw
{
    namespace
    {
        struct CharacterEquipmentCommandsInternal
        {
            /** @brief 활성 씬에서 이름 @p objectName 의 시작한 장비 컴포넌트입니다. 없으면 까닭을 @p outReply 에 적고 nullptr 입니다. */
            static CharacterEquipmentComponent* findEquipment( const string& objectName, string& outReply )
            {
                GameObject*                  pObject    = engine::findActiveObject( hashed_string( objectName ) );
                CharacterEquipmentComponent* pEquipment = pObject != nullptr ? pObject->getComponent<CharacterEquipmentComponent>() : nullptr;
                if ( pEquipment == nullptr || pEquipment->isStarted() == false )
                    outReply = "no object named '" + objectName + "' with a started CharacterEquipmentComponent in the active scene";
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
                    outReply = "the bag cannot take '" + listArgument[1] + "' (unknown item or no room)";
                    return false;
                }
                outReply = "gave " + listArgument[1];
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
                    outReply = "could not equip bag slot " + listArgument[1] + " (EquipResult " + to_string( static_cast<uint32>( result ) ) + ")";
                    return false;
                }
                outReply = "equipped bag slot " + listArgument[1];
                return true;
            }

            /** @brief `equipment.unequip <object> <slot>` — 장비 칸을 벗겨 가방에 넣는다. */
            static bool runUnequip( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 2 )
                    return false;
                CharacterEquipmentComponent* pEquipment = findEquipment( listArgument[0], outReply );
                if ( pEquipment == nullptr )
                    return false;
                if ( pEquipment->unequipToBag( hashed_string( listArgument[1] ) ) == false )
                {
                    outReply = "could not unequip '" + listArgument[1] + "' (empty slot or no room in the bag)";
                    return false;
                }
                outReply = "unequipped " + listArgument[1];
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
                    outReply = "the item in '" + listArgument[1] + "' has no customization parameter '" + listArgument[2] + "'";
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
                    outReply = "parameter '" + listArgument[2] + "' does not take that value (slider: one number, color: r g b [a], choice: an option name)";
                    return false;
                }
                outReply = listArgument[2] + " dyed";
                return true;
            }

            /**
             * @brief `equipment.restore <object> <base64>` — 장비 상태 바이트(`serializeState` 가 쓴 것, base64)를 겁니다. 게임 세이브의 그 구간과 같은 길이다.
             *        옛 판 세이브 표본(손으로 쓴 바이트)을 시나리오에서 불러 판 올림 사슬을 본다. 읽지 못하면 지금 장비를 그대로 두고 알린다.
             */
            static bool runRestore( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 2 )
                    return false;
                CharacterEquipmentComponent* pEquipment = findEquipment( listArgument[0], outReply );
                if ( pEquipment == nullptr )
                    return false;
                vector<uint8> bytes;
                if ( Base64Util::decode( listArgument[1], bytes ) == false || bytes.empty() )
                {
                    outReply = "the state is not base64 bytes";
                    return false;
                }
                pEquipment->restoreState( std::move( bytes ) );
                outReply = "restored the equipment state of " + listArgument[0];
                return true;
            }

            // ---- 감시 값(대상은 장비 컴포넌트를 가진 오브젝트) ----

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
                const size_t valueCount = item.getCustomization().getCount();
                outValue.setText( valueCount == 0 ? string( item._itemID.c_str() ) : string( item._itemID.c_str() ) + " +" + to_string( static_cast<uint32>( valueCount ) ) );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( EquipmentGive, "equipment.give", "equipment.give <object> <item>", "Put a new item instance in a character's equipment bag", &CharacterEquipmentCommandsInternal::runGive );
    SW_DEV_COMMAND( EquipmentEquip, "equipment.equip", "equipment.equip <object> <bagSlot> [slot]", "Wear the item instance of a bag slot (what it displaces goes to the bag)",
                    &CharacterEquipmentCommandsInternal::runEquip );
    SW_DEV_COMMAND( EquipmentUnequip, "equipment.unequip", "equipment.unequip <object> <slot>", "Take off an equipment slot into the bag", &CharacterEquipmentCommandsInternal::runUnequip );
    SW_DEV_COMMAND( EquipmentRestore, "equipment.restore", "equipment.restore <object> <base64>", "Apply saved equipment state bytes (an older save sample upgrades on read)",
                    &CharacterEquipmentCommandsInternal::runRestore );
    SW_DEV_COMMAND( EquipmentDye, "equipment.dye", "equipment.dye <object> <slot> <parameter> <number | r g b [a] | option>",
                    "Change one customization value (dye) of the item instance worn in a slot", &CharacterEquipmentCommandsInternal::runDye );
    SW_AUTOMATION_QUERY_WATCH( equipmentWorn, "Equipment.Worn", "Item the character's equipment wears in the slot named by key ('(empty)' when empty)",
                               &CharacterEquipmentCommandsInternal::readWorn, true, true );
    SW_AUTOMATION_QUERY_WATCH( equipmentBagItem, "Equipment.BagItem", "Item in the equipment bag slot key ('<id>' or '<id> +N' with N instance values, '(empty)')",
                               &CharacterEquipmentCommandsInternal::readBagItem, true, true );
} // namespace sw
