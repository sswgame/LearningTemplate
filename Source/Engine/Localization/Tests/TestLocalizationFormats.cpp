// 현지화 형식 — 서수(selectordinal) 규칙, 통화 형식 · 소수 자리, 시간대 표 검증 · 시간대 변환 · 일광 절약 경계 · 오프셋 글, 이진 번역 표(locres)의 왕복과 깨진 바이트 거절(nogpu).
// 문화권은 엔진 표(`engine/localization/engine.cultures.xml`)에서 읽는다. 복수형 · select · 숫자의 기본 해석은 `TextFormatterTest` 가 본다.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/Name/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Localization/CultureInfo.h"
#include "Engine/Localization/LocalizationBinaryTable.h"
#include "Engine/Localization/LocalizationDocuments.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Localization/Tests/LocalizationTestUtil.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Localization/TimeZoneInfo.h"

namespace
{
    struct LocalizationFormatsTestInternal
    {
        // 표는 부를 때마다 읽는다 — 함수 안 static 으로 두면 기준선 뒤에 처음 만들어져 프로세스 끝까지 살아 종료 누수 보고에 늘어난 블록으로 찍혔다.
        static sw::CultureTable loadCultures()
        {
            sw::CultureTable table;
            SW_EXPECT_TRUE( table.loadFromAsset( sw::AssetID{ "engine/localization/engine.cultures.xml" } ) );
            return table;
        }

        /** @brief 엔진 시간대 표입니다(`EngineDefaultAssets::_timeZoneTable` 과 같은 파일). */
        static sw::TimeZoneTable loadTimeZones()
        {
            sw::TimeZoneTable table;
            SW_EXPECT_TRUE( table.loadFromAsset( sw::AssetID{ "engine/localization/timezones.xml" } ) );
            return table;
        }

        static sw::string format( sw::string_view culture, sw::string_view pattern, const sw::TextArgumentList& arguments )
        {
            const sw::CultureInfo  empty{};
            const sw::CultureTable cultures = loadCultures();
            const sw::CultureInfo* pCulture = cultures.resolveCulture( culture );
            SW_EXPECT_TRUE( pCulture != nullptr );
            sw::string               text;
            const sw::expected<void> formatted = sw::TextFormatter::format( pattern, arguments, pCulture != nullptr ? *pCulture : empty, text );
            if ( formatted.has_value() == false )
            {
                sw::StringBuilder<sw::constant::buffer::kMax256> builder;
                builder.appendFormat( "<error: %#>", formatted.error().detail() );
                text = sw::string( builder.view() );
            }
            return text;
        }

        static sw::string ordinal( sw::string_view culture, sw::string_view pattern, int64 value )
        {
            return format( culture, pattern, sw::TextArgumentList().addInteger( "n", value ) );
        }

        /** @brief UTC 날짜 · 시각의 유닉스 시각(초)입니다. */
        static int64 makeUnixSeconds( int32 year, uint8 month, uint8 day, uint8 hour, uint8 minute, uint8 second )
        {
            sw::TextDateTime value;
            value._year   = year;
            value._month  = month;
            value._day    = day;
            value._hour   = hour;
            value._minute = minute;
            value._second = second;
            return sw::TimeZoneInfo::computeUnixSeconds( value );
        }

        static int32 offsetAt( const utf8* pZone, int64 unixSeconds )
        {
            const sw::TimeZoneTable zones = loadTimeZones();
            const sw::TimeZoneInfo* pInfo = zones.findZone( pZone );
            return pInfo != nullptr ? sw::TimeZoneInfo::computeOffsetMinutes( unixSeconds, *pInfo ) : -9999;
        }
    };
} // namespace

/**
 * @brief [SelectOrdinalTest] 영어는 1st 2nd 3rd 4th, 11 · 12 · 13 은 th, 21 · 22 · 23 · 101 은 다시 st nd rd
 */
SW_TEST_CASE( SelectOrdinalTest, EnglishSuffixes )
{
    using Internal                 = LocalizationFormatsTestInternal;
    const sw::string_view kPattern = "{n, selectordinal, one {#st} two {#nd} few {#rd} other {#th}}";
    SW_EXPECT_EQUAL( sw::string( "1st" ), Internal::ordinal( "en", kPattern, 1 ) );
    SW_EXPECT_EQUAL( sw::string( "2nd" ), Internal::ordinal( "en", kPattern, 2 ) );
    SW_EXPECT_EQUAL( sw::string( "3rd" ), Internal::ordinal( "en", kPattern, 3 ) );
    SW_EXPECT_EQUAL( sw::string( "4th" ), Internal::ordinal( "en", kPattern, 4 ) );
    SW_EXPECT_EQUAL( sw::string( "11th" ), Internal::ordinal( "en", kPattern, 11 ) );
    SW_EXPECT_EQUAL( sw::string( "12th" ), Internal::ordinal( "en", kPattern, 12 ) );
    SW_EXPECT_EQUAL( sw::string( "13th" ), Internal::ordinal( "en", kPattern, 13 ) );
    SW_EXPECT_EQUAL( sw::string( "21st" ), Internal::ordinal( "en", kPattern, 21 ) );
    SW_EXPECT_EQUAL( sw::string( "22nd" ), Internal::ordinal( "en", kPattern, 22 ) );
    SW_EXPECT_EQUAL( sw::string( "23rd" ), Internal::ordinal( "en", kPattern, 23 ) );
    SW_EXPECT_EQUAL( sw::string( "101st" ), Internal::ordinal( "en", kPattern, 101 ) );
    SW_EXPECT_EQUAL( sw::string( "111th" ), Internal::ordinal( "en", kPattern, 111 ) );
    SW_EXPECT_EQUAL( sw::string( "2nd" ), Internal::ordinal( "en_gb", kPattern, 2 ) ); // en 에서 물려받는다
}

/**
 * @brief [SelectOrdinalTest] 서수 규칙은 복수형 규칙과 따로다 — 프랑스어 1er · 2e, 한국어는 other 하나, 독일어(복수형은 english)도 서수는 other
 */
SW_TEST_CASE( SelectOrdinalTest, OrdinalRuleIsSeparateFromPlural )
{
    using Internal = LocalizationFormatsTestInternal;
    SW_EXPECT_EQUAL( sw::string( "1er" ), Internal::ordinal( "fr", "{n, selectordinal, one {#er} other {#e}}", 1 ) );
    SW_EXPECT_EQUAL( sw::string( "2e" ), Internal::ordinal( "fr", "{n, selectordinal, one {#er} other {#e}}", 2 ) );
    SW_EXPECT_EQUAL( sw::string( "3번째" ), Internal::ordinal( "ko", "{n, selectordinal, other {#번째}}", 3 ) );
    SW_EXPECT_EQUAL( sw::string( "1." ), Internal::ordinal( "de", "{n, selectordinal, one {#st} other {#.}}", 1 ) );
    // 스페인어 서수(CLDR ordinals.xml `n % 10 = 1,3 and n % 100 != 11`): 1 · 3 · 13 · 21 이 one(13.er = decimotercero), 11 만 other
    SW_EXPECT_EQUAL( sw::string( "3.er" ), Internal::ordinal( "es", "{n, selectordinal, one {#.er} other {#.º}}", 3 ) );
    SW_EXPECT_EQUAL( sw::string( "13.er" ), Internal::ordinal( "es", "{n, selectordinal, one {#.er} other {#.º}}", 13 ) );
    SW_EXPECT_EQUAL( sw::string( "11.º" ), Internal::ordinal( "es", "{n, selectordinal, one {#.er} other {#.º}}", 11 ) );
}

/**
 * @brief [SelectOrdinalTest] other 가 없거나 모르는 범주면 구문 오류이고, 서수에는 offset 이 없다
 */
SW_TEST_CASE( SelectOrdinalTest, SyntaxIsChecked )
{
    const sw::expected<void> missingOther = sw::TextFormatter::validatePattern( "{n, selectordinal, one {#st}}" );
    SW_EXPECT_FALSE( missingOther );
    SW_EXPECT_TRUE( missingOther.has_value() == false && missingOther.error().detail().find( "other" ) != sw::string_view::npos );
    SW_EXPECT_FALSE( sw::TextFormatter::validatePattern( "{n, selectordinal, tenth {#} other {#}}" ) );
    SW_EXPECT_TRUE( sw::TextFormatter::validatePattern( "{n, selectordinal, one {#st} other {#th}}" ) );
    SW_EXPECT_FALSE( sw::TextFormatter::validatePattern( "{n, selectordinal, offset:1 other {#th}}" ) );

    sw::vector<sw::string> listName;
    sw::TextFormatter::collectArgumentNames( "{rank, selectordinal, one {#st} other {#th}} of {total}", listName );
    SW_EXPECT_EQUAL( size_t( 2 ), listName.size() );
}

/**
 * @brief [CurrencyFormatTest] 문화권마다 기호 위치 · 구분 기호 · 소수 자리가 다르다 — 통화가 소수 자리를 정한다(KRW 0, USD 2)
 */
