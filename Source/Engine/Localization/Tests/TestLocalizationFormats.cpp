// 현지화 형식 — 서수(selectordinal) 규칙, 통화 형식 · 소수 자리, 시간대 변환 · 일광 절약 경계 · 오프셋 글, 이진 번역 표(locres)의 왕복과 깨진 바이트 거절(nogpu).
// 문화권은 엔진 표(`engine/localization/engine.cultures.xml`)에서 읽는다. 복수형 · select · 숫자의 기본 해석은 `TextFormatterTest` 가 본다.
#include "pch.h"

#include "Core/String/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/FileSystem/AssetID.h"
#include "Engine/Localization/CultureInfo.h"
#include "Engine/Localization/CurrencyUtil.h"
#include "Engine/Localization/LocalizationBinaryTable.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Localization/TimeZoneUtil.h"

namespace
{
    struct LocalizationFormatsTestInternal
    {
        static const sw::CultureTable& getCultures()
        {
            static sw::CultureTable s_table;
            static const bool       s_bLoaded = s_table.loadFromAsset( sw::AssetID{ "engine/localization/engine.cultures.xml" } );
            SW_EXPECT_TRUE( s_bLoaded );
            return s_table;
        }

        static sw::string format( sw::string_view culture, sw::string_view pattern, const sw::TextArgumentList& arguments )
        {
            static const sw::CultureInfo s_empty{};
            const sw::CultureInfo*       pCulture = getCultures().resolveCulture( culture );
            SW_EXPECT_TRUE( pCulture != nullptr );
            sw::string text;
            sw::string error;
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

    const sw::CurrencyInfo* pWon    = sw::CurrencyUtil::findCurrency( "KRW" );
    const sw::CurrencyInfo* pDollar = sw::CurrencyUtil::findCurrency( "USD" );
    SW_REQUIRE_NOT_NULL( pWon );
    SW_REQUIRE_NOT_NULL( pDollar );
    SW_EXPECT_EQUAL( uint8( 0 ), pWon->_minorDigitCount );
    SW_EXPECT_EQUAL( uint8( 2 ), pDollar->_minorDigitCount );
    SW_EXPECT_NULL( sw::CurrencyUtil::findCurrency( "XYZ" ) );
}

/**
 * @brief [TimeZoneTest] 고정 오프셋 시간대와 UTC → 현지 날짜 변환(날짜가 넘어가는 경우 포함)
 */
SW_TEST_CASE( TimeZoneTest, FixedOffsetConversion )
{
    const sw::TimeZoneInfo* pSeoul   = sw::TimeZoneUtil::findZone( "Asia/Seoul" );
    const sw::TimeZoneInfo* pKolkata = sw::TimeZoneUtil::findZone( "Asia/Kolkata" );
    const sw::TimeZoneInfo* pUTC     = sw::TimeZoneUtil::findZone( "UTC" );
    SW_REQUIRE_NOT_NULL( pSeoul );
    SW_REQUIRE_NOT_NULL( pKolkata );
    SW_REQUIRE_NOT_NULL( pUTC );
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

    const sw::TextDateTime kolkata = sw::TimeZoneUtil::convertUnixMilliseconds( utc * 1000, *pKolkata );
    SW_EXPECT_EQUAL( uint8( 2 ), kolkata._hour ); // 20:30 + 5:30 = 02:00
    SW_EXPECT_EQUAL( uint8( 0 ), kolkata._minute );

    SW_EXPECT_NULL( sw::TimeZoneUtil::findZone( "Mars/Olympus" ) );
    SW_EXPECT_EQUAL( utc, sw::TimeZoneUtil::computeUnixSeconds( sw::TimeZoneUtil::convertUnixMilliseconds( utc * 1000, *pUTC ) ) );
}

/**
 * @brief [TimeZoneTest] 1970 이전 시각도 내림으로 계산한다(1969-12-31 23:59:59 UTC)
 */
SW_TEST_CASE( TimeZoneTest, NegativeUnixTimeFloors )
{
    const sw::TimeZoneInfo* pUTC = sw::TimeZoneUtil::findZone( "UTC" );
    SW_REQUIRE_NOT_NULL( pUTC );
    const sw::TextDateTime value = sw::TimeZoneUtil::convertUnixMilliseconds( -1, *pUTC );
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

    const sw::TimeZoneInfo* pNewYork = sw::TimeZoneUtil::findZone( "America/New_York" );
    SW_REQUIRE_NOT_NULL( pNewYork );
    const sw::TextDateTime summer = sw::TimeZoneUtil::convertUnixMilliseconds( Internal::makeUnixSeconds( 2026, 7, 4, 16, 0, 0 ) * 1000, *pNewYork );
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
    SW_EXPECT_EQUAL( sw::string( "UTC" ), sw::TimeZoneUtil::formatOffset( 0, false ) );
    SW_EXPECT_EQUAL( sw::string( "UTC+9" ), sw::TimeZoneUtil::formatOffset( 540, false ) );
    SW_EXPECT_EQUAL( sw::string( "UTC+5:30" ), sw::TimeZoneUtil::formatOffset( 330, false ) );
    SW_EXPECT_EQUAL( sw::string( "UTC-3" ), sw::TimeZoneUtil::formatOffset( -180, false ) );
    SW_EXPECT_EQUAL( sw::string( "UTC+09:00" ), sw::TimeZoneUtil::formatOffset( 540, true ) );

    const int64      utc   = Internal::makeUnixSeconds( 2026, 10, 4, 4, 5, 9 ) * 1000;
    const sw::string zoned = Internal::format( "en", "{d, time, long}", sw::TextArgumentList().addUnixTime( "d", utc, "Asia/Seoul" ) );
    SW_EXPECT_TRUE( zoned.find( "1:05:09" ) != sw::string::npos ); // 04:05:09Z → 13:05:09 서울
    SW_EXPECT_TRUE( zoned.find( "UTC+9" ) != sw::string::npos );

    const sw::string plain = Internal::format( "en", "{d, time, medium}", sw::TextArgumentList().addUnixTime( "d", utc, "Asia/Seoul" ) );
    SW_EXPECT_TRUE( plain.find( "UTC" ) == sw::string::npos ); // medium 은 오프셋을 붙이지 않는다

    const sw::string unknown = Internal::format( "en", "{d, time, long}", sw::TextArgumentList().addUnixTime( "d", utc, "Mars/Olympus" ) );
    SW_EXPECT_TRUE( unknown.find( "4:05:09" ) != sw::string::npos ); // 모르는 시간대는 UTC
    SW_EXPECT_TRUE( unknown.find( "UTC" ) != sw::string::npos );

    const sw::CultureInfo* pEnglish = Internal::getCultures().findCulture( "en" );
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
    SW_REQUIRE_TRUE( loaded.parse( bytes.data(), bytes.size(), &error ) );
    SW_EXPECT_EQUAL( sw::string( "ko" ), loaded.getCulture() );
    SW_REQUIRE_EQUAL( size_t( 3 ), loaded.getTexts().size() );
    const auto startIt = loaded.getTexts().find( sw::hashed_string::computeHash( "Menu.Start" ) );
    const auto emptyIt = loaded.getTexts().find( sw::hashed_string::computeHash( "Menu.Empty" ) );
    SW_REQUIRE_TRUE( startIt != loaded.getTexts().end() && emptyIt != loaded.getTexts().end() );
    SW_EXPECT_EQUAL( sw::string( "시작" ), startIt->second );
    SW_EXPECT_TRUE( emptyIt->second.empty() );

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
