#include "pch.h"

#include "GameFramework/Base/Gameplay/Inventory/Shop.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Xml/XmlDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXml.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemCatalog.h"

namespace sw
{
    SW_LOG_CALLER( "Shop" );

    namespace
    {
        struct ShopInternal
        {
            static int32 roundPrice( float32 value ) { return MathUtil::max( 0, static_cast<int32>( MathUtil::round( value ) ) ); }

            static bool containsID( const vector<hashed_string>& listID, const hashed_string& id )
            {
                for ( const hashed_string& ownID : listID )
                {
                    if ( ownID == id )
                        return true;
                }
                return false;
            }

            static int32 findSaturationIndex( const ShopRuntime& runtime, const hashed_string& itemID )
            {
                for ( size_t index = 0; index < runtime._listSaturatedItem.size(); ++index )
                {
                    if ( runtime._listSaturatedItem[index] == itemID )
                        return static_cast<int32>( index );
                }
                return -1;
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
        WalletBalance& balance = findOrAddBalance( currency );
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
        WalletBalance& balance = findOrAddBalance( currency );
        balance._amount -= amount;
        ++_revision;
        _eventBuffer.push( WalletEvent{ currency, -amount, balance._amount } );
        return true;
    }

    void Wallet::charge( const hashed_string& currency, int64 amount )
    {
        if ( amount <= 0 || currency.empty() )
            return;
        WalletBalance& balance = findOrAddBalance( currency );
        balance._amount -= amount;
        ++_revision;
        _eventBuffer.push( WalletEvent{ currency, -amount, balance._amount } );
    }

    void Wallet::setBalance( const hashed_string& currency, int64 amount )
    {
        if ( currency.empty() )
            return;
        findOrAddBalance( currency )._amount = MathUtil::max<int64>( 0, amount );
        ++_revision;
    }

    void Wallet::clear()
    {
        _listBalance.clear();
        _eventBuffer.clear();
        ++_revision;
    }

    void Wallet::drainEvents( vector<WalletEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    void Wallet::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint32>( _listBalance.size() );
        for ( const WalletBalance& balance : _listBalance )
        {
            StateArchiveUtil::writeName( outArchive, balance._currency );
            outArchive << balance._amount;
        }
    }

    bool Wallet::readState( Archive& archive )
    {
        uint32 count = 0;
        // 칸마다 이름 길이(4) + 잔액(8) 이상
        if ( StateArchiveUtil::readCount( archive, 12, count ) == false )
            return false;
        vector<WalletBalance> listBalance;
        listBalance.reserve( count );
        for ( uint32 index = 0; index < count; ++index )
        {
            WalletBalance balance;
            if ( StateArchiveUtil::readName( archive, balance._currency ) == false )
                return false;
            archive >> balance._amount;
            const bool bValid = archive.isOk() && balance._currency.empty() == false;
            if ( bValid == false )
                return false;
            listBalance.push_back( balance );
        }
        _listBalance = std::move( listBalance );
        _eventBuffer.clear();
        ++_revision;
        return true;
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

    WalletBalance& Wallet::findOrAddBalance( const hashed_string& currency )
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
        for ( const ShopStockDef& stock : _listStock )
        {
            if ( stock._itemID == itemID )
                return &stock;
        }
        return nullptr;
    }

    bool ShopDef::refusesCategory( const hashed_string& category ) const
    {
        return category.empty() == false && ShopInternal::containsID( _listRefusedCategory, category );
    }

    const utf8* toString( ShopResult result )
    {
        switch ( result )
        {
            case ShopResult::Ok:
                return "Ok";
            case ShopResult::UnknownShop:
                return "UnknownShop";
            case ShopResult::UnknownItem:
                return "UnknownItem";
            case ShopResult::InvalidCount:
                return "InvalidCount";
            case ShopResult::Locked:
                return "Locked";
            case ShopResult::OutOfStock:
                return "OutOfStock";
            case ShopResult::NotEnoughMoney:
                return "NotEnoughMoney";
            case ShopResult::NoRoom:
                return "NoRoom";
            case ShopResult::Refused:
                return "Refused";
            case ShopResult::NotOwned:
                return "NotOwned";
        }
        return "Unknown";
    }

