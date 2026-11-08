// formatstring 의 실수 표기 — `%#` 는 `to_string` 과 같은 가장 짧은 왕복 표기, printf 실수 서식(`%f` · `%g` · `%e`)은 printf 와 같다.
#include "pch.h"

#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 값 하나를 @p pFormat 으로 적은 글입니다(테스트 비교용). */
    template <typename T>
    fixed_string<constant::buffer::kMax64> formatOne( const utf8* pFormat, T value )
    {
        fixed_string<constant::buffer::kMax64> text;
        formatstring( text.data(), text.capacity(), pFormat, value );
        return text;
    }
} // namespace

SW_TEST_CASE( FormatStringFloatTest, PlaceholderMatchesToString )
{
    SW_EXPECT_TRUE( formatOne( "%#", 1.5f ).view() == to_string( 1.5f ) );
    SW_EXPECT_TRUE( formatOne( "%#", 0.1f ).view() == "0.1" ); // float32 의 짧은 표기 — float64 로 넓혀 쓰지 않는다
    SW_EXPECT_TRUE( formatOne( "%#", 0.1 ).view() == to_string( 0.1 ) );
    SW_EXPECT_TRUE( formatOne( "%#", -2.0 ).view() == "-2" );
}

SW_TEST_CASE( FormatStringFloatTest, PrintfFixedKeepsSixDigits )
{
    SW_EXPECT_TRUE( formatOne( "%f", 1.5 ).view() == "1.500000" );
    SW_EXPECT_TRUE( formatOne( "%.2f", 1.005f ).view() == "1.00" );
}

SW_TEST_CASE( FormatStringFloatTest, PrintfGeneralAndScientific )
{
    SW_EXPECT_TRUE( formatOne( "%.9g", 0.1f ).view() == "0.100000001" );
    SW_EXPECT_TRUE( formatOne( "%.6g", 1234567.0 ).view() == "1.23457e+06" );
    SW_EXPECT_TRUE( formatOne( "%g", 0.5 ).view() == "0.5" );
    SW_EXPECT_TRUE( formatOne( "%.3e", 1500.0 ).view() == "1.500e+03" );
}
