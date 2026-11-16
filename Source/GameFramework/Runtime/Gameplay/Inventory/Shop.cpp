#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"

namespace sw
{
    namespace
    {
        struct ShopInternal
        {
            static int32 roundPrice( float32 value ) { return MathUtil::max( 0, static_cast<int32>( MathUtil::round( value ) ) ); }

            static optional<int32> findSaturationIndex( const ShopState& runtime, const hashed_string& itemID )
            {
                for ( size_t index = 0; index < runtime._listSaturatedItem.size(); ++index )
                {
                    if ( runtime._listSaturatedItem[index] == itemID )
                        return static_cast<int32>( index );
                }
                return nullopt;
            }

            static int32 countPieces( const vector<InventorySlot>& listPiece )
            {
                int32 count = 0;
                for ( const InventorySlot& piece : listPiece )
                {
                    count += piece._count;
                }
                return count;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // Wallet
    // ------------------------------------------------------------------------------
    hashed_string Wallet::getDefaultCurrency()
    {
        return hashed_string( "Gold" );
    }

    void Wallet::add( const hashed_string& currency, int64 amount )
    {
        if ( amount <= 0 || currency.empty() )
            return;
        WalletBalance& balance = getOrCreateBalance( currency );
        balance._amount += amount;
        ++_revision;
        _eventBuffer.push( WalletEvent{ currency, amount, balance._amount } );
    }

    bool Wallet::trySpend( const hashed_string& currency, int64 amount )
    {
        if ( amount < 0 || currency.empty() )
            return false;
        if ( amount == 0 )
            return true;
        if ( canAfford( currency, amount ) == false )
            return false;
        WalletBalance& balance = getOrCreateBalance( currency );
        balance._amount -= amount;
        ++_revision;
        _eventBuffer.push( WalletEvent{ currency, -amount, balance._amount } );
        return true;
    }

    void Wallet::charge( const hashed_string& currency, int64 amount )
    {
        if ( amount <= 0 || currency.empty() )
            return;
        WalletBalance& balance = getOrCreateBalance( currency );
        balance._amount -= amount;
        ++_revision;
        _eventBuffer.push( WalletEvent{ currency, -amount, balance._amount } );
    }

    void Wallet::setBalance( const hashed_string& currency, int64 amount )
    {
        if ( currency.empty() )
            return;
        getOrCreateBalance( currency )._amount = MathUtil::max<int64>( 0, amount );
        ++_revision;
    }

    void Wallet::clear()
    {
        _listBalance.clear();
        _eventBuffer.clear();
        ++_revision;
    }

    void Wallet::pollEvents( vector<WalletEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }

    void Wallet::serializeState( StateIO& io )
    {
        // 통화마다 이름 길이(4) + 잔액(8) 이상
        io.list( _listBalance, 12, [&]( WalletBalance& balance )
        {
            io.value( balance._currency );
            io.value( balance._amount );
            io.check( balance._currency.empty() == false );
        } );
        if ( io.isReading() )
        {
            _eventBuffer.clear();
            ++_revision;
        }
    }

    int64 Wallet::getBalance( const hashed_string& currency ) const
    {
        for ( const WalletBalance& balance : _listBalance )
        {
            if ( balance._currency == currency )
                return balance._amount;
        }
        return 0;
    }

    WalletBalance& Wallet::getOrCreateBalance( const hashed_string& currency )
    {
        for ( WalletBalance& balance : _listBalance )
        {
            if ( balance._currency == currency )
                return balance;
        }
        _listBalance.push_back( WalletBalance{ currency, 0 } );
        return _listBalance.back();
    }

    // ------------------------------------------------------------------------------
    // ShopDef / ShopCatalog
    // ------------------------------------------------------------------------------
    const ShopStockDef* ShopDef::findStock( const hashed_string& itemID ) const
    {
        return VectorUtil::findBy<&ShopStockDef::_itemID>( _listStock, itemID );
    }

    bool ShopDef::isRefusedCategory( const hashed_string& category ) const
    {
        return category.empty() == false && VectorUtil::contains( _listRefusedCategory, category );
    }

    uint32 ShopCatalog::loadRoot( const XMLCatalogRoot& root, string_view sourceName )
    {
        using Element                          = XMLCatalogElement<ShopCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&ShopCatalog::loadShop>( "Shop" ),
        };
        const XMLCatalogReadResult result = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement );
        return result._loadedCount;
    }

