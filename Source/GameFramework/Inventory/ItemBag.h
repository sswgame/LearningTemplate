/**
 * @file ItemBag.h
 * @brief 아이템 id → 개수 — 농장 인벤토리 · 출하함 · 전리품 · 상점 재고가 함께 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;

    /**
     * @class ItemBag
     * @brief 개수가 0 이 되면 항목을 지웁니다(빈 칸이 쌓이지 않는다). 슬롯 · 무게 같은 장르 규칙은 이 위에 얹습니다(핫바 · 칸 인벤토리).
     */
    class SW_GF_API ItemBag
    {
    public:
        ItemBag();

        /** @brief 더합니다. id 가 비거나 개수가 0 이하면 아무것도 하지 않습니다. */
        void addItem( const hashed_string& itemId, int32 count );
        /** @brief 뺍니다. 모자라면 빼지 않고 false 입니다. */
        [[nodiscard]] bool removeItem( const hashed_string& itemId, int32 count );
        /** @brief 이 봉투의 @p count 개를 @p target 으로 옮깁니다. 모자라면 false 입니다. */
        [[nodiscard]] bool moveItemTo( ItemBag& target, const hashed_string& itemId, int32 count );
        void               clear() { _mapItem.clear(); }

        int32 getItemCount( const hashed_string& itemId ) const;
        bool  hasItem( const hashed_string& itemId, int32 count = 1 ) const { return getItemCount( itemId ) >= count; }
        /** @brief 모든 아이템 개수의 합입니다. */
        int32 getTotalCount() const;
        bool  isEmpty() const { return _mapItem.empty(); }
        /** @brief 가진 아이템 id 입니다(순서는 정해지지 않는다 — 보일 때는 호출부가 정렬한다). */
        void                                       getItemIds( vector<hashed_string>& outListItem ) const;
        const unordered_map<hashed_string, int32>& getItems() const { return _mapItem; }

        /** @brief 아이템을 이름 순으로 씁니다(같은 가방이면 같은 바이트). */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 깨졌으면 false 이고 가방은 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        unordered_map<hashed_string, int32> _mapItem;
    };
} // namespace sw
