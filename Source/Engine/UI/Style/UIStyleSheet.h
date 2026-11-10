/**
 * @file UIStyleSheet.h
 * @brief 스타일 시트(`*.uistyle.xml`)를 정의합니다. 선택자, 특정도, 규칙, 변수로 이루어지며 유니티 USS 에 해당하고 형식은 엔진 XML 입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Reflection/PropertyHandle.h"
#include "Engine/UI/Style/WidgetStyle.h"

namespace sw
{
    struct PropertyInfo;
    struct ResourceContext;

    class Widget;

    /**
     * @struct UIStyleSelectorPart
     * @brief 복합 선택자 하나입니다. `타입? .클래스* #이름? :상태*` 형태이며, 비어 있는 조건은 "그 조건 없음" 입니다.
     */
    struct UIStyleSelectorPart
    {
        hashed_string         _typeName{};     ///< 정확한 위젯 타입 이름입니다(파생 타입은 맞지 않으며, USS 와 같습니다).
        hashed_string         _name{};         ///< `#이름` 조건입니다.
        vector<hashed_string> _listClass{};    ///< `.클래스` 조건들입니다(모두 있어야 맞습니다).
        uint32                _stateMask{ 0 }; ///< `:상태` 조건들입니다(`UIStyleState` 비트이며 모두 켜져야 맞습니다).
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UIStyleSelector
     * @brief 복합 선택자들을 공백(자손 결합자, 아무 조상)으로 이은 것입니다. `>`, `+`, `*`, `[속성]` 은 쓰지 않습니다(읽으면 오류).
     * @details 특정도는 (#이름 수, .클래스 + :상태 수, 타입 수)의 사전 순서이며, 한 자리에 8 비트씩 담는다.
     */
    struct SW_API UIStyleSelector
    {
        vector<UIStyleSelectorPart> _listPart{}; ///< 왼쪽(먼 조상)에서 오른쪽(대상) 순서입니다.
        uint32                      _specificity{ 0 };

        /** @brief 선택자 문자열을 읽습니다. 모르는 문법이나 상태 이름이면 false 를 반환하고 @p outError 에 이유를 둡니다. */
        [[nodiscard]] static expected<void> parse( string_view text, UIStyleSelector& outSelector );
        /** @brief 위젯 @p widget 의 지금 상태가 @p part 에 맞는지 반환합니다. @p bNavigationMode 면 포커스 위젯이 `:focus-visible` 입니다. */
        static bool matchesPart( const UIStyleSelectorPart& part, const Widget& widget, bool bNavigationMode );
        /** @brief 위젯 @p widget 에 맞는지 반환합니다. 맨 오른쪽 조각은 위젯에, 나머지는 가까운 조상부터 차례로 찾아 맞춥니다. */
        bool matches( const Widget& widget, bool bNavigationMode ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UIStyleAssignment
     * @brief 규칙의 필드 하나와 텍스트 값입니다(변수는 읽을 때 해석합니다). 구조체 필드(`_font`)의 안쪽 필드면 `_nestedProperty` 가 있습니다. 시트 수명 동안 들므로 포인터가 아니라 `PropertyHandle` 이다.
     */
    struct UIStyleAssignment
    {
        string         _text{};
        PropertyHandle _property{};       ///< `WidgetStyle` 의 필드입니다.
        PropertyHandle _nestedProperty{}; ///< 구조체 필드 안의 필드입니다(비었으면 필드 전체에 텍스트를 씁니다).
        UIStyleField   _field{ UIStyleField::Count };
    };
} // namespace sw

namespace sw
{
    /** @struct UIStyleRule @brief 선택자 하나와 필드들입니다. */
    struct UIStyleRule
    {
        UIStyleSelector           _selector{};
        vector<UIStyleAssignment> _listAssignment{};
        uint32                    _sourceLine{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct SW_API UIStyleSheetAsset
     * @brief 스타일 시트 하나입니다(`UIStyleSheetCache` 가 경로별로 나눠 줍니다).
     * @details 형식:
     *          ```xml
     *          <UIStyleSheet _schemaVersion="1">
     *              <Variable _name="accent" _value="0.25,0.5,1,1" />
     *              <Rule _selector="ButtonWidget.primary:hover" _backgroundColor="$accent" _padding="16,8,16,8">
     *              <_font _weight="Bold" />                                  (구조체 필드는 자식 요소로 쓰며, 적은 안쪽 필드만 바꾼다)
     *              </Rule>
     *          </UIStyleSheet>
     *          ```
     *          규칙의 속성 이름은 `WidgetStyle` 의 PROPERTY 이름이고 값은 그 타입의 텍스트 형식(씬 파일과 같은 변환)이며, `$이름` 은 이 시트의 변수다.
     *          모르는 필드, 변수, 선택자 문법, 읽지 못한 값은 로드 오류(`<경로>:<줄>:`)다.
     *
     *          변형 둘. 루트의 `_base="<부모 시트>"` 는 부모를 물려받아 변수(`_name`) · 규칙(`_selector`) · 형식 변형(`_name`)을 열쇠로 합친다 — 테마는 변수만 적은 시트 하나다
     *          (유니티 USS 변수 · Godot Theme 상속 자리, 규칙은 `Content/engine/settings/datacataloginheritance.xml`). 부모를 합친 시트의 오류는 줄 번호 없이 경로만 적는다.
     *          `<TypeVariation _name="danger" _extends="primary" />` 는 클래스 `danger` 를 `primary` 의 변형으로 둔다(Godot Theme Type Variation). `primary` 를 쓰는 규칙이 상태 · 자손까지
     *          복제되므로 위젯은 `_styleClass="danger"` 만 적고, 시트는 `danger` 의 차이만 적는다. 한 시트 안의 일이다 — 다른 시트의 규칙은 복제하지 않는다.
     */
    struct UIStyleSheetAsset
    {
        static constexpr uint32 kVersion     = 1;
        static constexpr utf8   kExtension[] = ".uistyle.xml";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `UIStyleSheet.cpp` 의 `SW_ASSET_TYPE`). */
        static constexpr AssetTypeDef kAssetType{ "UIStyleSheetAsset", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };
        static constexpr utf8         kRootElementName[] = "UIStyleSheet";

        string              _path{};
        vector<UIStyleRule> _listRule{}; ///< 시트 안의 순서입니다(같은 특정도에서는 뒤의 규칙이 이깁니다).

        // 스타일 시트 텍스트를 읽고, 규칙 필드를 계산된 스타일에 씁니다. */
        /** @brief 스타일 시트 텍스트를 읽습니다. 실패하면 false 를 반환하고 오류는 `<경로>:<줄>: <이유>` 형식입니다. */
        /** @param context 합성 문맥입니다(부모 `_base` · 조건 `_when` — 캐시는 리소스 id 와 변형의 태그를 싣는다, 글만 읽는 쪽은 `ResourceContext::forDocument()`). */
        [[nodiscard]] static expected<void> parse( string_view text, string_view path, const ResourceContext& context, UIStyleSheetAsset& outSheet );
        /** @brief 필드 하나를 @p inoutStyle 에 씁니다(읽을 때 이미 확인한 텍스트라 실패하지 않습니다). */
        static void applyAssignment( const UIStyleAssignment& assignment, WidgetStyle& inoutStyle );
    };
} // namespace sw