    bool ShopCatalog::loadShop( const XMLNode& node, string_view sourceName )
    {

        // 적지 않은 칸은 ShopDef 의 초기값이다. 깨진 값 · 범위 밖 값은 그 상점을 넣지 않는 오류다(자르지 않는다).
        ShopDef def;
        bool    bValid = XMLFields::readProperties( node, def, sourceName );
        if ( def._id.empty() )
            return false;
        const utf8* const pID = def._id.c_str();
        if ( def._currency.empty() )
            def._currency = Wallet::getDefaultCurrency();
        for ( XMLNode stockNode = node.findChild( "Stock" ); stockNode; stockNode = stockNode.findNextSibling( "Stock" ) )
        {
            ShopStockDef stock; // item 필수 · 재고 범위는 PROPERTY 가 본다
            if ( XMLFields::readProperties( stockNode, stock, sourceName ) == false || stock._itemID.empty() )
            {
                SW_LOG_ERROR_ID( "Shop.ShopStockInvalid", "%#: shop '%#' has a <Stock> without an item or with invalid fields", sourceName, pID );
                bValid = false;
                continue;
            }
            if ( ConditionExpr::compile( stock._requirement, sourceName, stock._condition ) == false )
            {
                bValid = false;
                continue;
            }
            def._listStock.push_back( stock );
        }
        if ( bValid == false )
        {
            SW_LOG_ERROR_ID( "Shop.ShopInvalidFieldsSkipped", "%#: shop '%#' has invalid fields - skipped", sourceName, pID );
            return false;
        }
        add( def );
        return true;
    }

    // ------------------------------------------------------------------------------
    // ShopManager
    // ------------------------------------------------------------------------------
    ShopManager::ShopManager()
        : _listRuntime{}
        , _eventBuffer{}
        , _pShopCatalog{ nullptr }
        , _pItemCatalog{ nullptr }
        , _pConditionContext{ nullptr }
    {
    }

    void ShopManager::initialize( const ShopCatalog* pShopCatalog, const ItemCatalog* pItemCatalog )
    {
        _pShopCatalog = pShopCatalog;
        _pItemCatalog = pItemCatalog;
        _listRuntime.clear();
        _eventBuffer.clear();
        if ( _pShopCatalog == nullptr )
            return;
        for ( const ShopDef& shop : _pShopCatalog->getAll() )
        {
            ShopState runtime;
            runtime._shopID = shop._id;
            for ( const ShopStockDef& stock : shop._listStock )
            {
                runtime._listStockCount.push_back( stock._count );
            }
            _listRuntime.push_back( runtime );
        }
    }

    bool ShopManager::setPriceModifier( const hashed_string& shopID, float32 buyModifier, float32 sellModifier )
    {
        if ( buyModifier < 0.0f || sellModifier < 0.0f )
        {
            SW_LOG_ERROR_ID( "Shop.ShopStateRefusedPriceModifiers", "ShopManager: refused price modifiers %# / %# for %# - neither may be negative", buyModifier, sellModifier, shopID.c_str() );
            return false;
        }
        ShopState* pRuntime = findRuntimeMutable( shopID );
        if ( pRuntime == nullptr )
            return false;
        pRuntime->_buyModifier  = buyModifier;
        pRuntime->_sellModifier = sellModifier;
        return true;
    }

    void ShopManager::refuseCategory( const hashed_string& shopID, const hashed_string& category )
    {
        ShopState* pRuntime            = findRuntimeMutable( shopID );
        const bool bAddRefusedCategory = pRuntime != nullptr && category.empty() == false && VectorUtil::contains( pRuntime->_listExtraRefused, category ) == false;
        if ( bAddRefusedCategory )
            pRuntime->_listExtraRefused.push_back( category );
    }

