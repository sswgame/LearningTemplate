/**
 * @file SlotTable.h
 * @brief 슬롯 테이블입니다. 장비 슬롯의 이름과 받는 종류, 아이템이 차지하는 슬롯 그룹(로브는 상의와 하의, 양손 무기는 두 손)을 담습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class AppearanceLoadReport;
    class XMLNode;

    /** @brief 슬롯 하나입니다. 테이블의 순서가 해석 순서입니다. */
    struct AppearanceSlotDef
    {
        hashed_string _name{};
        hashed_string _accept{}; ///< 받는 `ItemDef::_equipSlot` 종류. 비어 있으면 슬롯 이름과 같다
    };
} // namespace sw

namespace sw
{
    /** @brief 슬롯 점유 그룹입니다. 이 점유를 가진 외형은 자기 슬롯 외에 나머지 슬롯도 차지하고, 그 슬롯의 장비를 가립니다. */
    struct SlotOccupancyDef
    {
        vector<hashed_string> _listSlot{};
        hashed_string         _id{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class SlotTable
     * @brief `<SlotTable><Slot name="Head"/><Slot name="MainHand" accept="Weapon"/><Occupancy id="TwoHanded" slots="MainHand,OffHand"/></SlotTable>` 형식으로 읽습니다.
     * @details `makeEquipmentLayout` 이 `Equipment::initialize` 에 넘길 슬롯 문자열을 만든다. 그래서 게임플레이 슬롯과 외형 슬롯이 한 테이블에서 나온다.
     */
    class SW_GF_API SlotTable
    {
    public:
        [[nodiscard]] bool loadFromNode( const XMLNode& root, AppearanceLoadReport& report, string_view sourceName );
        void               clear();

        int32                            findSlotIndex( const hashed_string& slot ) const;
        bool                             hasSlot( const hashed_string& slot ) const { return findSlotIndex( slot ) >= 0; }
        const SlotOccupancyDef*          findOccupancy( const hashed_string& id ) const;
        const vector<AppearanceSlotDef>& getSlots() const { return _listSlot; }
        /** @brief `"Head,Body,MainHand:Weapon"` 형식의 문자열입니다. `Equipment::initialize` 에 그대로 넘깁니다. */
        string makeEquipmentLayout() const;

    private:
        vector<AppearanceSlotDef> _listSlot{};
        vector<SlotOccupancyDef>  _listOccupancy{};
    };
} // namespace sw
