/**
 * @file AppearanceRule.h
 * @brief 외형 규칙 표 — 조건(입힌 장비 · 캐릭터의 태그 질의, 칸 점유, 체형, 몸 종류) → 동작(숨기기 · 변형 고르기 · 메시 · 머티리얼 바꾸기 · 모프 · 소켓 덮어쓰기)입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/TagID.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Appearance/AppearanceTypes.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XmlNode;

    /** @brief 규칙 조건의 종류입니다. */
    enum class AppearanceRuleConditionKind : uint8
    {
        TargetTag = 0, ///< `target` 칸(주인)의 보이는 외형이 태그(하위 포함)를 가졌다
        AnyTag,        ///< 보이는 외형 중 하나가 태그를 가졌다
        CharacterTag,  ///< 캐릭터가 태그를 가졌다
        Occupied,      ///< 칸에 보이는 장비가 있다(점유 뒤)
        BodyShape,     ///< 체형이 목록의 하나다
        BodyType       ///< 몸 종류가 목록의 하나다
    };

    /** @brief 규칙 동작의 종류입니다. */
    enum class AppearanceRuleActionKind : uint8
    {
        HideTarget = 0, ///< 칸(주인)의 부품을 모두 숨긴다
        HideItemTag,    ///< 태그를 가진 외형을 숨긴다
        HideRegion,     ///< 몸 영역을 숨긴다
        ChooseVariant,  ///< 주인의 메시 변형 이름(비운 주인 = 몸)
        SwapMesh,       ///< 주인 부품의 메시를 바꾼다
        SwapMaterial,   ///< 주인 부품의 머티리얼을 바꾼다
        ApplyMorph,     ///< 주인(비면 몸)에 모프를 건다
        OverrideSocket  ///< 소켓 하나를 옮기거나 새로 낸다
    };

    SW_GF_API const utf8* toString( AppearanceRuleActionKind kind );

    /** @brief 조건 하나입니다. */
    struct SW_GF_API AppearanceRuleCondition
    {
        vector<hashed_string>       _listName{}; ///< BodyShape · BodyType
        hashed_string               _target{};   ///< TargetTag · Occupied
        TagID                       _tag{};
        AppearanceRuleConditionKind _kind{ AppearanceRuleConditionKind::AnyTag };
        uint8                       _bNegate{ SW_FALSE };

        /** @brief 같은 질문인가입니다(부정 여부는 빼고) — 서로 배타인 규칙 짝을 알아본다. */
        bool isSameQuestion( const AppearanceRuleCondition& other ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 동작 하나입니다. */
    struct SW_GF_API AppearanceRuleAction
    {
        AppearancePlacement      _placement{}; ///< OverrideSocket
        hashed_string            _target{};    ///< 주인(칸 · 꾸미기 매개변수) — 비면 몸
        hashed_string            _part{};      ///< SwapMesh · SwapMaterial(비면 주인의 모든 부품)
        hashed_string            _name{};      ///< 변형 · 모프 · 영역 · 소켓 이름
        hashed_string            _value{};     ///< 메시 · 머티리얼 경로
        TagID                    _tag{};       ///< HideItemTag
        float32                  _weight{ 0.0f };
        AppearanceRuleActionKind _kind{ AppearanceRuleActionKind::HideTarget };

        /** @brief 숨기기는 겹쳐도 같은 결과라 충돌하지 않습니다. */
        bool isHide() const;
        /** @brief 같은 대상을 바꾸는가입니다(충돌 판정의 키). */
        bool hasSameTarget( const AppearanceRuleAction& other ) const;
        /** @brief 같은 값으로 바꾸는가입니다. */
        bool hasSameValue( const AppearanceRuleAction& other ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 규칙 하나 — 조건이 모두 맞으면 동작을 모두 겁니다. */
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
     * @brief `<AppearanceRuleTable><Rule id="FullHelmetHidesHair" priority="10"><When target="Head" tag="Helmet.FullFace"/><Hide target="Hair"/></Rule></AppearanceRuleTable>` 입니다.
     * @details 같은 대상을 다르게 바꾸는 두 규칙은 우선순위가 높은 쪽이 이깁니다. **우선순위가 같으면 로드 오류**입니다 — 둘이 함께 맞을 수 없다고
     *          글에서 바로 보이는 경우(한쪽이 `<When … not="true"/>` 로 같은 질문을 뒤집음)만 허락합니다.
     */
    class SW_GF_API AppearanceRuleTable
    {
    public:
        [[nodiscard]] bool loadFromNode( const XmlNode& root, AppearanceLoadReport& report, string_view sourceName );
        void               clear() { _listRule.clear(); }

        const vector<AppearanceRuleDef>& getRules() const { return _listRule; }
        const AppearanceRuleDef*         findRule( const hashed_string& id ) const;
        /** @brief 두 규칙이 함께 맞을 수 없다고 글에서 보이는가입니다. */
        static bool areExclusive( const AppearanceRuleDef& lhs, const AppearanceRuleDef& rhs );

    private:
        void reportConflicts( AppearanceLoadReport& report, string_view sourceName ) const;

        vector<AppearanceRuleDef> _listRule{};
    };
} // namespace sw
