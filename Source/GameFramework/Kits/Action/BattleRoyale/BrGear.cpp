#include "pch.h"

#include "GameFramework/Kits/Action/BattleRoyale/BrGear.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Combat/Weapon.h"
#include "GameFramework/Inventory/Inventory.h"
#include "GameFramework/Kits/Action/BattleRoyale/BrCatalog.h"

namespace sw
{
    namespace
    {
        /** @brief 틱마다 쓰는 이름 — 리터럴을 매번 intern 하지 않게 한 번만 만든다. */
        struct BrGearInternal
        {
            static const hashed_string& getHelmetName()
            {
                static const hashed_string name( "Helmet" );
                return name;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    BrLoadout::BrLoadout()
        : _pCatalog{ nullptr }
        , _helmet{}
        , _vest{}
        , _backpackId{}
    {
    }

    void BrLoadout::initialize( const BrCatalog* pCatalog )
    {
        _pCatalog   = pCatalog;
        _helmet     = BrArmorSlot{};
        _vest       = BrArmorSlot{};
        _backpackId = hashed_string{};
    }

    bool BrLoadout::tryEquipArmor( const hashed_string& itemId, float32 durability )
    {
        const BrArmorDef* pDef = _pCatalog != nullptr ? _pCatalog->findArmor( itemId ) : nullptr;
        if ( pDef == nullptr )
            return false;
        BrArmorSlot& slot = pDef->_slot == BrGearInternal::getHelmetName() ? _helmet : _vest;
        slot._pDef        = pDef;
        slot._durability  = durability < 0.0f ? pDef->_durability : MathUtil::min( durability, pDef->_durability );
        return true;
    }

    bool BrLoadout::tryEquipBackpack( const hashed_string& itemId, Inventory& inoutInventory )
    {
        if ( itemId.empty() == false && ( _pCatalog == nullptr || _pCatalog->findBackpack( itemId ) == nullptr ) )
            return false;
        const float32 limit = computeCarryLimit( itemId );
        if ( inoutInventory.computeWeight() > limit )
            return false;
        _backpackId = itemId;
        inoutInventory.setMaxWeight( limit );
        return true;
    }

    BrArmorResult BrLoadout::absorbDamage( float32 damage, BrHitZone hitZone )
    {
        BrArmorResult result;
        result._damage     = MathUtil::max( 0.0f, damage );
        BrArmorSlot* pSlot = hitZone == BrHitZone::Head ? &_helmet : ( hitZone == BrHitZone::Body ? &_vest : nullptr );
        if ( pSlot == nullptr || pSlot->isEmpty() || result._damage <= 0.0f )
            return result;
        result._absorbed = result._damage * pSlot->_pDef->_reduction;
        result._damage -= result._absorbed;
        pSlot->_durability -= damage;
        if ( pSlot->_durability <= 0.0f )
        {
            *pSlot          = BrArmorSlot{};
            result._bBroken = SW_TRUE;
        }
        return result;
    }

    bool BrLoadout::tryReload( WeaponState& inoutWeapon, Inventory& inoutInventory ) const
    {
        const WeaponDef& def = inoutWeapon.getDef();
        if ( inoutWeapon.isReloading() || inoutWeapon.getMagazineAmmo() >= def._magazineSize )
            return false;
        if ( def._ammoId.empty() == false )
        {
            const int32 needed  = def._magazineSize - inoutWeapon.getMagazineAmmo() - inoutWeapon.getReserveAmmo();
            const int32 room    = def._maxReserveAmmo - inoutWeapon.getReserveAmmo();
            const int32 takeNow = MathUtil::min( MathUtil::min( needed, room ), inoutInventory.getItemCount( def._ammoId ) );
            if ( takeNow > 0 )
            {
                if ( inoutInventory.removeItem( def._ammoId, takeNow ) == false )
                    return false;
                inoutWeapon.addReserveAmmo( takeNow );
            }
        }
        return inoutWeapon.startReload();
    }

    float32 BrLoadout::computeCarryLimit() const { return computeCarryLimit( _backpackId ); }

    float32 BrLoadout::computeCarryLimit( const hashed_string& backpackId ) const
    {
        if ( _pCatalog == nullptr )
            return 0.0f;
        const BrBackpackDef* pBackpack = backpackId.empty() ? nullptr : _pCatalog->findBackpack( backpackId );
        return _pCatalog->getPlayerSettings()._baseCarryWeight + ( pBackpack != nullptr ? pBackpack->_capacity : 0.0f );
    }
} // namespace sw