    ShopResult ShopManager::evaluateBuy( const hashed_string& shopID, const hashed_string& itemID, int32 count, const Wallet& wallet,
                                         const Inventory& inventory ) const
    {
        const ShopDef*   pShop    = _pShopCatalog != nullptr ? _pShopCatalog->find( shopID ) : nullptr;
        const ShopState* pRuntime = findRuntime( shopID );
        if ( pShop == nullptr || pRuntime == nullptr )
            return ShopResult::UnknownShop;
        const optional<int32> foundStock       = findStockIndex( *pShop, itemID );
        const bool            bUnknownShopItem = foundStock.has_value() == false || _pItemCatalog == nullptr || _pItemCatalog->find( itemID ) == nullptr;
        if ( bUnknownShopItem )
            return ShopResult::UnknownItem;
        const int32 stockIndex = *foundStock;
        if ( count <= 0 )
            return ShopResult::InvalidCount;
        const ShopStockDef& stock = pShop->_listStock[static_cast<size_t>( stockIndex )];
        if ( isUnlocked( stock ) == false )
            return ShopResult::Locked;
        const int32 stockCount = pRuntime->_listStockCount[static_cast<size_t>( stockIndex )];
        if ( ShopStockDef::isFiniteStock( stockCount ) && stockCount < count )
            return ShopResult::OutOfStock;
        const int64 total = static_cast<int64>( computeBuyPrice( shopID, itemID ) ) * count;
        if ( wallet.canAfford( pShop->_currency, total ) == false )
            return ShopResult::NotEnoughMoney;
        if ( inventory.hasRoomFor( itemID, count ) == false )
            return ShopResult::NoRoom;
        return ShopResult::Ok;
    }

    ShopResult ShopManager::buy( const hashed_string& shopID, const hashed_string& itemID, int32 count, Wallet& wallet, Inventory& inventory, int64* pOutPaid )
    {
        const ShopResult result = evaluateBuy( shopID, itemID, count, wallet, inventory );
        if ( result != ShopResult::Ok )
            return result;
        const ShopDef* pShop      = _pShopCatalog->find( shopID );
        ShopState*     pRuntime   = findRuntimeMutable( shopID );
        const int32    stockIndex = findStockIndex( *pShop, itemID ).value(); // evaluateBuy 가 진열을 확인했다
        const int64    total      = static_cast<int64>( computeBuyPrice( shopID, itemID ) ) * count;
        if ( wallet.trySpend( pShop->_currency, total ) == false )
            return ShopResult::NotEnoughMoney;
        const int32 addedCount = inventory.addItem( itemID, count );
        if ( addedCount != count )
            SW_LOG_WARNING_ID( "Shop.InventoryAcceptedAfterHasRoomFor", "inventory accepted %# of %# '%#' after hasRoomFor said yes", addedCount, count, itemID.c_str() );
        int32& stockCount = pRuntime->_listStockCount[static_cast<size_t>( stockIndex )];
        if ( ShopStockDef::isFiniteStock( stockCount ) )
            stockCount -= count;
        _eventBuffer.push( ShopEvent{ shopID, itemID, total, count, ShopEvent::Kind::Bought } );
        if ( pOutPaid != nullptr )
            *pOutPaid = total;
        return ShopResult::Ok;
    }

    ShopResult ShopManager::evaluateSell( const hashed_string& shopID, const hashed_string& itemID, int32 count, const Inventory& inventory ) const
    {
        const ShopDef*   pShop    = _pShopCatalog != nullptr ? _pShopCatalog->find( shopID ) : nullptr;
        const ShopState* pRuntime = findRuntime( shopID );
        if ( pShop == nullptr || pRuntime == nullptr )
            return ShopResult::UnknownShop;
        if ( _pItemCatalog == nullptr || _pItemCatalog->find( itemID ) == nullptr )
            return ShopResult::UnknownItem;
        if ( count <= 0 )
            return ShopResult::InvalidCount;
        if ( isRefused( *pShop, *pRuntime, itemID ) )
            return ShopResult::Refused;
        if ( inventory.hasItem( itemID, count ) == false )
            return ShopResult::NotOwned;
        return ShopResult::Ok;
    }

