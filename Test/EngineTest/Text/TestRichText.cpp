#include "pch.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Text/RichTextParser.h"
#include "Engine/Text/TextLayout.h"

#include "EngineTest/Text/FakeFontRasterizer.h"

#include "TestFramework/TestFramework.h"

// RichTextTest — BBCode 꼴 리치 텍스트(굵게 · 기울임 · 색 · 크기 · [[) 파싱과 배치의 구간 경계. 가짜 래스터라이저(nogpu).

/** @brief [RichTextTest] 포갠 태그 — "a[b]b[color=#ff0000]c[/color][/b]d" 는 평문 "abcd", 구간 b(굵게) · c(굵게 + 빨강) */
SW_TEST_CASE( RichTextTest, ParsesNestedTags )
{
    sw::RichTextParseResult result{};
    sw::RichTextParser::parse( "a[b]b[color=#ff0000]c[/color][/b]d", result );
    SW_EXPECT_STREQ( "abcd", result._plainText.c_str() );
    SW_EXPECT_EQUAL( SW_TRUE, result._bHasMarkup );
    SW_EXPECT_EQUAL( 0u, result._problemCount );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( result._listSpan.size() ) );
    SW_EXPECT_EQUAL( 1u, result._listSpan[0]._firstByte );
    SW_EXPECT_EQUAL( 1u, result._listSpan[0]._byteCount );
    SW_EXPECT_EQUAL( SW_TRUE, result._listSpan[0]._bBold );
    SW_EXPECT_EQUAL( 0xFFFFFFFFu, result._listSpan[0]._colorRgba );
    SW_EXPECT_EQUAL( 2u, result._listSpan[1]._firstByte );
    SW_EXPECT_EQUAL( SW_TRUE, result._listSpan[1]._bBold );
    SW_EXPECT_EQUAL( 0xFF0000FFu, result._listSpan[1]._colorRgba );
}

/** @brief [RichTextTest] "[[" 는 글자 "[" 이고, 태그 모양이 아닌 "[" 도 글자다 — 구간 없음 */
SW_TEST_CASE( RichTextTest, DoubleBracketIsLiteral )
{
    sw::RichTextParseResult result{};
    sw::RichTextParser::parse( "[[x] [1] [ ok", result );
    SW_EXPECT_STREQ( "[x] [1] [ ok", result._plainText.c_str() );
    SW_EXPECT_TRUE( result._listSpan.empty() );
    SW_EXPECT_EQUAL( 0u, result._problemCount );

    sw::RichTextParser::parse( "plain", result );
    SW_EXPECT_EQUAL( SW_FALSE, result._bHasMarkup );
    SW_EXPECT_STREQ( "plain", result._plainText.c_str() );
}

/** @brief [RichTextTest] 모르는 태그 · 닫히지 않은 태그 · 틀린 값은 글자 그대로 남고 경고한다 — 번역 실수가 화면에서 보이게 */
SW_TEST_CASE( RichTextTest, UnknownOrUnclosedTagStaysVisibleAndWarns )
{
    SW_TEST_DEFENSIVE_SCOPE( "bad rich text tags are shown as plain text with a warning" );
    sw::RichTextParseResult result{};
    sw::RichTextParser::parse( "[foo]x", result );
    SW_EXPECT_STREQ( "[foo]x", result._plainText.c_str() );
    SW_EXPECT_EQUAL( 1u, result._problemCount );

    sw::RichTextParser::parse( "[b]x", result );
    SW_EXPECT_STREQ( "[b]x", result._plainText.c_str() );
    SW_EXPECT_EQUAL( 1u, result._problemCount );
    SW_EXPECT_TRUE( result._listSpan.empty() );

    sw::RichTextParser::parse( "[size=big]x[/size] [color=#12]y[/color]", result );
    SW_EXPECT_STREQ( "[size=big]x[/size] [color=#12]y[/color]", result._plainText.c_str() );
    SW_EXPECT_EQUAL( 4u, result._problemCount );
}

/** @brief [RichTextTest] 이름 색 · 크기 · 기울임 — 이름은 색 토큰(번호 + 1)으로, 같은 이름은 한 번 */
SW_TEST_CASE( RichTextTest, NamedColorSizeAndItalic )
{
    sw::RichTextParseResult result{};
    sw::RichTextParser::parse( "[color=accent]a[/color][size=1.5][i]b[/i][/size][color=accent]c[/color]", result );
    SW_EXPECT_STREQ( "abc", result._plainText.c_str() );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( result._listColorName.size() ) );
    SW_EXPECT_STREQ( "accent", result._listColorName[0].c_str() );
    SW_ASSERT_EQUAL( 3u, static_cast<uint32>( result._listSpan.size() ) );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( result._listSpan[0]._colorToken ) );
    SW_EXPECT_NEAR_EQUAL( 1.5f, result._listSpan[1]._sizeScale, 1e-5f );
    SW_EXPECT_EQUAL( SW_TRUE, result._listSpan[1]._bItalic );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( result._listSpan[2]._colorToken ) );
    SW_EXPECT_TRUE( sw::RichTextParser::hasSameTags( "[b]Start[/b]", "[b]시작[/b]" ) );
    SW_EXPECT_FALSE( sw::RichTextParser::hasSameTags( "[b]Start[/b]", "시작" ) );
}