SW_TEST_CASE( CurrencyFormatTest, PatternsAndMinorDigits )
{
    using Internal                     = LocalizationFormatsTestInternal;
    const sw::TextArgumentList decimal = sw::TextArgumentList().addDecimal( "p", 1234.6 );
    SW_EXPECT_EQUAL( sw::string( "$1,234.60" ), Internal::format( "en", "{p, number, currency}", decimal ) );
    SW_EXPECT_EQUAL( sw::string( "£1,234.60" ), Internal::format( "en_gb", "{p, number, currency}", decimal ) );
    SW_EXPECT_EQUAL( sw::string( "1.234,60 €" ), Internal::format( "de", "{p, number, currency}", decimal ) );
    SW_EXPECT_EQUAL( sw::string( "₩1,235" ), Internal::format( "ko", "{p, number, currency}", decimal ) ); // 소수 0 자리 — 반올림
    SW_EXPECT_EQUAL( sw::string( "₩15,000" ), Internal::format( "ko", "{p, number, currency}", sw::TextArgumentList().addInteger( "p", 15000 ) ) );
    SW_EXPECT_EQUAL( sw::string( "-$3.00" ), Internal::format( "en", "{p, number, currency}", sw::TextArgumentList().addInteger( "p", -3 ) ) );
}

/**
 * @brief [CurrencyFormatTest] `::currency/코드` 스켈레톤이 문화권의 기본 통화를 덮고, 모르는 코드는 코드를 기호 자리에 쓴다
 */
SW_TEST_CASE( CurrencyFormatTest, SkeletonOverridesCultureCurrency )
{
    using Internal                    = LocalizationFormatsTestInternal;
    const sw::TextArgumentList amount = sw::TextArgumentList().addInteger( "p", 1234 );
    SW_EXPECT_EQUAL( sw::string( "¥1,234" ), Internal::format( "en", "{p, number, ::currency/JPY}", amount ) );
    SW_EXPECT_EQUAL( sw::string( "1.234,00 €" ), Internal::format( "de", "{p, number, ::currency/EUR}", amount ) );
    SW_EXPECT_EQUAL( sw::string( "XYZ1,234.00" ), Internal::format( "en", "{p, number, ::currency/XYZ}", amount ) );

    const sw::CultureTable  cultures = Internal::loadCultures();
    const sw::CurrencyInfo* pWon     = cultures.findCurrency( "KRW" );
    const sw::CurrencyInfo* pDollar  = cultures.findCurrency( "USD" );
    SW_REQUIRE_NOT_NULL( pWon );
    SW_REQUIRE_NOT_NULL( pDollar );
    SW_EXPECT_EQUAL( uint8( 0 ), pWon->_minorDigitCount );
    SW_EXPECT_EQUAL( uint8( 2 ), pDollar->_minorDigitCount );
    SW_EXPECT_NULL( cultures.findCurrency( "XYZ" ) );
}

/**
 * @brief [CultureDataTest] 복수형 규칙은 CLDR 규칙 글이다 — 옛 코드 규칙(러시아어 · 폴란드어 · 아랍어 · 히브리어 · 페르시아어 · 체코어)과 같은 범주를 고른다
 */
SW_TEST_CASE( CultureDataTest, PluralRuleTextSelectsTheCldrCategory )
{
    const sw::CultureTable cultures = LocalizationFormatsTestInternal::loadCultures();
    const auto             plural   = [&cultures]( sw::string_view code, const sw::PluralOperands& operands )
    {
        const sw::CultureInfo* pCulture = cultures.findCulture( code );
        return pCulture != nullptr ? sw::CultureInfo::getPluralKeyword( pCulture->selectPlural( operands ) ) : "missing";
    };
    using Operands = sw::PluralOperands;
    SW_EXPECT_EQUAL( sw::string( "one" ), sw::string( plural( "ru", Operands::makeInteger( 21 ) ) ) );
    SW_EXPECT_EQUAL( sw::string( "few" ), sw::string( plural( "ru", Operands::makeInteger( 22 ) ) ) );
    SW_EXPECT_EQUAL( sw::string( "many" ), sw::string( plural( "ru", Operands::makeInteger( 12 ) ) ) );
    SW_EXPECT_EQUAL( sw::string( "other" ), sw::string( plural( "ru", Operands::makeDecimal( 1.5, 1 ) ) ) );
    SW_EXPECT_EQUAL( sw::string( "many" ), sw::string( plural( "pl", Operands::makeInteger( 5 ) ) ) );
    SW_EXPECT_EQUAL( sw::string( "few" ), sw::string( plural( "pl", Operands::makeInteger( 24 ) ) ) );
    SW_EXPECT_EQUAL( sw::string( "zero" ), sw::string( plural( "ar", Operands::makeInteger( 0 ) ) ) );
    SW_EXPECT_EQUAL( sw::string( "few" ), sw::string( plural( "ar", Operands::makeInteger( 103 ) ) ) );
    SW_EXPECT_EQUAL( sw::string( "many" ), sw::string( plural( "ar", Operands::makeInteger( 111 ) ) ) );
    SW_EXPECT_EQUAL( sw::string( "other" ), sw::string( plural( "ar", Operands::makeDecimal( 3.5, 1 ) ) ) ); // 범위는 정수만 받는다
    SW_EXPECT_EQUAL( sw::string( "one" ), sw::string( plural( "he", Operands::makeDecimal( 0.5, 1 ) ) ) );
    SW_EXPECT_EQUAL( sw::string( "two" ), sw::string( plural( "he", Operands::makeInteger( 2 ) ) ) );
    SW_EXPECT_EQUAL( sw::string( "one" ), sw::string( plural( "fa", Operands::makeDecimal( 1.0, 1 ) ) ) );
    SW_EXPECT_EQUAL( sw::string( "other" ), sw::string( plural( "en", Operands::makeDecimal( 1.0, 1 ) ) ) );   // v = 1 이라 one 이 아니다
    SW_EXPECT_EQUAL( sw::string( "other" ), sw::string( plural( "en", Operands::makeDecimal( 1.999, 0 ) ) ) ); // 보이는 "2" 의 범주다 — 반올림 전 정수부(1)로 고르면 one 이었다(감사 A-18)
    SW_EXPECT_EQUAL( sw::string( "one" ), sw::string( plural( "en", Operands::makeDecimal( 0.6, 0 ) ) ) );     // 보이는 "1"
    SW_EXPECT_EQUAL( sw::string( "other" ), sw::string( plural( "ko", Operands::makeInteger( 1 ) ) ) );
    // CLDR plurals.xml 과 대조해 더한 범주 — 스페인어 · 프랑스어의 백만 단위(un millón de, un million de)는 many 다.
    SW_EXPECT_EQUAL( sw::string( "many" ), sw::string( plural( "es", Operands::makeInteger( 2000000 ) ) ) );
    SW_EXPECT_EQUAL( sw::string( "many" ), sw::string( plural( "fr", Operands::makeInteger( 1000000 ) ) ) );
    SW_EXPECT_EQUAL( sw::string( "other" ), sw::string( plural( "es", Operands::makeInteger( 1000 ) ) ) );
    SW_EXPECT_EQUAL( sw::string( "one" ), sw::string( plural( "fr", Operands::makeDecimal( 1.5, 1 ) ) ) );
}

/**
 * @brief [CultureDataTest] CLDR 보기 글을 피연산자로 읽는다 — 보이는 소수 자리는 글 그대로, 압축 지수(`c`)는 소수점을 옮기고 e 에 남는다
 */
SW_TEST_CASE( CultureDataTest, PluralSampleTextKeepsVisibleDigits )
{
    sw::PluralOperands operands;
    SW_REQUIRE_TRUE( sw::PluralOperands::parseSample( "1.50", operands ) );
    SW_EXPECT_EQUAL( int64( 1 ), operands._integerDigits );
    SW_EXPECT_EQUAL( int64( 50 ), operands._fractionDigits );
    SW_EXPECT_EQUAL( uint32( 2 ), operands._visibleFractionCount );
    SW_REQUIRE_TRUE( sw::PluralOperands::parseSample( "1.0000001c6", operands ) );
    SW_EXPECT_EQUAL( int64( 1000000 ), operands._integerDigits );
    SW_EXPECT_EQUAL( uint32( 1 ), operands._visibleFractionCount );
    SW_EXPECT_EQUAL( uint32( 6 ), operands._exponent );
    SW_REQUIRE_TRUE( sw::PluralOperands::parseSample( "2c3", operands ) );
    SW_EXPECT_EQUAL( int64( 2000 ), operands._integerDigits );
    SW_EXPECT_EQUAL( uint32( 0 ), operands._visibleFractionCount );
    SW_EXPECT_FALSE( sw::PluralOperands::parseSample( "1.2.3", operands ) );
    SW_EXPECT_FALSE( sw::PluralOperands::parseSample( "", operands ) );

    sw::PluralRuleSet rules;
    SW_REQUIRE_TRUE( rules.compileRule( "one", "i = 0..1 @integer 0, 1 @decimal 0.0~1.5" ).has_value() );
    SW_EXPECT_EQUAL( size_t( 18 ), rules.countSamples( sw::PluralCategory::One ) ); // 0, 1 과 0.0 ~ 1.5 의 16 개
    SW_EXPECT_TRUE( rules.verifySamples().has_value() );
}

