#include "pch.h"

#include "GameFramework/Kits/Simulation/RestaurantSim/IngredientStock.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemStackList.h"

namespace sw
{
    SW_LOG_CALLER( "IngredientStock" );

    namespace
    {
        struct IngredientStockInternal
        {
            /** @brief 정렬 키 — 상하지 않는 묶음(−1)은 가장 뒤입니다. */
            static int32 makeExpiryKey( const IngredientBatch& batch ) { return batch._daysLeft < 0 ? 0x7fffffff : batch._daysLeft; }
        };
    } // namespace
} // namespace sw

namespace sw
{
    IngredientStock::IngredientStock()
        : _listBatch{}
        , _pInventory{ nullptr }
    {
    }

    void IngredientStock::initialize( Inventory* pInventory )
    {
        _pInventory = pInventory;
        _listBatch.clear();
    }

    int32 IngredientStock::addFresh( const hashed_string& itemId, int32 count, int32 shelfLife, int64 unitCost )
    {
        if ( _pInventory == nullptr || count <= 0 )
            return 0;
        const int32 addedCount = _pInventory->addItem( itemId, count );
        recordBatch( itemId, addedCount, shelfLife, unitCost );
        return addedCount;
    }

    void IngredientStock::recordBatch( const hashed_string& itemId, int32 count, int32 shelfLife, int64 unitCost )
    {
        if ( count <= 0 )
            return;
        _listBatch.push_back( IngredientBatch{ itemId, unitCost, count, shelfLife > 0 ? shelfLife : -1 } );
        sortBatches();
    }

    bool IngredientStock::consume( const hashed_string& itemId, int32 count, int64& outCost )
    {
        if ( _pInventory == nullptr || count <= 0 || _pInventory->hasItem( itemId, count ) == false )
            return false;
        reconcile( itemId );
        if ( _pInventory->removeItem( itemId, count ) == false )
            return false;
        int32 remaining = count;
        for ( IngredientBatch& batch : _listBatch )
        {
            if ( remaining == 0 )
                break;
            if ( batch._itemId != itemId || batch._count <= 0 )
                continue;
            const int32 takenCount = batch._count < remaining ? batch._count : remaining;
            batch._count -= takenCount;
            remaining -= takenCount;
            outCost += batch._unitCost * takenCount;
        }
        // 묶음 없이 인벤토리에만 있던 것은 원가 0 이다.
        _listBatch.erase( std::remove_if( _listBatch.begin(), _listBatch.end(), []( const IngredientBatch& batch )
        { return batch._count <= 0; } ),
                          _listBatch.end() );
        return true;
    }

    bool IngredientStock::consumeItems( const ItemStackList& items, int32 times, int64& outCost )
    {
        if ( _pInventory == nullptr || times <= 0 )
            return false;
        // 이름 순으로 돌아 결과(원가 합 · 로그)가 해시 순서에 기대지 않게 한다.
        vector<hashed_string> listItem;
        items.getItemIds( listItem );
        std::sort( listItem.begin(), listItem.end(), HashedStringLexicalLess{} );
        for ( const hashed_string& itemId : listItem )
        {
            if ( _pInventory->hasItem( itemId, items.getItemCount( itemId ) * times ) == false )
                return false;
        }
        for ( const hashed_string& itemId : listItem )
        {
            if ( consume( itemId, items.getItemCount( itemId ) * times, outCost ) == false )
                SW_LOG_WARNING( "lost '%#' between the check and the take", itemId.c_str() );
        }
        return true;
    }

    void IngredientStock::advanceDay( vector<IngredientSpoilage>& outListSpoilage )
    {
        for ( IngredientBatch& batch : _listBatch )
        {
            if ( batch._daysLeft < 0 )
                continue;
            reconcile( batch._itemId );
            --batch._daysLeft;
            if ( batch._daysLeft > 0 || batch._count <= 0 )
                continue;
            const int32 heldCount    = _pInventory != nullptr ? _pInventory->getItemCount( batch._itemId ) : 0;
            const int32 spoiledCount = batch._count < heldCount ? batch._count : heldCount;
            if ( spoiledCount > 0 && _pInventory != nullptr && _pInventory->removeItem( batch._itemId, spoiledCount ) )
                outListSpoilage.push_back( IngredientSpoilage{ batch._itemId, batch._unitCost * spoiledCount, spoiledCount } );
            batch._count = 0;
        }
        _listBatch.erase( std::remove_if( _listBatch.begin(), _listBatch.end(), []( const IngredientBatch& batch )
        { return batch._count <= 0; } ),
                          _listBatch.end() );
    }

    int32 IngredientStock::findEarliestExpiry( const hashed_string& itemId ) const
    {
        for ( const IngredientBatch& batch : _listBatch )
        {
            if ( batch._itemId == itemId && batch._daysLeft >= 0 )
                return batch._daysLeft;
        }
        return -1;
    }

    int32 IngredientStock::getBatchCount( const hashed_string& itemId ) const
    {
        int32 count = 0;
        for ( const IngredientBatch& batch : _listBatch )
        {
            if ( batch._itemId == itemId )
                count += batch._count;
        }
        return count;
    }

    void IngredientStock::reconcile( const hashed_string& itemId )
    {
        if ( _pInventory == nullptr )
            return;
        int32 excess = getBatchCount( itemId ) - _pInventory->getItemCount( itemId );
        for ( IngredientBatch& batch : _listBatch )
        {
            if ( excess <= 0 )
                break;
            if ( batch._itemId != itemId )
                continue;
            const int32 droppedCount = batch._count < excess ? batch._count : excess;
            batch._count -= droppedCount;
            excess -= droppedCount;
        }
    }

    void IngredientStock::sortBatches()
    {
        std::stable_sort( _listBatch.begin(), _listBatch.end(), []( const IngredientBatch& lhs, const IngredientBatch& rhs )
        { return IngredientStockInternal::makeExpiryKey( lhs ) < IngredientStockInternal::makeExpiryKey( rhs ); } );
    }

    void IngredientStock::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint32>( _listBatch.size() );
        for ( const IngredientBatch& batch : _listBatch )
        {
            StateArchiveUtil::writeName( outArchive, batch._itemId );
            outArchive << batch._unitCost;
            outArchive << batch._count;
            outArchive << batch._daysLeft;
        }
    }

    bool IngredientStock::readState( Archive& archive )
    {
        uint32 count = 0;
        // 묶음마다 이름(4) + 원가(8) + 개수 · 남은 날(8)
        if ( StateArchiveUtil::readCount( archive, 20, count ) == false )
            return false;
        vector<IngredientBatch> listBatch( count );
        for ( IngredientBatch& batch : listBatch )
        {
            if ( StateArchiveUtil::readName( archive, batch._itemId ) == false )
                return false;
            archive >> batch._unitCost;
            archive >> batch._count;
            archive >> batch._daysLeft;
            if ( archive.isError() || batch._count < 0 )
                return false;
        }
        _listBatch = std::move( listBatch );
        return true;
    }
} // namespace sw
