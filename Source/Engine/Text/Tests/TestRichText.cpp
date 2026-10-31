// RichTextTest — BBCode 꼴 리치 텍스트(굵게 · 기울임 · 색 · 크기 · [[) 파싱(nogpu). 배치의 구간 경계 · 액션 글리프는 가짜 글꼴 · 입력 하네스가 없어 아직 보지 않는다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Text/RichTextParser.h"

/** @brief [RichTextTest] 포갠 태그 — "a[b]b[color=#ff0000]c[/color][/b]d" 는 평문 "abcd", 구간 b(굵게) · c(굵게 + 빨강) */
SW_TEST_CASE( RichTextTest, ParsesNestedTags )
{
    sw::RichTextParseResult result{};
    sw::RichTextParser::parse( "a[b]b[color=#ff0000]c[/color][/b]d", result );
    SW_EXPECT_EQUAL( sw::string( "abcd" ), result._plainText );
    SW_EXPECT_EQUAL( SW_TRUE, result._bHasMarkup );
    SW_EXPECT_EQUAL( 0u, result._problemCount );
    SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( result._listSpan.size() ) );
    SW_EXPECT_EQUAL( 1u, result._listSpan[0]._firstByte );
    SW_EXPECT_EQUAL( 1u, result._listSpan[0]._byteCount );
    SW_EXPECT_EQUAL( SW_TRUE, result._listSpan[0]._bBold );
    SW_EXPECT_EQUAL( 0xFFFFFFFFu, result._listSpan[0]._colorRGBA );
    SW_EXPECT_EQUAL( 2u, result._listSpan[1]._firstByte );
    SW_EXPECT_EQUAL( SW_TRUE, result._listSpan[1]._bBold );
    SW_EXPECT_EQUAL( 0xFF0000FFu, result._listSpan[1]._colorRGBA );
}

/** @brief [RichTextTest] "[[" 는 글자 "[" 이고, 태그 모양이 아닌 "[" 도 글자다 — 구간 없음 */
SW_TEST_CASE( RichTextTest, DoubleBracketIsLiteral )
{
    sw::RichTextParseResult result{};
    sw::RichTextParser::parse( "[[x] [1] [ ok", result );
    SW_EXPECT_EQUAL( sw::string( "[x] [1] [ ok" ), result._plainText );
    SW_EXPECT_TRUE( result._listSpan.empty() );
    SW_EXPECT_EQUAL( 0u, result._problemCount );

    sw::RichTextParser::parse( "plain", result );
    SW_EXPECT_EQUAL( SW_FALSE, result._bHasMarkup );
    SW_EXPECT_EQUAL( sw::string( "plain" ), result._plainText );
}

/** @brief [RichTextTest] 모르는 태그 · 닫히지 않은 태그 · 틀린 값은 글자 그대로 남고 경고한다 — 번역 실수가 화면에서 보이게 */
SW_TEST_CASE( RichTextTest, UnknownOrUnclosedTagStaysVisibleAndWarns )
{
    SW_EXPECT_LOG( "Text.RichTextBadTag" ); // 문제마다 경고 한 줄이 이 케이스가 보려는 것이다
    sw::RichTextParseResult result{};
    sw::RichTextParser::parse( "[foo]x", result );
    SW_EXPECT_EQUAL( sw::string( "[foo]x" ), result._plainText );
    SW_EXPECT_EQUAL( 1u, result._problemCount );

    sw::RichTextParser::parse( "[b]x", result );
    SW_EXPECT_EQUAL( sw::string( "[b]x" ), result._plainText );
    SW_EXPECT_EQUAL( 1u, result._problemCount );
    SW_EXPECT_TRUE( result._listSpan.empty() );

    sw::RichTextParser::parse( "[size=big]x[/size] [color=#12]y[/color]", result );
    SW_EXPECT_EQUAL( sw::string( "[size=big]x[/size] [color=#12]y[/color]" ), result._plainText );
    SW_EXPECT_EQUAL( 4u, result._problemCount );
}

/** @brief [RichTextTest] 이름 색 · 크기 · 기울임 — 이름은 색 토큰(번호 + 1)으로, 같은 이름은 한 번 */
SW_TEST_CASE( RichTextTest, NamedColorSizeAndItalic )
{
    sw::RichTextParseResult result{};
    sw::RichTextParser::parse( "[color=accent]a[/color][size=1.5][i]b[/i][/size][color=accent]c[/color]", result );
    SW_EXPECT_EQUAL( sw::string( "abc" ), result._plainText );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( result._listColorName.size() ) );
    SW_EXPECT_EQUAL( sw::string( "accent" ), result._listColorName[0] );
    SW_REQUIRE_EQUAL( 3u, static_cast<uint32>( result._listSpan.size() ) );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( result._listSpan[0]._colorToken ) );
    SW_EXPECT_NEAR( 1.5f, result._listSpan[1]._sizeScale, 1e-5 );
    SW_EXPECT_EQUAL( SW_TRUE, result._listSpan[1]._bItalic );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( result._listSpan[2]._colorToken ) );
    SW_EXPECT_TRUE( sw::RichTextParser::hasSameTags( "[b]Start[/b]", "[b]시작[/b]" ) );
    SW_EXPECT_FALSE( sw::RichTextParser::hasSameTags( "[b]Start[/b]", "시작" ) );
}
