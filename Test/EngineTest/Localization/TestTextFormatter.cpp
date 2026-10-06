#include "pch.h"

#include "Engine/Localization/CultureInfo.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Resource/ResourceUtil.h"

#include "EngineTest/LocalizationTestUtil.h"

#include "TestFramework/TestFramework.h"

using sw::test::LocalizationTestUtil;

namespace
{
    struct TextFormatterTestInternal
    {
        /** @brief 엔진 문화권 표입니다(한 번 읽어 둔다). */
        static const sw::CultureTable& getCultures()
        {
            static sw::CultureTable s_table;
            if ( s_table.getCultureCount() == 0 )
                (void)s_table.loadFromResource( LocalizationTestUtil::kCultureTable ); // 실패는 로더가 오류로 남기고, 빈 표는 문화권을 찾는 단언이 드러낸다
            return s_table;
        }

        static const sw::CultureInfo& getCulture( sw::string_view code )
        {
            static const sw::CultureInfo s_empty{};
            const sw::CultureInfo*       pCulture = getCultures().resolveCulture( code );
            return pCulture != nullptr ? *pCulture : s_empty;
        }

        static sw::string format( sw::string_view culture, sw::string_view pattern, const sw::TextArgumentList& arguments )
        {
            sw::string text;
            sw::string error;
            const bool bFormatted = sw::TextFormatter::format( pattern, arguments, getCulture( culture ), text, &error );
            if ( bFormatted == false )
                text = "<error: " + error + ">";
            return text;
        }

        static sw::string plural( sw::string_view culture, sw::string_view pattern, int64 count )
        {
            return format( culture, pattern, sw::TextArgumentList().addInteger( "n", count ) );
        }

        /** @brief 날짜 · 시각 하나 — 2026-10-04 13:05:09. */
        static sw::TextDateTime makeSampleDate()
        {
            sw::TextDateTime value;
            value._year   = 2026;
            value._month  = 10;
            value._day    = 4;
            value._hour   = 13;
            value._minute = 5;
            value._second = 9;
            return value;
        }
    };
} // namespace

/**
 * @brief [CultureTableTest] 엔진 문화권 표가 읽히고, 부모에서 칸을 물려받으며, 지역 코드는 언어로 풀린다
 */
SW_TEST_CASE( CultureTableTest, EngineTableLoadsAndInherits )
{
    const sw::CultureTable& cultures = TextFormatterTestInternal::getCultures();
    SW_ASSERT_TRUE( cultures.getCultureCount() >= 12u );

    const sw::CultureInfo* pBritish = cultures.findCulture( "en-GB" );
    SW_ASSERT_NOT_NULL( pBritish );
    SW_EXPECT_STREQ( "english", pBritish->_pluralRuleName.c_str() ); // en 에서 물려받는다
    SW_EXPECT_STREQ( "dd/MM/y", pBritish->_dateShort.c_str() );      // 자기 칸

    const sw::CultureInfo* pKorean = cultures.resolveCulture( "ko-KR" ); // ko_kr 은 없다 → ko
    SW_ASSERT_NOT_NULL( pKorean );
    SW_EXPECT_STREQ( "ko", pKorean->_code.c_str() );
    SW_ASSERT_TRUE( pKorean->_listFont.empty() == false );
    SW_EXPECT_STREQ( "Noto Sans KR", pKorean->_listFont[0].c_str() );

    const sw::CultureInfo* pTraditional = cultures.findCulture( "zh_tw" );
    SW_ASSERT_NOT_NULL( pTraditional );
    SW_EXPECT_STREQ( "Noto Sans TC", pTraditional->_listFont[0].c_str() );
    SW_EXPECT_STREQ( "y年M月d日", pTraditional->_dateLong.c_str() ); // zh 에서 물려받는다

    const sw::CultureInfo* pArabic = cultures.findCulture( "ar" );
    SW_ASSERT_NOT_NULL( pArabic );
    SW_EXPECT_TRUE( pArabic->_bRightToLeft );

    const sw::CultureInfo* pMirrored = cultures.findCulture( "qps-plocm" );
    SW_ASSERT_NOT_NULL( pMirrored );
    SW_EXPECT_TRUE( pMirrored->isPseudo() );
    SW_EXPECT_TRUE( pMirrored->_bRightToLeft );
}

