// MarkupTagScanner — BBCode 꼴 표기 토큰(여는 · 닫는 태그 · [[ · 글), 태그 모양이 아닌 [ 는 글, 태그 열 비교.
#include "pch.h"

#include "Core/String/MarkupTagScanner.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

/** @brief [MarkupTagScannerTest] 토큰 — 글 · 여는 태그(값) · 닫는 태그 · [[ 를 차례로 읽고, 모양이 아닌 [ 는 글에 넣는다 */
SW_TEST_CASE( MarkupTagScannerTest, ReadsTokensInOrder )
{
    const string_view   markup = "a[color=#ff0000]b[/color][[c [1] [";
    vector<MarkupToken> listToken;
    size_t              offset = 0;
    MarkupToken         token{};
    while ( MarkupTagScanner::readToken( markup, offset, token ) )
    {
        listToken.push_back( token );
    }
    SW_ASSERT_EQUAL( 6u, static_cast<uint32>( listToken.size() ) );
    SW_EXPECT_TRUE( listToken[0]._kind == MarkupTokenKind::Text && listToken[0]._text == "a" );
    SW_EXPECT_TRUE( listToken[1]._kind == MarkupTokenKind::OpenTag && listToken[1]._name == "color" && listToken[1]._value == "#ff0000" );
    SW_EXPECT_TRUE( listToken[2]._kind == MarkupTokenKind::Text && listToken[2]._text == "b" );
    SW_EXPECT_TRUE( listToken[3]._kind == MarkupTokenKind::CloseTag && listToken[3]._name == "color" );
    SW_EXPECT_TRUE( listToken[4]._kind == MarkupTokenKind::EscapedBracket );
    SW_EXPECT_TRUE( listToken[5]._kind == MarkupTokenKind::Text && listToken[5]._text == "c [1] [" );
    SW_EXPECT_EQUAL( markup.size(), offset );
}

/** @brief [MarkupTagScannerTest] 태그 열은 이름 · 순서만 본다(값은 뺀다) — 번역 검사의 근거 */
SW_TEST_CASE( MarkupTagScannerTest, ComparesTagSequences )
{
    vector<string> listTag;
    MarkupTagScanner::collectTagSequence( "[b]x[/b][color=red]y[/color]", listTag );
    SW_ASSERT_EQUAL( 4u, static_cast<uint32>( listTag.size() ) );
    SW_EXPECT_STREQ( "b", listTag[0].c_str() );
    SW_EXPECT_STREQ( "/b", listTag[1].c_str() );
    SW_EXPECT_STREQ( "color", listTag[2].c_str() );
    SW_EXPECT_TRUE( MarkupTagScanner::hasSameTags( "[b]Start[/b]", "[b]시작[/b]" ) );
    SW_EXPECT_TRUE( MarkupTagScanner::hasSameTags( "[color=red]a[/color]", "[color=blue]b[/color]" ) );
    SW_EXPECT_FALSE( MarkupTagScanner::hasSameTags( "[b]Start[/b]", "시작" ) );
    SW_EXPECT_FALSE( MarkupTagScanner::hasSameTags( "[b][i]a[/i][/b]", "[i][b]a[/b][/i]" ) );
    SW_EXPECT_TRUE( MarkupTagScanner::hasMarkup( "a [[ b" ) );
    SW_EXPECT_FALSE( MarkupTagScanner::hasMarkup( "a [1] b" ) );
}
