/**
 * @file Inventory.h
 * @brief 슬롯 인벤토리입니다. 겹치기, 무게 한도, 슬롯 옮기기, 나누기, 정렬, 내구도를 다룹니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/GameData/CustomizationValueSet.h"

namespace sw
{
    class Archive;
    class ItemCatalog;
    class ItemStackList;

    /**
     * @brief 슬롯 하나에 담긴 내용입니다.
     * @details 인스턴스 상태(꾸미기 값, 피해, 떨어져 나간 부품)는 그 아이템 하나를 따라다닌다. 세이브와 네트워크도 이 슬롯을 그대로 싣는다.
     *          인스턴스 상태가 있는 아이템은 같은 id 와 겹치지 않는다(`hasInstanceState`).
     */
    struct InventorySlot
    {
        CustomizationValueSet _customization{};    ///< 아이템 인스턴스의 꾸미기 값(염색, 부착물, 변형). 뜻은 외형 스키마가 정한다
        vector<hashed_string> _listDetachedPart{}; ///< 맞아서 떨어져 나간 외형 부품 이름(모자가 날아감, 갑옷 판이 깨짐)
        hashed_string         _itemID{};
        int32                 _count{ 0 };
        float32               _durability{ 0.0f }; ///< 닳는 아이템에만 쓴다(겹치지 않음)
        float32               _damage{ 0.0f };     ///< 맞아서 입은 외형 피해(0..1). 내구도와 별개로 쌓으며, 외형 피해 단계는 둘 중 큰 쪽을 따른다

        bool isEmpty() const { return _count <= 0; }
        /** @brief 겹치면 잃게 되는 인스턴스 상태가 있으면 true 입니다. */
        bool hasInstanceState() const { return _customization.isEmpty() == false || _listDetachedPart.empty() == false || _damage > 0.0f; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class Inventory
     * @brief 정한 수의 슬롯에 아이템을 담습니다. 아이템 정의(겹침, 무게, 내구도)는 카탈로그에서 읽습니다. 카탈로그는 소유하지 않고 참조만 하므로 인벤토리보다 오래 살아 있어야 합니다.
     * @details 넣기는 같은 아이템이 있는 슬롯부터 채우고, 그다음 빈 슬롯으로 넘어간다. 무게 한도(0 이면 없음)를 넘는 만큼은 들어가지 않는다.
     *          빼기는 모두 있을 때만 뺀다. 제작이나 거래가 반쯤 실패한 상태로 남지 않게 하기 위해서다. 상태가 바뀔 때마다 `getRevision` 이 늘어나므로 화면 갱신에 쓴다.
     */
    class SW_GF_API Inventory
    {
    public:
        static constexpr uint32 kStateTag     = FourCCUtil::make( "INVT" );
        static constexpr uint32 kStateVersion = 1;

        Inventory();

        /** @brief 슬롯 수나 최대 무게가 음수이면 오류를 남기고 false 입니다(조용히 0 으로 맞추지 않습니다). */
        [[nodiscard]] bool initialize( const ItemCatalog* pCatalog, int32 slotCount, float32 maxWeight = 0.0f );
        /** @brief 슬롯 수를 바꿉니다(가방 교체). 슬롯이 줄어서 넘치는 것은 @p outListOverflow 로 나오며, 바닥에 떨어뜨리는 데 씁니다. */
        void resize( int32 slotCount, vector<InventorySlot>& outListOverflow );
        /** @brief 최대 무게가 음수이면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool setMaxWeight( float32 maxWeight );

        /** @brief 넣고, 넣은 개수를 반환합니다. 슬롯이나 무게가 모자라면 일부만 넣습니다. */
        int32 addItem( const hashed_string& itemID, int32 count );
        /** @brief 슬롯 하나(내구도 포함)를 통째로 넣습니다. 모두 들어갔으면 true 입니다. */
        [[nodiscard]] bool addStack( const InventorySlot& stack );
        /** @brief @p count 개가 모두 있으면 뺍니다. 뒤쪽 슬롯부터 뺍니다. */
        [[nodiscard]] bool removeItem( const hashed_string& itemID, int32 count );
        /**
         * @brief `removeItem` 과 같이 빼되, 뺀 것을 슬롯 조각(내구도 · 꾸미기 · 뗀 부품째)으로 @p outListTaken 에 덧붙입니다. 모자라면 아무것도 빼지 않고 false 입니다.
         * @details 되돌릴 수 있는 거래(제작 대기열의 취소)는 이 조각을 `addStack` 으로 그대로 돌려준다 — id 로 다시 넣으면 닳은 장비가 새것이 된다(Lyra 는 아이템 인스턴스를 옮긴다).
         */
        [[nodiscard]] bool takeItem( const hashed_string& itemID, int32 count, vector<InventorySlot>& outListTaken );
        /** @brief 슬롯에서 @p count 개까지 빼고 뺀 것을 반환합니다. */
        InventorySlot takeFromSlot( int32 slot, int32 count );
        /** @brief 슬롯을 옮깁니다. 같은 아이템이면 합치고(넘치면 남김), 다른 아이템이면 서로 바꿉니다. */
        [[nodiscard]] bool moveSlot( int32 fromSlot, int32 toSlot );
        /** @brief 슬롯에서 @p count 개를 떼어 빈 슬롯에 둡니다. 둔 슬롯의 번호를 반환하고, 못 하면 -1 입니다. */
        int32 splitSlot( int32 slot, int32 count );
        /** @brief 분류, 희귀도(높은 것 먼저), id 순으로 정렬하고, 겹칠 수 있는 것은 합칩니다. */
        void sortSlots();
        /** @brief 슬롯의 내구도를 깎습니다. 0 이 되어 부서졌으면 true 이고, 그 슬롯은 비워집니다. */
        bool reduceSlotDurability( int32 slot, float32 amount );
        void clear();

        int32 getItemCount( const hashed_string& itemID ) const;
        bool  hasItem( const hashed_string& itemID, int32 count = 1 ) const { return getItemCount( itemID ) >= count; }
        bool  hasItems( const ItemStackList& items ) const;
        /** @brief @p count 개가 모두 들어갈 공간(슬롯과 무게)이 있는지 판정합니다. */
        bool hasRoomFor( const hashed_string& itemID, int32 count ) const;
        /** @brief 그 아이템이 든 첫 슬롯입니다. 없으면 비어 있습니다. */
        optional<int32>      findFirstSlot( const hashed_string& itemID ) const;
        int32                countEmptySlots() const;
        float32              computeWeight() const;
        float32              getMaxWeight() const { return _maxWeight; }
        int32                getSlotCount() const { return static_cast<int32>( _listSlot.size() ); }
        const InventorySlot& getSlot( int32 slot ) const { return _listSlot[static_cast<size_t>( slot )]; }
        /** @brief 아이템별 개수를 값 목록에 추가합니다. 세이브와 거래 화면에 씁니다. */
        void               fillItemStackList( ItemStackList& outItems ) const;
        uint32             getRevision() const { return _revision; }
        const ItemCatalog* getCatalog() const { return _pCatalog; }

        /**
         * @brief 최대 무게와 슬롯별 아이템, 개수, 내구도, 외형 피해, 꾸미기 값, 떨어진 부품을 씁니다. 카탈로그는 `initialize` 가 받은 것이므로 저장하지 않습니다.
         *        `writeState` 가 쓴 바이트로 상태를 복원합니다. 슬롯 수가 지금과 다르거나 데이터가 깨졌으면 false 를 반환하고 상태는 바뀌지 않습니다.
         */
        SW_DECLARE_STATE_IO();
        /** @brief 슬롯 하나의 상태(이름 · 개수 · 내구도 · 피해 · 꾸미기 · 뗀 부품)입니다. 인벤토리와 제작 대기열의 꺼낸 재료가 같은 형식을 쓴다. 읽다 깨지면 io 가 거절 상태다. */
        static void serializeSlot( StateIO& io, InventorySlot& inoutSlot );
        /** @brief `serializeSlot` 하나의 최소 바이트입니다 — 이름(4) + 개수 · 내구도 · 피해(12) + 꾸미기 수 · 부품 수(8). 담은 개수를 읽을 때 남은 바이트로 거르는 데 쓴다. */
        static constexpr uint32 kSlotStateMinBytes = 24;

    private:
        bool    isValidSlot( int32 slot ) const { return slot >= 0 && slot < static_cast<int32>( _listSlot.size() ); }
        int32   computeWeightRoom( const hashed_string& itemID, int32 count ) const;
        float32 getItemWeight( const hashed_string& itemID ) const;

    private:
        vector<InventorySlot> _listSlot;
        const ItemCatalog*    _pCatalog;
        float32               _maxWeight;
        uint32                _revision;
    };
} // namespace sw