    uint32 ShopCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;
        for ( XmlNode node = root.findChild( "Shop" ); node; node = node.findNextSibling( "Shop" ) )
        {
            const utf8* pID = GameDataXml::findRequiredID( node, sourceName );
            if ( pID == nullptr )
                continue;
            ShopDef def;
            def._id               = hashed_string( pID );
            const utf8* pCurrency = node.findAttribute( "currency" );
            def._currency         = pCurrency != nullptr && pCurrency[0] != '\0' ? hashed_string( pCurrency ) : Wallet::getDefaultCurrency();
            def._buyMultiplier    = MathUtil::max( 0.0f, node.getAttributeFloat( "buyMultiplier", def._buyMultiplier ) );
            def._sellMultiplier   = MathUtil::max( 0.0f, node.getAttributeFloat( "sellMultiplier", def._sellMultiplier ) );
            def._saturation       = MathUtil::max( 0.0f, node.getAttributeFloat( "saturation", def._saturation ) );
            def._minSellFactor    = MathUtil::saturate( node.getAttributeFloat( "minSellFactor", def._minSellFactor ) );
            def._recoveryPerDay   = MathUtil::max( 0.0f, node.getAttributeFloat( "recovery", def._recoveryPerDay ) );
            def._restockDays      = MathUtil::max( 0, node.getAttributeInt( "restockDays", def._restockDays ) );
            GameDataXml::forEachToken( node.getAttributeText( "refuses" ), ",; ", [&]( string_view token )
            {
                def._listRefusedCategory.push_back( hashed_string( token ) );
            } );
            for ( XmlNode stockNode = node.findChild( "Stock" ); stockNode; stockNode = stockNode.findNextSibling( "Stock" ) )
            {
                const utf8* pItem = stockNode.findAttribute( "item" );
                if ( pItem == nullptr || pItem[0] == '\0' )
                {
                    SW_LOG_WARNING( "%#: shop '%#' has a <Stock> without an item - skipped", sourceName, pID );
                    continue;
                }
                ShopStockDef stock;
                stock._itemID            = hashed_string( pItem );
                stock._price             = stockNode.getAttributeInt( "price", stock._price );
                stock._count             = MathUtil::max( -1, stockNode.getAttributeInt( "count", stock._count ) );
                stock._restock           = MathUtil::max( 0, stockNode.getAttributeInt( "restock", stock._restock ) );
                const utf8* pRequirement = stockNode.findAttribute( "requires" );
                if ( pRequirement != nullptr )
                    stock._requirement = pRequirement;
                def._listStock.push_back( stock );
            }
            addShop( def );
            ++loadedCount;
        }
        if ( loadedCount == 0 )
            SW_LOG_WARNING( "%#: no <Shop> entries", sourceName );
        return loadedCount;
    }

    // ------------------------------------------------------------------------------
    // ShopState
    // ------------------------------------------------------------------------------
    ShopState::ShopState()
        : _listRuntime{}
        , _eventBuffer{}
        , _pShopCatalog{ nullptr }
        , _pItemCatalog{ nullptr }
        , _pConditionEvaluator{ nullptr }
    {
    }

    void ShopState::initialize( const ShopCatalog* pShopCatalog, const ItemCatalog* pItemCatalog )
    {
        _pShopCatalog = pShopCatalog;
        _pItemCatalog = pItemCatalog;
        _listRuntime.clear();
        _eventBuffer.clear();
        if ( _pShopCatalog == nullptr )
            return;
        for ( const ShopDef& shop : _pShopCatalog->getShops() )
        {
            ShopRuntime runtime;
            runtime._shopID = shop._id;
            for ( const ShopStockDef& stock : shop._listStock )
            {
                runtime._listStockCount.push_back( stock._count );
            }
            _listRuntime.push_back( runtime );
        }
    }

