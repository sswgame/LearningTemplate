#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Inherit/DataCatalogInheritance.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"
#include "Engine/UI/Style/UIStyleSheet.h"
#include "Engine/UI/WidgetTree/WidgetTypes.h"

using namespace sw;

namespace
{
    /** @brief 부모 시트 글을 한 벌 돌려주는 곳입니다. */
    class SheetParentSource final : public IDataInheritanceSource
    {
    public:
        explicit SheetParentSource( string_view text )
            : _text{ text }
        {
        }
        [[nodiscard]] bool readText( string_view /*assetID*/, string& outText ) override
        {
            outText = _text;
            return true;
        }

    private:
        string _text;
    };

    struct UIStyleSheetVariantTestUtil
    {
        /** @brief 규칙에서 필드 하나의 글입니다. 없으면 빈 글입니다. */
        static string findValue( const UIStyleRule& rule, UIStyleField field )
        {
            for ( const UIStyleAssignment& assignment : rule._listAssignment )
            {
                if ( assignment._field == field )
                    return assignment._text;
            }
            return {};
        }

        /** @brief 맨 오른쪽 조각에 @p className 클래스와 @p stateMask 상태가 있는 첫 규칙입니다. */
        static const UIStyleRule* findRule( const UIStyleSheetAsset& sheet, const utf8* pClassName, uint32 stateMask )
        {
            const hashed_string className( pClassName );
            for ( const UIStyleRule& rule : sheet._listRule )
            {
                const UIStyleSelectorPart& part = rule._selector._listPart.back();
                for ( const hashed_string& written : part._listClass )
                {
                    if ( written == className && part._stateMask == stateMask )
                        return &rule;
                }
            }
            return nullptr;
        }
    };
    using Util = UIStyleSheetVariantTestUtil;
} // namespace

/** @brief [UIStyleSheetVariantTest] 규칙 표의 한 줄로 시트가 부모를 물려받는다 — 변수를 덮으면 물려받은 규칙이 새 값을 읽고, 같은 선택자는 칸을 합치고, 새 선택자는 더한다 */
SW_TEST_CASE( UIStyleSheetVariantTest, ChildSheetMergesByKey )
{
    DataInheritanceSchema schema{ "" };
    bool                  bListed{ false };
    SW_REQUIRE_TRUE( DataCatalogInheritance::loadSchema( "UIStyleSheet", schema, bListed ) );
    SW_REQUIRE_TRUE( bListed );

    SheetParentSource source{ R"(<UIStyleSheet _schemaVersion="1">
        <Variable _name="accent" _value="0,0,1,1" />
        <Variable _name="text" _value="1,1,1,1" />
        <Rule _selector="ButtonWidget.primary" _backgroundColor="$accent" _borderWidth="1"><_font _weight="Bold" /></Rule>
        <Rule _selector="TextWidget.title" _textColor="$text" />
    </UIStyleSheet>)" };
    string            flat;
    SW_REQUIRE_TRUE( DataCatalogInheritance::flatten( R"(<UIStyleSheet _base="parent.uistyle.xml">
        <Variable _name="accent" _value="1,0,0,1" />
        <Rule _selector="ButtonWidget.primary" _borderWidth="3" />
        <Rule _selector="TextWidget.title" _op="Remove" />
        <Rule _selector="BorderPanel.window" _backgroundColor="$text" />
    </UIStyleSheet>)",
                                                      "child", schema, source, flat ) );

    UIStyleSheetAsset sheet;
    SW_REQUIRE_TRUE( UIStyleSheetAsset::parse( flat, "child.uistyle.xml", ResourceContext::forDocument(), sheet ).has_value() );
    SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( sheet._listRule.size() ) );
    const UIStyleRule* pPrimary = Util::findRule( sheet, "primary", 0u );
    SW_REQUIRE_NOT_NULL( pPrimary );
    SW_EXPECT_EQUAL( string( "1,0,0,1" ), Util::findValue( *pPrimary, UIStyleField::BackgroundColor ) );
    SW_EXPECT_EQUAL( string( "3" ), Util::findValue( *pPrimary, UIStyleField::BorderWidth ) );
    SW_EXPECT_EQUAL( string( "Bold" ), Util::findValue( *pPrimary, UIStyleField::Font ) );
}

