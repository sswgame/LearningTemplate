#include "pch.h"

#include "Core/Container/LittleEndianUtil.h"

#include "Engine/Localization/CultureInfo.h"
#include "Engine/Localization/CurrencyUtil.h"
#include "Engine/Localization/LocalizationBinaryTable.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Localization/TimeZoneUtil.h"
#include "Engine/Localization/XliffFile.h"

#include "EngineTest/LocalizationTestUtil.h"

#include "TestFramework/TestFramework.h"

using sw::test::LocalizationTestUtil;

namespace
{
    struct LocalizationFormatsTestInternal
    {
        static const sw::CultureTable& getCultures()
        {
            static sw::CultureTable s_table;
            if ( s_table.getCultureCount() == 0 )
                (void)s_table.loadFromResource( LocalizationTestUtil::kCultureTable ); // 실패는 로더가 오류로 남기고, 빈 표는 문화권을 찾는 단언이 드러낸다
            return s_table;
        }

        static sw::string format( sw::string_view culture, sw::string_view pattern, const sw::TextArgumentList& arguments )
        {
            static const sw::CultureInfo s_empty{};
            const sw::CultureInfo*       pCulture = getCultures().resolveCulture( culture );
            sw::string                   text;
            sw::string                   error;
            if ( sw::TextFormatter::format( pattern, arguments, pCulture != nullptr ? *pCulture : s_empty, text, &error ) == false )
                text = "<error: " + error + ">";
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
            return sw::TimeZoneUtil::computeUnixSeconds( value );
        }

        static int32 offsetAt( const utf8* pZone, int64 unixSeconds )
        {
            const sw::TimeZoneInfo* pInfo = sw::TimeZoneUtil::findZone( pZone );
            return pInfo != nullptr ? sw::TimeZoneUtil::computeOffsetMinutes( unixSeconds, *pInfo ) : -9999;
        }
    };
} // namespace

/**
 * @brief [SelectOrdinalTest] 영어는 1st 2nd 3rd 4th, 11 · 12 · 13 은 th, 21 · 22 · 23 · 101 은 다시 st nd rd
 */
SW_TEST_CASE( SelectOrdinalTest, EnglishSuffixes )
{
    const sw::string_view kPattern = "{n, selectordinal, one {#st} two {#nd} few {#rd} other {#th}}";
    SW_EXPECT_STREQ( "1st", LocalizationFormatsTestInternal::ordinal( "en", kPattern, 1 ).c_str() );
    SW_EXPECT_STREQ( "2nd", LocalizationFormatsTestInternal::ordinal( "en", kPattern, 2 ).c_str() );
    SW_EXPECT_STREQ( "3rd", LocalizationFormatsTestInternal::ordinal( "en", kPattern, 3 ).c_str() );
    SW_EXPECT_STREQ( "4th", LocalizationFormatsTestInternal::ordinal( "en", kPattern, 4 ).c_str() );
    SW_EXPECT_STREQ( "11th", LocalizationFormatsTestInternal::ordinal( "en", kPattern, 11 ).c_str() );
    SW_EXPECT_STREQ( "12th", LocalizationFormatsTestInternal::ordinal( "en", kPattern, 12 ).c_str() );
    SW_EXPECT_STREQ( "13th", LocalizationFormatsTestInternal::ordinal( "en", kPattern, 13 ).c_str() );
    SW_EXPECT_STREQ( "21st", LocalizationFormatsTestInternal::ordinal( "en", kPattern, 21 ).c_str() );
    SW_EXPECT_STREQ( "22nd", LocalizationFormatsTestInternal::ordinal( "en", kPattern, 22 ).c_str() );
    SW_EXPECT_STREQ( "23rd", LocalizationFormatsTestInternal::ordinal( "en", kPattern, 23 ).c_str() );
    SW_EXPECT_STREQ( "101st", LocalizationFormatsTestInternal::ordinal( "en", kPattern, 101 ).c_str() );
    SW_EXPECT_STREQ( "111th", LocalizationFormatsTestInternal::ordinal( "en", kPattern, 111 ).c_str() );
    SW_EXPECT_STREQ( "2nd", LocalizationFormatsTestInternal::ordinal( "en_gb", kPattern, 2 ).c_str() ); // en 에서 물려받는다
}

