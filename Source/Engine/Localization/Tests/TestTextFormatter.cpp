#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Localization/CultureInfo.h"
#include "Engine/Localization/TextFormatter.h"

// TextFormatterTest — 번역 글의 ICU MessageFormat 해석: 복수형(영어 · 러시아어 · 한국어), offset 과 `=N`, 서수, select, 숫자 · 화폐 형식, 따옴표,
// 없는 인자와 깨진 패턴. 문화권은 엔진 표(`engine/localization/engine.cultures.xml`)에서 읽는다(nogpu).

namespace
{
    // 표는 부를 때마다 읽는다 — 함수 안 static 은 기준선 뒤에 만들어져 종료 누수 보고에 늘어난 블록으로 찍혔다.
    sw::CultureTable loadCultures()
    {
        sw::CultureTable table;
        SW_EXPECT_TRUE( table.loadFromAsset( sw::AssetID{ "engine/localization/engine.cultures.xml" } ) );
        return table;
    }

    sw::string format( sw::string_view code, sw::string_view pattern, const sw::TextArgumentList& arguments )
    {
        const sw::CultureTable cultures = loadCultures();
        const sw::CultureInfo* pCulture = cultures.findCulture( code );
        SW_EXPECT_TRUE( pCulture != nullptr );
        sw::string text;
        SW_EXPECT_TRUE( pCulture != nullptr && sw::TextFormatter::format( pattern, arguments, *pCulture, text ) );
        return text;
    }

    constexpr const utf8* kApples = "{n, plural, =0 {no apples} one {# apple} few {# apples(few)} many {# apples(many)} other {# apples}}";
} // namespace

/**
 * @brief [TextFormatterTest] 복수형 범주는 문화권 규칙을 따른다 — 영어 one/other, 러시아어 one/few/many, 한국어 other 뿐. `=0` 이 범주보다 앞선다
 */
SW_TEST_CASE( TextFormatterTest, PluralCategoriesFollowTheCulture )
{
    SW_EXPECT_EQUAL( sw::string( "no apples" ), format( "en", kApples, sw::TextArgumentList().addInteger( "n", 0 ) ) );
    SW_EXPECT_EQUAL( sw::string( "1 apple" ), format( "en", kApples, sw::TextArgumentList().addInteger( "n", 1 ) ) );
    SW_EXPECT_EQUAL( sw::string( "2 apples" ), format( "en", kApples, sw::TextArgumentList().addInteger( "n", 2 ) ) );

    SW_EXPECT_EQUAL( sw::string( "21 apple" ), format( "ru", kApples, sw::TextArgumentList().addInteger( "n", 21 ) ) );
    SW_EXPECT_EQUAL( sw::string( "3 apples(few)" ), format( "ru", kApples, sw::TextArgumentList().addInteger( "n", 3 ) ) );
    SW_EXPECT_EQUAL( sw::string( "11 apples(many)" ), format( "ru", kApples, sw::TextArgumentList().addInteger( "n", 11 ) ) );

    SW_EXPECT_EQUAL( sw::string( "1 apples" ), format( "ko", kApples, sw::TextArgumentList().addInteger( "n", 1 ) ) );
}

/**
 * @brief [TextFormatterTest] offset 은 `#` 에서 빼고, 서수 · select 는 범주를 고르며, 숫자는 문화권 구분 기호로 쓴다
 */
SW_TEST_CASE( TextFormatterTest, OffsetOrdinalSelectAndNumbers )
{
    SW_EXPECT_EQUAL( sw::string( "You and 2 others" ),
                     format( "en", "{n, plural, offset:1 =1 {You} other {You and # others}}", sw::TextArgumentList().addInteger( "n", 3 ) ) );
    const utf8* pOrdinal = "{n, selectordinal, one {#st} two {#nd} few {#rd} other {#th}}";
    SW_EXPECT_EQUAL( sw::string( "22nd" ), format( "en", pOrdinal, sw::TextArgumentList().addInteger( "n", 22 ) ) );
    SW_EXPECT_EQUAL( sw::string( "13th" ), format( "en", pOrdinal, sw::TextArgumentList().addInteger( "n", 13 ) ) );
    SW_EXPECT_EQUAL( sw::string( "she" ),
                     format( "en", "{g, select, female {she} male {he} other {they}}", sw::TextArgumentList().addText( "g", "female" ) ) );
    SW_EXPECT_EQUAL( sw::string( "they" ),
                     format( "en", "{g, select, female {she} male {he} other {they}}", sw::TextArgumentList().addText( "g", "robot" ) ) );
    SW_EXPECT_EQUAL( sw::string( "1,234,567" ), format( "en", "{n, number}", sw::TextArgumentList().addInteger( "n", 1234567 ) ) );
    SW_EXPECT_EQUAL( sw::string( "1.234.567" ), format( "de", "{n, number}", sw::TextArgumentList().addInteger( "n", 1234567 ) ) );
    SW_EXPECT_EQUAL( sw::string( "it's {literal}" ), format( "en", "it''s '{literal}'", sw::TextArgumentList() ) );
}

