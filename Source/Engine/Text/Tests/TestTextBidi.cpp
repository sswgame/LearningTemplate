// TextBidiTest — 양방향 수준 해석과 눈에 보이는 순서(UAX #9: 강한 문자 · 숫자 · 중립 · 덮어쓰기 · 괄호 거울). 연결형이 없는 히브리어로 본다(nogpu).
// 배치(`TextLayoutEngine`)가 줄마다 뒤집고 괄호를 거울 글리프로 그리는지는 가짜 래스터라이저로 본다 — 글자 0.5 em · 공백 0.25 em, 크기 10 이면 글자 5 · 공백 2.5.
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Localization/PseudoLocalizer.h"
#include "Engine/Text/Tests/FakeFontRasterizer.h"
#include "Engine/Text/TextBidi.h"
#include "Engine/Text/TextItemizer.h"
#include "Engine/Text/TextLayout.h"

namespace
{
    /** @brief @p text 한 줄을 눈에 보이는 순서(왼쪽 → 오른쪽)의 UTF-8 로 바꿉니다. 폭 없는 문자(방향 제어)는 뺍니다. */
    sw::string makeVisualText( sw::string_view text, sw::TextDirection paragraphDirection )
    {
        sw::vector<uint32> listCodepoint;
        size_t             offset = 0;
        while ( offset < text.size() )
        {
            listCodepoint.push_back( sw::StringUtil::decodeUTF8( text, offset ) );
        }
        sw::vector<uint8> listLevel;
        sw::TextBidi::resolveLevels( listCodepoint, paragraphDirection, listLevel );
        sw::vector<uint32> listVisual;
        sw::TextBidi::reorderVisually( listLevel, listVisual );
        sw::string visual;
        for ( const uint32 logical : listVisual )
        {
            if ( sw::TextItemizer::isZeroWidth( listCodepoint[logical] ) == false )
                sw::StringUtil::appendUTF8( visual, listCodepoint[logical] );
        }
        return visual;
    }

    /** @brief 모든 코드 포인트를 가진 가족 하나의 글꼴 시스템을 시작합니다. */
    [[nodiscard]] bool initializeAllCoverage( sw::test::FakeFontServiceFixture& fixture )
    {
        sw::FontCatalogDesc catalog{};
        catalog._defaultFamily = "All";
        sw::test::FakeFontServiceFixture::addFamily( catalog, "All", "test/fonts/all.ttf" );
        return fixture.initialize( catalog );
    }

    sw::TextLayoutStyle makeRightToLeftStyle()
    {
        sw::TextLayoutStyle style{};
        style._fontSize           = 10.0f;
        style._paragraphDirection = sw::TextDirection::RightToLeft;
        return style;
    }
} // namespace

/** @brief [TextBidiTest] 히브리어 런은 눈에 거꾸로 선다 — RTL 문단의 "שלום" 은 "םולש", LTR 문단 가운데의 히브리어 낱말도 그 자리에서만 뒤집힌다 */
SW_TEST_CASE( TextBidiTest, HebrewRunIsReversedVisually )
{
    const sw::string word = "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D"; // שלום
    SW_EXPECT_EQUAL( sw::string( "\xD7\x9D\xD7\x95\xD7\x9C\xD7\xA9" ), makeVisualText( word, sw::TextDirection::RightToLeft ) );
    SW_EXPECT_EQUAL( sw::string( "abc \xD7\x9D\xD7\x95\xD7\x9C\xD7\xA9 def" ), makeVisualText( sw::string( "abc " ) + word + " def", sw::TextDirection::LeftToRight ) );

    sw::vector<uint32> listCodepoint{ 'a', 0x05D0u, 'b' };
    sw::vector<uint8>  listLevel;
    sw::TextBidi::resolveLevels( listCodepoint, sw::TextDirection::RightToLeft, listLevel );
    SW_REQUIRE_EQUAL( 3u, static_cast<uint32>( listLevel.size() ) );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( listLevel[0] ) ); // RTL 문단의 L 은 2
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( listLevel[1] ) );
}

