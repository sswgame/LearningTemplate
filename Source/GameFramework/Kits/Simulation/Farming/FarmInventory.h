/**
 * @file FarmInventory.h
 * @brief 아이템 · 돈 · 출하함입니다 — 출하함에 넣은 것은 하루가 끝날 때 팔립니다(하베스트 문의 출하 상자).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Inventory/ItemBag.h"

namespace sw
{
    class Archive;
    class CropCatalog;

    /**
     * @class FarmInventory
     * @brief 이름 → 개수의 아이템 가방, 돈, 출하함입니다. 아이템 종류를 코드가 정하지 않습니다(이름이 곧 아이템).
     * @details 가방 · 출하함은 기반의 `ItemBag` 이고, 여기는 돈과 "출하함은 밤에 팔린다" 는 농장 규칙만 얹습니다.
     */
    class SW_GF_API FarmInventory
    {
    public:
        FarmInventory();

        void addItem( const hashed_string& itemId, int32 count );
        /** @brief 개수만큼 뺍니다. 모자라면 아무것도 빼지 않고 false 입니다. */
        [[nodiscard]] bool removeItem( const hashed_string& itemId, int32 count );
        int32              getItemCount( const hashed_string& itemId ) const;
        /** @brief 가진 아이템 이름을 채웁니다(개수 1 이상, 순서 없음). */
        void getItemIds( vector<hashed_string>& outListItem ) const;

        int32 getGold() const { return _gold; }
        void  addGold( int32 amount );
        /** @brief 돈을 씁니다. 모자라면 쓰지 않고 false 입니다. */
        [[nodiscard]] bool spendGold( int32 amount );
        /** @brief 가게에서 씨앗 · 아이템을 삽니다(값 × 개수). 돈이 모자라면 false 입니다. */
        [[nodiscard]] bool buyItem( const hashed_string& itemId, int32 count, int32 unitPrice );

        /** @brief 가방의 아이템을 출하함으로 옮깁니다. 모자라면 false 입니다. */
        [[nodiscard]] bool shipItem( const hashed_string& itemId, int32 count );
        /** @brief 출하함에 든 아이템 수입니다. */
        int32 getShippedItemCount() const;
        /**
         * @brief 하루 끝 정산 — 출하함을 카탈로그 값으로 팔아 돈에 더하고 비웁니다. 번 돈을 돌려줍니다(값을 모르는 아이템은 0 으로 팔린다).
         */
        int32 settleShipping( const CropCatalog& catalog );

        /** @brief 가방입니다(아이템 개수 — 장르 공통 `ItemBag`). */
        const ItemBag& getBag() const { return _bag; }

        /** @brief 가방 · 출하함 · 돈을 씁니다(핫 리로드 · 세이브). */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 깨졌으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        ItemBag _bag;
        ItemBag _shippingBin; ///< 하루 끝에 팔린다
        int32   _gold;
    };
} // namespace sw
