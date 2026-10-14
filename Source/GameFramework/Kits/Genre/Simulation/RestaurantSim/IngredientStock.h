/**
 * @file IngredientStock.h
 * @brief 재료 재고의 신선도를 관리합니다. 기반 `Inventory` 의 개수 위에 들어온 배치(남은 날과 단가)를 얹어, 오래된 것부터 쓰고 기한이 지나면 버립니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;
    class Inventory;
    class ItemStackList;

    /** @brief 한 번에 들어온 재료 배치입니다. */
    struct IngredientBatch
    {
        hashed_string _itemID{};
        int64         _unitCost{ 0 }; ///< 한 개의 원가이다(결산의 재료비)
        int32         _count{ 0 };
        int32         _daysLeft{ 0 }; ///< 0 이하가 되면 버린다(-1 이면 상하지 않는다)
    };
} // namespace sw

namespace sw
{
    /** @brief 기한이 지나 버린 배치입니다. */
    struct IngredientSpoilage
    {
        hashed_string _itemID{};
        int64         _cost{ 0 };
        int32         _count{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class IngredientStock
     * @brief 인벤토리(빌림)의 재료를 배치 단위로 추적합니다. 쓸 때는 기한이 가까운 배치부터(같으면 먼저 들어온 것부터) 쓰고, 인벤토리에서도 같이 뺍니다.
     * @details 배치가 없는 아이템(손으로 인벤토리에 넣은 것)은 원가 0 이고 상하지 않는 것으로 본다. 인벤토리 개수가 배치 합보다 적어지면(밖에서 뺀 경우)
     *          `consume` 이 모자람을 알아채 배치를 맞춘다.
     */
    class SW_KIT_API IngredientStock
    {
    public:
        IngredientStock();

        void initialize( Inventory* pInventory );
        /** @brief 인벤토리에 넣고 배치를 적습니다. 들어간 개수를 반환합니다. @p shelfLife 가 0 이면 상하지 않습니다. */
        int32 addFresh( const hashed_string& itemID, int32 count, int32 shelfLife, int64 unitCost );
        /** @brief 이미 인벤토리에 들어간 것(가게에서 산 것)의 배치만 적습니다. */
        void recordBatch( const hashed_string& itemID, int32 count, int32 shelfLife, int64 unitCost );
        /** @brief @p count 개가 모두 있으면 오래된 것부터 빼고 원가 합을 @p outCost 에 더합니다. 모자라면 아무것도 빼지 않고 false 를 반환합니다. */
        [[nodiscard]] bool consume( const hashed_string& itemID, int32 count, int64& outCost );
        /** @brief 목록의 재료 x @p times 가 다 있으면 모두 뺍니다(전부 빼거나 아무것도 빼지 않습니다). */
        [[nodiscard]] bool consumeItems( const ItemStackList& items, int32 times, int64& outCost );
        /** @brief 하루를 넘깁니다. 남은 날을 줄이고 0 이 된 배치를 인벤토리에서 버립니다. */
        void advanceDay( vector<IngredientSpoilage>& outListSpoilage );

        /** @brief 그 재료의 가장 가까운 남은 날을 반환합니다(상하는 배치가 없으면 -1). */
        int32                          findEarliestExpiry( const hashed_string& itemID ) const;
        int32                          getBatchCount( const hashed_string& itemID ) const;
        const vector<IngredientBatch>& getBatches() const { return _listBatch; }

        /** @brief 배치(재료, 원가, 개수, 남은 날)를 씁니다. 빌린 창고는 저장하지 않습니다(주인이 저장합니다). */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트를 읽어 상태를 복원합니다. 데이터가 깨졌으면 false 를 반환하고 상태는 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        /** @brief 배치 합이 인벤토리 개수를 넘으면 오래된 배치부터 줄입니다(밖에서 뺀 만큼). */
        void reconcile( const hashed_string& itemID );
        void sortBatches();

        vector<IngredientBatch> _listBatch; ///< 남은 날이 적은 것부터 정렬한다(상하지 않는 것은 뒤)
        Inventory*              _pInventory;
    };
} // namespace sw