/**
 * @brief [CultureTableTest] 모르는 칸 · 모르는 복수형 규칙 · 없는 부모 · 정본이 아닌 코드는 로드 오류이고 표를 바꾸지 않는다
 */
SW_TEST_CASE( CultureTableTest, BadDataIsALoadError )
{
    sw::CultureTable table;
    sw::string       error;
    SW_EXPECT_FALSE( table.loadFromJsonText( R"({ "cultures": { "xx": { "pluralRule": "english", "decimalSep": "." } } })", "t", &error ) );
    SW_EXPECT_TRUE( error.find( "decimalSep" ) != sw::string::npos );
    SW_EXPECT_FALSE( table.loadFromJsonText( R"({ "cultures": { "xx": { "pluralRule": "klingon" } } })", "t", &error ) );
    SW_EXPECT_FALSE( table.loadFromJsonText( R"({ "cultures": { "xx": { "parent": "yy" } } })", "t", &error ) );
    SW_EXPECT_FALSE( table.loadFromJsonText( R"({ "cultures": { "xx": { "parent": "zz" }, "zz": { "parent": "xx" } } })", "t", &error ) );
    SW_EXPECT_FALSE( table.loadFromJsonText( R"({ "cultures": { "xx-YY": { "pluralRule": "none" } } })", "t", &error ) );
    SW_EXPECT_FALSE( table.loadFromJsonText( R"({ "cultures": { "xx": { "pluralRule": "none", "monthNames": [ "a" ] } } })", "t", &error ) );
    SW_EXPECT_EQUAL( size_t( 0 ), table.getCultureCount() );
}

/**
 * @brief [TextFormatterTest] 이름 인자 · 작은따옴표 규칙 · 없는 인자
 */
SW_TEST_CASE( TextFormatterTest, NamedArgumentsAndQuoting )
{
    const sw::TextArgumentList arguments = sw::TextArgumentList().addText( "name", "Bob" ).addInteger( "count", 1234 );
    SW_EXPECT_STREQ( "Hello Bob, you have 1,234 coins.", TextFormatterTestInternal::format( "en", "Hello {name}, you have {count} coins.", arguments ).c_str() );
    SW_EXPECT_STREQ( "It's {literal} Bob", TextFormatterTestInternal::format( "en", "It''s '{literal}' {name}", arguments ).c_str() );
    SW_EXPECT_STREQ( "don't stop, Bob", TextFormatterTestInternal::format( "en", "don't stop, {name}", arguments ).c_str() );

    sw::string text;
    sw::string error;
    SW_EXPECT_FALSE( sw::TextFormatter::format( "Hi {who}", arguments, TextFormatterTestInternal::getCulture( "en" ), text, &error ) );
    SW_EXPECT_STREQ( "Hi {who}", text.c_str() ); // 없는 인자는 자리표시 그대로 — 화면이 비지 않는다
    SW_EXPECT_TRUE( error.find( "who" ) != sw::string::npos );
}

/**
 * @brief [TextFormatterTest] CLDR 복수형 범주 — en · ko · ja · zh · ru · pl · ar · fr · cs
 * @details 언어마다 범주가 다르다: 러시아어 21 은 one, 22 는 few, 25 · 11 은 many / 폴란드어 22 는 few, 12 는 many / 아랍어는 0 · 1 · 2 · 3~10 · 11~99 를 가른다.
 */