/** @brief [UIStyleSheetVariantTest] 형식 변형은 부모 클래스의 규칙을 상태와 자손까지 복제하고, 시트에 적은 변형의 규칙이 같은 특정도에서 이긴다 */
SW_TEST_CASE( UIStyleSheetVariantTest, TypeVariationClonesRulesBeforeItsOwn )
{
    UIStyleSheetAsset sheet;
    SW_REQUIRE_TRUE( UIStyleSheetAsset::parse( R"(<UIStyleSheet _schemaVersion="1">
        <TypeVariation _name="danger" _extends="primary" />
        <TypeVariation _name="dangerSmall" _extends="danger" />
        <Rule _selector="ButtonWidget.danger" _backgroundColor="1,0,0,1" />
        <Rule _selector="ButtonWidget.primary" _backgroundColor="0,0,1,1" _borderWidth="2" />
        <Rule _selector="ButtonWidget.primary:hover" _backgroundColor="0,1,0,1" />
        <Rule _selector="ButtonWidget.other" _borderWidth="9" />
    </UIStyleSheet>)",
                                               "sheet.uistyle.xml", ResourceContext::forDocument(), sheet )
                         .has_value() );
    // 적은 넷 + danger 의 복제 둘(primary 둘) + dangerSmall 의 복제 셋(danger 하나, danger 복제 둘)
    SW_REQUIRE_EQUAL( 9u, static_cast<uint32>( sheet._listRule.size() ) );

    const UIStyleRule* pHover = Util::findRule( sheet, "danger", UIStyleState::kHover );
    SW_REQUIRE_NOT_NULL( pHover );
    SW_EXPECT_EQUAL( string( "0,1,0,1" ), Util::findValue( *pHover, UIStyleField::BackgroundColor ) );

    // 같은 특정도면 뒤가 이긴다 — 마지막 danger 규칙이 시트에 적은 자기 규칙이어야 한다.
    const hashed_string danger( "danger" );
    const UIStyleRule*  pLast = nullptr;
    for ( const UIStyleRule& rule : sheet._listRule )
    {
        const UIStyleSelectorPart& part = rule._selector._listPart.back();
        if ( part._stateMask == 0u && part._listClass.empty() == false && part._listClass[0] == danger )
            pLast = &rule;
    }
    SW_REQUIRE_NOT_NULL( pLast );
    SW_EXPECT_EQUAL( string( "1,0,0,1" ), Util::findValue( *pLast, UIStyleField::BackgroundColor ) );
    SW_EXPECT_TRUE( Util::findRule( sheet, "dangerSmall", UIStyleState::kHover ) != nullptr );
}

/** @brief [UIStyleSheetVariantTest] 어떤 규칙도 쓰지 않는 클래스를 잇거나 이름이 빠지면 읽기 오류다 */
SW_TEST_CASE( UIStyleSheetVariantTest, TypeVariationErrors )
{
    UIStyleSheetAsset    sheet;
    const expected<void> unused = UIStyleSheetAsset::parse( R"(<UIStyleSheet _schemaVersion="1"><TypeVariation _name="a" _extends="nope" /><Rule _selector=".b" _borderWidth="1" /></UIStyleSheet>)", "s", ResourceContext::forDocument(), sheet );
    SW_REQUIRE_FALSE( unused.has_value() );
    SW_EXPECT_TRUE( unused.error().detail().find( "no rule uses" ) != string_view::npos );
    SW_EXPECT_FALSE( UIStyleSheetAsset::parse( R"(<UIStyleSheet _schemaVersion="1"><TypeVariation _name="a" /></UIStyleSheet>)", "s", ResourceContext::forDocument(), sheet ).has_value() );
    SW_EXPECT_FALSE( UIStyleSheetAsset::parse( R"(<UIStyleSheet _schemaVersion="1"><TypeVariation _name="a" _extends="a" /></UIStyleSheet>)", "s", ResourceContext::forDocument(), sheet ).has_value() );
}