/**
 * @brief [CultureDataTest] 언어 하나는 데이터 한 장이다 — 새 숫자 체계 · 통화 · 복수형 규칙을 표에 더하면 코드 없이 형식이 선다(힌디어 데바나가리 숫자 · INR)
 */
SW_TEST_CASE( CultureDataTest, NewLanguageIsOneDataSheet )
{
    sw::CultureTable                                table;
    const sw::expected<void>                        loaded = table.loadFromXMLText( R"XML(<CultureTable formatVersion="0">
  <numberingSystems><numberingSystem id="deva" digits="०१२३४५६७८९" /></numberingSystems>
  <currencies><currency code="INR" symbol="₹" digits="2" /></currencies>
  <CultureInfo _code="hi" _numberingSystem="deva" _nativeName="हिन्दी" _decimalSeparator="." _groupSeparator="," _percentPattern="#%" _currencyCode="INR" _currencyPattern="¤#">
    <_pluralRules><entry key="one">i = 0 or n = 1 @integer 0, 1 @decimal 0.0~1.0, 0.00~0.04</entry><entry key="other">@integer 2~17, 100, 1000, 10000, 100000, 1000000, … @decimal 1.1~2.6, 10.0, 100.0, 1000.0, 10000.0, 100000.0, 1000000.0, …</entry></_pluralRules>
    <_datePatterns _short="dd/MM/yy" _medium="d MMM y" _long="d MMMM y" />
    <_timePatterns _short="HH:mm" _medium="HH:mm:ss" />
  </CultureInfo>
</CultureTable>)XML",
                                                                                    "hi.cultures.xml" );
    sw::fixed_string<sw::constant::buffer::kMax512> loadError;
    if ( loaded.has_value() == false )
        sw::formatstring( loadError.data(), loadError.capacity(), "%# (%#)", loaded.error().message(), loaded.error().detail() );
    SW_REQUIRE_TRUE_MSG( loaded.has_value(), loadError );
    const sw::CultureInfo* pHindi = table.findCulture( "hi" );
    SW_REQUIRE_NOT_NULL( pHindi );
    SW_EXPECT_EQUAL( sw::string( "१,२३४" ), pHindi->formatInteger( 1234 ) );
    SW_EXPECT_EQUAL( sw::string( "₹१२.५०" ), pHindi->formatCurrency( 12.5, {} ) );
    SW_EXPECT_EQUAL( sw::string( "one" ), sw::string( sw::CultureInfo::getPluralKeyword( pHindi->selectPlural( sw::PluralOperands::makeDecimal( 0.5, 1 ) ) ) ) );
}

/**
 * @brief [CultureDataTest] 틀린 문화권 데이터는 읽기 오류이고 표는 그대로다 — 규칙 글 · 범주 · 숫자 체계 · 통화 · 뿌리 문화권의 필수 칸
 */
SW_TEST_CASE( CultureDataTest, MalformedDataIsALoadError )
{
    const auto rejects = []( sw::string_view xml, sw::string_view expectedWord )
    {
        sw::CultureTable         table;
        const sw::expected<void> loaded = table.loadFromXMLText( xml, "bad.cultures.xml" );
        SW_EXPECT_FALSE( loaded );
        sw::fixed_string<sw::constant::buffer::kMax512> error;
        if ( loaded.has_value() == false )
            sw::formatstring( error.data(), error.capacity(), "%# (%#)", loaded.error().message(), loaded.error().detail() );
        SW_EXPECT_TRUE_MSG( error.view().find( expectedWord ) != sw::string_view::npos, error.c_str() );
        SW_EXPECT_EQUAL( size_t( 0 ), table.getCultureCount() );
    };
    static constexpr const utf8* kLatin         = R"(<numberingSystems><numberingSystem id="latn" digits="0123456789" /></numberingSystems>)";
    const auto                   rejectsCulture = [&rejects]( sw::string_view numberingSystem, sw::string_view rules, sw::string_view expectedWord )
    {
        sw::StringBuilder<sw::constant::buffer::kMax2048> xml;
        xml.appendFormat( R"(<CultureTable formatVersion="0">%#<CultureInfo _code="xx" _numberingSystem="%#" _decimalSeparator="." _groupSeparator="," _percentPattern="#%" _currencyCode="USD" _currencyPattern="¤#">%#<_datePatterns _short="yy" _medium="y" _long="yyyy" /><_timePatterns _short="HH:mm" _medium="HH:mm:ss" /></CultureInfo></CultureTable>)",
                          kLatin, numberingSystem, rules );
        rejects( xml.view(), expectedWord );
    };
    const auto rejectsLatin = [&rejectsCulture]( sw::string_view rules, sw::string_view expectedWord )
    { rejectsCulture( "latn", rules, expectedWord ); };
    rejectsLatin( R"(<_pluralRules><entry key="one">i = = 1</entry></_pluralRules>)", "one rule" );
    rejectsLatin( R"(<_pluralRules><entry key="several">n = 1 @integer 1</entry></_pluralRules>)", "unknown plural category" );
    rejectsLatin( R"(<_pluralRules><entry key="other">n = 1 @integer 1</entry></_pluralRules>)", "samples only" );
    rejectsLatin( R"(<_pluralRules><entry key="one">w = 0 @integer 0</entry></_pluralRules>)", "operand" );
    // CLDR 글은 보기째 붙여 넣는다 — 보기 없는 규칙, other 를 뺀 규칙, 보기와 어긋나게 고친 규칙(pl few 의 12..14 를 지운 것)은 읽기 오류다.
    rejectsLatin( R"(<_pluralRules><entry key="one">i = 1 and v = 0</entry><entry key="other">@integer 0</entry></_pluralRules>)", "samples" );
    rejectsLatin( R"(<_pluralRules><entry key="one">i = 1 and v = 0 @integer 1</entry></_pluralRules>)", "other samples" );
    rejectsLatin(
        R"(<_pluralRules><entry key="few">v = 0 and i % 10 = 2..4 @integer 2~4, 22~24</entry><entry key="many">v = 0 and i % 100 = 12..14 @integer 12~14</entry><entry key="other">@decimal 0.0~1.5</entry></_pluralRules>)",
        "does not match" );
    rejectsLatin( "", "pluralRules" );
    rejectsCulture( "thai", "<_pluralRules></_pluralRules>", "thai" );
    rejects( R"(<CultureTable formatVersion="0"><numberingSystems><numberingSystem id="short" digits="012" /></numberingSystems></CultureTable>)", "10 digits" );
    rejects( R"(<CultureTable formatVersion="0"><currencies><currency code="KRW" symbol="₩" /></currencies></CultureTable>)", "currency" );
    rejects( R"(<CultureTable formatVersion="0"><currencies><currency code="KRW" symbol="₩" digits="0" digit="1" /></currencies></CultureTable>)", "digit" ); // 모르는 속성(오타)이 조용히 지나가지 않는다
    rejects( R"(<CultureTable formatVersion="0"><currencies><currency code="KRW" symbol="₩" digits="5" /></currencies></CultureTable>)", "0..4" );
    rejects( R"(<CultureTable formatVersion="0"><numberingSystems><numberingSystem id="latn" digits="0123456789" order="1" /></numberingSystems></CultureTable>)", "order" );
    rejects( R"(<CultureTable formatVersion="0"><calendars /></CultureTable>)", "calendars" );
    // 판 칸이 없는 글은 첫 판(0)이 아니다
    SW_EXPECT_LOG( "AssetFormat.AssetFormatVersionMissing" );
    rejects( "<CultureTable/>", "format version" );
}

/**
 * @brief [TimeZoneTest] 고정 오프셋 시간대와 UTC → 현지 날짜 변환(날짜가 넘어가는 경우 포함)
 */
SW_TEST_CASE( TimeZoneTest, FixedOffsetConversion )
{
    const sw::TimeZoneTable zones    = LocalizationFormatsTestInternal::loadTimeZones();
    const sw::TimeZoneInfo* pSeoul   = zones.findZone( "Asia/Seoul" );
    const sw::TimeZoneInfo* pKolkata = zones.findZone( "Asia/Kolkata" );
    const sw::TimeZoneInfo* pUTC     = zones.findZone( "UTC" );
    SW_REQUIRE_NOT_NULL( pSeoul );
    SW_REQUIRE_NOT_NULL( pKolkata );
    SW_REQUIRE_NOT_NULL( pUTC );
    const int64            utc   = LocalizationFormatsTestInternal::makeUnixSeconds( 2026, 12, 31, 20, 30, 15 );
    const sw::TextDateTime local = sw::TimeZoneInfo::convertUnixMilliseconds( utc * 1000 + 999, *pSeoul );
    SW_EXPECT_EQUAL( 2027, local._year ); // 날짜가 넘어간다
    SW_EXPECT_EQUAL( uint8( 1 ), local._month );
    SW_EXPECT_EQUAL( uint8( 1 ), local._day );
    SW_EXPECT_EQUAL( uint8( 5 ), local._hour );
    SW_EXPECT_EQUAL( uint8( 30 ), local._minute );
    SW_EXPECT_EQUAL( uint8( 15 ), local._second );
    SW_EXPECT_TRUE( local._bZoned );
    SW_EXPECT_EQUAL( int16( 540 ), local._utcOffsetMinutes );

    const sw::TextDateTime kolkata = sw::TimeZoneInfo::convertUnixMilliseconds( utc * 1000, *pKolkata );
    SW_EXPECT_EQUAL( uint8( 2 ), kolkata._hour ); // 20:30 + 5:30 = 02:00
    SW_EXPECT_EQUAL( uint8( 0 ), kolkata._minute );

    SW_EXPECT_NULL( zones.findZone( "Mars/Olympus" ) );
    SW_EXPECT_EQUAL( utc, sw::TimeZoneInfo::computeUnixSeconds( sw::TimeZoneInfo::convertUnixMilliseconds( utc * 1000, *pUTC ) ) );
}

