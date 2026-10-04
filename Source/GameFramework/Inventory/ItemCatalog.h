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

#include "GameFramework/Data/GameCatalog.h"
#include "GameFramework/Data/StatBlock.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XmlNode;

    /** @brief 아이템 한 종류입니다. 분류 · 장비 칸 · 태그는 문자열 id 라 장르마다 마음대로 정합니다. */
    struct SW_GF_API ItemDef
    {
        hashed_string         _id{};
        hashed_string         _category{};  ///< "Weapon" · "Food" · "Material" … (정렬 · 필터)
        hashed_string         _equipSlot{}; ///< 낄 수 있는 칸 종류("Head" · "Ring" — 비면 장비가 아니다)
        hashed_string         _useEffect{}; ///< 쓰면 적용할 효과 id(어빌리티 카탈로그의 이펙트 — 게임이 잇는다)
        string                _name{};
        vector<hashed_string> _listTag{};
        StatBlock             _stats{};
        float32               _weight{ 0.0f };
        float32               _maxDurability{ 0.0f }; ///< 0 = 닳지 않는다
        int32                 _maxStack{ 1 };
        int32                 _value{ 0 }; ///< 팔 때 값
        int32                 _rarity{ 0 };

        bool hasTag( const hashed_string& tag ) const;
        bool isEquipment() const { return _equipSlot.empty() == false; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class ItemCatalog
     * @brief `<ItemCatalog><Item id="sword" category="Weapon" slot="MainHand" maxStack="1" weight="3" value="40" rarity="1" durability="100"
     *        tags="Metal,Blade" useEffect=""><Stats attack="12"/></Item></ItemCatalog>` 를 읽습니다.
     */
    class SW_GF_API ItemCatalog
    {
    public:
        [[nodiscard]] bool loadFromResource( string_view path );
        [[nodiscard]] bool loadFromXmlText( string_view xmlText, string_view sourceName = {} );
        void               addItem( const ItemDef& def ) { (void)_catalog.add( def ); }

        const ItemDef*         findItem( const hashed_string& id ) const { return _catalog.find( id ); }
        const vector<ItemDef>& getItems() const { return _catalog.getAll(); }
        /** @brief 겹침 수입니다. 모르는 아이템은 1 입니다. */
        int32 getMaxStack( const hashed_string& id ) const;

    private:
        uint32 loadRoot( const XmlNode& root, string_view sourceName );

        GameCatalog<ItemDef> _catalog{};
    };
} // namespace sw
