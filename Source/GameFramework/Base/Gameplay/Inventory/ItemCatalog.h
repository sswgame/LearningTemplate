/**
 * @file ItemCatalog.h
 * @brief 아이템 정의 — 분류 · 겹침 수 · 무게 · 값 · 희귀도 · 장비 칸 · 내구도 · 태그 · 능력치 · 사용 효과입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Data/GameCatalog.h"
#include "GameFramework/Base/Foundation/Data/StatBlock.h"
#include "GameFramework/Base/Foundation/Data/XMLCatalog.h"
#include "GameFramework/Base/Gameplay/Inventory/EquipCondition.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /** @brief 아이템 한 종류입니다. 분류 · 장비 칸 · 태그는 문자열 id 라 장르마다 마음대로 정합니다. */
    struct SW_GF_API ItemDef
    {
        hashed_string          _id{};
        hashed_string          _category{};  ///< "Weapon" · "Food" · "Material" … (정렬 · 필터)
        hashed_string          _equipSlot{}; ///< 낄 수 있는 칸 종류("Head" · "Ring" — 비면 장비가 아니다)
        hashed_string          _useEffect{}; ///< 쓰면 적용할 효과 id(어빌리티 카탈로그의 이펙트 — 게임이 잇는다)
        hashed_string          _visualID{};  ///< 아이템 외형 id(`ItemVisualCatalog` — 게임플레이 정의와 따로 둔 외형, 비면 안 보인다)
        string                 _name{};
        vector<hashed_string>  _listTag{};
        StatBlock              _stats{};
        vector<EquipCondition> _listEquipCondition{}; ///< 끼려면 모두 맞아야 하는 조건(`<Requires>`)
        float32                _weight{ 0.0f };
        float32                _maxDurability{ 0.0f }; ///< 0 = 닳지 않는다
        int32                  _maxStack{ 1 };
        int32                  _value{ 0 }; ///< 팔 때 값
        int32                  _rarity{ 0 };
        EquipBreakPolicy       _breakPolicy{ EquipBreakPolicy::UnequipTogether }; ///< 조건이 깨질 때

        bool hasTag( const hashed_string& tag ) const;
        /** @brief 태그 중 하나가 @p parentTag 이거나 그 하위 태그(`Armor.Heavy` 는 `Armor` 아래)인가입니다. */
        bool hasTagUnder( TagID parentTag ) const;
        bool hasEquipConditions() const { return _listEquipCondition.empty() == false; }
        bool isEquipment() const { return _equipSlot.empty() == false; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class ItemCatalog
     * @brief `<ItemCatalog><Item id="sword" category="Weapon" slot="MainHand" maxStack="1" weight="3" value="40" rarity="1" durability="100"
     *        tags="Metal,Blade" useEffect="" visual="sword_iron" breakPolicy="KeepHidden"><Stats attack="12"/><Requires set="KnightSet" pieces="2"/></Item></ItemCatalog>`
     *        를 읽습니다.
     * @details `<Requires>` 하나는 속성 하나로 조건 하나입니다 — `set="S"`(세트 완성) · `set="S" pieces="n"` · `equippedTag="T"` · `characterTag="T"` ·
     *          `bodyShape="A,B"`. 세트 이름이 실제로 있는지와 조건 순환은 세트를 아는 쪽(`AppearanceDatabase`)이 검사합니다.
     */
    class SW_GF_API ItemCatalog : public XMLCatalog<ItemCatalog>
    {
        friend class XMLCatalog<ItemCatalog>;

    public:
        void addItem( const ItemDef& def ) { (void)_catalog.add( def ); }

        const ItemDef*         findItem( const hashed_string& id ) const { return _catalog.find( id ); }
        const vector<ItemDef>& getItems() const { return _catalog.getAll(); }
        /** @brief 겹침 수입니다. 모르는 아이템은 1 입니다. */
        int32 getMaxStack( const hashed_string& id ) const;

    private:
        static constexpr const utf8* kXMLRootName = "ItemCatalog"; ///< 루트 원소(`XMLCatalog`)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );

        GameCatalog<ItemDef> _catalog{};
    };
} // namespace sw