/**
 * @brief [TimeZoneTest] 시간대 표는 이름이 겹치거나 UTC 가 없거나 칸이 빠지면 읽지 않고(표는 빈 채), 관리자는 표에 없는 이름을 UTC 로 바꾸지 않는다
 */
SW_TEST_CASE( TimeZoneTest, TableRejectsBadRowsAndUnknownNamesAreNotFound )
{
    const sw::test::ScopedScratchDomain domain;
    const sw::AbsolutePath              folder     = domain.getFolder();
    const auto                          writeTable = [&folder]( const utf8* pFileName, sw::string_view rows ) -> sw::AssetID
    {
        const sw::AbsolutePath                            path = folder.join( pFileName );
        sw::StringBuilder<sw::constant::buffer::kMax1024> text;
        text.appendFormat( "<TimeZoneTable><_listZone>%#</_listZone></TimeZoneTable>", rows );
        SW_EXPECT_TRUE( sw::FileUtil::writeTextFile( path, text.view() ) );
        return sw::test::ScopedScratchDomain::toID( path );
    };
    const sw::string utcRow = R"(<TimeZoneInfo _name="UTC" _standardOffsetMinutes="0" _daylightRule="None" />)";

    sw::TimeZoneTable table;
    SW_EXPECT_LOG( "TimeZoneTable.DuplicateName" );
    sw::StringBuilder<sw::constant::buffer::kMax256> duplicateRows;
    duplicateRows.appendFormat( "%#%#", utcRow, utcRow );
    SW_EXPECT_FALSE( table.loadFromAsset( writeTable( "duplicate.xml", duplicateRows.view() ) ) );
    SW_EXPECT_NULL( table.findZone( "UTC" ) );
    SW_EXPECT_LOG( "TimeZoneTable.MissingUTC" );
    SW_EXPECT_FALSE( table.loadFromAsset( writeTable( "noutc.xml", R"(<TimeZoneInfo _name="Asia/Seoul" _standardOffsetMinutes="540" _daylightRule="None" />)" ) ) );
    SW_EXPECT_LOG( "Serialization.RequiredMissing" );
    SW_EXPECT_LOG( "TimeZoneTable.CannotRead" );
    SW_EXPECT_FALSE( table.loadFromAsset( writeTable( "missing.xml", R"(<TimeZoneInfo _name="UTC" _daylightRule="None" />)" ) ) );

    sw::LocalizationManager localization;
    SW_REQUIRE_TRUE( localization.loadTimeZoneTable( sw::AssetID{ "engine/localization/timezones.xml" } ) );
    sw::TimeZoneInfo zone;
    SW_REQUIRE_TRUE( localization.findTimeZone( "Asia/Kolkata", zone ) );
    SW_EXPECT_EQUAL( int16( 330 ), zone._standardOffsetMinutes );
    SW_EXPECT_FALSE( localization.findTimeZone( "Mars/Olympus", zone ) );
    SW_EXPECT_EQUAL( sw::string( "Asia/Kolkata" ), zone._name ); // 못 찾으면 그대로
}

/**
 * @brief [TimeZoneTest] 1970 이전 시각도 내림으로 계산한다(1969-12-31 23:59:59 UTC)
 */
SW_TEST_CASE( TimeZoneTest, NegativeUnixTimeFloors )
{
    const sw::TimeZoneTable zones = LocalizationFormatsTestInternal::loadTimeZones();
    const sw::TimeZoneInfo* pUTC  = zones.findZone( "UTC" );
    SW_REQUIRE_NOT_NULL( pUTC );
    const sw::TextDateTime value = sw::TimeZoneInfo::convertUnixMilliseconds( -1, *pUTC );
    SW_EXPECT_EQUAL( 1969, value._year );
    SW_EXPECT_EQUAL( uint8( 12 ), value._month );
    SW_EXPECT_EQUAL( uint8( 31 ), value._day );
    SW_EXPECT_EQUAL( uint8( 23 ), value._hour );
    SW_EXPECT_EQUAL( uint8( 59 ), value._second );
}

/**
 * @brief [TimeZoneTest] 미국 일광 절약 — 2026-03-08 02:00 EST(07:00Z) 시작, 2026-11-01 02:00 EDT(06:00Z) 끝
 */
SW_TEST_CASE( TimeZoneTest, UnitedStatesDaylightSavingBoundaries )
{
    using Internal = LocalizationFormatsTestInternal;
    SW_EXPECT_EQUAL( -300, Internal::offsetAt( "America/New_York", Internal::makeUnixSeconds( 2026, 3, 8, 6, 59, 59 ) ) );
    SW_EXPECT_EQUAL( -240, Internal::offsetAt( "America/New_York", Internal::makeUnixSeconds( 2026, 3, 8, 7, 0, 0 ) ) );
    SW_EXPECT_EQUAL( -240, Internal::offsetAt( "America/New_York", Internal::makeUnixSeconds( 2026, 11, 1, 5, 59, 59 ) ) );
    SW_EXPECT_EQUAL( -300, Internal::offsetAt( "America/New_York", Internal::makeUnixSeconds( 2026, 11, 1, 6, 0, 0 ) ) );
    SW_EXPECT_EQUAL( -420, Internal::offsetAt( "America/Los_Angeles", Internal::makeUnixSeconds( 2026, 7, 4, 16, 0, 0 ) ) ); // 여름 -480 + 60
    SW_EXPECT_EQUAL( -480, Internal::offsetAt( "America/Los_Angeles", Internal::makeUnixSeconds( 2026, 1, 15, 16, 0, 0 ) ) );

    const sw::TimeZoneTable zones    = LocalizationFormatsTestInternal::loadTimeZones();
    const sw::TimeZoneInfo* pNewYork = zones.findZone( "America/New_York" );
    SW_REQUIRE_NOT_NULL( pNewYork );
    const sw::TextDateTime summer = sw::TimeZoneInfo::convertUnixMilliseconds( Internal::makeUnixSeconds( 2026, 7, 4, 16, 0, 0 ) * 1000, *pNewYork );
    SW_EXPECT_EQUAL( uint8( 12 ), summer._hour );
}

/**
 * @brief [TimeZoneTest] 유럽 일광 절약 — 2026-03-29 01:00Z 시작, 2026-10-25 01:00Z 끝(전 지역 같은 순간)
 */
SW_TEST_CASE( TimeZoneTest, EuropeDaylightSavingBoundaries )
{
    using Internal = LocalizationFormatsTestInternal;
    SW_EXPECT_EQUAL( 0, Internal::offsetAt( "Europe/London", Internal::makeUnixSeconds( 2026, 3, 29, 0, 59, 59 ) ) );
    SW_EXPECT_EQUAL( 60, Internal::offsetAt( "Europe/London", Internal::makeUnixSeconds( 2026, 3, 29, 1, 0, 0 ) ) );
    SW_EXPECT_EQUAL( 120, Internal::offsetAt( "Europe/Berlin", Internal::makeUnixSeconds( 2026, 3, 29, 1, 0, 0 ) ) );
    SW_EXPECT_EQUAL( 60, Internal::offsetAt( "Europe/London", Internal::makeUnixSeconds( 2026, 10, 25, 0, 59, 59 ) ) );
    SW_EXPECT_EQUAL( 0, Internal::offsetAt( "Europe/London", Internal::makeUnixSeconds( 2026, 10, 25, 1, 0, 0 ) ) );
    SW_EXPECT_EQUAL( 60, Internal::offsetAt( "Europe/Berlin", Internal::makeUnixSeconds( 2026, 10, 25, 1, 0, 0 ) ) );
}

/**
 * @brief [TimeZoneTest] 오프셋 글(UTC+9 · UTC+5:30 · UTC-3 · 맞춘 UTC+09:00)과 포맷 패턴의 z · 시각 long 스타일
 */
