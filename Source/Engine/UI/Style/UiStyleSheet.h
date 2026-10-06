/**
 * @file UiStyleSheet.h
 * @brief 스타일 시트(`*.uistyle.xml`) — 선택자 · 특정도 · 규칙 · 변수입니다(유니티 USS 의 자리, 형식은 엔진 XML).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/UI/Style/WidgetStyle.h"

namespace sw
{
    struct PropertyInfo;

    class Widget;

    /**
     * @struct UiStyleSelectorPart
     * @brief 복합 선택자 하나 — `타입? .클래스* #이름? :상태*`(빈 칸은 그 조건 없음)입니다.
     */
    struct UiStyleSelectorPart
    {
        hashed_string         _typeName{};     ///< 정확한 위젯 타입 이름(파생은 맞지 않는다 — USS 와 같다)
        hashed_string         _name{};         ///< `#이름`
        vector<hashed_string> _listClass{};    ///< `.클래스` 들(모두 있어야 맞는다)
        uint32                _stateMask{ 0 }; ///< `:상태` 들(`UiStyleState` 비트 — 모두 켜져야 맞는다)
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UiStyleSelector
     * @brief 복합 선택자들을 빈 칸(자손 결합자 — 아무 조상)으로 이은 것입니다. `>` · `+` · `*` · `[속성]` 은 쓰지 않습니다(읽으면 오류).
     * @details 특정도 = (#이름 수, .클래스 + :상태 수, 타입 수) 사전 순 — 한 자리에 8 비트씩 담습니다.
     */
    struct SW_API UiStyleSelector
    {
        vector<UiStyleSelectorPart> _listPart{}; ///< 왼쪽(먼 조상) → 오른쪽(대상)
        uint32                      _specificity{ 0 };

        /** @brief 선택자 글을 읽습니다. 모르는 문법 · 상태 이름이면 false 와 @p outError 입니다. */
        [[nodiscard]] static bool parse( string_view text, UiStyleSelector& outSelector, string& outError );
        /** @brief 위젯 @p widget 의 지금 상태가 @p part 에 맞는가입니다. @p bNavigationMode 면 포커스 위젯이 `:focus-visible` 입니다. */
        static bool matchesPart( const UiStyleSelectorPart& part, const Widget& widget, bool bNavigationMode );
        /** @brief 위젯 @p widget 에 맞는가입니다 — 맨 오른쪽 조각은 위젯, 나머지는 조상 쪽으로(가까운 것부터) 차례로 찾습니다. */
        bool matches( const Widget& widget, bool bNavigationMode ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UiStyleAssignment
     * @brief 규칙의 칸 하나 = 글 값입니다(변수는 읽을 때 풀었다). 구조체 칸(`_font`)의 안쪽 칸이면 `_pNestedProperty` 가 있습니다.
     */
    struct UiStyleAssignment
    {
        string              _text{};
        const PropertyInfo* _pProperty{ nullptr };       ///< `WidgetStyle` 의 칸
        const PropertyInfo* _pNestedProperty{ nullptr }; ///< 구조체 칸 안의 칸(없으면 칸 전체에 글을 쓴다)
        UiStyleField        _field{ UiStyleField::Count };
    };
} // namespace sw

namespace sw
{
    /** @struct UiStyleRule @brief 선택자 하나와 칸들입니다. */
    struct UiStyleRule
    {
        UiStyleSelector           _selector{};
        vector<UiStyleAssignment> _listAssignment{};
        uint32                    _sourceLine{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UiStyleSheetAsset
     * @brief 스타일 시트 하나입니다(`UiStyleSheetCache` 가 경로로 나눠 준다).
     * @details 형식:
     *          ```xml
     *          <UiStyleSheet _schemaVersion="1">
     *              <Variable _name="accent" _value="0.25,0.5,1,1" />
     *              <Rule _selector="ButtonWidget.primary:hover" _backgroundColor="$accent" _padding="16,8,16,8">
     *                  <_font _weight="Bold" />                                  (구조체 칸은 자식 원소로 — 적은 안쪽 칸만 바꾼다)
     *              </Rule>
     *          </UiStyleSheet>
     *          ```
     *          규칙의 속성 이름은 `WidgetStyle` 의 PROPERTY 이름이고 값은 그 타입의 글 형식(씬 파일과 같은 변환), `$이름` 은 이 시트의 변수입니다.
     *          모르는 칸 · 변수 · 선택자 문법 · 읽지 못한 값은 로드 오류(`<경로>:<줄>:`)입니다.
     */
    struct UiStyleSheetAsset
    {
        static constexpr uint32 kVersion           = 1;
        static constexpr utf8   kExtension[]       = ".uistyle.xml";
        static constexpr utf8   kRootElementName[] = "UiStyleSheet";

        string              _path{};
        vector<UiStyleRule> _listRule{}; ///< 시트 안의 순서(뒤가 같은 특정도에서 이긴다)
    };
} // namespace sw

namespace sw
{
    /** @struct UiStyleSheetLoader @brief 스타일 시트 글을 읽고, 규칙 칸을 계산된 스타일에 씁니다. */
    struct SW_API UiStyleSheetLoader
    {
        /** @brief 스타일 시트 글을 읽습니다. 실패하면 false 와 `<경로>:<줄>: <이유>` 입니다. */
        [[nodiscard]] static bool parse( string_view text, string_view path, UiStyleSheetAsset& outSheet, string& outError );
        /** @brief 칸 하나를 @p inoutStyle 에 씁니다(읽을 때 이미 확인한 글이라 실패하지 않는다). */
        static void applyAssignment( const UiStyleAssignment& assignment, WidgetStyle& inoutStyle );
    };
} // namespace sw