    void ShopState::setPriceModifier( const hashed_string& shopID, float32 buyModifier, float32 sellModifier )
    {
        ShopRuntime* pRuntime = findRuntime( shopID );
        if ( pRuntime == nullptr )
            return;
        pRuntime->_buyModifier  = MathUtil::max( 0.0f, buyModifier );
        pRuntime->_sellModifier = MathUtil::max( 0.0f, sellModifier );
    }

    void ShopState::refuseCategory( const hashed_string& shopID, const hashed_string& category )
    {
        ShopRuntime* pRuntime = findRuntime( shopID );
        if ( pRuntime != nullptr && category.empty() == false && ShopInternal::containsID( pRuntime->_listExtraRefused, category ) == false )
            pRuntime->_listExtraRefused.push_back( category );
    }

    ShopResult ShopState::evaluateBuy( const hashed_string& shopID, const hashed_string& itemID, int32 count, const Wallet& wallet,
                                       const Inventory& inventory ) const
    {
        const ShopDef*     pShop    = _pShopCatalog != nullptr ? _pShopCatalog->findShop( shopID ) : nullptr;
        const ShopRuntime* pRuntime = findRuntime( shopID );
        if ( pShop == nullptr || pRuntime == nullptr )
            return ShopResult::UnknownShop;
        const int32 stockIndex = findStockIndex( *pShop, itemID );
        if ( stockIndex < 0 || _pItemCatalog == nullptr || _pItemCatalog->findItem( itemID ) == nullptr )
            return ShopResult::UnknownItem;
        if ( count <= 0 )
            return ShopResult::InvalidCount;
        const ShopStockDef& stock = pShop->_listStock[static_cast<size_t>( stockIndex )];
        if ( isUnlocked( stock ) == false )
            return ShopResult::Locked;
        const int32 stockCount = pRuntime->_listStockCount[static_cast<size_t>( stockIndex )];
        if ( stockCount >= 0 && stockCount < count )
            return ShopResult::OutOfStock;
        const int64 total = static_cast<int64>( computeBuyPrice( shopID, itemID ) ) * count;
        if ( wallet.canAfford( pShop->_currency, total ) == false )
            return ShopResult::NotEnoughMoney;
        if ( inventory.hasRoomFor( itemID, count ) == false )
            return ShopResult::NoRoom;
        return ShopResult::Ok;
    }

    ShopResult ShopState::buy( const hashed_string& shopID, const hashed_string& itemID, int32 count, Wallet& wallet, Inventory& inventory, int64* pOutPaid )
    {
        const ShopResult result = evaluateBuy( shopID, itemID, count, wallet, inventory );
        if ( result != ShopResult::Ok )
            return result;
        const ShopDef* pShop      = _pShopCatalog->findShop( shopID );
        ShopRuntime*   pRuntime   = findRuntime( shopID );
        const int32    stockIndex = findStockIndex( *pShop, itemID );
        const int64    total      = static_cast<int64>( computeBuyPrice( shopID, itemID ) ) * count;
        if ( wallet.trySpend( pShop->_currency, total ) == false )
            return ShopResult::NotEnoughMoney;
        const int32 addedCount = inventory.addItem( itemID, count );
        if ( addedCount != count )
            SW_LOG_WARNING( "inventory accepted %# of %# '%#' after hasRoomFor said yes", addedCount, count, itemID.c_str() );
        int32& stockCount = pRuntime->_listStockCount[static_cast<size_t>( stockIndex )];
        if ( stockCount >= 0 )
            stockCount -= count;
        _eventBuffer.push( ShopEvent{ shopID, itemID, total, count, ShopEvent::Kind::Bought } );
        if ( pOutPaid != nullptr )
            *pOutPaid = total;
        return ShopResult::Ok;
    }