SW_TEST_CASE( TimeZoneTest, OffsetTextAndFormatterIntegration )
{
    using Internal = LocalizationFormatsTestInternal;
    SW_EXPECT_EQUAL( sw::string( "UTC" ), sw::TimeZoneInfo::formatOffset( 0, false ) );
    SW_EXPECT_EQUAL( sw::string( "UTC+9" ), sw::TimeZoneInfo::formatOffset( 540, false ) );
    SW_EXPECT_EQUAL( sw::string( "UTC+5:30" ), sw::TimeZoneInfo::formatOffset( 330, false ) );
    SW_EXPECT_EQUAL( sw::string( "UTC-3" ), sw::TimeZoneInfo::formatOffset( -180, false ) );
    SW_EXPECT_EQUAL( sw::string( "UTC+09:00" ), sw::TimeZoneInfo::formatOffset( 540, true ) );

    const sw::TimeZoneTable zones  = Internal::loadTimeZones();
    const sw::TimeZoneInfo* pSeoul = zones.findZone( "Asia/Seoul" );
    SW_REQUIRE_NOT_NULL( pSeoul );
    const int64      utc   = Internal::makeUnixSeconds( 2026, 10, 4, 4, 5, 9 ) * 1000;
    const sw::string zoned = Internal::format( "en", "{d, time, long}", sw::TextArgumentList().addUnixTime( "d", utc, *pSeoul ) );
    SW_EXPECT_TRUE( zoned.find( "1:05:09" ) != sw::string::npos ); // 04:05:09Z → 13:05:09 서울
    SW_EXPECT_TRUE( zoned.find( "UTC+9" ) != sw::string::npos );

    const sw::string plain = Internal::format( "en", "{d, time, medium}", sw::TextArgumentList().addUnixTime( "d", utc, *pSeoul ) );
    SW_EXPECT_TRUE( plain.find( "UTC" ) == sw::string::npos ); // medium 은 오프셋을 붙이지 않는다

    const sw::CultureTable cultures = Internal::loadCultures();
    const sw::CultureInfo* pEnglish = cultures.findCulture( "en" );
    SW_REQUIRE_NOT_NULL( pEnglish );
    sw::TextDateTime wallClock;
    wallClock._hour = 13;
    SW_EXPECT_EQUAL( sw::string( "13 " ), pEnglish->formatDateTime( wallClock, "H z" ) ); // 시간대를 모르는 값은 z 가 비어 있다
}

/**
 * @brief [LocalizationBinaryTableTest] 쓰고 읽으면 같고, 같은 내용이면 같은 바이트이며, 깨진 바이트는 모두 거절한다
 */
SW_TEST_CASE( LocalizationBinaryTableTest, RoundTripAndCorruption )
{
    sw::LocalizationBinaryTable table;
    table.setCulture( "ko" );
    SW_REQUIRE_TRUE( table.setText( "Menu.Start", "시작" ) );
    SW_REQUIRE_TRUE( table.setText( "Menu.Quit", "종료" ) );
    SW_REQUIRE_TRUE( table.setText( "Menu.Empty", "" ) );

    sw::vector<uint8> bytes;
    table.toBytes( bytes );
    sw::vector<uint8> again;
    table.toBytes( again );
    SW_EXPECT_TRUE( bytes == again );

    sw::LocalizationBinaryTable loaded;
    SW_REQUIRE_TRUE( loaded.parse( bytes.data(), bytes.size() ) );
    SW_EXPECT_EQUAL( sw::string( "ko" ), loaded.getCulture() );
    SW_REQUIRE_EQUAL( size_t( 3 ), loaded.getTexts().size() );
    const auto startIt = loaded.getTexts().find( sw::LocalizationKeyIndex::computeHash( "Menu.Start" ) );
    const auto emptyIt = loaded.getTexts().find( sw::LocalizationKeyIndex::computeHash( "Menu.Empty" ) );
    SW_REQUIRE_TRUE( startIt != loaded.getTexts().end() && emptyIt != loaded.getTexts().end() );
    SW_EXPECT_EQUAL( sw::string( "시작" ), startIt->second );
    SW_EXPECT_TRUE( emptyIt->second.empty() );

    // 글 한 바이트를 바꾸면 검사합이 거절한다 — 일부만 적용되지 않는다
    sw::vector<uint8> flipped = bytes;
    flipped[flipped.size() / 2] ^= 0x01u;
    SW_EXPECT_FALSE( loaded.parse( flipped.data(), flipped.size() ) );
    SW_EXPECT_TRUE( loaded.getTexts().empty() );

    sw::vector<uint8> truncated( bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>( bytes.size() - 3 ) );
    SW_EXPECT_FALSE( loaded.parse( truncated.data(), truncated.size() ) );
    SW_EXPECT_FALSE( loaded.parse( bytes.data(), 5 ) );

    sw::vector<uint8> wrongTag            = bytes;
    wrongTag[0]                           = 'X';
    const sw::expected<void> wrongTagRead = loaded.parse( wrongTag.data(), wrongTag.size() );
    SW_EXPECT_FALSE( wrongTagRead );
    SW_EXPECT_TRUE( wrongTagRead.has_value() == false && wrongTagRead.error().message().find( "another format" ) != sw::string_view::npos );

    // 어느 길이로 잘려도 거절한다(끝의 검사합이 먼저 지지만 길이가 머리보다 짧으면 거기서 진다).
    for ( size_t length = 0; length < bytes.size(); ++length )
    {
        SW_EXPECT_FALSE( loaded.parse( bytes.data(), length ) );
        SW_EXPECT_TRUE( loaded.getTexts().empty() );
    }
}

/**
 * @brief [LocalizationBinaryTableTest] 매니저가 프로젝트에서 화면에 나오는 글만 베이크하고(낡은 번역 제외), 저작 파일 없이 그 바이트만으로 같은 글을 낸다
 */
SW_TEST_CASE( LocalizationBinaryTableTest, ManagerCooksAndLoadsWithoutAuthoringFiles )
{
    using sw::test::LocalizationTestUtil;
    const sw::test::ScopedScratchDomain domain;
    const sw::AbsolutePath              folder  = domain.getFolder();
    const sw::AbsolutePath              project = LocalizationTestUtil::writeProject( folder, "en", R"([ "ko" ])" );
    LocalizationTestUtil::writeSourceTable( folder, "en", R"("ui.start": { "source": "Start" }, "ui.quit": { "source": "Quit" })" );
    const sw::string                                 staleHash = sw::SourceTextEntry::formatSourceHash( sw::SourceTextEntry::computeSourceHash( "Begin" ) );
    sw::StringBuilder<sw::constant::buffer::kMax256> koEntries;
    koEntries.appendFormat( "\"ui.start\": { \"text\": \"시작\" }, \"ui.quit\": { \"text\": \"종료\", \"sourceHash\": \"%#\" }", staleHash );
    LocalizationTestUtil::writeTranslation( folder, "ko", koEntries.view() );

    sw::LocalizationManager cooker;
    SW_REQUIRE_TRUE( cooker.mountProject( sw::test::ScopedScratchDomain::toID( project ), sw::LocalizationScope::Game ) );
    sw::vector<uint8> bytes;
    SW_REQUIRE_TRUE( cooker.cookBinaryTable( "ko", bytes ) );
    sw::vector<uint8> none;
    SW_EXPECT_FALSE( cooker.cookBinaryTable( "fr", none ) ); // 글이 없는 문화권

    sw::LocalizationManager runtime; // 프로젝트를 올리지 않은 배포본
    SW_REQUIRE_TRUE( runtime.loadBinaryTable( bytes.data(), bytes.size() ) );
    SW_REQUIRE_TRUE( runtime.setCurrentLanguage( "ko" ) );
    SW_REQUIRE_NOT_NULL( runtime.findStringByText( "ui.start" ) );
    SW_EXPECT_EQUAL( sw::string( "시작" ), sw::string( runtime.findStringByText( "ui.start" ) ) );
    SW_EXPECT_NULL( runtime.findStringByText( "ui.quit" ) ); // 낡은 번역은 베이크하지 않았다

    // 깨진 표는 거절하고 이미 있는 글을 바꾸지 않는다
    sw::vector<uint8> broken = bytes;
    broken[broken.size() - 1] ^= 0xFFu;
    SW_EXPECT_LOG( "Localization.BinaryTableRejected" );
    SW_EXPECT_FALSE( runtime.loadBinaryTable( broken.data(), broken.size() ) );
    SW_EXPECT_EQUAL( sw::string( "시작" ), sw::string( runtime.findStringByText( "ui.start" ) ) );

    runtime.unloadLanguage( "ko" );
    SW_EXPECT_NULL( runtime.findStringByText( "ui.start" ) );
}

/**
 * @brief [LocalizationBinaryTableTest] 32 비트 이름 해시가 같은 두 키를 쿠킹해도 서로 덮지 않고, 매니저가 둘을 따로 낸다(감사 C-8)
 */
