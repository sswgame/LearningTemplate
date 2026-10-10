#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Trade/Shared/TradeInventoryUtil.h"

#include "GameFramework/Base/Gameplay/Inventory/Inventory.h"

namespace sw
{
    TradeResult TradeInventoryUtil::makeLegs( const Inventory& inventory, const vector<int32>& listSlot, AssetIDFromItem pAssetIDFromItem, vector<TradeLeg>& outListLeg )
    {
        outListLeg.clear();
        if ( pAssetIDFromItem == nullptr )
            return TradeResult::Invalid;
        for ( size_t slotIndex = 0; slotIndex < listSlot.size(); ++slotIndex )
        {
            const int32 slot = listSlot[slotIndex];
            for ( size_t previousIndex = 0; previousIndex < slotIndex; ++previousIndex )
            {
                if ( listSlot[previousIndex] == slot )
                    return TradeResult::Invalid; // 같은 칸 두 번
            }
            if ( slot < 0 || slot >= inventory.getSlotCount() )
                return TradeResult::Invalid;
            const InventorySlot& stack = inventory.getSlot( slot );
            if ( stack.isEmpty() )
                return TradeResult::Invalid;
            if ( stack.hasInstanceState() || stack._durability > 0.0f )
                return TradeResult::NotTradable;
            string assetID;
            if ( pAssetIDFromItem( stack._itemID, assetID ) == false )
                return TradeResult::NotTradable;
            TradeLeg* pLeg = nullptr;
            for ( TradeLeg& leg : outListLeg )
            {
                if ( leg._assetID == assetID )
                    pLeg = &leg;
            }
            if ( pLeg == nullptr )
            {
                pLeg           = &outListLeg.emplace_back();
                pLeg->_assetID = assetID;
            }
            pLeg->_amount += stack._count;
        }
        return static_cast<int32>( outListLeg.size() ) > TradeConstant::kMaxLegsPerSide ? TradeResult::TooManyLegs : TradeResult::Ok;
    }
} // namespace sw
