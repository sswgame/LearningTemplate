#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/FileSystem/AssetID.h"
#include "Engine/Localization/CultureInfo.h"
#include "Engine/Localization/TextFormatter.h"

// TextFormatterTest — 번역 글의 ICU MessageFormat 해석: 복수형(영어 · 러시아어 · 한국어), offset 과 `=N`, 서수, select, 숫자 · 화폐 형식, 따옴표,
// 없는 인자와 깨진 패턴. 문화권은 엔진 표(`engine/localization/engine.cultures.xml`)에서 읽는다(nogpu).

namespace
{
    const sw::CultureTable& getCultures()
    {
        static sw::CultureTable s_table;
        static const bool       s_bLoaded = s_table.loadFromAsset( sw::AssetID{ "engine/localization/engine.cultures.xml" } );
        SW_EXPECT_TRUE( s_bLoaded );
        return s_table;
    }

    sw::string format( sw::string_view code, sw::string_view pattern, const sw::TextArgumentList& arguments )
    {
        const sw::CultureInfo* pCulture = getCultures().findCulture( code );
        SW_EXPECT_TRUE( pCulture != nullptr );
        sw::string text;
        sw::string error;
        SW_EXPECT_TRUE( pCulture != nullptr && sw::TextFormatter::format( pattern, arguments, *pCulture, text, &error ) );
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
 * @brief [TextFormatterTest] 없는 인자 · other 가 빠진 복수형 · 닫히지 않은 괄호는 오류이고, 글은 비지 않게 채운다
 */
SW_TEST_CASE( TextFormatterTest, ReportsBrokenPatterns )
{
    const sw::CultureInfo* pEnglish = getCultures().findCulture( "en" );
    SW_REQUIRE_TRUE( pEnglish != nullptr );
    sw::string text;
    sw::string error;
    SW_EXPECT_FALSE( sw::TextFormatter::format( "Hello {name}", sw::TextArgumentList(), *pEnglish, text, &error ) );
    SW_EXPECT_EQUAL( sw::string( "Hello {name}" ), text );
    SW_EXPECT_FALSE( error.empty() );

    SW_EXPECT_FALSE( sw::TextFormatter::validatePattern( "{n, plural, one {x}}" ) );
    SW_EXPECT_FALSE( sw::TextFormatter::validatePattern( "{n, plural, other {x}" ) );
    SW_EXPECT_TRUE( sw::TextFormatter::validatePattern( "{n, plural, one {# item} other {# items}}" ) );

    sw::vector<sw::string> listName;
    sw::TextFormatter::collectArgumentNames( "{a} and {b, number} and {c, plural, other {#}}", listName );
    SW_EXPECT_EQUAL( size_t( 3 ), listName.size() );
}
