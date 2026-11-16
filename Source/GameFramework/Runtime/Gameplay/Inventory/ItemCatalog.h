/**
 * @file ItemCatalog.h
 * @brief 아이템 정의입니다. 분류, 겹침 수, 무게, 값, 희귀도, 장비 슬롯, 내구도, 태그, 능력치, 사용 효과를 담습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/GameData/GameCatalog.h"
#include "GameFramework/Runtime/Foundation/GameData/StatBlock.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalog.h"
#include "GameFramework/Runtime/Gameplay/Inventory/EquipmentCondition.h"

namespace sw
{
    class XMLNode;

    /** @brief 아이템 한 종류입니다. 분류, 장비 슬롯, 태그는 문자열 id 이므로 장르마다 자유롭게 정합니다. */
    /** @brief 장착 조건(`<Requires …/>`)의 칸입니다. `set` · `equippedTag` · `characterTag` · `bodyShape` 중 정확히 하나를 적는다(읽기가 본다). 몸 모양은 이름 목록이라 손으로 푼다. */
    REFLECT()
    struct SW_GF_API EquipmentRequiresParams
    {
        REFLECT_BODY();

        PROPERTY()
        hashed_string _set{};
        PROPERTY( Min = 0 )
        int32 _pieces{ 0 }; ///< 적지 않으면 세트를 다 갖춘 조건이다
        PROPERTY()
        string _equippedTag{};
        PROPERTY()
        string _characterTag{};
    };

    REFLECT()
    struct SW_GF_API ItemDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY()
        hashed_string _category{}; ///< "Weapon", "Food", "Material" 등. 정렬과 필터에 쓴다
        PROPERTY( Name = "slot" )
        hashed_string _equipSlot{}; ///< 낄 수 있는 슬롯 종류("Head", "Ring"). 비어 있으면 장비가 아니다
        PROPERTY()
        hashed_string _useEffect{}; ///< 쓰면 적용할 효과 id(어빌리티 카탈로그의 이펙트). 게임이 연결한다
        PROPERTY( Name = "visual" )
        hashed_string _visualID{}; ///< 아이템 외형 id(`ItemVisualCatalog`). 게임플레이 정의와 따로 둔 외형이며, 비어 있으면 보이지 않는다
        PROPERTY()
        string _name{};
        PROPERTY( Name = "tags" )
        vector<hashed_string>      _listTag{};
        StatBlock                  _stats{};
        vector<EquipmentCondition> _listEquipCondition{}; ///< 끼려면 모두 맞아야 하는 조건(`<Requires>`)
        PROPERTY( Min = 0.0 )
        float32 _weight{ 0.0f };
        PROPERTY( Name = "durability", Min = 0.0 )
        float32 _maxDurability{ 0.0f }; ///< 0 이면 닳지 않는다
        PROPERTY( Min = 1 )
        int32 _maxStack{ 1 };
        PROPERTY( Min = 0 )
        int32 _value{ 0 }; ///< 팔 때 값
        PROPERTY()
        int32 _rarity{ 0 };
        PROPERTY()
        EquipBreakPolicy _breakPolicy{ EquipBreakPolicy::UnequipTogether }; ///< 조건이 깨질 때

        bool hasTag( const hashed_string& tag ) const;
        /** @brief 태그 중 하나가 @p parentTag 이거나 그 하위 태그이면 true 입니다(`Armor.Heavy` 는 `Armor` 의 하위). */
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
     * @details `<Requires>` 하나는 속성 하나이고 조건 하나이다. 속성은 `set="S"`(세트 완성), `set="S" pieces="n"`, `equippedTag="T"`, `characterTag="T"`, `bodyShape="A,B"` 중 하나이다.
     *          세트 이름이 실제로 있는지와 조건 순환은 세트를 아는 쪽(`AppearanceDatabase`)이 검사한다.
     */
    class SW_GF_API ItemCatalog : public GameCatalog<ItemDef, XMLCatalog<ItemCatalog>>
    {
        friend class XMLCatalog<ItemCatalog>;

    public:
        /** @brief 데이터 종류 이름입니다(`SW_ASSET_TYPE` 등록 줄과 팩 경로 `GamePackDataUtil::tryMakeKindPath` 가 이것을 쓴다). */
        static constexpr const utf8* kDataKind = "items";

        /** @brief 겹침 수입니다. 알 수 없는 아이템은 1 입니다. */
        int32 getMaxStack( const hashed_string& id ) const;

    private:
        static constexpr const utf8* kXMLRootName = "ItemCatalog"; ///< 루트 원소(`XMLCatalog`)
        uint32                       loadRoot( const XMLCatalogRoot& root, string_view sourceName );
        [[nodiscard]] bool           loadItem( const XMLNode& node, string_view sourceName );
    };

    /** @brief 공유 캐시는 이 카탈로그의 .cpp 가 인스턴스화합니다. 쓰는 쪽은 사본을 읽지 않고 `SharedAssetCache<ItemCatalog>::acquire` 로 한 벌을 받습니다. */
    SW_DECLARE_SHARED_ASSET_CACHE( SW_GF_API_EXTERN_TEMPLATE, ItemCatalog );
} // namespace sw
