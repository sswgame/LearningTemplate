#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/Farming/FarmShippingBin.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Kits/Genre/Simulation/Farming/CropCatalog.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"

namespace sw
{
    FarmShippingBin::FarmShippingBin()
        : _bin{}
        , _currency{ Wallet::getDefaultCurrency() }
    {
    }

    void FarmShippingBin::serializeState( StateIO& io )
    {
        io.value( _bin );
    }

    bool FarmShippingBin::shipItem( Inventory& inoutBag, const hashed_string& itemID, int32 count )
    {
        if ( count <= 0 || inoutBag.consumeItem( itemID, count ) == false )
            return false;
        _bin.addItem( itemID, count );
        return true;
    }

    int64 FarmShippingBin::settleShipping( const CropCatalog& catalog, Wallet& inoutWallet )
    {
        // 개수는 상태 바이트에서 int32 상한까지 온다 — 값은 지갑과 같은 64 비트로 더한다.
        int64 earned = 0;
        for ( const auto& [itemID, count] : _bin.getItems() )
        {
            earned += static_cast<int64>( catalog.findSellPrice( itemID ) ) * count;
        }
        _bin.clear();
        if ( 0 < earned )
            inoutWallet.add( _currency, earned );
        return earned;
    }
} // namespace sw