/** @brief [TextBidiTest] 숫자는 RTL 안에서도 왼쪽에서 오른쪽이다 — "שלום 123" 은 눈에 "123 םולש" */
SW_TEST_CASE( TextBidiTest, NumbersStayLeftToRightInsideRtl )
{
    const sw::string text = "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D 123"; // שלום 123
    SW_EXPECT_EQUAL( sw::string( "123 \xD7\x9D\xD7\x95\xD7\x9C\xD7\xA9" ), makeVisualText( text, sw::TextDirection::RightToLeft ) );
    // LTR 문단에서 L 뒤의 숫자는 L 이다(W7) — 제자리.
    SW_EXPECT_EQUAL( sw::string( "abc 123" ), makeVisualText( "abc 123", sw::TextDirection::LeftToRight ) );
}

/** @brief [TextBidiTest] 중립은 양쪽이 같으면 그 방향, 다르면 문단 방향이다 — 히브리어 두 낱말 사이 ", " 는 R, L 과 R 사이 공백은 문단 방향 */
SW_TEST_CASE( TextBidiTest, NeutralsTakeSurroundingDirection )
{
    // LTR 문단 "abc שלום, עולם def" — 쉼표 · 공백이 R 이 되어 히브리어 두 낱말이 한 덩어리로 뒤집힌다.
    const sw::string text = "abc \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D, \xD7\xA2\xD7\x95\xD7\x9C\xD7\x9D def";
    SW_EXPECT_EQUAL( sw::string( "abc \xD7\x9D\xD7\x9C\xD7\x95\xD7\xA2 ,\xD7\x9D\xD7\x95\xD7\x9C\xD7\xA9 def" ), makeVisualText( text, sw::TextDirection::LeftToRight ) );

    // L 과 R 사이의 공백은 문단 방향을 받는다: LTR 문단이면 0, RTL 문단이면 1.
    const sw::vector<uint32> listCodepoint{ 'a', ' ', 0x05D0u };
    sw::vector<uint8>        listLevel;
    sw::TextBidi::resolveLevels( listCodepoint, sw::TextDirection::LeftToRight, listLevel );
    SW_REQUIRE_EQUAL( 3u, static_cast<uint32>( listLevel.size() ) );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( listLevel[1] ) );
    sw::TextBidi::resolveLevels( listCodepoint, sw::TextDirection::RightToLeft, listLevel );
    SW_REQUIRE_EQUAL( 3u, static_cast<uint32>( listLevel.size() ) );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( listLevel[1] ) );
}

/** @brief [TextBidiTest] 괄호는 짝 글리프로 거울된다 — '(' ↔ ')', « ↔ », 괄호가 아닌 글자는 그대로 */
SW_TEST_CASE( TextBidiTest, MirroredBrackets )
{
    SW_EXPECT_EQUAL( static_cast<uint32>( ')' ), sw::TextBidi::getMirroredCodepoint( '(' ) );
    SW_EXPECT_EQUAL( 0x00BBu, sw::TextBidi::getMirroredCodepoint( 0x00ABu ) );
    SW_EXPECT_EQUAL( static_cast<uint32>( 'a' ), sw::TextBidi::getMirroredCodepoint( 'a' ) );
}

/** @brief [TextBidiTest] 덮어쓰기(RLO … PDF)는 안의 글자를 R 로 본다 — 의사 문화권 qps-plocm 의 글이 LTR 문단에서도 눈에 거꾸로 선다 */
SW_TEST_CASE( TextBidiTest, RightToLeftOverrideReversesPseudoText )
{
    sw::string overridden;
    sw::StringUtil::appendUTF8( overridden, sw::PseudoLocalizer::kRightToLeftOverride );
    overridden += "Abc";
    sw::StringUtil::appendUTF8( overridden, sw::PseudoLocalizer::kPopDirectionalFormat );
    overridden += " x";
    SW_EXPECT_EQUAL( sw::string( "cbA x" ), makeVisualText( overridden, sw::TextDirection::LeftToRight ) );
}

