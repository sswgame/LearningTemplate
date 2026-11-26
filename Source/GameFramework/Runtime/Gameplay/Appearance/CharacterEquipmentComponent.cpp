#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Appearance/CharacterEquipmentComponent.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectEvents.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/FormatUpgrade.h"

#include "GameFramework/Runtime/Foundation/GameData/StatBlock.h"
#include "GameFramework/Runtime/Foundation/Save/StatefulComponentRegistrar.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearance.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearanceComponent.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CustomizationSchema.h"
#include "GameFramework/Runtime/Gameplay/Appearance/ItemVisual.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Runtime/Gameplay/Inventory/PickupComponent.h"

namespace sw
{
    namespace
    {
        /** @brief 장비 칸과 가방을 게임 세이브에 싣습니다(모든 게임 — 게임마다 올리지 않는다). */
        const StatefulComponentRegistrar<CharacterEquipmentComponent> s_characterEquipmentComponentStateRegistration{ "CharacterEquipmentComponent" };
    } // namespace

    CharacterEquipmentComponent::CharacterEquipmentComponent()
        : _character{}
        , _specTags{}
        , _tagSubscription{}
        , _equipment{}
        , _bag{}
        , _pendingStateBytes{}
        , _bagSlotCount{ 16 }
        , _bagMaxWeight{ 0.0f }
        , _pickupPrefab{}
        , _bStarted{ SW_FALSE }
        , _itemReceiverPort{}
    {
    }

    CharacterEquipmentComponent::~CharacterEquipmentComponent() = default;

    void CharacterEquipmentComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        _tagSubscription = GameObjectEvents::listen<&CharacterEquipmentComponent::handleTagsChanged>( *this, &GameObjectEvents::_tagsChanged );
    }

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
        _equipment.initialize( *pCatalog, pDatabase->getSlotCatalog().makeEquipmentLayout(), &pDatabase->getSets(), &_character );
        if ( _bag.initialize( *pCatalog, _bagSlotCount, _bagMaxWeight ) == false )
        {
            SW_LOG_ERROR_ID( "CharacterEquipment.BagInvalid", "'%#' bag of %# slots could not be made", getOwner() != nullptr ? getOwner()->getName().c_str() : "?", _bagSlotCount );
            return;
        }
        _bStarted = SW_TRUE;
        // 세이브의 차림을 놓기 전에 캐릭터를 안다 — 외형이 아직 조립하지 않아도 전개는 알 수 있다.
        CharacterAppearanceComponent* pAppearance = getOwner()->getComponent<CharacterAppearanceComponent>();
        CharacterAppearanceSpec       spec;
        if ( pAppearance != nullptr && pAppearance->computeSpec( spec ) )
            setCharacterSpec( spec );
        rebuildCharacter( getOwner()->getTags() );
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

    void CharacterEquipmentComponent::setCharacterSpec( const CharacterAppearanceSpec& spec )
    {
        _character._bodyType  = spec._bodyType;
        _character._bodyShape = spec._bodyShape;
        _specTags             = spec._tags;
        if ( _bStarted == SW_FALSE )
            return;
        rebuildCharacter( getOwner()->getTags() );
        _equipment.settleCharacterChange( _bag );
    }

    void CharacterEquipmentComponent::handleTagsChanged( const TagContainer& tags )
    {
        if ( _bStarted == SW_FALSE )
            return;
        const uint32 revision = _equipment.getRevision();
        rebuildCharacter( tags );
        _equipment.settleCharacterChange( _bag );
        if ( _equipment.getRevision() != revision )
            updateAppearance();
    }

    void CharacterEquipmentComponent::rebuildCharacter( const TagContainer& ownerTags )
    {
        _character._tags = ownerTags;
        for ( const TagID& tag : _specTags.getTags() )
        {
            _character._tags.addTag( tag );
        }
    }

    ItemGrantResult CharacterEquipmentComponent::ItemReceiverPort::receiveItem( const hashed_string& item, int32 count )
    {
        CharacterEquipmentComponent& owner = getPortOwner<CharacterEquipmentComponent>( *this, offsetof( CharacterEquipmentComponent, _itemReceiverPort ) );
        if ( owner.isStarted() == false || owner._bag.hasRoomFor( item, count ) == false )
            return ItemGrantResult::NoRoom;
        (void)owner._bag.addItem( item, count ); // 자리를 먼저 봤으므로 모두 들어간다
        return ItemGrantResult::Granted;
    }

    ItemGrantResult CharacterEquipmentComponent::ItemReceiverPort::receiveStack( const InventorySlot& stack )
    {
        CharacterEquipmentComponent& owner = getPortOwner<CharacterEquipmentComponent>( *this, offsetof( CharacterEquipmentComponent, _itemReceiverPort ) );
        return owner.addToBag( stack ) ? ItemGrantResult::Granted : ItemGrantResult::NoRoom;
    }

    bool CharacterEquipmentComponent::addToBag( const InventorySlot& item )
    {
        return _bStarted == SW_TRUE && _bag.addStack( item );
    }

    InventoryCommandResult CharacterEquipmentComponent::applyCommand( const InventoryCommand& command, vector<InventorySlot>& outListDropped )
    {
        if ( _bStarted == SW_FALSE )
            return InventoryCommandResult::InvalidSlot;
        const uint32                 equipmentRevision = _equipment.getRevision();
        const InventoryCommandResult result            = command.apply( _bag, _equipment, outListDropped );
        if ( _equipment.getRevision() != equipmentRevision )
            updateAppearance();
        return result;
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
                               pSchema->_id.c_str(), inoutItem.getCustomization().getSchemaVersion().value_or( 0 ), pSchema->getVersion() );
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
        io.header( kStateFormat );
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
        _equipment.restoreWorn( listWorn, bag );
        _bag = std::move( bag );
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