    ShopResult ShopState::evaluateSell( const hashed_string& shopID, const hashed_string& itemID, int32 count, const Inventory& inventory ) const
    {
        const ShopDef*     pShop    = _pShopCatalog != nullptr ? _pShopCatalog->findShop( shopID ) : nullptr;
        const ShopRuntime* pRuntime = findRuntime( shopID );
        if ( pShop == nullptr || pRuntime == nullptr )
            return ShopResult::UnknownShop;
        if ( _pItemCatalog == nullptr || _pItemCatalog->findItem( itemID ) == nullptr )
            return ShopResult::UnknownItem;
        if ( count <= 0 )
            return ShopResult::InvalidCount;
        if ( isRefused( *pShop, *pRuntime, itemID ) )
            return ShopResult::Refused;
        if ( inventory.hasItem( itemID, count ) == false )
            return ShopResult::NotOwned;
        return ShopResult::Ok;
    }

    ShopResult ShopState::sell( const hashed_string& shopID, const hashed_string& itemID, int32 count, Wallet& wallet, Inventory& inventory, int64* pOutReceived )
    {
        const ShopResult result = evaluateSell( shopID, itemID, count, inventory );
        if ( result != ShopResult::Ok )
            return result;
        const int64 total = computeSellTotal( shopID, itemID, count );
        if ( inventory.removeItem( itemID, count ) == false )
            return ShopResult::NotOwned;
        const ShopDef* pShop    = _pShopCatalog->findShop( shopID );
        ShopRuntime*   pRuntime = findRuntime( shopID );
        wallet.add( pShop->_currency, total );
        if ( pOutReceived != nullptr )
            *pOutReceived = total;
        if ( pShop->_saturation > 0.0f )
        {
            int32 saturationIndex = ShopInternal::findSaturationIndex( *pRuntime, itemID );
            if ( saturationIndex < 0 )
            {
                saturationIndex = static_cast<int32>( pRuntime->_listSaturatedItem.size() );
                pRuntime->_listSaturatedItem.push_back( itemID );
                pRuntime->_listSellFactor.push_back( 1.0f );
            }
            float32& factor = pRuntime->_listSellFactor[static_cast<size_t>( saturationIndex )];
            factor          = MathUtil::max( pShop->_minSellFactor, factor - pShop->_saturation * static_cast<float32>( count ) );
        }
        _eventBuffer.push( ShopEvent{ shopID, itemID, total, count, ShopEvent::Kind::Sold } );
        return ShopResult::Ok;
    }

    void ShopState::advanceDay()
    {
        if ( _pShopCatalog == nullptr )
            return;
        for ( ShopRuntime& runtime : _listRuntime )
        {
            const ShopDef* pShop = _pShopCatalog->findShop( runtime._shopID );
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
                const ShopStockDef& stock      = pShop->_listStock[stockIndex];
                int32&              stockCount = runtime._listStockCount[stockIndex];
                if ( stock._count < 0 || stock._restock <= 0 || stockCount >= stock._count )
                    continue;
                const int32 newCount = MathUtil::min( stock._count, stockCount + stock._restock );
                _eventBuffer.push( ShopEvent{ runtime._shopID, stock._itemID, 0, newCount - stockCount, ShopEvent::Kind::Restocked } );
                stockCount = newCount;
            }
        }
    }