/**
 * @brief [SelectOrdinalTest] 서수 규칙은 복수형 규칙과 따로다 — 프랑스어 1er · 2e, 한국어는 other 하나, 독일어(복수형은 english)도 서수는 other
 */
SW_TEST_CASE( SelectOrdinalTest, OrdinalRuleIsSeparateFromPlural )
{
    SW_EXPECT_STREQ( "1er", LocalizationFormatsTestInternal::ordinal( "fr", "{n, selectordinal, one {#er} other {#e}}", 1 ).c_str() );
    SW_EXPECT_STREQ( "2e", LocalizationFormatsTestInternal::ordinal( "fr", "{n, selectordinal, one {#er} other {#e}}", 2 ).c_str() );
    SW_EXPECT_STREQ( "3번째", LocalizationFormatsTestInternal::ordinal( "ko", "{n, selectordinal, other {#번째}}", 3 ).c_str() );
    SW_EXPECT_STREQ( "1.", LocalizationFormatsTestInternal::ordinal( "de", "{n, selectordinal, one {#st} other {#.}}", 1 ).c_str() );
}

/**
 * @brief [SelectOrdinalTest] other 가 없거나 모르는 범주면 구문 오류이고, 서수에는 offset 이 없다
 */
SW_TEST_CASE( SelectOrdinalTest, SyntaxIsChecked )
{
    sw::string error;
    SW_EXPECT_FALSE( sw::TextFormatter::validatePattern( "{n, selectordinal, one {#st}}", &error ) );
    SW_EXPECT_TRUE( error.find( "other" ) != sw::string::npos );
    SW_EXPECT_FALSE( sw::TextFormatter::validatePattern( "{n, selectordinal, tenth {#} other {#}}", &error ) );
    SW_EXPECT_TRUE( sw::TextFormatter::validatePattern( "{n, selectordinal, one {#st} other {#th}}", &error ) );
    SW_EXPECT_FALSE( sw::TextFormatter::validatePattern( "{n, selectordinal, offset:1 other {#th}}", &error ) );

    sw::vector<sw::string> listName;
    sw::TextFormatter::collectArgumentNames( "{rank, selectordinal, one {#st} other {#th}} of {total}", listName );
    SW_ASSERT_EQUAL( size_t( 2 ), listName.size() );
}

/**
 * @brief [CurrencyFormatTest] 문화권마다 기호 위치 · 구분 기호 · 소수 자리가 다르다 — 통화가 소수 자리를 정한다(KRW 0, USD 2)
 */
SW_TEST_CASE( CurrencyFormatTest, PatternsAndMinorDigits )
{
    const sw::TextArgumentList decimal = sw::TextArgumentList().addDecimal( "p", 1234.6 );
    SW_EXPECT_STREQ( "$1,234.60", LocalizationFormatsTestInternal::format( "en", "{p, number, currency}", decimal ).c_str() );
    SW_EXPECT_STREQ( "£1,234.60", LocalizationFormatsTestInternal::format( "en_gb", "{p, number, currency}", decimal ).c_str() );
    SW_EXPECT_STREQ( "1.234,60 €", LocalizationFormatsTestInternal::format( "de", "{p, number, currency}", decimal ).c_str() );
    SW_EXPECT_STREQ( "₩1,235", LocalizationFormatsTestInternal::format( "ko", "{p, number, currency}", decimal ).c_str() ); // 소수 0 자리 — 반올림
    SW_EXPECT_STREQ( "₩15,000", LocalizationFormatsTestInternal::format( "ko", "{p, number, currency}", sw::TextArgumentList().addInteger( "p", 15000 ) ).c_str() );
    SW_EXPECT_STREQ( "-$3.00", LocalizationFormatsTestInternal::format( "en", "{p, number, currency}", sw::TextArgumentList().addInteger( "p", -3 ) ).c_str() );
}

/**
 * @brief [CurrencyFormatTest] `::currency/코드` 스켈레톤이 문화권의 기본 통화를 덮고, 모르는 코드는 코드를 기호 자리에 쓴다
 */
