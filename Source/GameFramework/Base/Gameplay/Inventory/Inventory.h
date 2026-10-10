/**
 * @file Inventory.h
 * @brief 칸 인벤토리 — 겹치기 · 무게 한도 · 칸 옮기기 · 나누기 · 정렬 · 내구도입니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Data/CustomizationValueSet.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;
    class ItemCatalog;
    class ItemStackList;

    /**
     * @brief 칸 하나에 든 것입니다.
     * @details 인스턴스 상태(꾸미기 값 · 피해 · 떨어져 나간 부품)는 그 아이템 하나를 따라다닙니다 — 세이브 · 네트워크도 이 칸을 그대로 싣습니다.
     *          인스턴스 상태가 있는 것은 같은 id 와 겹치지 않습니다(`hasInstanceState`).
     */
    struct InventorySlot
    {
        CustomizationValueSet _customization{};    ///< 아이템 인스턴스의 꾸미기 값(염색 · 부착물 · 변형 — 외형 스키마가 뜻을 정한다)
        vector<hashed_string> _listDetachedPart{}; ///< 맞아서 떨어져 나간 외형 부품 이름(모자가 날아감 · 갑옷 판이 깨짐)
        hashed_string         _itemID{};
        int32                 _count{ 0 };
        float32               _durability{ 0.0f }; ///< 닳는 아이템만(겹치지 않는다)
        float32               _damage{ 0.0f };     ///< 맞아서 입은 외형 피해 0..1(내구도와 별개로 쌓는다 — 외형 피해 단계는 둘 중 큰 쪽)

        bool isEmpty() const { return _count <= 0; }
        /** @brief 겹치면 잃는 인스턴스 상태가 있는가입니다. */
        bool hasInstanceState() const { return _customization.isEmpty() == false || _listDetachedPart.empty() == false || _damage > 0.0f; }
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
        static constexpr uint32 kStateTag     = FourCcUtil::make( "INVT" );
        static constexpr uint32 kStateVersion = 1;

        Inventory();

        void initialize( const ItemCatalog* pCatalog, int32 slotCount, float32 maxWeight = 0.0f );
        /** @brief 칸 수를 바꿉니다(가방 바꾸기). 줄어서 넘치는 것은 @p outListOverflow 로 나옵니다(바닥에 떨어뜨리기). */
        void resize( int32 slotCount, vector<InventorySlot>& outListOverflow );
        void setMaxWeight( float32 maxWeight );

        /** @brief 넣고, 넣은 개수를 돌려줍니다(칸 · 무게가 모자라면 일부만). */
        int32 addItem( const hashed_string& itemID, int32 count );
        /** @brief 칸 하나(내구도 포함)를 통째로 넣습니다. 다 들어갔으면 true 입니다. */
        [[nodiscard]] bool addStack( const InventorySlot& stack );
        /** @brief @p count 개가 모두 있으면 뺍니다(뒤 칸부터). */
        [[nodiscard]] bool removeItem( const hashed_string& itemID, int32 count );
        /** @brief 칸에서 @p count 개까지 빼고 뺀 것을 돌려줍니다. */
        InventorySlot takeFromSlot( int32 slot, int32 count );
        /** @brief 칸을 옮깁니다 — 같은 아이템이면 합치고(넘치면 남김), 아니면 바꿉니다. */
        [[nodiscard]] bool moveSlot( int32 fromSlot, int32 toSlot );
        /** @brief 칸에서 @p count 개를 떼어 빈 칸에 둡니다. 둔 칸 번호입니다(못 하면 −1). */
        int32 splitSlot( int32 slot, int32 count );
        /** @brief 분류 → 희귀도(높은 것 먼저) → id 순으로 정렬하고 겹칠 수 있는 것은 합칩니다. */
        void sortSlots();
        /** @brief 칸의 내구도를 깎습니다. 0 이 되어 부서졌으면 true 입니다(칸은 빈다). */
        bool wearSlot( int32 slot, float32 amount );
        void clear();

        int32 getItemCount( const hashed_string& itemID ) const;
        bool  hasItem( const hashed_string& itemID, int32 count = 1 ) const { return getItemCount( itemID ) >= count; }
        bool  hasItems( const ItemStackList& items ) const;
        /** @brief @p count 개가 다 들어갈 자리(칸 · 무게)가 있는가입니다. */
        bool                 hasRoomFor( const hashed_string& itemID, int32 count ) const;
        int32                findFirstSlot( const hashed_string& itemID ) const;
        int32                countEmptySlots() const;
        float32              computeWeight() const;
        float32              getMaxWeight() const { return _maxWeight; }
        int32                getSlotCount() const { return static_cast<int32>( _listSlot.size() ); }
        const InventorySlot& getSlot( int32 slot ) const { return _listSlot[static_cast<size_t>( slot )]; }
        /** @brief 아이템마다 개수를 값 목록에 더합니다(세이브 · 거래 화면). */
        void               fillItemStackList( ItemStackList& outItems ) const;
        uint32             getRevision() const { return _revision; }
        const ItemCatalog* getCatalog() const { return _pCatalog; }

        /** @brief 최대 무게와 칸마다 아이템 · 개수 · 내구도 · 외형 피해 · 꾸미기 값 · 떨어진 부품을 씁니다. 카탈로그는 `initialize` 의 것이라 싣지 않습니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 칸 수가 지금과 다르거나 깨졌으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        bool    isValidSlot( int32 slot ) const { return slot >= 0 && slot < static_cast<int32>( _listSlot.size() ); }
        int32   computeWeightRoom( const hashed_string& itemID, int32 count ) const;
        float32 getItemWeight( const hashed_string& itemID ) const;

        vector<InventorySlot> _listSlot;
        const ItemCatalog*    _pCatalog;
        float32               _maxWeight;
        uint32                _revision;
    };
} // namespace sw
