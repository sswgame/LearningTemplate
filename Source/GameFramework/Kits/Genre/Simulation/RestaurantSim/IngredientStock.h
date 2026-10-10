/**
 * @file IngredientStock.h
 * @brief 재료 재고의 신선도 — 기반 `Inventory` 의 개수 위에 들어온 묶음(남은 날 · 단가)을 얹어, 오래된 것부터 쓰고 기한이 지나면 버립니다.
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

    /** @brief 한 번에 들어온 재료 묶음입니다. */
    struct IngredientBatch
    {
        hashed_string _itemID{};
        int64         _unitCost{ 0 }; ///< 한 개의 원가(결산의 재료비)
        int32         _count{ 0 };
        int32         _daysLeft{ 0 }; ///< 0 이하가 되면 버린다(−1 = 상하지 않는다)
    };
} // namespace sw

namespace sw
{
    /** @brief 기한이 지나 버린 묶음입니다. */
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
     * @brief 인벤토리(빌림)의 재료를 묶음으로 따라갑니다. 쓰기는 기한이 가까운 묶음부터(같으면 먼저 들어온 것) 하고, 인벤토리에서도 같이 뺍니다.
     * @details 묶음이 없는 아이템(손으로 인벤토리에 넣은 것)은 원가 0 · 상하지 않는 것으로 봅니다. 인벤토리 개수가 묶음 합보다 적어지면(밖에서 뺐다)
     *          `consume` 이 모자람을 알아채 묶음을 맞춥니다.
     */
    class SW_GF_API IngredientStock
    {
    public:
        IngredientStock();

        void initialize( Inventory* pInventory );
        /** @brief 인벤토리에 넣고 묶음을 적습니다. 들어간 개수입니다. @p shelfLife 0 이면 상하지 않습니다. */
        int32 addFresh( const hashed_string& itemID, int32 count, int32 shelfLife, int64 unitCost );
        /** @brief 이미 인벤토리에 들어간 것(가게에서 산 것)의 묶음만 적습니다. */
        void recordBatch( const hashed_string& itemID, int32 count, int32 shelfLife, int64 unitCost );
        /** @brief @p count 개가 모두 있으면 오래된 것부터 빼고 원가 합을 @p outCost 에 더합니다. 모자라면 아무것도 빼지 않고 false 입니다. */
        [[nodiscard]] bool consume( const hashed_string& itemID, int32 count, int64& outCost );
        /** @brief 목록의 재료 × @p times 를 다 있으면 모두 뺍니다(다 되거나 아무것도). */
        [[nodiscard]] bool consumeItems( const ItemStackList& items, int32 times, int64& outCost );
        /** @brief 하루를 넘깁니다 — 남은 날을 줄이고 0 이 된 묶음을 인벤토리에서 버립니다. */
        void advanceDay( vector<IngredientSpoilage>& outListSpoilage );

        /** @brief 그 재료의 가장 가까운 남은 날입니다(상하는 묶음이 없으면 −1). */
        int32                          findEarliestExpiry( const hashed_string& itemID ) const;
        int32                          getBatchCount( const hashed_string& itemID ) const;
        const vector<IngredientBatch>& getBatches() const { return _listBatch; }

        /** @brief 묶음(재료 · 원가 · 개수 · 남은 날)을 씁니다. 빌린 창고는 싣지 않습니다(주인이 싣는다). */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 깨졌으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        /** @brief 묶음 합이 인벤토리 개수를 넘으면 오래된 묶음부터 줄입니다(밖에서 뺀 만큼). */
        void reconcile( const hashed_string& itemID );
        void sortBatches();

        vector<IngredientBatch> _listBatch; ///< 남은 날이 적은 것부터(상하지 않는 것은 뒤)
        Inventory*              _pInventory;
    };
} // namespace sw
