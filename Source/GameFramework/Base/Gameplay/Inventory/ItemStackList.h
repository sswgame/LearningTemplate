/**
 * @file ItemStackList.h
 * @brief 아이템 id + 개수의 값 목록 — 레시피 재료 · 전리품 · 퀘스트 보상 · 출하 대기 · 도시 물자가 함께 씁니다.
 * @details 들고 있는 가방(칸 · 무게 · 꾸미기 · 소유)은 `Inventory` 입니다. 이것은 "아이템 몇 개" 라는 값이라 칸이 없습니다.
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

    /**
     * @struct ItemStack
     * @brief 값 목록의 원소 — 아이템 하나와 그 개수입니다(인스턴스 상태 없음 — 가방의 칸 하나는 `InventorySlot`).
     */
    struct ItemStack
    {
        hashed_string _itemId{};
        int32         _count{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ItemStackList
     * @brief 같은 아이템은 한 원소로 합치고, 개수가 0 이 되면 원소를 지웁니다(빈 원소가 쌓이지 않는다). 순서는 처음 더한 순서입니다.
     * @details 칸 · 무게 같은 장르 규칙은 이 위에 얹지 않습니다 — 그것은 `Inventory` · `GridInventory` 입니다. 원소가 몇 개뿐인 목록이라 찾기는 앞에서부터 훑습니다.
     */
    class SW_GF_API ItemStackList
    {
    public:
        ItemStackList();

        /** @brief 더합니다. id 가 비거나 개수가 0 이하면 아무것도 하지 않습니다. */
        void addItem( const hashed_string& itemId, int32 count );
        /** @brief 뺍니다. 모자라면 빼지 않고 false 입니다. */
        [[nodiscard]] bool removeItem( const hashed_string& itemId, int32 count );
        /** @brief 이 목록의 @p count 개를 @p target 으로 옮깁니다. 모자라면 false 입니다. */
        [[nodiscard]] bool moveItemTo( ItemStackList& target, const hashed_string& itemId, int32 count );
        void               clear() { _listStack.clear(); }

        int32 getItemCount( const hashed_string& itemId ) const;
        bool  hasItem( const hashed_string& itemId, int32 count = 1 ) const { return getItemCount( itemId ) >= count; }
        /** @brief 모든 아이템 개수의 합입니다. */
        int32 getTotalCount() const;
        bool  isEmpty() const { return _listStack.empty(); }
        /** @brief 가진 아이템 id 입니다(처음 더한 순서). */
        void                     getItemIds( vector<hashed_string>& outListItem ) const;
        const vector<ItemStack>& getItems() const { return _listStack; }

        /** @brief 아이템을 이름 순으로 씁니다(같은 목록이면 더한 순서와 무관하게 같은 바이트). */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 깨졌으면 false 이고 목록은 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        ItemStack* findStack( const hashed_string& itemId );

        vector<ItemStack> _listStack;
    };
} // namespace sw