    ShopResult ShopManager::sell( const hashed_string& shopID, const hashed_string& itemID, int32 count, Wallet& wallet, Inventory& inventory, int64* pOutReceived )
    {
        const ShopResult result = evaluateSell( shopID, itemID, count, inventory );
        if ( result != ShopResult::Ok )
            return result;
        const int64           total = computeSellTotal( shopID, itemID, count );
        vector<InventorySlot> listPiece;
        if ( inventory.takeItem( itemID, count, listPiece ) == false )
            return ShopResult::NotOwned;
        settleSale( shopID, itemID, count, total, std::move( listPiece ), wallet, pOutReceived );
        return ShopResult::Ok;
    }

    ShopResult ShopManager::sell( const hashed_string& shopID, int32 slot, int32 count, Wallet& wallet, Inventory& inventory, int64* pOutReceived )
    {
        const bool bNoSlotItem = slot < 0 || slot >= inventory.getSlotCount() || inventory.getSlot( slot ).isEmpty();
        if ( bNoSlotItem )
            return ShopResult::NotOwned;
        const hashed_string itemID = inventory.getSlot( slot )._itemID;
        const ShopResult    result = evaluateSell( shopID, itemID, count, inventory );
        if ( result != ShopResult::Ok )
            return result;
        if ( inventory.getSlot( slot )._count < count )
            return ShopResult::NotOwned;
        const int64           total = computeSellTotal( shopID, itemID, count );
        vector<InventorySlot> listPiece;
        listPiece.push_back( inventory.takeFromSlot( slot, count ) );
        settleSale( shopID, itemID, count, total, std::move( listPiece ), wallet, pOutReceived );
        return ShopResult::Ok;
    }

    void ShopManager::settleSale( const hashed_string& shopID, const hashed_string& itemID, int32 count, int64 total, vector<InventorySlot>&& listPiece, Wallet& wallet,
                                  int64* pOutReceived )
    {
        const ShopDef* pShop    = _pShopCatalog->find( shopID );
        ShopState*     pRuntime = findRuntimeMutable( shopID );
        wallet.add( pShop->_currency, total );
        if ( pShop->_buybackLimit > 0 )
        {
            pRuntime->_listBuyback.push_back( ShopBuybackEntry{ std::move( listPiece ), itemID, total } );
            const size_t limit = static_cast<size_t>( pShop->_buybackLimit );
            if ( pRuntime->_listBuyback.size() > limit )
                pRuntime->_listBuyback.erase( pRuntime->_listBuyback.begin(), pRuntime->_listBuyback.end() - static_cast<std::ptrdiff_t>( limit ) );
        }
        if ( pOutReceived != nullptr )
            *pOutReceived = total;
        if ( pShop->_saturation > 0.0f )
        {
            optional<int32> saturationIndex = ShopInternal::findSaturationIndex( *pRuntime, itemID );
            if ( saturationIndex.has_value() == false )
            {
                saturationIndex = static_cast<int32>( pRuntime->_listSaturatedItem.size() );
                pRuntime->_listSaturatedItem.push_back( itemID );
                pRuntime->_listSellFactor.push_back( 1.0f );
            }
            float32& factor = pRuntime->_listSellFactor[static_cast<size_t>( *saturationIndex )];
            factor          = MathUtil::max( pShop->_minSellFactor, factor - pShop->_saturation * static_cast<float32>( count ) );
        }
        _eventBuffer.push( ShopEvent{ shopID, itemID, total, count, ShopEvent::Kind::Sold } );
    }