SW_TEST_CASE( TextFormatterTest, PluralCategoriesPerLanguage )
{
    const sw::string_view kEnglish = "{n, plural, one {# item} other {# items}}";
    SW_EXPECT_STREQ( "1 item", TextFormatterTestInternal::plural( "en", kEnglish, 1 ).c_str() );
    SW_EXPECT_STREQ( "2 items", TextFormatterTestInternal::plural( "en", kEnglish, 2 ).c_str() );
    SW_EXPECT_STREQ( "0 items", TextFormatterTestInternal::plural( "en", kEnglish, 0 ).c_str() );
    SW_EXPECT_STREQ( "1.5 items", TextFormatterTestInternal::format( "en", kEnglish, sw::TextArgumentList().addDecimal( "n", 1.5 ) ).c_str() );

    const sw::string_view kOther = "{n, plural, other {#개}}";
    SW_EXPECT_STREQ( "1개", TextFormatterTestInternal::plural( "ko", kOther, 1 ).c_str() );
    SW_EXPECT_STREQ( "1개", TextFormatterTestInternal::plural( "ja", kOther, 1 ).c_str() );
    SW_EXPECT_STREQ( "1개", TextFormatterTestInternal::plural( "zh", kOther, 1 ).c_str() );

    const sw::string_view kRussian = "{n, plural, one {# one} few {# few} many {# many} other {# other}}";
    SW_EXPECT_STREQ( "1 one", TextFormatterTestInternal::plural( "ru", kRussian, 1 ).c_str() );
    SW_EXPECT_STREQ( "21 one", TextFormatterTestInternal::plural( "ru", kRussian, 21 ).c_str() );
    SW_EXPECT_STREQ( "2 few", TextFormatterTestInternal::plural( "ru", kRussian, 2 ).c_str() );
    SW_EXPECT_STREQ( "22 few", TextFormatterTestInternal::plural( "ru", kRussian, 22 ).c_str() );
    SW_EXPECT_STREQ( "5 many", TextFormatterTestInternal::plural( "ru", kRussian, 5 ).c_str() );
    SW_EXPECT_STREQ( "11 many", TextFormatterTestInternal::plural( "ru", kRussian, 11 ).c_str() );
    SW_EXPECT_STREQ( "12 many", TextFormatterTestInternal::plural( "ru", kRussian, 12 ).c_str() );
    SW_EXPECT_STREQ( "1,5 other", TextFormatterTestInternal::format( "ru", kRussian, sw::TextArgumentList().addDecimal( "n", 1.5 ) ).c_str() );

    SW_EXPECT_STREQ( "1 one", TextFormatterTestInternal::plural( "pl", kRussian, 1 ).c_str() );
    SW_EXPECT_STREQ( "22 few", TextFormatterTestInternal::plural( "pl", kRussian, 22 ).c_str() );
    SW_EXPECT_STREQ( "12 many", TextFormatterTestInternal::plural( "pl", kRussian, 12 ).c_str() );
    SW_EXPECT_STREQ( "21 many", TextFormatterTestInternal::plural( "pl", kRussian, 21 ).c_str() ); // 러시아어와 다르다

    const sw::string_view kArabic = "{n, plural, zero {zero} one {one} two {two} few {few} many {many} other {other}}";
    SW_EXPECT_STREQ( "zero", TextFormatterTestInternal::plural( "ar", kArabic, 0 ).c_str() );
    SW_EXPECT_STREQ( "one", TextFormatterTestInternal::plural( "ar", kArabic, 1 ).c_str() );
    SW_EXPECT_STREQ( "two", TextFormatterTestInternal::plural( "ar", kArabic, 2 ).c_str() );
    SW_EXPECT_STREQ( "few", TextFormatterTestInternal::plural( "ar", kArabic, 3 ).c_str() );
    SW_EXPECT_STREQ( "few", TextFormatterTestInternal::plural( "ar", kArabic, 103 ).c_str() );
    SW_EXPECT_STREQ( "many", TextFormatterTestInternal::plural( "ar", kArabic, 11 ).c_str() );
    SW_EXPECT_STREQ( "other", TextFormatterTestInternal::plural( "ar", kArabic, 100 ).c_str() );

    SW_EXPECT_STREQ( "0 one", TextFormatterTestInternal::plural( "fr", kRussian, 0 ).c_str() ); // 프랑스어는 0 도 one
}

/**
 * @brief [TextFormatterTest] `=N` 이 범주보다 앞서고, offset 은 범주와 `#` 에만 쓰이며, 갈래 안의 인자 · 중첩도 풀린다
 */
