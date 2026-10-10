#include "pch.h"

#include "GameFramework/Kits/Genre/Action/BattleRoyale/Rule/BrGear.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Actor/Combat/Weapon/Weapon.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Kits/Genre/Action/BattleRoyale/Catalog/BrCatalog.h"

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
        , _backpackID{}
    {
    }

    void BrLoadout::initialize( const BrCatalog* pCatalog )
    {
        _pCatalog   = pCatalog;
        _helmet     = BrArmorSlot{};
        _vest       = BrArmorSlot{};
        _backpackID = hashed_string{};
    }

    bool BrLoadout::tryEquipArmor( const hashed_string& itemID, float32 durability )
    {
        const BrArmorDef* pDef = _pCatalog != nullptr ? _pCatalog->findArmor( itemID ) : nullptr;
        if ( pDef == nullptr )
            return false;
        BrArmorSlot& slot = pDef->_slot == BrGearInternal::getHelmetName() ? _helmet : _vest;
        slot._pDef        = pDef;
        slot._durability  = durability < 0.0f ? pDef->_durability : MathUtil::min( durability, pDef->_durability );
        return true;
    }

    bool BrLoadout::tryEquipBackpack( const hashed_string& itemID, Inventory& inoutInventory )
    {
        if ( itemID.empty() == false && ( _pCatalog == nullptr || _pCatalog->findBackpack( itemID ) == nullptr ) )
            return false;
        const float32 limit = computeCarryLimit( itemID );
        if ( inoutInventory.computeWeight() > limit )
            return false;
        _backpackID = itemID;
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
        if ( def._ammoID.empty() == false )
        {
            const int32 needed  = def._magazineSize - inoutWeapon.getMagazineAmmo() - inoutWeapon.getReserveAmmo();
            const int32 room    = def._maxReserveAmmo - inoutWeapon.getReserveAmmo();
            const int32 takeNow = MathUtil::min( MathUtil::min( needed, room ), inoutInventory.getItemCount( def._ammoID ) );
            if ( takeNow > 0 )
            {
                if ( inoutInventory.removeItem( def._ammoID, takeNow ) == false )
                    return false;
                inoutWeapon.addReserveAmmo( takeNow );
            }
        }
        return inoutWeapon.startReload();
    }

    float32 BrLoadout::computeCarryLimit() const { return computeCarryLimit( _backpackID ); }

    float32 BrLoadout::computeCarryLimit( const hashed_string& backpackID ) const
    {
        if ( _pCatalog == nullptr )
            return 0.0f;
        const BrBackpackDef* pBackpack = backpackID.empty() ? nullptr : _pCatalog->findBackpack( backpackID );
        return _pCatalog->getPlayerSettings()._baseCarryWeight + ( pBackpack != nullptr ? pBackpack->_capacity : 0.0f );
    }

    void BrLoadout::writeState( Archive& outArchive ) const
    {
        for ( const BrArmorSlot* pSlot : { &_helmet, &_vest } )
        {
            StateArchiveUtil::writeName( outArchive, pSlot->_pDef != nullptr ? pSlot->_pDef->_id : hashed_string{} );
            outArchive << pSlot->_durability;
        }
        StateArchiveUtil::writeName( outArchive, _backpackID );
    }

    bool BrLoadout::readState( Archive& archive )
    {
        if ( _pCatalog == nullptr )
            return false;
        // 헬멧 · 조끼 순서 — 방어구는 그 칸의 것이어야 한다
        BrArmorSlot arrSlot[2] = {};
        for ( int32 slotIndex = 0; slotIndex < 2; ++slotIndex )
        {
            BrArmorSlot&  slot = arrSlot[slotIndex];
            hashed_string armorID;
            if ( StateArchiveUtil::readName( archive, armorID ) == false )
                return false;
            archive >> slot._durability;
            if ( archive.isError() )
                return false;
            if ( armorID.empty() )
            {
                slot = BrArmorSlot{};
                continue;
            }
            slot._pDef               = _pCatalog->findArmor( armorID );
            const bool bHelmetSlot   = slotIndex == 0;
            const bool bSlotMatches  = slot._pDef != nullptr && ( slot._pDef->_slot == BrGearInternal::getHelmetName() ) == bHelmetSlot;
            const bool bDurableValid = 0.0f < slot._durability && slot._durability <= ( slot._pDef != nullptr ? slot._pDef->_durability : 0.0f );
            if ( bSlotMatches == false || bDurableValid == false )
                return false;
        }
        hashed_string backpackID;
        if ( StateArchiveUtil::readName( archive, backpackID ) == false )
            return false;
        if ( backpackID.empty() == false && _pCatalog->findBackpack( backpackID ) == nullptr )
            return false;
        _helmet     = arrSlot[0];
        _vest       = arrSlot[1];
        _backpackID = backpackID;
        return true;
    }
} // namespace sw