    ShopResult ShopManager::buyBack( const hashed_string& shopID, int32 buybackIndex, Wallet& wallet, Inventory& inventory, int64* pOutPaid )
    {
        const ShopDef* pShop    = _pShopCatalog != nullptr ? _pShopCatalog->find( shopID ) : nullptr;
        ShopState*     pRuntime = findRuntimeMutable( shopID );
        if ( pShop == nullptr || pRuntime == nullptr )
            return ShopResult::UnknownShop;
        if ( buybackIndex < 0 || buybackIndex >= static_cast<int32>( pRuntime->_listBuyback.size() ) )
            return ShopResult::InvalidCount;
        const ShopBuybackEntry& entry = pRuntime->_listBuyback[static_cast<size_t>( buybackIndex )];
        if ( wallet.canAfford( pShop->_currency, entry._price ) == false )
            return ShopResult::NotEnoughMoney;
        // 조각마다 `addStack` 은 그 조각만 원자적이다 — 하나라도 안 들어가면 넣기 전 인벤토리로 되돌린다
        const Inventory before = inventory;
        for ( const InventorySlot& piece : entry._listPiece )
        {
            if ( inventory.addStack( piece ) == false )
            {
                inventory = before;
                return ShopResult::NoRoom;
            }
        }
        if ( wallet.trySpend( pShop->_currency, entry._price ) == false )
        {
            inventory = before;
            return ShopResult::NotEnoughMoney;
        }
        const int64 paid = entry._price;
        _eventBuffer.push( ShopEvent{ shopID, entry._itemID, paid, ShopInternal::countPieces( entry._listPiece ), ShopEvent::Kind::BoughtBack } );
        pRuntime->_listBuyback.erase( pRuntime->_listBuyback.begin() + static_cast<std::ptrdiff_t>( buybackIndex ) );
        if ( pOutPaid != nullptr )
            *pOutPaid = paid;
        return ShopResult::Ok;
    }

    void ShopManager::advanceDay()
    {
        if ( _pShopCatalog == nullptr )
            return;
        for ( ShopState& runtime : _listRuntime )
        {
            const ShopDef* pShop = _pShopCatalog->find( runtime._shopID );
            if ( pShop == nullptr )
                continue;
            for ( float32& factor : runtime._listSellFactor )
            {
                factor = MathUtil::min( 1.0f, factor + pShop->_recoveryPerDay );
            }
            if ( pShop->_restockDays <= 0 )
                continue;
            ++runtime._daysSinceRestock;
            if ( runtime._daysSinceRestock < pShop->_restockDays )
                continue;
            runtime._daysSinceRestock = 0;
            for ( size_t stockIndex = 0; stockIndex < pShop->_listStock.size() && stockIndex < runtime._listStockCount.size(); ++stockIndex )
            {
                const ShopStockDef& stock        = pShop->_listStock[stockIndex];
                int32&              stockCount   = runtime._listStockCount[stockIndex];
                const bool          bSkipRestock = stock.hasFiniteStock() == false || stock._restock <= 0 || stockCount >= stock._count;
                if ( bSkipRestock )
                    continue;
                const int32 newCount = MathUtil::min( stock._count, stockCount + stock._restock );
                _eventBuffer.push( ShopEvent{ runtime._shopID, stock._itemID, 0, newCount - stockCount, ShopEvent::Kind::Restocked } );
                stockCount = newCount;
            }
        }
    }

    void ShopManager::pollEvents( vector<ShopEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }

    int32 ShopManager::computeBuyPrice( const hashed_string& shopID, const hashed_string& itemID ) const
    {
        const ShopDef*   pShop    = _pShopCatalog != nullptr ? _pShopCatalog->find( shopID ) : nullptr;
        const ShopState* pRuntime = findRuntime( shopID );
        if ( pShop == nullptr || pRuntime == nullptr )
            return constant::invalid_index::kInt32;
        const optional<int32> stockIndex = findStockIndex( *pShop, itemID );
        if ( stockIndex.has_value() == false )
            return constant::invalid_index::kInt32;
        const int32 basePrice = computeBasePrice( pShop->_listStock[static_cast<size_t>( *stockIndex )] );
        return ShopInternal::roundPrice( static_cast<float32>( basePrice ) * pShop->_buyMultiplier * pRuntime->_buyModifier );
    }

