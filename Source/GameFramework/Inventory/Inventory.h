/**
 * @file Inventory.h
 * @brief 칸 인벤토리 — 겹치기 · 무게 한도 · 칸 옮기기 · 나누기 · 정렬 · 내구도입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class ItemBag;
    class ItemCatalog;

    /** @brief 칸 하나에 든 것입니다. */
    struct ItemStack
    {
        hashed_string _itemId{};
        int32         _count{ 0 };
        float32       _durability{ 0.0f }; ///< 닳는 아이템만(겹치지 않는다)

        bool isEmpty() const { return _count <= 0; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class Inventory
     * @brief 정한 수의 칸에 아이템을 담습니다. 아이템 정의(겹침 · 무게 · 내구도)는 카탈로그에서 봅니다(빌려 쓴다 — 인벤토리보다 오래 살아야 한다).
     * @details 넣기는 같은 아이템이 든 칸부터 채우고 빈 칸으로 넘어갑니다. 무게 한도(0 = 없음)를 넘는 만큼은 들어가지 않습니다.
     *          빼기는 모두 있을 때만 뺍니다(제작 · 거래가 반쯤 실패하지 않게). 바뀔 때마다 `getRevision` 이 오릅니다(화면 갱신).
     */
    class SW_GF_API Inventory
    {
    public:
        Inventory();

        void initialize( const ItemCatalog* pCatalog, int32 slotCount, float32 maxWeight = 0.0f );
        /** @brief 칸 수를 바꿉니다(가방 바꾸기). 줄어서 넘치는 것은 @p outListOverflow 로 나옵니다(바닥에 떨어뜨리기). */
        void resize( int32 slotCount, vector<ItemStack>& outListOverflow );
        void setMaxWeight( float32 maxWeight );

        /** @brief 넣고, 넣은 개수를 돌려줍니다(칸 · 무게가 모자라면 일부만). */
        int32 addItem( const hashed_string& itemId, int32 count );
        /** @brief 칸 하나(내구도 포함)를 통째로 넣습니다. 다 들어갔으면 true 입니다. */
        [[nodiscard]] bool addStack( const ItemStack& stack );
        /** @brief @p count 개가 모두 있으면 뺍니다(뒤 칸부터). */
        [[nodiscard]] bool removeItem( const hashed_string& itemId, int32 count );
        /** @brief 칸에서 @p count 개까지 빼고 뺀 것을 돌려줍니다. */
        ItemStack takeFromSlot( int32 slot, int32 count );
        /** @brief 칸을 옮깁니다 — 같은 아이템이면 합치고(넘치면 남김), 아니면 바꿉니다. */
        [[nodiscard]] bool moveSlot( int32 fromSlot, int32 toSlot );
        /** @brief 칸에서 @p count 개를 떼어 빈 칸에 둡니다. 둔 칸 번호입니다(못 하면 −1). */
        int32 splitSlot( int32 slot, int32 count );
        /** @brief 분류 → 희귀도(높은 것 먼저) → id 순으로 정렬하고 겹칠 수 있는 것은 합칩니다. */
        void sortSlots();
        /** @brief 칸의 내구도를 깎습니다. 0 이 되어 부서졌으면 true 입니다(칸은 빈다). */
        bool wearSlot( int32 slot, float32 amount );
        void clear();

        int32 getItemCount( const hashed_string& itemId ) const;
        bool  hasItem( const hashed_string& itemId, int32 count = 1 ) const { return getItemCount( itemId ) >= count; }
        bool  hasItems( const ItemBag& bag ) const;
        /** @brief @p count 개가 다 들어갈 자리(칸 · 무게)가 있는가입니다. */
        bool             hasRoomFor( const hashed_string& itemId, int32 count ) const;
        int32            findFirstSlot( const hashed_string& itemId ) const;
        int32            countEmptySlots() const;
        float32          computeWeight() const;
        float32          getMaxWeight() const { return _maxWeight; }
        int32            getSlotCount() const { return static_cast<int32>( _listSlot.size() ); }
        const ItemStack& getSlot( int32 slot ) const { return _listSlot[static_cast<size_t>( slot )]; }
        /** @brief 아이템마다 개수를 봉투에 더합니다(세이브 · 거래 화면). */
        void               fillItemBag( ItemBag& outBag ) const;
        uint32             getRevision() const { return _revision; }
        const ItemCatalog* getCatalog() const { return _pCatalog; }

    private:
        bool    isValidSlot( int32 slot ) const { return slot >= 0 && slot < static_cast<int32>( _listSlot.size() ); }
        int32   computeWeightRoom( const hashed_string& itemId, int32 count ) const;
        float32 getItemWeight( const hashed_string& itemId ) const;

        vector<ItemStack>  _listSlot;
        const ItemCatalog* _pCatalog;
        float32            _maxWeight;
        uint32             _revision;
    };
} // namespace sw