    void ShopState::drainEvents( vector<ShopEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    int32 ShopState::computeBuyPrice( const hashed_string& shopID, const hashed_string& itemID ) const
    {
        const ShopDef*     pShop    = _pShopCatalog != nullptr ? _pShopCatalog->findShop( shopID ) : nullptr;
        const ShopRuntime* pRuntime = findRuntime( shopID );
        if ( pShop == nullptr || pRuntime == nullptr )
            return -1;
        const int32 stockIndex = findStockIndex( *pShop, itemID );
        if ( stockIndex < 0 )
            return -1;
        const int32 basePrice = computeBasePrice( pShop->_listStock[static_cast<size_t>( stockIndex )] );
        return ShopInternal::roundPrice( static_cast<float32>( basePrice ) * pShop->_buyMultiplier * pRuntime->_buyModifier );
    }

    int64 ShopState::computeSellTotal( const hashed_string& shopID, const hashed_string& itemID, int32 count ) const
    {
        const ShopDef*     pShop    = _pShopCatalog != nullptr ? _pShopCatalog->findShop( shopID ) : nullptr;
        const ShopRuntime* pRuntime = findRuntime( shopID );
        const ItemDef*     pItem    = _pItemCatalog != nullptr ? _pItemCatalog->findItem( itemID ) : nullptr;
        if ( pShop == nullptr || pRuntime == nullptr || pItem == nullptr || count <= 0 )
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

    int32 ShopState::getStockCount( const hashed_string& shopID, const hashed_string& itemID ) const
    {
        const ShopDef*     pShop    = _pShopCatalog != nullptr ? _pShopCatalog->findShop( shopID ) : nullptr;
        const ShopRuntime* pRuntime = findRuntime( shopID );
        if ( pShop == nullptr || pRuntime == nullptr )
            return 0;
        const int32 stockIndex = findStockIndex( *pShop, itemID );
        return stockIndex >= 0 ? pRuntime->_listStockCount[static_cast<size_t>( stockIndex )] : 0;
    }

    float32 ShopState::getSellFactor( const hashed_string& shopID, const hashed_string& itemID ) const
    {
        const ShopRuntime* pRuntime = findRuntime( shopID );
        if ( pRuntime == nullptr )
            return 1.0f;
        const int32 saturationIndex = ShopInternal::findSaturationIndex( *pRuntime, itemID );
        return saturationIndex >= 0 ? pRuntime->_listSellFactor[static_cast<size_t>( saturationIndex )] : 1.0f;
    }

    bool ShopState::isUnlocked( const ShopStockDef& stock ) const
    {
        if ( stock._requirement.empty() )
            return true;
        return _pConditionEvaluator != nullptr && _pConditionEvaluator->isConditionMet( string_view( stock._requirement.c_str(), stock._requirement.size() ) );
    }

    hashed_string ShopState::getCurrency( const hashed_string& shopID ) const
    {
        const ShopDef* pShop = _pShopCatalog != nullptr ? _pShopCatalog->findShop( shopID ) : nullptr;
        return pShop != nullptr ? pShop->_currency : Wallet::getDefaultCurrency();
    }

    const ShopRuntime* ShopState::findRuntime( const hashed_string& shopID ) const
    {
        for ( const ShopRuntime& runtime : _listRuntime )
        {
            if ( runtime._shopID == shopID )
                return &runtime;
        }
        return nullptr;
    }

    ShopRuntime* ShopState::findRuntime( const hashed_string& shopID )
    {
        return const_cast<ShopRuntime*>( static_cast<const ShopState*>( this )->findRuntime( shopID ) );
    }

    int32 ShopState::findStockIndex( const ShopDef& shop, const hashed_string& itemID ) const
    {
        for ( size_t index = 0; index < shop._listStock.size(); ++index )
        {
            if ( shop._listStock[index]._itemID == itemID )
                return static_cast<int32>( index );
        }
        return -1;
    }

    int32 ShopState::computeBasePrice( const ShopStockDef& stock ) const
    {
        if ( stock._price >= 0 )
            return stock._price;
        const ItemDef* pItem = _pItemCatalog != nullptr ? _pItemCatalog->findItem( stock._itemID ) : nullptr;
        return pItem != nullptr ? pItem->_value : 0;
    }

    bool ShopState::isRefused( const ShopDef& shop, const ShopRuntime& runtime, const hashed_string& itemID ) const
    {
        const ItemDef* pItem = _pItemCatalog != nullptr ? _pItemCatalog->findItem( itemID ) : nullptr;
        if ( pItem == nullptr || pItem->_category.empty() )
            return false;
        return shop.refusesCategory( pItem->_category ) || ShopInternal::containsID( runtime._listExtraRefused, pItem->_category );
    }

    void ShopState::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint32>( _listRuntime.size() );
        for ( const ShopRuntime& runtime : _listRuntime )
        {
            StateArchiveUtil::writeName( outArchive, runtime._shopID );
            outArchive << static_cast<uint32>( runtime._listStockCount.size() );
            for ( const int32 count : runtime._listStockCount )
            {
                outArchive << count;
            }
            outArchive << static_cast<uint32>( runtime._listSaturatedItem.size() );
            for ( size_t index = 0; index < runtime._listSaturatedItem.size(); ++index )
            {
                StateArchiveUtil::writeName( outArchive, runtime._listSaturatedItem[index] );
                outArchive << ( index < runtime._listSellFactor.size() ? runtime._listSellFactor[index] : 1.0f );
            }
            outArchive << static_cast<uint32>( runtime._listExtraRefused.size() );
            for ( const hashed_string& category : runtime._listExtraRefused )
            {
                StateArchiveUtil::writeName( outArchive, category );
            }
            outArchive << runtime._buyModifier;
            outArchive << runtime._sellModifier;
            outArchive << runtime._daysSinceRestock;
        }
    }

    bool ShopState::readState( Archive& archive )
    {
        vector<ShopRuntime> listRuntime = _listRuntime;
        uint32              shopCount   = 0;
        // 가게마다 이름(4) + 개수 셋(12) + 배율 둘(8) + 날(4) 이상
        if ( StateArchiveUtil::readCount( archive, 28, shopCount ) == false )
            return false;
        for ( uint32 shopIndex = 0; shopIndex < shopCount; ++shopIndex )
        {
            ShopRuntime saved;
            uint32      count = 0;
            if ( StateArchiveUtil::readName( archive, saved._shopID ) == false || StateArchiveUtil::readCount( archive, 4, count ) == false )
                return false;
            saved._listStockCount.resize( count, 0 );
            for ( int32& stockCount : saved._listStockCount )
            {
                archive >> stockCount;
            }
            if ( StateArchiveUtil::readCount( archive, 8, count ) == false )
                return false;
            saved._listSaturatedItem.resize( count );
            saved._listSellFactor.resize( count, 1.0f );
            for ( uint32 index = 0; index < count; ++index )
            {
                if ( StateArchiveUtil::readName( archive, saved._listSaturatedItem[index] ) == false )
                    return false;
                archive >> saved._listSellFactor[index];
            }
            if ( StateArchiveUtil::readCount( archive, 4, count ) == false )
                return false;
            saved._listExtraRefused.resize( count );
            for ( hashed_string& category : saved._listExtraRefused )
            {
                if ( StateArchiveUtil::readName( archive, category ) == false )
                    return false;
            }
            archive >> saved._buyModifier;
            archive >> saved._sellModifier;
            archive >> saved._daysSinceRestock;
            if ( archive.isError() )
                return false;
            // 카탈로그에서 지운 가게는 버리고, 재고 줄 수가 바뀐 가게는 깨진 것으로 본다(재고는 정의 순서로 짝짓는다)
            for ( ShopRuntime& runtime : listRuntime )
            {
                if ( runtime._shopID != saved._shopID )
                    continue;
                if ( runtime._listStockCount.size() != saved._listStockCount.size() )
                    return false;
                runtime = std::move( saved );
                break;
            }
        }
        _listRuntime = std::move( listRuntime );
        _eventBuffer.clear();
        return true;
    }
} // namespace sw