SW_TEST_CASE( CurrencyFormatTest, SkeletonOverridesCultureCurrency )
{
    const sw::TextArgumentList amount = sw::TextArgumentList().addInteger( "p", 1234 );
    SW_EXPECT_STREQ( "¥1,234", LocalizationFormatsTestInternal::format( "en", "{p, number, ::currency/JPY}", amount ).c_str() );
    SW_EXPECT_STREQ( "1.234,00 €", LocalizationFormatsTestInternal::format( "de", "{p, number, ::currency/EUR}", amount ).c_str() );
    SW_EXPECT_STREQ( "XYZ1,234.00", LocalizationFormatsTestInternal::format( "en", "{p, number, ::currency/XYZ}", amount ).c_str() );

    SW_ASSERT_NOT_NULL( sw::CurrencyUtil::findCurrency( "KRW" ) );
    SW_EXPECT_EQUAL( uint8( 0 ), sw::CurrencyUtil::findCurrency( "KRW" )->_minorDigitCount );
    SW_EXPECT_EQUAL( uint8( 2 ), sw::CurrencyUtil::findCurrency( "USD" )->_minorDigitCount );
    SW_EXPECT_TRUE( sw::CurrencyUtil::findCurrency( "XYZ" ) == nullptr );
}

/**
 * @brief [TimeZoneTest] 고정 오프셋 시간대와 UTC → 현지 날짜 변환(날짜가 넘어가는 경우 포함)
 */
SW_TEST_CASE( TimeZoneTest, FixedOffsetConversion )
{
    const sw::TimeZoneInfo* pSeoul = sw::TimeZoneUtil::findZone( "Asia/Seoul" );
    SW_ASSERT_NOT_NULL( pSeoul );
    const int64            utc   = LocalizationFormatsTestInternal::makeUnixSeconds( 2026, 12, 31, 20, 30, 15 );
    const sw::TextDateTime local = sw::TimeZoneUtil::convertUnixMilliseconds( utc * 1000 + 999, *pSeoul );
    SW_EXPECT_EQUAL( 2027, local._year ); // 날짜가 넘어간다
    SW_EXPECT_EQUAL( uint8( 1 ), local._month );
    SW_EXPECT_EQUAL( uint8( 1 ), local._day );
    SW_EXPECT_EQUAL( uint8( 5 ), local._hour );
    SW_EXPECT_EQUAL( uint8( 30 ), local._minute );
    SW_EXPECT_EQUAL( uint8( 15 ), local._second );
    SW_EXPECT_TRUE( local._bZoned );
    SW_EXPECT_EQUAL( int16( 540 ), local._utcOffsetMinutes );

    const sw::TextDateTime kolkata = sw::TimeZoneUtil::convertUnixMilliseconds( utc * 1000, *sw::TimeZoneUtil::findZone( "Asia/Kolkata" ) );
    SW_EXPECT_EQUAL( uint8( 2 ), kolkata._hour ); // 20:30 + 5:30 = 02:00
    SW_EXPECT_EQUAL( uint8( 0 ), kolkata._minute );

    SW_EXPECT_TRUE( sw::TimeZoneUtil::findZone( "Mars/Olympus" ) == nullptr );
    SW_EXPECT_EQUAL( utc, sw::TimeZoneUtil::computeUnixSeconds( sw::TimeZoneUtil::convertUnixMilliseconds( utc * 1000, *sw::TimeZoneUtil::findZone( "UTC" ) ) ) );
}

/**
 * @brief [TimeZoneTest] 1970 이전 시각도 내림으로 계산한다(1969-12-31 23:59:59 UTC)
 */
