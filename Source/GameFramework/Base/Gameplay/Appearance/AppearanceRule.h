/**
 * @file AppearanceRule.h
 * @brief 외형 규칙 테이블입니다. 조건(입힌 장비나 캐릭터의 태그 질의, 슬롯 점유, 체형, 몸 종류)을 만족하면 동작(숨기기, 변형 고르기, 메시나 머티리얼 바꾸기, 모프, 소켓 덮어쓰기)을 적용합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/TagID.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceTypes.h"
#include "GameFramework/GameFrameworkExports.h"

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
    enum class AppearanceRuleActionKind : uint8
    {
        HideTarget = 0, ///< 슬롯(주인)의 부품을 모두 숨긴다
        HideItemTag,    ///< 태그를 가진 외형을 숨긴다
        HideRegion,     ///< 몸 영역을 숨긴다
        ChooseVariant,  ///< 주인의 메시 변형 이름(주인이 비어 있으면 몸)
        SwapMesh,       ///< 주인 부품의 메시를 바꾼다
        SwapMaterial,   ///< 주인 부품의 머티리얼을 바꾼다
        ApplyMorph,     ///< 주인(비어 있으면 몸)에 모프를 적용한다
        OverrideSocket  ///< 소켓 하나를 옮기거나 새로 만든다
    };

    SW_GF_API const utf8* toString( AppearanceRuleActionKind kind );

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
        AppearancePlacement      _placement{}; ///< OverrideSocket
        hashed_string            _target{};    ///< 주인(슬롯 또는 꾸미기 매개변수). 비어 있으면 몸
        hashed_string            _part{};      ///< SwapMesh, SwapMaterial 에서 쓴다(비어 있으면 주인의 모든 부품)
        hashed_string            _name{};      ///< 변형, 모프, 영역, 소켓 이름
        hashed_string            _value{};     ///< 메시나 머티리얼 경로
        TagID                    _tag{};       ///< HideItemTag
        float32                  _weight{ 0.0f };
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
    struct AppearanceRuleDef
    {
        vector<AppearanceRuleCondition> _listCondition{};
        vector<AppearanceRuleAction>    _listAction{};
        hashed_string                   _id{};
        int32                           _priority{ 0 };
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

        vector<AppearanceRuleDef> _listRule{};
    };
} // namespace sw