SW_TEST_CASE( LocalizationBinaryTableTest, KeysSharingThe32BitHashStayDistinct )
{
    // 대소문자 무시 FNV-1a 32 비트가 같은 쌍이다(생일 탐색으로 찾았다) — 예전 실행 표는 나중 것이 이겼다.
    constexpr const utf8* kKeyA = "ui.key1022789";
    constexpr const utf8* kKeyB = "ui.key1239192";
    SW_REQUIRE_EQUAL( sw::hashed_string::computeHash( kKeyA ), sw::hashed_string::computeHash( kKeyB ) );
    SW_REQUIRE_TRUE( sw::LocalizationKeyIndex::computeHash( kKeyA ) != sw::LocalizationKeyIndex::computeHash( kKeyB ) );

    sw::LocalizationBinaryTable table;
    table.setCulture( "ko" );
    SW_REQUIRE_TRUE( table.setText( kKeyA, "가" ) );
    SW_REQUIRE_TRUE( table.setText( kKeyB, "나" ) );
    SW_EXPECT_EQUAL( size_t( 2 ), table.getTexts().size() );

    sw::vector<uint8> bytes;
    table.toBytes( bytes );
    sw::LocalizationManager runtime;
    SW_REQUIRE_TRUE( runtime.loadBinaryTable( bytes.data(), bytes.size() ) );
    SW_REQUIRE_TRUE( runtime.setCurrentLanguage( "ko" ) );
    SW_REQUIRE_NOT_NULL( runtime.findStringByText( kKeyA ) );
    SW_REQUIRE_NOT_NULL( runtime.findStringByText( kKeyB ) );
    SW_EXPECT_EQUAL( sw::string( "가" ), sw::string( runtime.findStringByText( kKeyA ) ) );
    SW_EXPECT_EQUAL( sw::string( "나" ), sw::string( runtime.findStringByText( kKeyB ) ) );

    // 낱개 표(키 글이 있는 읽기 길)도 둘을 따로 받는다 — 충돌 검사가 32 비트 쌍을 거절하지 않는다.
    sw::LocalizationManager loose;
    loose.setString( "en", sw::hashed_string( kKeyA ), "A" );
    loose.setString( "en", sw::hashed_string( kKeyB ), "B" );
    SW_REQUIRE_TRUE( loose.setCurrentLanguage( "en" ) );
    SW_REQUIRE_NOT_NULL( loose.findString( sw::hashed_string( kKeyA ) ) );
    SW_REQUIRE_NOT_NULL( loose.findString( sw::hashed_string( kKeyB ) ) );
    SW_EXPECT_EQUAL( sw::string( "A" ), sw::string( loose.findString( sw::hashed_string( kKeyA ) ) ) );
    SW_EXPECT_EQUAL( sw::string( "B" ), sw::string( loose.findString( sw::hashed_string( kKeyB ) ) ) );
}

namespace sw
{
    /** @brief 매니저의 충돌 검사 해시를 바꾸는 테스트 창구입니다(`LocalizationManager` 의 friend). 실제 64 비트 충돌은 찾을 수 없어 검사가 지는 길을 이것으로 지나간다. */
    struct LocalizationManagerTestAccess
    {
        static void setCheckKeyHash( LocalizationManager& manager, LocalizationKeyIndex::KeyHashFunction pKeyHash ) { manager._pCheckKeyHash = pKeyHash; }
        /** @brief 쿠킹본 구성의 올리기 길입니다(실행 구성은 `AssetIO::usesSource` 가 고른다). */
        [[nodiscard]] static bool mountCooked( LocalizationManager& manager, const AssetID& projectID ) { return manager.mountProjectFrom( projectID, LocalizationScope::Game, ContentSource::Cooked ); }
        /** @brief 지금 읽어 둔 쿠킹된 이진 표 수입니다(조회 사슬만큼인지 본다). */
        static size_t countCookedTables( const LocalizationManager& manager )
        {
            ScopedSharedLock<shared_mutex> lock{ manager._mutex };
            size_t                         count = 0;
            for ( const LocalizationManager::MountedProject& project : manager._listProject )
                count += project._mapCookedTable.size();
            return count;
        }
    };
} // namespace sw

/**
 * @brief [LocalizationBinaryTableTest] 쿠커가 원문 · 출시 문화권의 표를 굽고, 쿠킹본 구성은 저작 XML 없이 조회 사슬의 표만 읽는다(감사 A-16)
 */
SW_TEST_CASE( LocalizationBinaryTableTest, CookedProjectReadsOnlyTheLookupChain )
{
    using sw::test::LocalizationTestUtil;
    const sw::test::ScopedScratchDomain domain;
    const sw::AbsolutePath              folder  = domain.getFolder();
    const sw::AbsolutePath              project = LocalizationTestUtil::writeProject( folder, "en", R"([ "ko", "ja" ])", R"("stagedCultures": [ "ko" ])" );
    LocalizationTestUtil::writeSourceTable( folder, "en", R"("ui.start": { "source": "Start" }, "ui.quit": { "source": "Quit" })" );
    const sw::string                                 staleHash = sw::SourceTextEntry::formatSourceHash( sw::SourceTextEntry::computeSourceHash( "Begin" ) );
    sw::StringBuilder<sw::constant::buffer::kMax256> koEntries;
    koEntries.appendFormat( "\"ui.start\": { \"text\": \"시작\" }, \"ui.quit\": { \"text\": \"종료\", \"sourceHash\": \"%#\" }", staleHash );
    LocalizationTestUtil::writeTranslation( folder, "ko", koEntries.view() );
    LocalizationTestUtil::writeTranslation( folder, "ja", R"("ui.start": { "text": "開始" })" );

    // 쿠커와 같은 길: 원문 + 출시 문화권만 굽는다(ja 는 번역 중이라 굽지 않는다).
    sw::LocalizationProject projectDoc;
    SW_REQUIRE_TRUE( projectDoc.loadFromFile( project ) );
    const sw::vector<sw::string> listCulture = projectDoc.collectCookedCultures();
    SW_REQUIRE_EQUAL( size_t( 2 ), listCulture.size() );
    SW_EXPECT_EQUAL( sw::string( "en" ), listCulture[0] );
    SW_EXPECT_EQUAL( sw::string( "ko" ), listCulture[1] );
    const sw::AssetID projectID = sw::test::ScopedScratchDomain::toID( project );
    for ( const sw::string& culture : listCulture )
    {
        sw::vector<uint8>       bytes;
        sw::LocalizationManager cookManager; // 쿠커(DevTools `LocalizationCooker::cookCulture`)와 같은 길 — 프로젝트 하나만 올린 새 관리자
        SW_REQUIRE_TRUE( cookManager.mountProject( projectID, sw::LocalizationScope::Game ) );
        SW_REQUIRE_TRUE( cookManager.cookBinaryTable( culture, bytes ) );
        SW_REQUIRE_TRUE( sw::FileUtil::writeFile( project.getParent().join( sw::FileUtil::getFileNamePart( projectDoc.makeCookedTablePath( "engine/test.locproject.xml", culture ) ) ), bytes.data(), bytes.size() ) );
    }

    // 배포본에는 저작 표가 없다 — 지워도 읽힌다.
    SW_REQUIRE_TRUE( sw::FileUtil::removeFile( folder.join( "test.strings.xml" ) ) );
    SW_REQUIRE_TRUE( sw::FileUtil::removeFile( folder.join( "ko.translation.xml" ) ) );
    SW_REQUIRE_TRUE( sw::FileUtil::removeFile( folder.join( "ja.translation.xml" ) ) );

    sw::LocalizationManager runtime;
    SW_REQUIRE_TRUE( sw::LocalizationManagerTestAccess::mountCooked( runtime, projectID ) );
    SW_EXPECT_TRUE( runtime.hasLanguage( "ko" ) ); // 아직 읽지 않았어도 고를 수 있다
    SW_EXPECT_FALSE( runtime.hasLanguage( "ja" ) );
    SW_REQUIRE_NOT_NULL( runtime.findStringByText( "ui.start" ) );
    SW_EXPECT_EQUAL( sw::string( "Start" ), sw::string( runtime.findStringByText( "ui.start" ) ) );
    SW_EXPECT_EQUAL( size_t( 1 ), sw::LocalizationManagerTestAccess::countCookedTables( runtime ) ); // 사슬은 en 하나

    SW_REQUIRE_TRUE( runtime.setCurrentLanguage( "ko" ) );
    SW_EXPECT_EQUAL( sw::string( "시작" ), sw::string( runtime.findStringByText( "ui.start" ) ) );
    SW_EXPECT_EQUAL( sw::string( "Quit" ), sw::string( runtime.findStringByText( "ui.quit" ) ) ); // 낡은 번역은 굽지 않아 원문으로 넘어간다
    SW_EXPECT_EQUAL( size_t( 2 ), sw::LocalizationManagerTestAccess::countCookedTables( runtime ) );

    SW_REQUIRE_TRUE( runtime.setCurrentLanguage( "en" ) );
    SW_EXPECT_EQUAL( sw::string( "Start" ), sw::string( runtime.findStringByText( "ui.start" ) ) );
    SW_EXPECT_EQUAL( size_t( 1 ), sw::LocalizationManagerTestAccess::countCookedTables( runtime ) ); // 사슬에서 빠진 ko 는 내렸다
}

namespace
{
    /** @brief 모든 키를 같은 해시로 보내는 위조 해시입니다(충돌을 일부러 만든다). */
    uint64 craftSameKeyHash( sw::string_view )
    {
        return 0x1234u;
    }
} // namespace