    int64 ShopManager::computeSellTotal( const hashed_string& shopID, const hashed_string& itemID, int32 count ) const
    {
        const ShopDef*   pShop               = _pShopCatalog != nullptr ? _pShopCatalog->find( shopID ) : nullptr;
        const ShopState* pRuntime            = findRuntime( shopID );
        const ItemDef*   pItem               = _pItemCatalog != nullptr ? _pItemCatalog->find( itemID ) : nullptr;
        const bool       bInvalidSellRequest = pShop == nullptr || pRuntime == nullptr || pItem == nullptr || count <= 0;
        if ( bInvalidSellRequest )
            return 0;
        const float32 unitValue = static_cast<float32>( pItem->_value ) * pShop->_sellMultiplier * pRuntime->_sellModifier;
        float32       factor    = getSellFactor( shopID, itemID );
        int64         total     = 0;
        for ( int32 unitIndex = 0; unitIndex < count; ++unitIndex )
        {
            total += ShopInternal::roundPrice( unitValue * factor );
            factor = MathUtil::max( pShop->_minSellFactor, factor - pShop->_saturation );
        }
        return total;
    }

    int32 ShopManager::getStockCount( const hashed_string& shopID, const hashed_string& itemID ) const
    {
        const ShopDef*   pShop    = _pShopCatalog != nullptr ? _pShopCatalog->find( shopID ) : nullptr;
        const ShopState* pRuntime = findRuntime( shopID );
        if ( pShop == nullptr || pRuntime == nullptr )
            return 0;
        const optional<int32> stockIndex = findStockIndex( *pShop, itemID );
        return stockIndex.has_value() ? pRuntime->_listStockCount[static_cast<size_t>( *stockIndex )] : 0;
    }

    float32 ShopManager::getSellFactor( const hashed_string& shopID, const hashed_string& itemID ) const
    {
        const ShopState* pRuntime = findRuntime( shopID );
        if ( pRuntime == nullptr )
            return 1.0f;
        const optional<int32> saturationIndex = ShopInternal::findSaturationIndex( *pRuntime, itemID );
        return saturationIndex.has_value() ? pRuntime->_listSellFactor[static_cast<size_t>( *saturationIndex )] : 1.0f;
    }

    bool ShopManager::isUnlocked( const ShopStockDef& stock ) const
    {
        if ( stock._condition.isEmpty() )
            return true;
        return _pConditionContext != nullptr && stock._condition.evaluate( *_pConditionContext );
    }

    hashed_string ShopManager::getCurrency( const hashed_string& shopID ) const
    {
        const ShopDef* pShop = _pShopCatalog != nullptr ? _pShopCatalog->find( shopID ) : nullptr;
        return pShop != nullptr ? pShop->_currency : Wallet::getDefaultCurrency();
    }

    const ShopState* ShopManager::findRuntime( const hashed_string& shopID ) const
    {
        return VectorUtil::findBy<&ShopState::_shopID>( _listRuntime, shopID );
    }

    ShopState* ShopManager::findRuntimeMutable( const hashed_string& shopID )
    {
        return const_cast<ShopState*>( static_cast<const ShopManager*>( this )->findRuntime( shopID ) );
    }

    optional<int32> ShopManager::findStockIndex( const ShopDef& shop, const hashed_string& itemID ) const
    {
        for ( size_t index = 0; index < shop._listStock.size(); ++index )
        {
            if ( shop._listStock[index]._itemID == itemID )
                return static_cast<int32>( index );
        }
        return nullopt;
    }

    int32 ShopManager::computeBasePrice( const ShopStockDef& stock ) const
    {
        if ( stock.hasFixedPrice() )
            return stock._price;
        const ItemDef* pItem = _pItemCatalog != nullptr ? _pItemCatalog->find( stock._itemID ) : nullptr;
        return pItem != nullptr ? pItem->_value : 0;
    }

    bool ShopManager::isRefused( const ShopDef& shop, const ShopState& runtime, const hashed_string& itemID ) const
    {
        const ItemDef* pItem = _pItemCatalog != nullptr ? _pItemCatalog->find( itemID ) : nullptr;
        if ( pItem == nullptr || pItem->_category.empty() )
            return false;
        return shop.isRefusedCategory( pItem->_category ) || VectorUtil::contains( runtime._listExtraRefused, pItem->_category );
    }

