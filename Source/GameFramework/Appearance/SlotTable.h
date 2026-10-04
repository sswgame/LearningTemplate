/**
 * @file SlotTable.h
 * @brief 슬롯 표 — 장비 칸 이름 · 받는 종류와, 아이템이 차지하는 칸 묶음(로브는 상의 + 하의, 양손 무기는 두 손)입니다.
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
    class XmlNode;

    /** @brief 칸 하나입니다. 표의 순서가 해석 순서입니다. */
    struct AppearanceSlotDef
    {
        hashed_string _name{};
        hashed_string _accept{}; ///< 받는 `ItemDef::_equipSlot` 종류 — 비면 칸 이름과 같다
    };
} // namespace sw

namespace sw
{
    /** @brief 칸 묶음 — 이 점유를 가진 외형은 자기 칸 말고 나머지 칸도 차지해 그 칸의 장비를 가립니다. */
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
     * @brief `<SlotTable><Slot name="Head"/><Slot name="MainHand" accept="Weapon"/><Occupancy id="TwoHanded" slots="MainHand,OffHand"/></SlotTable>` 입니다.
     * @details `makeEquipmentLayout` 이 `Equipment::initialize` 의 칸 문자열을 만들어 게임플레이 칸과 외형 칸이 한 표에서 나옵니다.
     */
    class SW_GF_API SlotTable
    {
    public:
        [[nodiscard]] bool loadFromNode( const XmlNode& root, AppearanceLoadReport& report, string_view sourceName );
        void               clear();

        int32                            findSlotIndex( const hashed_string& slot ) const;
        bool                             hasSlot( const hashed_string& slot ) const { return findSlotIndex( slot ) >= 0; }
        const SlotOccupancyDef*          findOccupancy( const hashed_string& id ) const;
        const vector<AppearanceSlotDef>& getSlots() const { return _listSlot; }
        /** @brief `"Head,Body,MainHand:Weapon"` — `Equipment::initialize` 에 그대로 넘깁니다. */
        string makeEquipmentLayout() const;

    private:
        vector<AppearanceSlotDef> _listSlot{};
        vector<SlotOccupancyDef>  _listOccupancy{};
    };
} // namespace sw
