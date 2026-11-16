#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/RestaurantSim/IngredientStock.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemStackList.h"

namespace sw
{
    namespace
    {
        struct IngredientStockInternal
        {
            /** @brief 정렬 키입니다. 상하지 않는 배치(-1)는 가장 뒤입니다. */
            static int32 makeExpiryKey( const IngredientBatch& batch ) { return batch._daysLeft < 0 ? 0x7fffffff : batch._daysLeft; }

            /** @brief 다 쓴 배치이면 true 입니다. 쓰거나 상한 뒤 목록에서 지웁니다. */
            static bool isEmptyBatch( const IngredientBatch& batch ) { return batch._count <= 0; }
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

    int32 IngredientStock::addFresh( const hashed_string& itemID, int32 count, int32 shelfLife, int64 unitCost )
    {
        if ( _pInventory == nullptr || count <= 0 )
            return 0;
        const int32 addedCount = _pInventory->addItem( itemID, count );
        recordBatch( itemID, addedCount, shelfLife, unitCost );
        return addedCount;
    }

    void IngredientStock::recordBatch( const hashed_string& itemID, int32 count, int32 shelfLife, int64 unitCost )
    {
        if ( count <= 0 )
            return;
        _listBatch.push_back( IngredientBatch{ itemID, unitCost, count, shelfLife > 0 ? shelfLife : IngredientBatch::kNoExpiry } );
        sortBatches();
    }

    bool IngredientStock::consume( const hashed_string& itemID, int32 count, int64& outCost )
    {
        const bool bCannotConsume = _pInventory == nullptr || count <= 0 || _pInventory->hasItem( itemID, count ) == false;
        if ( bCannotConsume )
            return false;
        reconcile( itemID );
        if ( _pInventory->consumeItem( itemID, count ) == false )
            return false;
        int32 remaining = count;
        for ( IngredientBatch& batch : _listBatch )
        {
            if ( remaining == 0 )
                break;
            if ( batch._itemID != itemID || batch._count <= 0 )
                continue;
            const int32 takenCount = batch._count < remaining ? batch._count : remaining;
            batch._count -= takenCount;
            remaining -= takenCount;
            outCost += batch._unitCost * takenCount;
        }
        // 배치 없이 인벤토리에만 있던 것은 원가 0 이다.
        VectorUtil::removeIf( _listBatch, IngredientStockInternal::isEmptyBatch );
        return true;
    }

    bool IngredientStock::consumeItems( const ItemStackList& items, int32 times, int64& outCost )
    {
        if ( _pInventory == nullptr || times <= 0 )
            return false;
        // 이름 순으로 돌아서, 결과(원가 합과 로그)가 해시 순서에 기대지 않게 한다.
        vector<hashed_string> listItem;
        items.getItemIDs( listItem );
        VectorUtil::sort( listItem, HashedStringLexicalLess{} );
        for ( const hashed_string& itemID : listItem )
        {
            if ( _pInventory->hasItem( itemID, items.getItemCount( itemID ) * times ) == false )
                return false;
        }
        for ( const hashed_string& itemID : listItem )
        {
            if ( consume( itemID, items.getItemCount( itemID ) * times, outCost ) == false )
                SW_LOG_WARNING_ID( "IngredientStock.LostBetweenCheckTake", "lost '%#' between the check and the take", itemID.c_str() );
        }
        return true;
    }

    void IngredientStock::advanceDay( vector<IngredientSpoilage>& outListSpoilage )
    {
        for ( IngredientBatch& batch : _listBatch )
        {
            if ( batch._daysLeft < 0 )
                continue;
            reconcile( batch._itemID );
            --batch._daysLeft;
            if ( batch._daysLeft > 0 || batch._count <= 0 )
                continue;
            const int32 heldCount         = _pInventory != nullptr ? _pInventory->getItemCount( batch._itemID ) : 0;
            const int32 spoiledCount      = batch._count < heldCount ? batch._count : heldCount;
            const bool  bCanRemoveSpoiled = spoiledCount > 0 && _pInventory != nullptr && _pInventory->consumeItem( batch._itemID, spoiledCount );
            if ( bCanRemoveSpoiled )
                outListSpoilage.push_back( IngredientSpoilage{ batch._itemID, batch._unitCost * spoiledCount, spoiledCount } );
            batch._count = 0;
        }
        VectorUtil::removeIf( _listBatch, IngredientStockInternal::isEmptyBatch );
    }

    optional<int32> IngredientStock::findEarliestExpiry( const hashed_string& itemID ) const
    {
        for ( const IngredientBatch& batch : _listBatch )
        {
            if ( batch._itemID == itemID && batch._daysLeft >= 0 )
                return batch._daysLeft;
        }
        return nullopt;
    }

    int32 IngredientStock::getBatchCount( const hashed_string& itemID ) const
    {
        int32 count = 0;
        for ( const IngredientBatch& batch : _listBatch )
        {
            if ( batch._itemID == itemID )
                count += batch._count;
        }
        return count;
    }

    void IngredientStock::reconcile( const hashed_string& itemID )
    {
        if ( _pInventory == nullptr )
            return;
        int32 excess = getBatchCount( itemID ) - _pInventory->getItemCount( itemID );
        for ( IngredientBatch& batch : _listBatch )
        {
            if ( excess <= 0 )
                break;
            if ( batch._itemID != itemID )
                continue;
            const int32 droppedCount = batch._count < excess ? batch._count : excess;
            batch._count -= droppedCount;
            excess -= droppedCount;
        }
    }

    void IngredientStock::sortBatches()
    {
        VectorUtil::stableSort( _listBatch, []( const IngredientBatch& lhs, const IngredientBatch& rhs )
        { return IngredientStockInternal::makeExpiryKey( lhs ) < IngredientStockInternal::makeExpiryKey( rhs ); } );
    }

    void IngredientStock::serializeState( StateIO& io )
    {
        // 배치마다 이름(4) + 원가(8) + 개수, 남은 날(8)
        io.list( _listBatch, 20, [&]( IngredientBatch& batch )
        {
            io.value( batch._itemID );
            io.value( batch._unitCost );
            io.value( batch._count );
            io.value( batch._daysLeft );
            // 남은 날은 -1(상하지 않는다)이거나 양수이다. `advanceDay` 가 0 을 하루 줄이면 -1 이 되어 상하지 않는 배치로 바뀐다.
            io.check( batch._itemID.empty() == false && 0 <= batch._unitCost && 0 <= batch._count && ( batch._daysLeft == -1 || batch._daysLeft > 0 ) );
        } );
    }

} // namespace sw
