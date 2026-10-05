#include "pch.h"

#include "GameFramework/Kits/Simulation/Farming/FarmShippingBin.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Inventory/Inventory.h"
#include "GameFramework/Base/Inventory/Shop.h"
#include "GameFramework/Kits/Simulation/Farming/CropCatalog.h"

namespace sw
{
    FarmShippingBin::FarmShippingBin()
        : _bin{}
        , _currency{ Wallet::getDefaultCurrency() }
    {
    }

    bool FarmShippingBin::readState( Archive& archive )
    {
        ItemBag bin;
        if ( bin.readState( archive ) == false )
            return false;
        _bin = std::move( bin );
        return true;
    }

    bool FarmShippingBin::shipItem( Inventory& inoutBag, const hashed_string& itemId, int32 count )
    {
        if ( count <= 0 || inoutBag.removeItem( itemId, count ) == false )
            return false;
        _bin.addItem( itemId, count );
        return true;
    }

    int32 FarmShippingBin::settleShipping( const CropCatalog& catalog, Wallet& inoutWallet )
    {
        int32 earned = 0;
        for ( const auto& [itemId, count] : _bin.getItems() )
            earned += catalog.findSellPrice( itemId ) * count;
        _bin.clear();
        if ( 0 < earned )
            inoutWallet.add( _currency, earned );
        return earned;
    }
} // namespace sw