/** @brief [RichTextTest] 배치는 구간 경계에서 런을 끊는다 — 굵게는 굵은 면이 없는 가족이라 가짜 굵게, 색 · 크기는 그 글리프에만 */
SW_TEST_CASE( RichTextTest, LayoutSplitsRunsAtSpanBoundaries )
{
    sw::test::FakeFontSystemFixture fixture;
    sw::FontCatalogDesc             catalog{};
    catalog._defaultFamily = "Latin";
    sw::test::FakeFontSystemFixture::addFamily( catalog, "Latin", "test/fonts/latin.ttf" );
    SW_ASSERT_TRUE( fixture.initialize( catalog ) );
    sw::TextLayoutEngine layout( *fixture._fontSystem );

    sw::RichTextParseResult parsed{};
    sw::RichTextParser::parse( "a[b]b[/b][color=#00ff00]c[/color][size=2]d[/size]", parsed );
    SW_EXPECT_STREQ( "abcd", parsed._plainText.c_str() );
    sw::TextLayoutStyle style{};
    style._fontSize = 10.0f;
    sw::TextLayoutResult result{};
    layout.layout( parsed._plainText, style, 0.0f, result, &parsed._listSpan );
    SW_ASSERT_EQUAL( 4u, static_cast<uint32>( result._listGlyph.size() ) );
    SW_EXPECT_EQUAL( SW_FALSE, result._listGlyph[0]._bFauxBold );
    SW_EXPECT_EQUAL( SW_TRUE, result._listGlyph[1]._bFauxBold );
    SW_EXPECT_EQUAL( 0xFFFFFFFFu, result._listGlyph[1]._colorRgba );
    SW_EXPECT_EQUAL( 0x00FF00FFu, result._listGlyph[2]._colorRgba );
    SW_EXPECT_NEAR_EQUAL( 20.0f, result._listGlyph[3]._fontSize, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 15.0f, result._listGlyph[3]._origin._x, 1e-4f ); // a · b · c 가 5 씩
    SW_EXPECT_EQUAL( 3u, result._listGlyph[3]._cluster );                  // 클러스터는 평문 기준
    SW_EXPECT_NEAR_EQUAL( 25.0f, result._size._x, 1e-4f );                 // d 는 크기 20 이라 10
    SW_EXPECT_NEAR_EQUAL( 20.0f, result._listLine[0]._height, 1e-4f );     // 가장 큰 글자가 줄 높이를 정한다
    const sw::float2 measured = layout.measure( parsed._plainText, style, 0.0f, &parsed._listSpan );
    SW_EXPECT_NEAR_EQUAL( result._size._x, measured._x, 1e-4f );
}

/**
 * @brief [RichTextTest] 행동 태그 [action=이름] 은 지금 장치의 글리프 글로 바뀐다 — 키보드면 "[ E ]", 패드면 패드 버튼. 다른 태그 · [[ 는 그대로
 * @details 변이: `expandActionTags` 가 태그를 그대로 두면 키보드 글이 "[action=Interact] open" 으로 남아 진다.
 */
SW_TEST_CASE( RichTextTest, ActionTagUsesCurrentInputGlyph )
{
    sw::InputMap inputMap;
    inputMap.bind( "Interact", sw::Key::E );
    inputMap.bind( "Interact", sw::GamepadButton::X );
    sw::InputGlyphStyle                   style    = sw::InputGlyphStyle::KeyboardMouse;
    const sw::RichTextActionGlyphResolver resolver = SW_DELEGATE_LAMBDA( sw::RichTextActionGlyphResolver, [&inputMap, &style]( sw::string_view action )
    {
        return inputMap.getGlyphForAction( sw::hashed_string( action ), style );
    } );
    sw::string                            text;
    SW_EXPECT_EQUAL( 1u, sw::RichTextParser::expandActionTags( "[action=Interact] [b]open[/b] [[x]", resolver, text ) );
    SW_EXPECT_STREQ( "[ E ] [b]open[/b] [[x]", text.c_str() );

    style = sw::InputGlyphStyle::GamepadXbox;
    SW_EXPECT_EQUAL( 1u, sw::RichTextParser::expandActionTags( "[action=Interact] [b]open[/b] [[x]", resolver, text ) );
    SW_EXPECT_TRUE( text.find( "[ E ]" ) == sw::string::npos );
    SW_EXPECT_STREQ( ( inputMap.getGlyphForAction( "Interact", style ) + " [b]open[/b] [[x]" ).c_str(), text.c_str() );

    SW_EXPECT_EQUAL( 0u, sw::RichTextParser::expandActionTags( "no tags [[action=Interact]", resolver, text ) );
    SW_EXPECT_STREQ( "no tags [[action=Interact]", text.c_str() );
}
