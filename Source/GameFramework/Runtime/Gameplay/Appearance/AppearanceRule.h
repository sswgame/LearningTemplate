/**
 * @file AppearanceRule.h
 * @brief 외형 규칙 테이블입니다. 조건(입힌 장비나 캐릭터의 태그 질의, 슬롯 점유, 체형, 몸 종류)을 만족하면 동작(숨기기, 변형 고르기, 메시나 머티리얼 바꾸기, 모프, 소켓 덮어쓰기)을 적용합니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/TagID.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceTypes.h"

namespace sw
{
    class XMLNode;

    /** @brief 규칙 조건의 종류입니다. */
    enum class AppearanceRuleConditionKind : uint8
    {
        TargetTag = 0, ///< `target` 슬롯(주인)의 보이는 외형이 태그(하위 태그 포함)를 가지고 있다
        AnyTag,        ///< 보이는 외형 중 하나가 태그를 가지고 있다
        CharacterTag,  ///< 캐릭터가 태그를 가지고 있다
        Occupied,      ///< 슬롯에 보이는 장비가 있다(점유 처리 뒤)
        BodyShape,     ///< 체형이 목록에 있는 것 중 하나다
        BodyType       ///< 몸 종류가 목록에 있는 것 중 하나다
    };

    /** @brief 규칙 동작의 종류입니다. */
    ENUM()
    enum class AppearanceRuleActionKind : uint8
    {
        HideTarget = 0, ///< 슬롯(주인)의 부품을 모두 숨긴다. `_part` 가 있으면 그 부품 하나만(두건이 머리카락만 감추고 눈썹은 남긴다)
        HideItemTag,    ///< 태그를 가진 외형을 숨긴다
        HideRegion,     ///< 몸 영역을 숨긴다
        ChooseVariant,  ///< 주인의 메시 변형 이름(주인이 비어 있으면 몸)
        SwapMesh,       ///< 주인 부품의 메시를 바꾼다
        SwapMaterial,   ///< 주인 부품의 머티리얼을 바꾼다
        ApplyMorph,     ///< 주인(비어 있으면 몸)에 모프를 적용한다
        OverrideSocket  ///< 소켓 하나를 옮기거나 새로 만든다
    };

    /** @brief 조건 하나입니다. */
    struct SW_GF_API AppearanceRuleCondition
    {
        vector<hashed_string>       _listName{}; ///< BodyShape, BodyType 에서 쓴다
        hashed_string               _target{};   ///< TargetTag, Occupied 에서 쓴다
        TagID                       _tag{};
        AppearanceRuleConditionKind _kind{ AppearanceRuleConditionKind::AnyTag };
        uint8                       _bNegate{ SW_FALSE };

        /** @brief 같은 질문이면 true 입니다. 부정 여부는 비교하지 않으며, 서로 배타적인 규칙 짝을 찾는 데 씁니다. */
        bool isSameQuestion( const AppearanceRuleCondition& other ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 동작 하나입니다. */
    struct SW_GF_API AppearanceRuleAction
    {
        AppearancePlacement      _placement{};    ///< OverrideSocket
        hashed_string            _target{};       ///< 주인(슬롯 또는 꾸미기 매개변수). 비어 있으면 몸
        hashed_string            _part{};         ///< SwapMesh, SwapMaterial, HideTarget 에서 쓴다(비어 있으면 주인의 모든 부품)
        hashed_string            _name{};         ///< 변형, 모프, 영역, 소켓 이름
        hashed_string            _value{};        ///< 메시나 머티리얼 경로
        TagID                    _tag{};          ///< HideItemTag
        float32                  _weight{ 1.0f }; ///< ApplyMorph — `weight` 를 적지 않으면 모프를 다 건다
        AppearanceRuleActionKind _kind{ AppearanceRuleActionKind::HideTarget };

        /** @brief 숨기기는 여러 번 겹쳐도 결과가 같으므로 충돌하지 않습니다. */
        bool isHide() const;
        /** @brief 같은 대상을 바꾸면 true 입니다. 충돌 판정의 키로 씁니다. */
        bool hasSameTarget( const AppearanceRuleAction& other ) const;
        /** @brief 같은 값으로 바꾸면 true 입니다. */
        bool hasSameValue( const AppearanceRuleAction& other ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 규칙 하나입니다. 조건이 모두 맞으면 동작을 모두 적용합니다. */
    REFLECT()
    struct SW_GF_API AppearanceRuleDef
    {
        REFLECT_BODY();

        vector<AppearanceRuleCondition> _listCondition{};
        vector<AppearanceRuleAction>    _listAction{};
        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY()
        int32 _priority{ 0 };
    };

    /**
     * @brief 규칙 조건(`<When …/>`)의 칸입니다. `tag` · `characterTag` · `occupied` · `bodyShape` · `bodyType` 중 정확히 하나를 적는다(읽기가 본다).
     * @details 몸 모양 · 몸 종류는 이름 목록이라 손으로 푼다. `target` 은 `tag` 와만 쓴다.
     */
    REFLECT()
    struct SW_GF_API AppearanceRuleWhenFields
    {
        REFLECT_BODY();

        PROPERTY( Name = "not" )
        bool _bNot{ false };
        PROPERTY()
        hashed_string _target{};
        PROPERTY()
        string _tag{};
        PROPERTY()
        string _characterTag{};
        PROPERTY()
        hashed_string _occupied{};
    };

    /**
     * @brief 규칙 동작(`<Hide>` · `<Variant>` · `<SwapMesh>` · `<SwapMaterial>` · `<Morph>` · `<OverrideSocket>`)이 함께 받는 칸입니다.
     * @details 동작마다 받는 칸이 다르다 — 그 동작이 받지 않는 칸을 적으면 읽기가 오류로 낸다(`AppearanceRuleInternal::kArrActionField`). 배치(`parent` · `offset` · `rotation`)는 손으로 푼다.
     */
    REFLECT()
    struct SW_GF_API AppearanceRuleActionFields
    {
        REFLECT_BODY();

        PROPERTY()
        hashed_string _target{};
        PROPERTY()
        hashed_string _part{};
        PROPERTY()
        string _itemTag{};
        PROPERTY()
        hashed_string _region{};
        PROPERTY()
        hashed_string _name{};
        PROPERTY()
        hashed_string _mesh{};
        PROPERTY()
        hashed_string _material{};
        PROPERTY()
        float32 _weight{ 1.0f }; ///< Morph — 적지 않으면 모프를 다 건다
    };
} // namespace sw

namespace sw
{
    /**
     * @class AppearanceRuleTable
     * @brief `<AppearanceRuleTable><Rule id="FullHelmetHidesHair" priority="10"><When target="Head" tag="Helmet.FullFace"/><Hide target="Hair"/></Rule></AppearanceRuleTable>` 형식으로 읽습니다.
     * @details 같은 대상을 다르게 바꾸는 두 규칙은 우선순위가 높은 쪽이 이긴다. **우선순위가 같으면 로드 오류**이다.
     *          다만 둘이 함께 맞을 수 없음이 글에서 바로 드러나는 경우는 허용한다. 한쪽이 `<When … not="true"/>` 로 같은 질문을 뒤집는 경우가 그렇다.
     */
    class SW_GF_API AppearanceRuleTable
    {
    public:
        [[nodiscard]] bool loadFromNode( const XMLNode& root, AppearanceLoadReport& report, string_view sourceName );
        void               clear() { _listRule.clear(); }

        const vector<AppearanceRuleDef>& getRules() const { return _listRule; }
        const AppearanceRuleDef*         findRule( const hashed_string& id ) const;
        /** @brief 두 규칙이 함께 맞을 수 없음이 글에서 바로 드러나면 true 입니다. */
        static bool areExclusive( const AppearanceRuleDef& lhs, const AppearanceRuleDef& rhs );

    private:
        void reportConflicts( AppearanceLoadReport& report, string_view sourceName ) const;

    private:
        vector<AppearanceRuleDef> _listRule{};
    };
} // namespace sw