SW_TEST_CASE( TimeZoneTest, NegativeUnixTimeFloors )
{
    const sw::TextDateTime value = sw::TimeZoneUtil::convertUnixMilliseconds( -1, *sw::TimeZoneUtil::findZone( "UTC" ) );
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

    const sw::TextDateTime summer = sw::TimeZoneUtil::convertUnixMilliseconds( Internal::makeUnixSeconds( 2026, 7, 4, 16, 0, 0 ) * 1000, *sw::TimeZoneUtil::findZone( "America/New_York" ) );
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
    SW_EXPECT_STREQ( "UTC", sw::TimeZoneUtil::formatOffset( 0, false ).c_str() );
    SW_EXPECT_STREQ( "UTC+9", sw::TimeZoneUtil::formatOffset( 540, false ).c_str() );
    SW_EXPECT_STREQ( "UTC+5:30", sw::TimeZoneUtil::formatOffset( 330, false ).c_str() );
    SW_EXPECT_STREQ( "UTC-3", sw::TimeZoneUtil::formatOffset( -180, false ).c_str() );
    SW_EXPECT_STREQ( "UTC+09:00", sw::TimeZoneUtil::formatOffset( 540, true ).c_str() );

    const int64      utc   = LocalizationFormatsTestInternal::makeUnixSeconds( 2026, 10, 4, 4, 5, 9 ) * 1000;
    const sw::string zoned = LocalizationFormatsTestInternal::format( "en", "{d, time, long}", sw::TextArgumentList().addUnixTime( "d", utc, "Asia/Seoul" ) );
    SW_EXPECT_TRUE( zoned.find( "1:05:09" ) != sw::string::npos ); // 04:05:09Z → 13:05:09 서울
    SW_EXPECT_TRUE( zoned.find( "UTC+9" ) != sw::string::npos );

    const sw::string plain = LocalizationFormatsTestInternal::format( "en", "{d, time, medium}", sw::TextArgumentList().addUnixTime( "d", utc, "Asia/Seoul" ) );
    SW_EXPECT_TRUE( plain.find( "UTC" ) == sw::string::npos ); // medium 은 오프셋을 붙이지 않는다

    const sw::string unknown = LocalizationFormatsTestInternal::format( "en", "{d, time, long}", sw::TextArgumentList().addUnixTime( "d", utc, "Mars/Olympus" ) );
    SW_EXPECT_TRUE( unknown.find( "4:05:09" ) != sw::string::npos ); // 모르는 시간대는 UTC
    SW_EXPECT_TRUE( unknown.find( "UTC" ) != sw::string::npos );

    const sw::CultureInfo* pEnglish = LocalizationFormatsTestInternal::getCultures().findCulture( "en" );
    SW_ASSERT_NOT_NULL( pEnglish );
    sw::TextDateTime wallClock;
    wallClock._hour = 13;
    SW_EXPECT_STREQ( "13 ", pEnglish->formatDateTime( wallClock, "H z" ).c_str() ); // 시간대를 모르는 값은 z 가 비어 있다
}

/**
 * @brief [XliffFileTest] 쓰고 읽으면 항목이 그대로다 — 키 · 원문 · 번역 · 검토 표시 · 설명 · 위치 · 옛 원문 · 번역가 메모
 */
SW_TEST_CASE( XliffFileTest, RoundTrip )
{
    sw::XliffFile file;
    file._sourceLanguage = "en";
    file._language       = "ko";
    file._projectName    = "test";

    sw::PortableObjectEntry first;
    first._context           = "Menu.Start";
    first._source            = "Start <b>Game</b> & {name}";
    first._translation       = "시작 <b>게임</b> & {name}";
    first._translatorComment = "버튼 글";
    first._previousSource    = "Begin";
    first._listExtractedComment.push_back( "메뉴 첫 줄" );
    first._listReference.push_back( "Source/Game/Menu.cpp" );
    file._listEntry.push_back( first );

    sw::PortableObjectEntry second;
    second._context     = "Menu.Quit";
    second._source      = "Quit";
    second._translation = "종료";
    second._bFuzzy      = true;
    file._listEntry.push_back( second );

    sw::PortableObjectEntry untranslated;
    untranslated._context = "Menu.Options";
    untranslated._source  = "Options";
    file._listEntry.push_back( untranslated );

    const sw::string text = file.toText();
    SW_EXPECT_TRUE( text.find( "urn:oasis:names:tc:xliff:document:1.2" ) != sw::string::npos );
    SW_EXPECT_TRUE( text.find( "needs-review-translation" ) != sw::string::npos );

    sw::XliffFile loaded;
    sw::string    error;
    SW_ASSERT_TRUE( loaded.parse( text, &error ) );
    SW_EXPECT_STREQ( "en", loaded._sourceLanguage.c_str() );
    SW_EXPECT_STREQ( "ko", loaded._language.c_str() );
    SW_EXPECT_STREQ( "test", loaded._projectName.c_str() );
    SW_ASSERT_EQUAL( size_t( 3 ), loaded._listEntry.size() );

    const sw::PortableObjectEntry& one = loaded._listEntry[0];
    SW_EXPECT_STREQ( "Menu.Start", one._context.c_str() );
    SW_EXPECT_STREQ( "Start <b>Game</b> & {name}", one._source.c_str() ); // XML 특수 문자는 이스케이프되어 왕복한다
    SW_EXPECT_STREQ( "시작 <b>게임</b> & {name}", one._translation.c_str() );
    SW_EXPECT_STREQ( "버튼 글", one._translatorComment.c_str() );
    SW_EXPECT_STREQ( "Begin", one._previousSource.c_str() );
    SW_EXPECT_FALSE( one._bFuzzy );
    SW_ASSERT_EQUAL( size_t( 1 ), one._listExtractedComment.size() );
    SW_EXPECT_STREQ( "메뉴 첫 줄", one._listExtractedComment[0].c_str() );
    SW_ASSERT_EQUAL( size_t( 1 ), one._listReference.size() );
    SW_EXPECT_STREQ( "Source/Game/Menu.cpp", one._listReference[0].c_str() );

    SW_EXPECT_TRUE( loaded._listEntry[1]._bFuzzy );
    SW_EXPECT_STREQ( "종료", loaded._listEntry[1]._translation.c_str() );
    SW_EXPECT_TRUE( loaded._listEntry[2]._translation.empty() ); // 번역이 없는 항목은 target 이 없다
}

