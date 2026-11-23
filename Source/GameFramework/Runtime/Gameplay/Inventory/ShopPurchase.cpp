#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Inventory/ShopPurchase.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"

#include <limits>

namespace sw
{
    namespace
    {
        struct ShopPurchaseInternal
        {
            /** @brief 가게 결과를 구매 상태로 옮깁니다. */
            static ShopPurchaseStatus toStatus( ShopResult result )
            {
                switch ( result )
                {
                    case ShopResult::Ok:
                    {
                        return ShopPurchaseStatus::Succeeded;
                    }
                    case ShopResult::NotEnoughMoney:
                    {
                        return ShopPurchaseStatus::NotEnoughMoney;
                    }
                    case ShopResult::OutOfStock:
                    {
                        return ShopPurchaseStatus::OutOfStock;
                    }
                    case ShopResult::NoRoom:
                    {
                        return ShopPurchaseStatus::NoRoom;
                    }
                    case ShopResult::UnknownShop:
                    case ShopResult::UnknownItem:
                    case ShopResult::InvalidCount:
                    case ShopResult::Locked:
                    case ShopResult::Refused:
                    case ShopResult::NotOwned:
                    {
                        break;
                    }
                }
                return ShopPurchaseStatus::Rejected;
            }
        };
    } // namespace

    bool ShopOffer::computeTotal( int32 count, int64& outTotal ) const
    {
        if ( count < 1 || count > _maxCount || _unitPrice < 0 )
            return false;
        if ( _unitPrice > std::numeric_limits<int64>::max() / static_cast<int64>( count ) )
            return false;
        outTotal = _unitPrice * static_cast<int64>( count );
        return true;
    }

    LocalShopPurchaseProvider::LocalShopPurchaseProvider( const ShopCatalog& shops, const ItemCatalog& items, ShopManager& shop, Wallet& wallet, Inventory& inventory, const hashed_string& shopID )
        : _shops{ shops }
        , _items{ items }
        , _shop{ shop }
        , _wallet{ wallet }
        , _inventory{ inventory }
        , _shopID{ shopID }
        , _purchaseCount{ 0 }
    {
    }

    void LocalShopPurchaseProvider::collectOffers( vector<ShopOffer>& outListOffer ) const
    {
        outListOffer.clear();
        const ShopDef* pShop = _shops.find( _shopID );
        if ( pShop == nullptr )
            return;
        const hashed_string currency = _shop.getCurrency( _shopID );
        for ( const ShopStockDef& stock : pShop->_listStock )
        {
            const ItemDef*        pItem = _items.find( stock._itemID );
            const optional<int32> price = _shop.computeBuyPrice( _shopID, stock._itemID );
            if ( pItem == nullptr || price.has_value() == false || _shop.isUnlocked( stock ) == false )
                continue; // 잠긴 진열은 보이지 않는다(가게 규칙과 같은 판정)
            ShopOffer offer{};
            offer._offerID     = stock._itemID.c_str();
            offer._displayName = pItem->_name.empty() ? string( stock._itemID.c_str() ) : pItem->_name;
            offer._currency    = currency;
            offer._unitPrice   = *price;
            offer._maxCount    = MathUtil::max( _items.getMaxStack( stock._itemID ), 1 );
            offer._stock       = _shop.getStockCount( _shopID, stock._itemID );
            outListOffer.push_back( std::move( offer ) );
        }
    }

    int64 LocalShopPurchaseProvider::getBalance( const hashed_string& currency ) const
    {
        return _wallet.getBalance( currency );
    }

    uint32 LocalShopPurchaseProvider::getRevision() const
    {
        return _wallet.getRevision() + _inventory.getRevision() + _purchaseCount;
    }

    void LocalShopPurchaseProvider::beginPurchase( const ShopOffer& offer, int32 count, const ShopPurchaseDelegate& onFinished )
    {
        ShopPurchaseReceipt receipt{};
        receipt._offerID    = offer._offerID;
        receipt._count      = count;
        int64 expectedTotal = 0;
        if ( offer.computeTotal( count, expectedTotal ) == false )
        {
            receipt._status = ShopPurchaseStatus::Rejected;
        }
        else
        {
            int64            paid   = 0;
            const ShopResult result = _shop.buy( _shopID, hashed_string( offer._offerID ), count, _wallet, _inventory, &paid );
            receipt._status         = ShopPurchaseInternal::toStatus( result );
            receipt._paid           = result == ShopResult::Ok ? paid : 0;
            ++_purchaseCount;
        }
        if ( onFinished.isBound() )
            onFinished( receipt );
    }
} // namespace sw