    void ShopManager::serializeState( StateIO& io )
    {
        const auto serializeShop = [&]( ShopState& shop )
        {
            io.value( shop._shopID );
            io.list( shop._listStockCount );
            uint32 saturatedCount = static_cast<uint32>( shop._listSaturatedItem.size() );
            io.count( 8, saturatedCount );
            if ( io.isOk() == false )
                return;
            if ( io.isReading() )
            {
                shop._listSaturatedItem.resize( saturatedCount );
                shop._listSellFactor.resize( saturatedCount, 1.0f );
            }
            for ( uint32 index = 0; index < saturatedCount; ++index )
            {
                io.value( shop._listSaturatedItem[index] );
                float32 sellFactor = index < shop._listSellFactor.size() ? shop._listSellFactor[index] : 1.0f;
                io.value( sellFactor );
                if ( io.isReading() )
                {
                    io.check( 0.0f <= sellFactor );
                    shop._listSellFactor[index] = sellFactor;
                }
            }
            io.list( shop._listExtraRefused );
            io.value( shop._buyModifier );
            io.value( shop._sellModifier );
            io.value( shop._daysSinceRestock );
            // `setPriceModifier` 가 음수 배율을 거절한다 — 상태 바이트도 같다. 음수 값은 팔 때 돈을 내게 하고 살 때 돈을 준다.
            io.check( 0.0f <= shop._buyModifier && 0.0f <= shop._sellModifier && 0 <= shop._daysSinceRestock );
            // 되사기 판매마다 이름(4) + 돈(8) + 조각 수(4), 조각마다 슬롯 하나 — 제작 대기열의 꺼낸 재료와 같은 슬롯 형식
            io.list( shop._listBuyback, uint32( sizeof( uint32 ) + sizeof( int64 ) + sizeof( uint32 ) ), [&]( ShopBuybackEntry& entry )
            {
                io.value( entry._itemID );
                io.value( entry._price );
                uint32 pieceCount = static_cast<uint32>( entry._listPiece.size() );
                io.count( Inventory::kSlotStateMinBytes, pieceCount );
                if ( io.check( entry._itemID.empty() == false && 0 <= entry._price && 0 < pieceCount ) == false )
                    return;
                if ( io.isReading() )
                {
                    entry._listPiece.assign( pieceCount, InventorySlot{} );
                }
                for ( InventorySlot& piece : entry._listPiece )
                {
                    Inventory::serializeSlot( io, piece );
                    if ( io.isOk() == false )
                        return;
                }
            } );
        };
        io.header( kStateTag, kStateVersion );
        uint32 shopCount = static_cast<uint32>( _listRuntime.size() );
        // 가게마다 이름(4) + 개수 네 개(16) + 배율 두 개(8) + 날짜(4) 이상
        io.count( 32, shopCount );
        if ( io.isOk() == false )
            return;
        if ( io.isWriting() )
        {
            for ( ShopState& runtime : _listRuntime )
            {
                serializeShop( runtime );
            }
            return;
        }
        for ( uint32 shopIndex = 0; shopIndex < shopCount; ++shopIndex )
        {
            ShopState saved;
            serializeShop( saved );
            if ( io.isOk() == false )
                return;
            // 카탈로그에서 지운 가게는 버리고, 재고 항목 수가 바뀐 가게는 깨진 것으로 본다. 재고는 정의 순서로 짝지어 복원한다
            for ( ShopState& runtime : _listRuntime )
            {
                if ( runtime._shopID != saved._shopID )
                    continue;
                if ( io.check( runtime._listStockCount.size() == saved._listStockCount.size() ) == false )
                    return;
                runtime = std::move( saved );
                break;
            }
        }
        _eventBuffer.clear();
    }

} // namespace sw

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<ShopCatalog>::acquire`). 경로 규칙 없이 접미사로 가르는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( ShopCatalog, ShopCatalog, SW_GF_API, ShopCatalog::kAssetType.withLoader( AssetTypeLoader::of<ShopCatalog>( MemoryTag::Game ) ) );
} // namespace sw