SW_TEST_CASE( TextFormatterTest, ExactMatchOffsetAndNesting )
{
    const sw::string_view kGuests = "{n, plural, offset:1 =0 {Nobody came} =1 {{host} came} one {{host} and # guest came} other {{host} and # guests came}}";
    SW_EXPECT_STREQ( "Nobody came", TextFormatterTestInternal::format( "en", kGuests, sw::TextArgumentList().addInteger( "n", 0 ).addText( "host", "Bob" ) ).c_str() );
    SW_EXPECT_STREQ( "Bob came", TextFormatterTestInternal::format( "en", kGuests, sw::TextArgumentList().addInteger( "n", 1 ).addText( "host", "Bob" ) ).c_str() );
    SW_EXPECT_STREQ( "Bob and 1 guest came", TextFormatterTestInternal::format( "en", kGuests, sw::TextArgumentList().addInteger( "n", 2 ).addText( "host", "Bob" ) ).c_str() );
    SW_EXPECT_STREQ( "Bob and 4 guests came", TextFormatterTestInternal::format( "en", kGuests, sw::TextArgumentList().addInteger( "n", 5 ).addText( "host", "Bob" ) ).c_str() );

    // 갈래 순서와 무관하게 `=1` 이 one 보다 앞선다.
    SW_EXPECT_STREQ( "exactly one", TextFormatterTestInternal::plural( "en", "{n, plural, one {category} =1 {exactly one} other {many}}", 1 ).c_str() );
}

/**
 * @brief [TextFormatterTest] select(성별 등) — 맞는 갈래, 없으면 other / 복수형 안의 select
 */
SW_TEST_CASE( TextFormatterTest, SelectBranches )
{
    const sw::string_view kPattern = "{gender, select, female {She} male {He} other {They}} found {n, plural, one {a key} other {# keys}}.";
    SW_EXPECT_STREQ( "She found a key.", TextFormatterTestInternal::format( "en", kPattern, sw::TextArgumentList().addText( "gender", "female" ).addInteger( "n", 1 ) ).c_str() );
    SW_EXPECT_STREQ( "He found 3 keys.", TextFormatterTestInternal::format( "en", kPattern, sw::TextArgumentList().addText( "gender", "male" ).addInteger( "n", 3 ) ).c_str() );
    SW_EXPECT_STREQ( "They found 3 keys.", TextFormatterTestInternal::format( "en", kPattern, sw::TextArgumentList().addText( "gender", "robot" ).addInteger( "n", 3 ) ).c_str() );
}

/**
 * @brief [TextFormatterTest] 숫자 · 백분율 형식은 문화권의 기호 · 숫자 글자를 쓴다
 */
SW_TEST_CASE( TextFormatterTest, NumbersFollowTheCulture )
{
    const sw::TextArgumentList arguments = sw::TextArgumentList().addInteger( "big", 1234567 ).addDecimal( "price", 1234.56 ).addDecimal( "ratio", 0.25 ).addInteger( "negative", -9876 );
    SW_EXPECT_STREQ( "1,234,567 | 1,234.56 | 25% | -9,876", TextFormatterTestInternal::format( "en", "{big, number} | {price, number} | {ratio, number, percent} | {negative}", arguments ).c_str() );
    SW_EXPECT_STREQ( "1.234.567 | 1.234,56 | 25 %", TextFormatterTestInternal::format( "de", "{big, number} | {price, number} | {ratio, number, percent}", arguments ).c_str() );
    SW_EXPECT_STREQ( "1 234 567", TextFormatterTestInternal::format( "fr", "{big}", arguments ).c_str() );
    SW_EXPECT_STREQ( "1 234 567", TextFormatterTestInternal::format( "ru", "{big}", arguments ).c_str() );
    SW_EXPECT_STREQ( "١٬٢٣٤٬٥٦٧", TextFormatterTestInternal::format( "ar", "{big}", arguments ).c_str() );
    SW_EXPECT_STREQ( "1,235", TextFormatterTestInternal::format( "en", "{price, number, integer}", arguments ).c_str() );
}

/**
 * @brief [TextFormatterTest] int64 를 넘는 실수의 정수부도 자리가 그대로 남는다 — 0 으로 찍히지 않는다
 * @details 정수부를 정수로 되읽으면 9.2e18 을 넘는 값은 읽기에 실패한다. 글자 그대로 묶어야 한다.
 */