/**
 * @brief [LocalizationKeyIndexTest] 같은 해시에 다른 키가 오면 오류이고 먼저 든 키를 지킨다 — 대소문자만 다른 철자는 같은 키다
 */
SW_TEST_CASE( LocalizationKeyIndexTest, SameHashDifferentKeyIsAnError )
{
    sw::LocalizationKeyIndex crafted{ &craftSameKeyHash };
    SW_REQUIRE_TRUE( crafted.add( "Menu.Start" ) );
    SW_EXPECT_TRUE( crafted.add( "Menu.Start" ) );
    SW_EXPECT_TRUE( crafted.add( "menu.start" ) );

    const sw::expected<uint64> collided = crafted.add( "Menu.Quit" );
    SW_REQUIRE_FALSE( collided );
    SW_EXPECT_TRUE( collided.error().kind() == sw::ErrorKind::AlreadyExists );
    SW_EXPECT_TRUE( collided.error().detail().find( "Menu.Start" ) != sw::string_view::npos );
    SW_EXPECT_TRUE( collided.error().detail().find( "Menu.Quit" ) != sw::string_view::npos );
    // 진 뒤에도 먼저 든 키가 그대로라 같은 해시의 다른 키는 계속 진다.
    SW_EXPECT_FALSE( crafted.add( "Menu.Quit" ) );
    crafted.clear();
    SW_EXPECT_TRUE( crafted.add( "Menu.Quit" ) );

    sw::LocalizationKeyIndex   real;
    const sw::expected<uint64> added = real.add( "Menu.Quit" );
    SW_REQUIRE_TRUE( added );
    SW_EXPECT_EQUAL( sw::LocalizationKeyIndex::computeHash( "Menu.Quit" ), *added );
}

/**
 * @brief [LocalizationKeyIndexTest] 매니저가 해시가 겹치는 키를 들이지 않는다 — 프로젝트 올리기 · 낱개 XML · 낱개 글이 모두 오류를 남기고 기존 글을 지킨다
 */
SW_TEST_CASE( LocalizationKeyIndexTest, ManagerRefusesCollidingKeys )
{
    using sw::test::LocalizationTestUtil;
    const sw::test::ScopedScratchDomain domain;
    const sw::AbsolutePath              folder  = domain.getFolder();
    const sw::AbsolutePath              project = LocalizationTestUtil::writeProject( folder, "en", R"([ "ko" ])" );
    LocalizationTestUtil::writeSourceTable( folder, "en", R"("ui.start": { "source": "Start" }, "ui.quit": { "source": "Quit" })" );

    sw::LocalizationManager manager;
    sw::LocalizationManagerTestAccess::setCheckKeyHash( manager, &craftSameKeyHash );
    SW_EXPECT_LOG( "LocalizationManager.KeyHashCollision" );
    SW_EXPECT_FALSE( manager.mountProject( sw::test::ScopedScratchDomain::toID( project ), sw::LocalizationScope::Game ) );
    SW_EXPECT_TRUE( manager.getMountedProjectNames().empty() );

    manager.setString( "en", sw::hashed_string( "ui.start" ), "Start" );
    manager.setString( "en", sw::hashed_string( "ui.quit" ), "Quit" ); // 위조 해시로 앞 키와 겹친다 — 넣지 않는다
    SW_REQUIRE_TRUE( manager.setCurrentLanguage( "en" ) );
    SW_REQUIRE_NOT_NULL( manager.findString( sw::hashed_string( "ui.start" ) ) );
    SW_EXPECT_EQUAL( sw::string( "Start" ), sw::string( manager.findString( sw::hashed_string( "ui.start" ) ) ) );
    SW_EXPECT_NULL( manager.findString( sw::hashed_string( "ui.quit" ) ) );

    const sw::string xml = R"(<TranslationTable culture="en"><entries><entry key="ui.other" text="Other" /></entries></TranslationTable>)";
    SW_EXPECT_FALSE( manager.loadLanguageXML( "en", xml ) );
    SW_EXPECT_NULL( manager.findString( sw::hashed_string( "ui.other" ) ) );
}

namespace
{
    /** @brief 부모 하나와 자식 하나(적은 칸만 덮는다)를 든 표입니다. */
    constexpr const utf8* kInheritanceTableXML = R"XML(<CultureTable formatVersion="0">
  <numberingSystems><numberingSystem id="latn" digits="0123456789" /></numberingSystems>
  <currencies><currency code="EUR" symbol="€" digits="2" /></currencies>
  <CultureInfo _code="xx" _numberingSystem="latn" _nativeName="Root" _decimalSeparator="," _groupSeparator="." _percentPattern="# %" _currencyCode="EUR" _currencyPattern="# ¤">
    <_pluralRules><entry key="one">i = 1 and v = 0 @integer 1</entry><entry key="other">@integer 0, 2~16, 100, 1000, 10000, 100000, 1000000, … @decimal 0.0~1.5, 10.0, 100.0, 1000.0, 10000.0, 100000.0, 1000000.0, …</entry></_pluralRules>
    <_datePatterns _short="dd.MM.yy" _medium="dd.MM.y" _long="d. MMMM y" />
    <_timePatterns _short="HH:mm" _medium="HH:mm:ss" />
    <_listMonthName><item>Jan</item><item>Feb</item><item>Mar</item><item>Apr</item><item>May</item><item>Jun</item><item>Jul</item><item>Aug</item><item>Sep</item><item>Oct</item><item>Nov</item><item>Dec</item></_listMonthName>
  </CultureInfo>
  <CultureInfo _code="xx_yy" _base="xx" _decimalSeparator=".">
    <_listMonthName><item>M1</item><item>M2</item><item>M3</item><item>M4</item><item>M5</item><item>M6</item><item>M7</item><item>M8</item><item>M9</item><item>M10</item><item>M11</item><item>M12</item></_listMonthName>
  </CultureInfo>
</CultureTable>)XML";
} // namespace

/**
 * @brief [CultureInfoTest] 자식 문화권은 자기가 적은 칸만 덮는다 — 적지 않은 속성 · 패턴 · 복수형 규칙은 부모 것이다
 */
SW_TEST_CASE( CultureInfoTest, ChildOverridesOnlyWhatItWrites )
{
    sw::CultureTable                                table;
    const sw::expected<void>                        loaded = table.loadFromXMLText( kInheritanceTableXML, "inherit.cultures.xml" );
    sw::fixed_string<sw::constant::buffer::kMax512> loadError;
    if ( loaded.has_value() == false )
        sw::formatstring( loadError.data(), loadError.capacity(), "%# (%#)", loaded.error().message(), loaded.error().detail() );
    SW_REQUIRE_TRUE_MSG( loaded.has_value(), loadError );
    const sw::CultureInfo* pRoot  = table.findCulture( "xx" );
    const sw::CultureInfo* pChild = table.findCulture( "xx_yy" );
    SW_REQUIRE_NOT_NULL( pRoot );
    SW_REQUIRE_NOT_NULL( pChild );
    SW_EXPECT_EQUAL( sw::string( "." ), pChild->_decimalSeparator );    // 덮었다
    SW_EXPECT_EQUAL( pRoot->_groupSeparator, pChild->_groupSeparator ); // 물려받았다
    SW_EXPECT_EQUAL( pRoot->_currencyPattern, pChild->_currencyPattern );
    SW_EXPECT_EQUAL( pRoot->_numberingSystem, pChild->_numberingSystem );
    SW_EXPECT_EQUAL( sw::string( "dd.MM.yy" ), sw::string( pChild->findDatePattern( "short" ) ) );
    SW_EXPECT_EQUAL( sw::string( "HH:mm:ss" ), sw::string( pChild->findTimePattern( "medium" ) ) );
    SW_REQUIRE_EQUAL( size_t{ 12 }, pChild->_listMonthName.size() );
    SW_EXPECT_EQUAL( sw::string( "M3" ), pChild->_listMonthName[2] );
    SW_EXPECT_EQUAL( sw::string( "Mar" ), pRoot->_listMonthName[2] ); // 자식이 부모를 바꾸지 않는다
    SW_EXPECT_EQUAL( sw::string( "1.234,5" ), pRoot->formatDecimal( 1234.5, 1, 1 ) );
    SW_EXPECT_EQUAL( sw::string( "1.234.5" ), pChild->formatDecimal( 1234.5, 1, 1 ) );
    SW_EXPECT_EQUAL( sw::string( "one" ), sw::string( sw::CultureInfo::getPluralKeyword( pChild->selectPlural( sw::PluralOperands::makeInteger( 1 ) ) ) ) );
}

/**
 * @brief [CultureInfoTest] 부모가 없는 문화권에 `_pluralRules` 가 없으면 읽기 오류다 — 부모가 있으면 물려받으므로 없어도 된다
 */
