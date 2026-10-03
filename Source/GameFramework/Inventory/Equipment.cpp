#include "pch.h"

#include "GameFramework/Inventory/Equipment.h"

#include "GameFramework/Data/GameDataXml.h"
#include "GameFramework/Inventory/ItemCatalog.h"

namespace sw
{
    const utf8* toString( EquipResult result )
    {
        switch ( result )
        {
            case EquipResult::Ok:
                return "Ok";
            case EquipResult::UnknownSlot:
                return "UnknownSlot";
            case EquipResult::UnknownItem:
                return "UnknownItem";
            case EquipResult::WrongSlot:
                return "WrongSlot";
            case EquipResult::InventoryFull:
                return "InventoryFull";
        }
        return "Unknown";
    }

    Equipment::Equipment()
        : _listSlot{}
        , _pCatalog{ nullptr }
        , _revision{ 0 }
    {
    }

    void Equipment::initialize( const ItemCatalog* pCatalog, string_view slotLayout )
    {
        _pCatalog = pCatalog;
        _listSlot.clear();
        GameDataXml::forEachToken( slotLayout, ", ", [&]( string_view token )
        {
            EquipSlot         slot;
            const size_t      colon = token.find( ':' );
            const string_view name  = colon == string_view::npos ? token : token.substr( 0, colon );
            slot._name              = hashed_string( name );
            slot._accept            = hashed_string( colon == string_view::npos ? name : token.substr( colon + 1 ) );
            _listSlot.push_back( slot );
        } );
        ++_revision;
    }

    EquipSlot* Equipment::findSlot( const hashed_string& slot )
    {
        for ( EquipSlot& equipSlot : _listSlot )
        {
            if ( equipSlot._name == slot )
                return &equipSlot;
        }
        return nullptr;
    }

    bool Equipment::canEquip( const hashed_string& slot, const hashed_string& itemId ) const
    {
        const ItemDef* pDef = _pCatalog != nullptr ? _pCatalog->findItem( itemId ) : nullptr;
        for ( const EquipSlot& equipSlot : _listSlot )
        {
            if ( equipSlot._name == slot )
                return pDef != nullptr && pDef->_equipSlot == equipSlot._accept;
        }
        return false;
    }

    hashed_string Equipment::findSlotFor( const hashed_string& itemId ) const
    {
        const ItemDef* pDef = _pCatalog != nullptr ? _pCatalog->findItem( itemId ) : nullptr;
        if ( pDef == nullptr || pDef->isEquipment() == false )
            return hashed_string{};
        hashed_string firstMatch{};
        for ( const EquipSlot& equipSlot : _listSlot )
        {
            if ( equipSlot._accept != pDef->_equipSlot )
                continue;
            if ( equipSlot._item.isEmpty() )
                return equipSlot._name;
            if ( firstMatch.empty() )
                firstMatch = equipSlot._name;
        }
        return firstMatch;
    }

    EquipResult Equipment::equip( const hashed_string& slot, const ItemStack& item, ItemStack& outPrevious )
    {
        outPrevious           = ItemStack{};
        EquipSlot* pEquipSlot = findSlot( slot );
        if ( pEquipSlot == nullptr )
            return EquipResult::UnknownSlot;
        const ItemDef* pDef = _pCatalog != nullptr ? _pCatalog->findItem( item._itemId ) : nullptr;
        if ( pDef == nullptr || item.isEmpty() )
            return EquipResult::UnknownItem;
        if ( pDef->_equipSlot != pEquipSlot->_accept )
            return EquipResult::WrongSlot;
        outPrevious              = pEquipSlot->_item;
        pEquipSlot->_item        = item;
        pEquipSlot->_item._count = 1;
        ++_revision;
        return EquipResult::Ok;
    }

    ItemStack Equipment::unequip( const hashed_string& slot )
    {
        EquipSlot* pEquipSlot = findSlot( slot );
        if ( pEquipSlot == nullptr || pEquipSlot->_item.isEmpty() )
            return ItemStack{};
        const ItemStack item = pEquipSlot->_item;
        pEquipSlot->_item    = ItemStack{};
        ++_revision;
        return item;
    }

    EquipResult Equipment::equipFromInventory( Inventory& inventory, int32 inventorySlot, const hashed_string& slot )
    {
        if ( inventorySlot < 0 || inventorySlot >= inventory.getSlotCount() || inventory.getSlot( inventorySlot ).isEmpty() )
            return EquipResult::UnknownItem;
        const ItemStack     source     = inventory.getSlot( inventorySlot );
        const hashed_string targetSlot = slot.empty() ? findSlotFor( source._itemId ) : slot;
        if ( targetSlot.empty() )
            return EquipResult::WrongSlot; // 그 아이템을 받는 칸이 없다
        if ( canEquip( targetSlot, source._itemId ) == false )
            return findSlot( targetSlot ) == nullptr ? EquipResult::UnknownSlot : EquipResult::WrongSlot;
        // 하나를 꺼내고, 벗은 것은 그 자리(또는 빈 칸)로. 자리가 없으면 되돌린다.
        const ItemStack taken = inventory.takeFromSlot( inventorySlot, 1 );
        ItemStack       previous;
        (void)equip( targetSlot, taken, previous );
        if ( previous.isEmpty() == false && inventory.addStack( previous ) == false )
        {
            ItemStack ignored;
            (void)equip( targetSlot, previous, ignored );
            (void)inventory.addStack( taken );
            return EquipResult::InventoryFull;
        }
        return EquipResult::Ok;
    }

    bool Equipment::unequipToInventory( const hashed_string& slot, Inventory& inventory )
    {
        const ItemStack* pItem = findEquipped( slot );
        if ( pItem == nullptr )
            return false;
        if ( inventory.addStack( *pItem ) == false )
            return false;
        (void)unequip( slot );
        return true;
    }

    const ItemStack* Equipment::findEquipped( const hashed_string& slot ) const
    {
        for ( const EquipSlot& equipSlot : _listSlot )
        {
            if ( equipSlot._name == slot )
                return equipSlot._item.isEmpty() ? nullptr : &equipSlot._item;
        }
        return nullptr;
    }

    void Equipment::computeStats( StatBlock& outStats ) const
    {
        outStats.clear();
        for ( const EquipSlot& equipSlot : _listSlot )
        {
            if ( equipSlot._item.isEmpty() )
                continue;
            const ItemDef* pDef = _pCatalog != nullptr ? _pCatalog->findItem( equipSlot._item._itemId ) : nullptr;
            if ( pDef != nullptr )
                outStats.merge( pDef->_stats );
        }
    }
} // namespace sw