/** @brief [TextBidiTest] RTL 문단의 괄호는 배치에서 거울 글리프로 그린다 — "(שלום)" 의 눈에 보이는 첫 글리프는 논리 끝 ')' 를 거울한 '(' 이다 */
SW_TEST_CASE( TextBidiTest, MirroredBracketsInRtlLayout )
{
    sw::test::FakeFontServiceFixture fixture;
    SW_REQUIRE_TRUE( initializeAllCoverage( fixture ) );
    sw::TextLayoutEngine layout{ *fixture._fontService };
    sw::TextLayoutResult result{};
    layout.layout( "(\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D)", makeRightToLeftStyle(), 0.0f, result );
    SW_REQUIRE_EQUAL( 6u, static_cast<uint32>( result._listGlyph.size() ) );
    SW_EXPECT_EQUAL( static_cast<uint32>( '(' ), result._listGlyph[0]._glyphIndex );
    SW_EXPECT_EQUAL( 9u, result._listGlyph[0]._cluster ); // 논리 끝의 ')' 를 거울로
    SW_EXPECT_EQUAL( 0x05DDu, result._listGlyph[1]._glyphIndex );
    SW_EXPECT_EQUAL( static_cast<uint32>( ')' ), result._listGlyph[5]._glyphIndex );
    SW_EXPECT_NEAR( 0.0f, result._listGlyph[0]._origin._x, 1e-4f );
    SW_EXPECT_NEAR( 25.0f, result._listGlyph[5]._origin._x, 1e-4f );
}

/**
 * @brief [TextBidiTest] 줄을 나눈 뒤 줄마다 뒤집는다 — RTL 문단 "אב גד" 를 너비 12 에: 첫 줄 "אב" 가 눈에 "בא"(끝 공백은 왼쪽 밖), 둘째 줄 "גד" 가 "דג",
 *        Start 정렬은 오른쪽이라 둘 다 2 만큼 민다
 */
SW_TEST_CASE( TextBidiTest, LineBreakThenReorder )
{
    sw::test::FakeFontServiceFixture fixture;
    SW_REQUIRE_TRUE( initializeAllCoverage( fixture ) );
    sw::TextLayoutEngine layout{ *fixture._fontService };
    sw::TextLayoutResult result{};
    layout.layout( "\xD7\x90\xD7\x91 \xD7\x92\xD7\x93", makeRightToLeftStyle(), 12.0f, result ); // אב גד
    SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( result._listLine.size() ) );
    SW_REQUIRE_EQUAL( 5u, static_cast<uint32>( result._listGlyph.size() ) );
    SW_EXPECT_NEAR( 10.0f, result._listLine[0]._width, 1e-4f );

    // 첫 줄: 끝 공백(문단 수준)이 맨 왼쪽 · 너비 밖, 그다음 ב · א.
    SW_EXPECT_EQUAL( static_cast<uint32>( ' ' ), result._listGlyph[0]._glyphIndex );
    SW_EXPECT_NEAR( -0.5f, result._listGlyph[0]._origin._x, 1e-4f );
    SW_EXPECT_EQUAL( 0x05D1u, result._listGlyph[1]._glyphIndex );
    SW_EXPECT_NEAR( 2.0f, result._listGlyph[1]._origin._x, 1e-4f );
    SW_EXPECT_EQUAL( 0x05D0u, result._listGlyph[2]._glyphIndex );
    SW_EXPECT_NEAR( 7.0f, result._listGlyph[2]._origin._x, 1e-4f );
    // 둘째 줄: ד · ג — 문단 전체를 먼저 뒤집었다면 ג ד 가 첫 줄에 왔을 것이다.
    SW_EXPECT_EQUAL( 0x05D3u, result._listGlyph[3]._glyphIndex );
    SW_EXPECT_NEAR( 2.0f, result._listGlyph[3]._origin._x, 1e-4f );
    SW_EXPECT_EQUAL( 0x05D2u, result._listGlyph[4]._glyphIndex );
    SW_EXPECT_NEAR( 7.0f, result._listGlyph[4]._origin._x, 1e-4f );

    // End 정렬은 RTL 문단에서 왼쪽이다.
    sw::TextLayoutStyle endStyle = makeRightToLeftStyle();
    endStyle._alignment          = sw::TextAlignment::End;
    layout.layout( "\xD7\x90\xD7\x91 \xD7\x92\xD7\x93", endStyle, 12.0f, result );
    SW_REQUIRE_EQUAL( 5u, static_cast<uint32>( result._listGlyph.size() ) );
    SW_EXPECT_NEAR( 0.0f, result._listGlyph[3]._origin._x, 1e-4f );
}
