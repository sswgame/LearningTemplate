#include "pch.h"

#include "Core/Common/HexDigitUtil.h"

#include "TestFramework/TestFramework.h"

/**
 * @brief [HexDigitUtilTest] writeDigits 는 높은 자리부터 대소문자와 자릿수대로 쓰고 넘치는 높은 자리는 버린다
 */
SW_TEST_CASE( HexDigitUtilTest, WriteDigitsCaseAndWidth )
{
    utf8 arrText[17]{};
    sw::HexDigitUtil::writeDigits( arrText, 0xDEADBEEFull, 8, sw::HexCase::Lower );
    SW_EXPECT_EQUAL( sw::string_view{ "deadbeef" }, sw::string_view( arrText, 8 ) );
    sw::HexDigitUtil::writeDigits( arrText, 0xDEADBEEFull, 8, sw::HexCase::Upper );
    SW_EXPECT_EQUAL( sw::string_view{ "DEADBEEF" }, sw::string_view( arrText, 8 ) );
    sw::HexDigitUtil::writeDigits( arrText, 0xABull, 4, sw::HexCase::Lower );
    SW_EXPECT_EQUAL( sw::string_view{ "00ab" }, sw::string_view( arrText, 4 ) );
    sw::HexDigitUtil::writeDigits( arrText, 0x1234ull, 2, sw::HexCase::Lower );
    SW_EXPECT_EQUAL( sw::string_view{ "34" }, sw::string_view( arrText, 2 ) );
}

/**
 * @brief [HexDigitUtilTest] tryParseFixed 는 대소문자를 가리지 않고 16 자리 가득 찬 값을 왕복한다
 */
SW_TEST_CASE( HexDigitUtilTest, ParseFixedRoundTrip )
{
    utf8 arrText[16]{};
    for ( const sw::HexCase hexCase : { sw::HexCase::Lower, sw::HexCase::Upper } )
    {
        sw::HexDigitUtil::writeDigits( arrText, 0xFEDCBA9876543210ull, 16, hexCase );
        uint64 value = 0;
        SW_EXPECT_TRUE( sw::HexDigitUtil::tryParseFixed( sw::string_view( arrText, 16 ), value ) );
        SW_EXPECT_EQUAL( uint64{ 0xFEDCBA9876543210ull }, value );
    }
    uint64 mixed = 0;
    SW_EXPECT_TRUE( sw::HexDigitUtil::tryParseFixed( "aBc", mixed ) );
    SW_EXPECT_EQUAL( uint64{ 0xABC }, mixed );
}

/**
 * @brief [HexDigitUtilTest] 빈 글 · 17 자리(넘침) · 잘못된 글자는 실패하고 출력을 건드리지 않는다
 */
SW_TEST_CASE( HexDigitUtilTest, ParseFixedRejectsBadInput )
{
    uint64 value = 7;
    SW_EXPECT_FALSE( sw::HexDigitUtil::tryParseFixed( "", value ) );
    SW_EXPECT_FALSE( sw::HexDigitUtil::tryParseFixed( "10000000000000000", value ) );
    SW_EXPECT_FALSE( sw::HexDigitUtil::tryParseFixed( "12g4", value ) );
    SW_EXPECT_FALSE( sw::HexDigitUtil::tryParseFixed( "0x12", value ) );
    SW_EXPECT_EQUAL( uint64{ 7 }, value );
}

/**
 * @brief [HexDigitUtilTest] tryParseDigit 는 0~15 만 읽는다
 */
SW_TEST_CASE( HexDigitUtilTest, ParseDigit )
{
    uint32 digit = 99;
    SW_EXPECT_TRUE( sw::HexDigitUtil::tryParseDigit( 'f', digit ) );
    SW_EXPECT_EQUAL( uint32{ 15 }, digit );
    SW_EXPECT_TRUE( sw::HexDigitUtil::tryParseDigit( 'A', digit ) );
    SW_EXPECT_EQUAL( uint32{ 10 }, digit );
    digit = 99;
    SW_EXPECT_FALSE( sw::HexDigitUtil::tryParseDigit( 'g', digit ) );
    SW_EXPECT_FALSE( sw::HexDigitUtil::tryParseDigit( '/', digit ) );
    SW_EXPECT_EQUAL( uint32{ 99 }, digit );
}

/**
 * @brief [HexDigitUtilTest] tryParseBytes 는 글자 수가 바이트 수의 두 배일 때만 읽는다
 */
SW_TEST_CASE( HexDigitUtilTest, ParseBytes )
{
    uint8 arrBytes[3]{};
    SW_EXPECT_TRUE( sw::HexDigitUtil::tryParseBytes( "00ffA5", arrBytes, 3 ) );
    SW_EXPECT_EQUAL( uint8{ 0x00 }, arrBytes[0] );
    SW_EXPECT_EQUAL( uint8{ 0xFF }, arrBytes[1] );
    SW_EXPECT_EQUAL( uint8{ 0xA5 }, arrBytes[2] );
    SW_EXPECT_FALSE( sw::HexDigitUtil::tryParseBytes( "00ff", arrBytes, 3 ) );
    SW_EXPECT_FALSE( sw::HexDigitUtil::tryParseBytes( "00ffA5A", arrBytes, 3 ) );
    SW_EXPECT_FALSE( sw::HexDigitUtil::tryParseBytes( "00zz00", arrBytes, 3 ) );
}