/**
 * @brief [XliffFileTest] 다른 도구가 쓴 글(2.0 판 · 루트 이름 · id 없는 단위 · 구문 오류)은 거절하고 이유를 남긴다
 */
SW_TEST_CASE( XliffFileTest, BadInputIsRejected )
{
    sw::XliffFile file;
    sw::string    error;
    SW_EXPECT_FALSE( file.parse( R"(<xliff version="2.0"><file><body/></file></xliff>)", &error ) );
    SW_EXPECT_TRUE( error.find( "1.2" ) != sw::string::npos );
    SW_EXPECT_FALSE( file.parse( R"(<root/>)", &error ) );
    SW_EXPECT_FALSE( file.parse( R"(<xliff version="1.2"><file source-language="en"><body><trans-unit><source>a</source></trans-unit></body></file></xliff>)", &error ) );
    SW_EXPECT_TRUE( error.find( "id" ) != sw::string::npos );
    SW_EXPECT_FALSE( file.parse( "<xliff version=\"1.2\"", &error ) );

    // 외부 도구의 글(선언 · 이름공간 · 모르는 note 종류)은 읽고 모르는 것은 버린다
    const sw::string_view external =
        "<?xml version=\"1.0\"?><xliff version=\"1.2\" xmlns=\"urn:oasis:names:tc:xliff:document:1.2\"><file source-language=\"en\" target-language=\"ja\"><body>"
        "<trans-unit id=\"A.B\"><source>Hi</source><target state=\"translated\">やあ</target><note from=\"crowdin\">ignored</note></trans-unit></body></file></xliff>";
    SW_ASSERT_TRUE( file.parse( external, &error ) );
    SW_ASSERT_EQUAL( size_t( 1 ), file._listEntry.size() );
    SW_EXPECT_STREQ( "やあ", file._listEntry[0]._translation.c_str() );
    SW_EXPECT_FALSE( file._listEntry[0]._bFuzzy );
}

/**
 * @brief [LocalizationBinaryTableTest] 쓰고 읽으면 같고, 같은 내용이면 같은 바이트이며, 깨진 바이트는 모두 거절한다
 */