/**
 * @brief [TextFormatterTest] 소수 자리를 고정한 소수 인자는 그 자리까지 채워 반올림하고(`%.2f` 판독), 기본 소수 인자는 0 ~ 3 자리다. 구분자는 문화권을 따른다
 */
SW_TEST_CASE( TextFormatterTest, DecimalKeepsFixedFractionDigits )
{
    SW_EXPECT_EQUAL( sw::string( "1.50 s" ), format( "en", "{t} s", sw::TextArgumentList().addDecimal( "t", 1.5, 2 ) ) );
    SW_EXPECT_EQUAL( sw::string( "1,50 s" ), format( "de", "{t} s", sw::TextArgumentList().addDecimal( "t", 1.5, 2 ) ) );
    SW_EXPECT_EQUAL( sw::string( "3" ), format( "en", "{t}", sw::TextArgumentList().addDecimal( "t", 2.6, 0 ) ) );
    SW_EXPECT_EQUAL( sw::string( "1.5" ), format( "en", "{t}", sw::TextArgumentList().addDecimal( "t", 1.5 ) ) );
    SW_EXPECT_EQUAL( sw::string( "0.125" ), format( "en", "{t, number}", sw::TextArgumentList().addDecimal( "t", 0.1249, 3 ) ) );
}

/**
 * @brief [TextFormatterTest] select 분기 안에 다른 인자를 둘 수 있다 — 코드는 고르개 값만 넘기고 낱말과 덧붙임은 패턴(번역)이 정한다(에디터 판독 줄의 PASS · FAIL)
 */
SW_TEST_CASE( TextFormatterTest, SelectBranchCarriesArgument )
{
    const utf8* pPattern = "{result, select, pass {PASS} other {FAIL}}{hasReason, select, yes { - {reason}} other {}}";
    SW_EXPECT_EQUAL( sw::string( "FAIL - timeout" ),
                     format( "en", pPattern, sw::TextArgumentList().addText( "result", "fail" ).addText( "hasReason", "yes" ).addText( "reason", "timeout" ) ) );
    SW_EXPECT_EQUAL( sw::string( "PASS" ), format( "en", pPattern, sw::TextArgumentList().addText( "result", "pass" ).addText( "hasReason", "no" ).addText( "reason", "" ) ) );
}

/**
 * @brief [TextFormatterTest] 없는 인자 · other 가 빠진 복수형 · 닫히지 않은 괄호는 오류이고, 글은 비지 않게 채운다
 */
SW_TEST_CASE( TextFormatterTest, ReportsBrokenPatterns )
{
    const sw::CultureTable cultures = loadCultures();
    const sw::CultureInfo* pEnglish = cultures.findCulture( "en" );
    SW_REQUIRE_TRUE( pEnglish != nullptr );
    sw::string               text;
    const sw::expected<void> formatted = sw::TextFormatter::format( "Hello {name}", sw::TextArgumentList(), *pEnglish, text );
    SW_EXPECT_FALSE( formatted );
    SW_EXPECT_EQUAL( sw::string( "Hello {name}" ), text );
    SW_EXPECT_TRUE( formatted.has_value() == false && formatted.error().detail().empty() == false );

    SW_EXPECT_FALSE( sw::TextFormatter::validatePattern( "{n, plural, one {x}}" ) );
    SW_EXPECT_FALSE( sw::TextFormatter::validatePattern( "{n, plural, other {x}" ) );
    SW_EXPECT_TRUE( sw::TextFormatter::validatePattern( "{n, plural, one {# item} other {# items}}" ) );

    sw::vector<sw::string> listName;
    sw::TextFormatter::collectArgumentNames( "{a} and {b, number} and {c, plural, other {#}}", listName );
    SW_EXPECT_EQUAL( size_t( 3 ), listName.size() );
}