SW_TEST_CASE( TextFormatterTest, DecimalBeyondInt64KeepsItsIntegerDigits )
{
    const sw::CultureInfo& english = TextFormatterTestInternal::getCulture( "en" );
    SW_EXPECT_STREQ( "10,000,000,000,000,000,000", english.formatDecimal( 1e19, 0, 2 ).c_str() );
    SW_EXPECT_STREQ( "-25,000,000,000,000,000,000", english.formatDecimal( -2.5e19, 0, 0 ).c_str() );
    SW_EXPECT_STREQ( "1.000.000.000.000.000.000.000", TextFormatterTestInternal::getCulture( "de" ).formatDecimal( 1e21, 0, 0 ).c_str() );
    SW_EXPECT_STREQ( "-1,234.5", english.formatDecimal( -1234.5, 0, 1 ).c_str() ); // 평소 값은 그대로
}

/**
 * @brief [TextFormatterTest] 날짜 · 시각 형식은 문화권의 패턴 · 월 이름 · 오전/오후를 쓴다
 */
SW_TEST_CASE( TextFormatterTest, DatesFollowTheCulture )
{
    const sw::TextArgumentList arguments = sw::TextArgumentList().addDateTime( "d", TextFormatterTestInternal::makeSampleDate() );
    SW_EXPECT_STREQ( "10/4/26 | Oct 4, 2026 | October 4, 2026 | 1:05 PM", TextFormatterTestInternal::format( "en", "{d, date, short} | {d, date} | {d, date, long} | {d, time, short}", arguments ).c_str() );
    SW_EXPECT_STREQ( "04/10/2026 | 13:05", TextFormatterTestInternal::format( "en-GB", "{d, date, short} | {d, time}", arguments ).c_str() );
    SW_EXPECT_STREQ( "2026년 10월 4일 오후 1:05", TextFormatterTestInternal::format( "ko", "{d, date, long} {d, time}", arguments ).c_str() );
    SW_EXPECT_STREQ( "2026年10月4日 13:05", TextFormatterTestInternal::format( "ja", "{d, date, long} {d, time}", arguments ).c_str() );
    SW_EXPECT_STREQ( "4 октября 2026 г.", TextFormatterTestInternal::format( "ru", "{d, date, long}", arguments ).c_str() );
    SW_EXPECT_STREQ( "4 października 2026", TextFormatterTestInternal::format( "pl", "{d, date, long}", arguments ).c_str() );
    SW_EXPECT_STREQ( "4 de octubre de 2026", TextFormatterTestInternal::format( "es", "{d, date, long}", arguments ).c_str() );
}

/**
 * @brief [TextFormatterTest] 구문 검사 — other 없는 plural · 닫히지 않은 괄호 · 모르는 타입 · 모르는 복수형 키워드는 오류다
 */
SW_TEST_CASE( TextFormatterTest, ValidationRejectsBrokenPatterns )
{
    SW_EXPECT_TRUE( sw::TextFormatter::validatePattern( "{a} {n, plural, one {#} other {# {b}}} {g, select, x {y} other {z}}" ) );
    SW_EXPECT_FALSE( sw::TextFormatter::validatePattern( "{n, plural, one {x}}" ) );
    SW_EXPECT_FALSE( sw::TextFormatter::validatePattern( "{name" ) );
    SW_EXPECT_FALSE( sw::TextFormatter::validatePattern( "name}" ) );
    SW_EXPECT_FALSE( sw::TextFormatter::validatePattern( "{n, currency}" ) );
    SW_EXPECT_FALSE( sw::TextFormatter::validatePattern( "{n, plural, several {x} other {y}}" ) );

    sw::vector<sw::string> listName;
    sw::TextFormatter::collectArgumentNames( "{a} {n, plural, one {{c}} other {{b}}} {a}", listName );
    SW_ASSERT_EQUAL( size_t( 4 ), listName.size() );
    SW_EXPECT_STREQ( "a", listName[0].c_str() );
    SW_EXPECT_STREQ( "b", listName[1].c_str() );
    SW_EXPECT_STREQ( "c", listName[2].c_str() );
    SW_EXPECT_STREQ( "n", listName[3].c_str() );
}
