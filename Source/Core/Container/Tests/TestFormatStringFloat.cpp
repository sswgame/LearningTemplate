// formatstring 의 실수 표기 — `%#` 는 `to_string` 과 같은 가장 짧은 왕복 표기, printf 실수 서식(`%f` · `%g` · `%e`)은 printf 와 같다.
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/formatString.h"
#include "Core/String/StringBuilder.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

SW_TEST_CASE( FormatStringFloatTest, PlaceholderMatchesToString )
{
    SW_EXPECT_TRUE( formatToString( "%#", 1.5f ) == to_string( 1.5f ) );
    SW_EXPECT_TRUE( formatToString( "%#", 0.1f ) == "0.1" ); // float32 의 짧은 표기 — float64 로 넓혀 쓰지 않는다
    SW_EXPECT_TRUE( formatToString( "%#", 0.1 ) == to_string( 0.1 ) );
    SW_EXPECT_TRUE( formatToString( "%#", -2.0 ) == "-2" );
}

SW_TEST_CASE( FormatStringFloatTest, PrintfFixedKeepsSixDigits )
{
    SW_EXPECT_TRUE( formatToString( "%f", 1.5 ) == "1.500000" );
    SW_EXPECT_TRUE( formatToString( "%.2f", 1.005f ) == "1.00" );
}

SW_TEST_CASE( FormatStringFloatTest, PrintfGeneralAndScientific )
{
    SW_EXPECT_TRUE( formatToString( "%.9g", 0.1f ) == "0.100000001" );
    SW_EXPECT_TRUE( formatToString( "%.6g", 1234567.0 ) == "1.23457e+06" );
    SW_EXPECT_TRUE( formatToString( "%g", 0.5 ) == "0.5" );
    SW_EXPECT_TRUE( formatToString( "%.3e", 1500.0 ) == "1.500e+03" );
}