SW_TEST_CASE( CultureInfoTest, RootWithoutPluralRulesIsAnError )
{
    constexpr const utf8*                            kLatin = R"(<numberingSystems><numberingSystem id="latn" digits="0123456789" /></numberingSystems>)";
    sw::StringBuilder<sw::constant::buffer::kMax512> rootXML;
    rootXML.appendFormat( R"(<CultureTable formatVersion="0">%#<CultureInfo _code="xx" _numberingSystem="latn" _decimalSeparator="." _groupSeparator="," _percentPattern="#%" _currencyCode="USD" _currencyPattern="¤#"><_datePatterns _short="yy" _medium="y" _long="yyyy" /><_timePatterns _short="HH:mm" _medium="HH:mm:ss" /></CultureInfo></CultureTable>)",
                          kLatin );
    sw::CultureTable         rootTable;
    const sw::expected<void> rootLoaded = rootTable.loadFromXMLText( rootXML.view(), "root.cultures.xml" );
    SW_REQUIRE_FALSE( rootLoaded.has_value() );
    sw::fixed_string<sw::constant::buffer::kMax512> error;
    sw::formatstring( error.data(), error.capacity(), "%# (%#)", rootLoaded.error().message(), rootLoaded.error().detail() );
    SW_EXPECT_TRUE_MSG( error.view().find( "pluralRules" ) != sw::string_view::npos, error.c_str() );
    SW_EXPECT_EQUAL( size_t( 0 ), rootTable.getCultureCount() );

    sw::CultureTable childTable;
    SW_EXPECT_TRUE( childTable.loadFromXMLText( kInheritanceTableXML, "inherit.cultures.xml" ) ); // xx_yy 는 규칙을 적지 않았다
}

/**
 * @brief [CultureInfoTest] 부모 순환(서로를 부모로 가리킴)과 없는 부모는 읽기 오류이고 표는 그대로다
 * @details 사슬은 카탈로그 항목 상속이 본다(그 오류 로그가 까닭이다).
 */
SW_TEST_CASE( CultureInfoTest, ParentCycleIsAnError )
{
    static constexpr const utf8* kLatin  = R"(<numberingSystems><numberingSystem id="latn" digits="0123456789" /></numberingSystems>)";
    const auto                   rejects = []( sw::string_view cultures )
    {
        sw::StringBuilder<sw::constant::buffer::kMax512> xml;
        xml.appendFormat( "<CultureTable formatVersion=\"0\">%#%#</CultureTable>", kLatin, cultures );
        sw::CultureTable         table;
        const sw::expected<void> loaded = table.loadFromXMLText( xml.view(), "cycle.cultures.xml" );
        SW_EXPECT_FALSE( loaded );
        SW_EXPECT_EQUAL( size_t( 0 ), table.getCultureCount() );
    };
    {
        SW_EXPECT_LOG( "DataInheritance.Cycle" );
        rejects( R"(<CultureInfo _code="aa" _base="bb" /><CultureInfo _code="bb" _base="aa" />)" );
    }
    {
        SW_EXPECT_LOG( "DataInheritance.BaseNotFound" );
        rejects( R"(<CultureInfo _code="aa" _base="zz" />)" );
    }
}

/**
 * @brief [CultureInfoTest] 문화권은 리플렉션 원소이고 `_base` 로 같은 표의 부모를 가리킨다 — 패턴 묶음은 적은 스타일만, 규칙 맵 · 목록은 통째로 바뀐다
 */
SW_TEST_CASE( CultureInfoTest, BaseChainReadsTheReflectedElements )
{
    constexpr const utf8*                           kTableXML = R"XML(<CultureTable formatVersion="0">
  <numberingSystems><numberingSystem id="latn" digits="0123456789" /></numberingSystems>
  <CultureInfo _code="xx" _nativeName="Root" _numberingSystem="latn" _decimalSeparator="," _groupSeparator="." _percentPattern="# %" _currencyCode="EUR" _currencyPattern="# ¤">
    <_pluralRules><entry key="one">i = 1 and v = 0 @integer 1</entry><entry key="other">@integer 0, 2~16, 100, 1000, 10000, 100000, 1000000, … @decimal 0.0~1.5, 10.0, 100.0, 1000.0, 10000.0, 100000.0, 1000000.0, …</entry></_pluralRules>
    <_datePatterns _short="dd.MM.yy" _medium="dd.MM.y" _long="d. MMMM y" />
    <_timePatterns _short="HH:mm" _medium="HH:mm:ss" />
  </CultureInfo>
  <CultureInfo _code="xx_yy" _base="xx" _nativeName="Child">
    <_datePatterns _long="y MMMM d" />
  </CultureInfo>
</CultureTable>)XML";
    sw::CultureTable                                table;
    const sw::expected<void>                        loaded = table.loadFromXMLText( kTableXML, "base.cultures.xml" );
    sw::fixed_string<sw::constant::buffer::kMax512> loadError;
    if ( loaded.has_value() == false )
        sw::formatstring( loadError.data(), loadError.capacity(), "%# (%#)", loaded.error().message(), loaded.error().detail() );
    SW_REQUIRE_TRUE_MSG( loaded.has_value(), loadError );
    const sw::CultureInfo* pChild = table.findCulture( "xx_yy" );
    SW_REQUIRE_NOT_NULL( pChild );
    SW_EXPECT_EQUAL( sw::string( "," ), pChild->_decimalSeparator );
    SW_EXPECT_EQUAL( sw::string( "dd.MM.yy" ), sw::string( pChild->findDatePattern( "short" ) ) ); // 구조체 칸 — 적지 않은 스타일은 부모 것
    SW_EXPECT_EQUAL( sw::string( "y MMMM d" ), sw::string( pChild->findDatePattern( "long" ) ) );
    SW_EXPECT_EQUAL( sw::string( "one" ), sw::string( sw::CultureInfo::getPluralKeyword( pChild->selectPlural( sw::PluralOperands::makeInteger( 1 ) ) ) ) );
}

/**
 * @brief [CultureInfoTest] 출하 표의 모든 문화권에 대해 숫자 · 통화 · 날짜 · 복수형 서식 결과가 기록과 글자까지 같다
 * @details 손 파서를 리플렉션 상속으로 바꾸기 **전에** 기록한다(`App --test --test-filter=CultureInfoTest.FormatsMatchTheHandParserForEveryShippedCulture --update-snapshots`) —
 *          바꾼 뒤에도 같은 스냅숏이어야 한다.
 */
SW_TEST_CASE( CultureInfoTest, FormatsMatchTheHandParserForEveryShippedCulture )
{
    const sw::CultureTable table = LocalizationFormatsTestInternal::loadCultures();
    sw::vector<sw::string> listCode;
    table.collectCultureCodes( listCode );
    SW_REQUIRE_TRUE( listCode.empty() == false );

    sw::TextDateTime when;
    when._year   = 2026;
    when._month  = 3;
    when._day    = 7;
    when._hour   = 14;
    when._minute = 5;
    when._second = 9;
    sw::StringBuilder<sw::constant::buffer::kMax8192> text;
    for ( const sw::string& code : listCode )
    {
        const sw::CultureInfo* pCulture = table.findCulture( code );
        SW_REQUIRE_NOT_NULL( pCulture );
        text.appendFormat( "%#\n", code );
        text.appendFormat( "  integer  %#\n", pCulture->formatInteger( -1234567 ) );
        text.appendFormat( "  decimal  %#\n", pCulture->formatDecimal( 1234.5, 1, 2 ) );
        text.appendFormat( "  percent  %#\n", pCulture->formatPercent( 0.256 ) );
        text.appendFormat( "  currency %# | %#\n", pCulture->formatCurrency( 1234.5, {} ), pCulture->formatCurrency( 99.0, "USD" ) );
        for ( const utf8* pStyle : { "short", "medium", "long" } )
        {
            text.appendFormat( "  date.%# %#\n", pStyle, pCulture->formatDateTime( when, pCulture->findDatePattern( pStyle ) ) );
        }
        for ( const utf8* pStyle : { "short", "medium" } )
        {
            text.appendFormat( "  time.%# %#\n", pStyle, pCulture->formatDateTime( when, pCulture->findTimePattern( pStyle ) ) );
        }
        text.append( "  plural  " );
        for ( const int64 count : { 0, 1, 2, 3, 5, 11, 21, 101 } )
        {
            text.appendFormat( "%# ", sw::CultureInfo::getPluralKeyword( pCulture->selectPlural( sw::PluralOperands::makeInteger( count ) ) ) );
        }
        text.appendFormat( "%#\n", sw::CultureInfo::getPluralKeyword( pCulture->selectPlural( sw::PluralOperands::makeDecimal( 1.5, 1 ) ) ) );
        text.append( "  ordinal " );
        for ( const int64 count : { 1, 2, 3, 4, 11, 21, 22, 23 } )
        {
            text.appendFormat( "%# ", sw::CultureInfo::getPluralKeyword( pCulture->selectOrdinal( sw::PluralOperands::makeInteger( count ) ) ) );
        }
        const sw::string_view font = pCulture->_listFont.empty() ? sw::string_view( "-" ) : sw::string_view( pCulture->_listFont.front() );
        text.appendFormat( "\n  fonts   %# rtl=%#\n", font, pCulture->_bRightToLeft == SW_TRUE ? "1" : "0" );
    }
    SW_EXPECT_SNAPSHOT( "formats", text.view() );
}
