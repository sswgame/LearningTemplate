#include "pch.h"

#include "GameFramework/Kits/Genre/Action/BattleRoyale/Rule/BattleRoyaleGear.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Action/BattleRoyale/Catalog/BattleRoyaleCatalog.h"
#include "GameFramework/Runtime/Character/Combat/Weapon/Weapon.h"
#include "GameFramework/Runtime/Foundation/GameData/DepletingValue.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"

namespace sw
{
    BattleRoyaleLoadout::BattleRoyaleLoadout()
        : _pCatalog{ nullptr }
        , _helmet{}
        , _vest{}
        , _backpackID{}
    {
    }

    void BattleRoyaleLoadout::initialize( const BattleRoyaleCatalog* pCatalog )
    {
        _pCatalog   = pCatalog;
        _helmet     = BattleRoyaleArmorSlot{};
        _vest       = BattleRoyaleArmorSlot{};
        _backpackID = hashed_string{};
    }

    bool BattleRoyaleLoadout::tryEquipArmor( const hashed_string& itemID, float32 durability )
    {
        const BattleRoyaleArmorDef* pDef = _pCatalog != nullptr ? _pCatalog->findArmor( itemID ) : nullptr;
        // 내구도 0 은 부서진 방어구다. 입히면 다음 한 방을 공짜로 막고, 저장한 상태는 `readState` 가 거절해 세이브가 깨진다.
        const bool bBroken = durability == 0.0f || MathUtil::isNan( durability );
        if ( pDef == nullptr || bBroken )
            return false;
        BattleRoyaleArmorSlot& slot = pDef->_slot == BattleRoyaleCatalog::getHelmetSlotName() ? _helmet : _vest;
        slot._pDef                  = pDef;
        slot._durability            = durability < 0.0f ? pDef->_durability : MathUtil::min( durability, pDef->_durability );
        return true;
    }

    bool BattleRoyaleLoadout::tryEquipBackpack( const hashed_string& itemID, Inventory& inoutInventory )
    {
        if ( itemID.empty() == false && ( _pCatalog == nullptr || _pCatalog->findBackpack( itemID ) == nullptr ) )
            return false;
        const float32 limit = computeCarryLimit( itemID );
        if ( inoutInventory.computeWeight() > limit )
            return false;
        if ( inoutInventory.setMaxWeight( limit ) == false )
            return false;
        _backpackID = itemID;
        return true;
    }

    BattleRoyaleArmorResult BattleRoyaleLoadout::absorbDamage( float32 damage, BattleRoyaleHitZone hitZone )
    {
        BattleRoyaleArmorResult result;
        result._damage                            = MathUtil::max( 0.0f, damage );
        BattleRoyaleArmorSlot* pSlot              = hitZone == BattleRoyaleHitZone::Head ? &_helmet : ( hitZone == BattleRoyaleHitZone::Body ? &_vest : nullptr );
        const bool             bNoArmorAbsorption = pSlot == nullptr || pSlot->isEmpty() || result._damage <= 0.0f;
        if ( bNoArmorAbsorption )
            return result;
        result._absorbed = result._damage * pSlot->_pDef->_reduction;
        result._damage -= result._absorbed;
        if ( DepletingValue::take( pSlot->_durability, damage )._bDepleted == SW_TRUE )
        {
            *pSlot          = BattleRoyaleArmorSlot{};
            result._bBroken = SW_TRUE;
        }
        return result;
    }

    bool BattleRoyaleLoadout::tryReload( WeaponState& inoutWeapon, Inventory& inoutInventory ) const
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
                if ( inoutInventory.consumeItem( def._ammoID, takeNow ) == false )
                    return false;
                inoutWeapon.addReserveAmmo( takeNow );
            }
        }
        return inoutWeapon.startReload();
    }

    float32 BattleRoyaleLoadout::computeCarryLimit() const
    {
        return computeCarryLimit( _backpackID );
    }

    float32 BattleRoyaleLoadout::computeCarryLimit( const hashed_string& backpackID ) const
    {
        if ( _pCatalog == nullptr )
            return 0.0f;
        const BattleRoyaleBackpackDef* pBackpack = backpackID.empty() ? nullptr : _pCatalog->findBackpack( backpackID );
        return _pCatalog->getPlayerSettings()._baseCarryWeight + ( pBackpack != nullptr ? pBackpack->_capacity : 0.0f );
    }

    void BattleRoyaleLoadout::serializeState( StateIO& io )
    {
        if ( io.check( _pCatalog != nullptr ) == false )
            return;
        // 헬멧, 조끼 순서로 놓는다. 방어구는 그 슬롯의 것이어야 한다
        BattleRoyaleArmorSlot* arrSlot[2] = { &_helmet, &_vest };
        for ( int32 slotIndex = 0; slotIndex < 2; ++slotIndex )
        {
            BattleRoyaleArmorSlot& slot    = *arrSlot[slotIndex];
            hashed_string          armorID = slot._pDef != nullptr ? slot._pDef->_id : hashed_string{};
            io.value( armorID );
            io.value( slot._durability );
            if ( io.isWriting() )
                continue;
            if ( io.isOk() == false )
                return;
            if ( armorID.empty() )
            {
                slot = BattleRoyaleArmorSlot{};
                continue;
            }
            slot._pDef               = _pCatalog->findArmor( armorID );
            const bool bHelmetSlot   = slotIndex == 0;
            const bool bSlotMatches  = slot._pDef != nullptr && ( slot._pDef->_slot == BattleRoyaleCatalog::getHelmetSlotName() ) == bHelmetSlot;
            const bool bDurableValid = 0.0f < slot._durability && slot._durability <= ( slot._pDef != nullptr ? slot._pDef->_durability : 0.0f );
            if ( io.check( bSlotMatches && bDurableValid ) == false )
                return;
        }
        io.value( _backpackID );
        if ( io.isReading() )
        {
            io.check( _backpackID.empty() || _pCatalog->findBackpack( _backpackID ) != nullptr );
        }
    }

} // namespace sw
