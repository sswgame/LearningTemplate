#include "pch.h"

#include "GameFramework/Kits/Action/ActionAdventure/AdventureWeaponWear.h"

#include "GameFramework/Base/Inventory/Inventory.h"
#include "GameFramework/Base/Inventory/ItemCatalog.h"

namespace sw
{
    AdventureWeaponWear::AdventureWeaponWear()
        : _settings{}
    {
    }

    AdventureStrikeResult AdventureWeaponWear::strike( Inventory& inventory, int32 slot, float32 baseDamage, bool bThrown ) const
    {
        AdventureStrikeResult result;
        if ( slot < 0 || slot >= inventory.getSlotCount() )
            return result;
        const ItemStack stack = inventory.getSlot( slot );
        if ( stack.isEmpty() )
            return result;
        result._itemId                   = stack._itemId;
        result._damage                   = baseDamage;
        result._durabilityLeft           = stack._durability;
        const ItemCatalog* pCatalog      = inventory.getCatalog();
        const ItemDef*     pItem         = pCatalog != nullptr ? pCatalog->findItem( stack._itemId ) : nullptr;
        const float32      maxDurability = pItem != nullptr ? pItem->_maxDurability : 0.0f;
        if ( maxDurability <= 0.0f || stack._durability <= 0.0f )
            return result; // 닳지 않는 무기
        const float32 wear = bThrown ? _settings._wearPerThrow : _settings._wearPerHit;
        if ( inventory.wearSlot( slot, wear ) )
        {
            result._bBroke         = SW_TRUE;
            result._durabilityLeft = 0.0f;
            result._damage         = baseDamage * _settings._breakMultiplier;
            return result;
        }
        result._durabilityLeft     = inventory.getSlot( slot )._durability;
        const float32 warningLevel = maxDurability * _settings._warningRatio;
        result._bWarning           = stack._durability > warningLevel && result._durabilityLeft <= warningLevel ? SW_TRUE : SW_FALSE;
        return result;
    }
} // namespace sw
