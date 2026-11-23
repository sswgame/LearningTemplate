#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Inventory/InventoryCommand.h"

#include "GameFramework/Runtime/Gameplay/Inventory/Equipment.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"

namespace sw
{
    namespace
    {
        struct InventoryCommandInternal
        {
            static bool isBagSlot( const Inventory& bag, int32 slot ) { return slot >= 0 && slot < bag.getSlotCount(); }

            static bool hasEquipSlot( const Equipment& equipment, const hashed_string& name )
            {
                for ( const EquipmentSlot& slot : equipment.getSlots() )
                {
                    if ( slot._name == name )
                        return true;
                }
                return false;
            }

            static InventoryCommandResult toResult( EquipResult result )
            {
                switch ( result )
                {
                    case EquipResult::Ok:
                    {
                        return InventoryCommandResult::Ok;
                    }
                    case EquipResult::UnknownSlot:
                    {
                        return InventoryCommandResult::InvalidSlot;
                    }
                    case EquipResult::InventoryFull:
                    {
                        return InventoryCommandResult::NoRoom;
                    }
                    case EquipResult::EmptySlot:
                    {
                        return InventoryCommandResult::EmptySlot;
                    }
                    case EquipResult::UnknownItem:
                    case EquipResult::WrongSlot:
                    case EquipResult::ConditionNotMet:
                    case EquipResult::RefusedByDependent:
                    {
                        break;
                    }
                }
                return InventoryCommandResult::Refused;
            }
        };
    } // namespace

    InventoryCommandResult InventoryCommand::validate( const Inventory& bag, const Equipment& equipment ) const
    {
        using Internal = InventoryCommandInternal;
        if ( _kind == InventoryCommandKind::Sort )
            return InventoryCommandResult::Ok;
        if ( _kind == InventoryCommandKind::Unequip )
        {
            if ( Internal::hasEquipSlot( equipment, _equipSlot ) == false )
                return InventoryCommandResult::InvalidSlot;
            const InventorySlot* pWorn = equipment.findEquipped( _equipSlot );
            if ( pWorn == nullptr || pWorn->isEmpty() )
                return InventoryCommandResult::EmptySlot;
            return bag.countEmptySlots() > 0 ? InventoryCommandResult::Ok : InventoryCommandResult::NoRoom;
        }
        if ( Internal::isBagSlot( bag, _fromSlot ) == false )
            return InventoryCommandResult::InvalidSlot;
        const InventorySlot& from = bag.getSlot( _fromSlot );
        if ( from.isEmpty() )
            return InventoryCommandResult::EmptySlot;
        switch ( _kind )
        {
            case InventoryCommandKind::Move:
            {
                return Internal::isBagSlot( bag, _toSlot ) && _toSlot != _fromSlot ? InventoryCommandResult::Ok : InventoryCommandResult::InvalidSlot;
            }
            case InventoryCommandKind::Split:
            {
                // 나누기는 일부만이다 — 전부를 떼면 옮기기이고, 가진 것보다 많으면 복제 요청이다.
                if ( _count < 1 || _count >= from._count )
                    return InventoryCommandResult::InvalidCount;
                return bag.countEmptySlots() > 0 ? InventoryCommandResult::Ok : InventoryCommandResult::NoRoom;
            }
            case InventoryCommandKind::Drop:
            {
                return _count >= 1 && _count <= from._count ? InventoryCommandResult::Ok : InventoryCommandResult::InvalidCount;
            }
            case InventoryCommandKind::Equip:
            {
                const hashed_string slot = _equipSlot.empty() ? equipment.findSlotFor( from._itemID ) : _equipSlot;
                if ( slot.empty() || Internal::hasEquipSlot( equipment, slot ) == false )
                    return InventoryCommandResult::InvalidSlot;
                return Internal::toResult( equipment.evaluateEquip( slot, from._itemID ) );
            }
            case InventoryCommandKind::Use:
            {
                const ItemCatalog* pCatalog = bag.getCatalog();
                const ItemDef*     pItem    = pCatalog != nullptr ? pCatalog->find( from._itemID ) : nullptr;
                return pItem != nullptr && pItem->_useEffect.empty() == false ? InventoryCommandResult::Ok : InventoryCommandResult::NotUsable;
            }
            case InventoryCommandKind::Unequip:
            case InventoryCommandKind::Sort:
            {
                break; // 위에서 끝났다
            }
        }
        return InventoryCommandResult::InvalidSlot;
    }

    InventoryCommandResult InventoryCommand::apply( Inventory& bag, Equipment& equipment, vector<InventorySlot>& outListDropped ) const
    {
        using Internal                       = InventoryCommandInternal;
        const InventoryCommandResult checked = validate( bag, equipment );
        if ( checked != InventoryCommandResult::Ok )
            return checked;
        switch ( _kind )
        {
            case InventoryCommandKind::Move:
            {
                return bag.moveSlot( _fromSlot, _toSlot ) ? InventoryCommandResult::Ok : InventoryCommandResult::NoRoom;
            }
            case InventoryCommandKind::Split:
            {
                return bag.splitSlot( _fromSlot, _count ).has_value() ? InventoryCommandResult::Ok : InventoryCommandResult::NoRoom;
            }
            case InventoryCommandKind::Drop:
            {
                outListDropped.push_back( bag.takeFromSlot( _fromSlot, _count ) );
                return InventoryCommandResult::Ok;
            }
            case InventoryCommandKind::Equip:
            {
                return Internal::toResult( equipment.equipFromInventory( bag, _fromSlot, _equipSlot ) );
            }
            case InventoryCommandKind::Unequip:
            {
                return equipment.unequipToInventory( _equipSlot, bag ) ? InventoryCommandResult::Ok : InventoryCommandResult::NoRoom;
            }
            case InventoryCommandKind::Use:
            {
                (void)bag.takeFromSlot( _fromSlot, 1 ); // 쓴 하나는 사라진다 — 효과는 게임이 결과를 보고 건다
                return InventoryCommandResult::Ok;
            }
            case InventoryCommandKind::Sort:
            {
                bag.sortSlots();
                return InventoryCommandResult::Ok;
            }
        }
        return InventoryCommandResult::InvalidSlot;
    }
} // namespace sw
