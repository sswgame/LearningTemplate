#include "pch.h"

#include "GameFramework/Kits/Farming/FarmInventory.h"

#include "GameFramework/Kits/Farming/CropCatalog.h"

namespace sw
{
    FarmInventory::FarmInventory()
        : _mapItem{}
        , _mapShipped{}
        , _gold{ 0 }
    {
    }

    void FarmInventory::addItem( const hashed_string& itemId, int32 count )
    {
        if ( itemId.empty() || count <= 0 )
            return;
        _mapItem[itemId] += count;
    }

    bool FarmInventory::removeItem( const hashed_string& itemId, int32 count )
    {
        const auto mapIter = _mapItem.find( itemId );
        if ( count <= 0 || mapIter == _mapItem.end() || mapIter->second < count )
            return false;
        mapIter->second -= count;
        if ( mapIter->second == 0 )
            _mapItem.erase( mapIter );
        return true;
    }

    int32 FarmInventory::getItemCount( const hashed_string& itemId ) const
    {
        const auto mapIter = _mapItem.find( itemId );
        return mapIter != _mapItem.end() ? mapIter->second : 0;
    }

    void FarmInventory::getItemIds( vector<hashed_string>& outListItem ) const
    {
        outListItem.clear();
        for ( const auto& [itemId, count] : _mapItem )
        {
            if ( count > 0 )
                outListItem.push_back( itemId );
        }
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
        if ( removeItem( itemId, count ) == false )
            return false;
        _mapShipped[itemId] += count;
        return true;
    }

    int32 FarmInventory::getShippedItemCount() const
    {
        int32 shippedCount = 0;
        for ( const auto& [itemId, count] : _mapShipped )
        {
            (void)itemId;
            shippedCount += count;
        }
        return shippedCount;
    }

    int32 FarmInventory::settleShipping( const CropCatalog& catalog )
    {
        int32 earned = 0;
        for ( const auto& [itemId, count] : _mapShipped )
        {
            earned += catalog.findSellPrice( itemId ) * count;
        }
        _mapShipped.clear();
        addGold( earned );
        return earned;
    }
} // namespace sw
