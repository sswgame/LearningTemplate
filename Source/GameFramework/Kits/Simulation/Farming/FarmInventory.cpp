#include "pch.h"

#include "GameFramework/Kits/Simulation/Farming/FarmInventory.h"

#include "GameFramework/Kits/Simulation/Farming/CropCatalog.h"

namespace sw
{
    FarmInventory::FarmInventory()
        : _bag{}
        , _shippingBin{}
        , _gold{ 0 }
    {
    }

    void FarmInventory::addItem( const hashed_string& itemId, int32 count )
    {
        _bag.addItem( itemId, count );
    }

    bool FarmInventory::removeItem( const hashed_string& itemId, int32 count )
    {
        return _bag.removeItem( itemId, count );
    }

    int32 FarmInventory::getItemCount( const hashed_string& itemId ) const
    {
        return _bag.getItemCount( itemId );
    }

    void FarmInventory::getItemIds( vector<hashed_string>& outListItem ) const
    {
        _bag.getItemIds( outListItem );
    }

    void FarmInventory::addGold( int32 amount )
    {
        if ( amount > 0 )
            _gold += amount;
    }

    bool FarmInventory::spendGold( int32 amount )
    {
        if ( amount < 0 || _gold < amount )
            return false;
        _gold -= amount;
        return true;
    }

    bool FarmInventory::buyItem( const hashed_string& itemId, int32 count, int32 unitPrice )
    {
        if ( itemId.empty() || count <= 0 || unitPrice < 0 )
            return false;
        if ( spendGold( count * unitPrice ) == false )
            return false;
        addItem( itemId, count );
        return true;
    }

    bool FarmInventory::shipItem( const hashed_string& itemId, int32 count )
    {
        return _bag.moveItemTo( _shippingBin, itemId, count );
    }

    int32 FarmInventory::getShippedItemCount() const
    {
        return _shippingBin.getTotalCount();
    }

    int32 FarmInventory::settleShipping( const CropCatalog& catalog )
    {
        int32 earned = 0;
        for ( const auto& [itemId, count] : _shippingBin.getItems() )
            earned += catalog.findSellPrice( itemId ) * count;
        _shippingBin.clear();
        addGold( earned );
        return earned;
    }
} // namespace sw
