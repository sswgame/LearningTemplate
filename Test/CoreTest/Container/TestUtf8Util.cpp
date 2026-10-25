#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/Utf8Util.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

/**
 * @brief [Utf8UtilTest] 연속 바이트 판정은 10xxxxxx 만 true 이고, 선두 바이트 · ASCII · 잘못된 바이트는 false 다
 */
SW_TEST_CASE( Utf8UtilTest, ContinuationIsOnlyTheTenBitPattern )
{
    SW_EXPECT_TRUE( Utf8Util::isContinuation( static_cast<utf8>( 0x80 ) ) );
    SW_EXPECT_TRUE( Utf8Util::isContinuation( static_cast<utf8>( 0xBF ) ) );
    SW_EXPECT_FALSE( Utf8Util::isContinuation( 'a' ) );
    SW_EXPECT_FALSE( Utf8Util::isContinuation( static_cast<utf8>( 0xC3 ) ) );
    SW_EXPECT_FALSE( Utf8Util::isContinuation( static_cast<utf8>( 0xF0 ) ) );
}

/**
 * @brief [Utf8UtilTest] 글자 한가운데 자르는 위치는 글자 시작으로 물러나고, 경계 · 끝 · 잘못된 연속 바이트 열에서는 멈춘다
 */
SW_TEST_CASE( Utf8UtilTest, BackOffStopsAtCharacterStarts )
{
    const string text = "a가😀b"; // 1 + 3 + 4 + 1 바이트
    SW_EXPECT_EQUAL( StringUtil::floorToUtf8Boundary( text, 0 ), size_t{ 0 } );
    SW_EXPECT_EQUAL( StringUtil::floorToUtf8Boundary( text, 1 ), size_t{ 1 } );
    SW_EXPECT_EQUAL( StringUtil::floorToUtf8Boundary( text, 2 ), size_t{ 1 } );
    SW_EXPECT_EQUAL( StringUtil::floorToUtf8Boundary( text, 3 ), size_t{ 1 } );
    SW_EXPECT_EQUAL( StringUtil::floorToUtf8Boundary( text, 4 ), size_t{ 4 } );
    SW_EXPECT_EQUAL( StringUtil::floorToUtf8Boundary( text, 7 ), size_t{ 4 } );
    SW_EXPECT_EQUAL( StringUtil::floorToUtf8Boundary( text, 8 ), size_t{ 8 } );
    SW_EXPECT_EQUAL( StringUtil::floorToUtf8Boundary( text, 9 ), text.size() );
    SW_EXPECT_EQUAL( StringUtil::floorToUtf8Boundary( text, 100 ), text.size() );

    // 연속 바이트만 이어지는 잘못된 글은 세 칸까지만 물러난다(끝없이 거슬러 가지 않는다).
    const string garbage( 8, static_cast<utf8>( 0x80 ) );
    SW_EXPECT_EQUAL( StringUtil::floorToUtf8Boundary( garbage, 6 ), size_t{ 3 } );
}

/**
 * @brief [Utf8UtilTest] 글자 하나 디코드는 올바른 글자만 true 로 읽고, 정확히 한 글자인 글만 단일로 인정한다
 */
SW_TEST_CASE( Utf8UtilTest, DecodeReportsValidityAndSingleCharacters )
{
    uint32 codepoint = 0;
    SW_EXPECT_TRUE( StringUtil::tryDecodeSingleCodepoint( "가", codepoint ) );
    SW_EXPECT_EQUAL( codepoint, uint32{ 0xAC00 } );
    SW_EXPECT_TRUE( StringUtil::tryDecodeSingleCodepoint( "😀", codepoint ) );
    SW_EXPECT_EQUAL( codepoint, uint32{ 0x1F600 } );
    SW_EXPECT_FALSE( StringUtil::tryDecodeSingleCodepoint( "", codepoint ) );
    SW_EXPECT_FALSE( StringUtil::tryDecodeSingleCodepoint( "가a", codepoint ) );
    SW_EXPECT_FALSE( StringUtil::tryDecodeSingleCodepoint( string_view( "\xE3\x81", 2 ), codepoint ) ); // 잘린 3 바이트 글자

    size_t offset = 0;
    SW_EXPECT_FALSE( StringUtil::tryDecodeUtf8( "\x80z", offset, codepoint ) ); // 선두가 될 수 없는 바이트
    SW_EXPECT_EQUAL( codepoint, uint32{ 0xFFFD } );
    SW_EXPECT_EQUAL( offset, size_t{ 1 } );
    SW_EXPECT_TRUE( StringUtil::tryDecodeUtf8( "\x80z", offset, codepoint ) );
    SW_EXPECT_EQUAL( codepoint, uint32{ 'z' } );
}

/**
 * @brief [Utf8UtilTest] UCS-2 변환은 기본 평면 글자를 그대로 두고, 평면 밖 글자와 잘못된 바이트를 U+FFFD 한 글자로 바꾼다
 */
SW_TEST_CASE( Utf8UtilTest, Ucs2ReplacesWhatItCannotHold )
{
    vector<uint16> listUnit;
    StringUtil::utf8ToUcs2( string( "a가😀" ) + string( 1, static_cast<utf8>( 0x80 ) ) + "b", listUnit );
    SW_ASSERT_EQUAL( listUnit.size(), size_t{ 5 } );
    SW_EXPECT_EQUAL( listUnit[0], uint16{ 'a' } );
    SW_EXPECT_EQUAL( listUnit[1], uint16{ 0xAC00 } );
    SW_EXPECT_EQUAL( listUnit[2], uint16{ 0xFFFD } );
    SW_EXPECT_EQUAL( listUnit[3], uint16{ 0xFFFD } );
    SW_EXPECT_EQUAL( listUnit[4], uint16{ 'b' } );
}
