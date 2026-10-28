#include "Core/Foundation/HexDigitUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

SW_TEST_CASE( HexDigitUtilTest, ParsesBothCasesAndRejectsOthers )
{
    uint32 value = 0;
    SW_EXPECT_TRUE( HexDigitUtil::tryParseDigit( 'a', value ) );
    SW_EXPECT_EQUAL( 10u, value );
    SW_EXPECT_TRUE( HexDigitUtil::tryParseDigit( 'F', value ) );
    SW_EXPECT_EQUAL( 15u, value );
    SW_EXPECT_FALSE( HexDigitUtil::tryParseDigit( 'g', value ) );
}

SW_TEST_CASE( HexDigitUtilTest, ParsesFixedWidthText )
{
    uint64 value = 0;
    SW_EXPECT_TRUE( HexDigitUtil::tryParseFixed( "00ff", value ) );
    SW_EXPECT_EQUAL( uint64{ 0xFF }, value );
    SW_EXPECT_FALSE( HexDigitUtil::tryParseFixed( "", value ) );
}