SW_TEST_CASE( LocalizationBinaryTableTest, RoundTripAndCorruption )
{
    sw::LocalizationBinaryTable table;
    table.setCulture( "ko" );
    table.setText( "Menu.Start", "시작" );
    table.setText( "Menu.Quit", "종료" );
    table.setText( "Menu.Empty", "" );

    sw::vector<uint8> bytes;
    table.toBytes( bytes );
    sw::vector<uint8> again;
    table.toBytes( again );
    SW_EXPECT_TRUE( bytes == again );

    sw::LocalizationBinaryTable loaded;
    sw::string                  error;
    SW_ASSERT_TRUE( loaded.parse( bytes.data(), bytes.size(), &error ) );
    SW_EXPECT_STREQ( "ko", loaded.getCulture().c_str() );
    SW_ASSERT_EQUAL( size_t( 3 ), loaded.getTexts().size() );
    SW_EXPECT_STREQ( "시작", loaded.getTexts().at( sw::hashed_string::computeHash( "Menu.Start" ) ).c_str() );
    SW_EXPECT_TRUE( loaded.getTexts().at( sw::hashed_string::computeHash( "Menu.Empty" ) ).empty() );

    // 글 한 바이트를 바꾸면 검사합이 거절한다 — 일부만 적용되지 않는다
    sw::vector<uint8> flipped = bytes;
    flipped[flipped.size() / 2] ^= 0x01u;
    SW_EXPECT_FALSE( loaded.parse( flipped.data(), flipped.size(), &error ) );
    SW_EXPECT_TRUE( loaded.getTexts().empty() );

    sw::vector<uint8> truncated( bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>( bytes.size() - 3 ) );
    SW_EXPECT_FALSE( loaded.parse( truncated.data(), truncated.size(), &error ) );
    SW_EXPECT_FALSE( loaded.parse( bytes.data(), 5, &error ) );

    sw::vector<uint8> wrongMagic = bytes;
    wrongMagic[0]                = 'X';
    SW_EXPECT_FALSE( loaded.parse( wrongMagic.data(), wrongMagic.size(), &error ) );
    SW_EXPECT_TRUE( error.find( "magic" ) != sw::string::npos );
}

/**
 * @brief [LocalizationBinaryTableTest] 매니저가 프로젝트에서 화면에 나오는 글만 베이크하고(낡은 번역 제외), 저작 파일 없이 그 바이트만으로 같은 글을 낸다
 */
SW_TEST_CASE( LocalizationBinaryTableTest, ManagerCooksAndLoadsWithoutAuthoringFiles )
{
    const sw::string folder  = test::makeTempDirectory( "loc_binary_cook" );
    const sw::string project = LocalizationTestUtil::writeProject( folder, "en", R"([ "ko" ])" );
    LocalizationTestUtil::writeSourceTable( folder, "en", R"("ui.start": { "source": "Start" }, "ui.quit": { "source": "Quit" })" );
    const sw::string staleHash = sw::LocalizationTextUtil::formatSourceHash( sw::LocalizationTextUtil::computeSourceHash( "Begin" ) );
    LocalizationTestUtil::writeTranslation( folder, "ko", "\"ui.start\": { \"text\": \"시작\" }, \"ui.quit\": { \"text\": \"종료\", \"sourceHash\": \"" + staleHash + "\" }" );

    sw::LocalizationManager cooker;
    SW_ASSERT_TRUE( cooker.mountProject( ::test::makeTempResourceID( project ), sw::LocalizationScope::Game ) );
    sw::vector<uint8> bytes;
    SW_ASSERT_TRUE( cooker.cookBinaryTable( "ko", bytes ) );
    sw::vector<uint8> none;
    SW_EXPECT_FALSE( cooker.cookBinaryTable( "fr", none ) ); // 글이 없는 문화권

    sw::LocalizationManager runtime; // 프로젝트를 올리지 않은 배포본
    SW_ASSERT_TRUE( runtime.loadBinaryTable( bytes.data(), bytes.size() ) );
    SW_ASSERT_TRUE( runtime.setCurrentLanguage( "ko" ) );
    SW_EXPECT_STREQ( "시작", runtime.getString( sw::hashed_string( "ui.start" ) ) );
    SW_EXPECT_NULL( runtime.findStringByText( "ui.quit" ) ); // 낡은 번역은 베이크해지지 않았다

    // 깨진 표는 거절하고 이미 있는 글을 바꾸지 않는다
    test::ScopedLogCollector logs;
    sw::vector<uint8>        broken = bytes;
    broken[broken.size() - 1] ^= 0xFFu;
    SW_EXPECT_FALSE( runtime.loadBinaryTable( broken.data(), broken.size() ) );
    SW_EXPECT_STREQ( "시작", runtime.getString( sw::hashed_string( "ui.start" ) ) );

    runtime.unloadLanguage( "ko" );
    SW_EXPECT_NULL( runtime.findStringByText( "ui.start" ) );
}
