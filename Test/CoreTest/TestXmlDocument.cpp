#include "pch.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 1) Core_Xml — 대소문자 무시 키·옵트아웃
// ------------------------------------------------------------------------------
/**
 * @brief [XmlDocumentTest] 파싱·탐색과 대소문자 무시 키
 */

SW_TEST_CASE( XmlDocumentTest, ParseAndNavigateIgnoreCaseKeys )
{
    sw::XmlDocument doc;
    const bool      bParsed = doc.parse(
        R"(<Root Name="Demo">
			<_score>12</_score>
			<item id="1">A</item>
			<item id="2">B</item>
		</Root>)" );
    SW_EXPECT_TRUE( bParsed );

    sw::XmlNode missing = doc.getRoot( "Missing" );
    SW_EXPECT_TRUE( missing.isValid() == false );

    sw::XmlNode root = doc.getRoot( "root" ); // 기본은 대소문자 무시
    SW_EXPECT_TRUE( root.isValid() );
    SW_EXPECT_STREQ( "Root", root.getName() );
    SW_EXPECT_STREQ( "Demo", root.findAttribute( "name" ) );
    SW_EXPECT_EQUAL( 12, root.getAttributeInt( "missing", 12 ) );

    SW_EXPECT_STREQ( "12", root.findChildText( "_score" ) );
    SW_EXPECT_STREQ( "12", root.findChildText( "_SCORE" ) );

    sw::XmlNode firstItem = root.findChild( "ITEM" );
    SW_EXPECT_TRUE( firstItem.isValid() );
    SW_EXPECT_STREQ( "1", firstItem.findAttribute( "ID" ) );
    SW_EXPECT_STREQ( "A", firstItem.getText() );

    sw::XmlNode secondItem = firstItem.findNextSibling( "item" );
    SW_EXPECT_TRUE( secondItem.isValid() );
    SW_EXPECT_STREQ( "2", secondItem.findAttribute( "id" ) );
    SW_EXPECT_STREQ( "B", secondItem.getText() );

    sw::string scoreText;
    SW_EXPECT_TRUE( root.takeChildText( "_score", scoreText ) );
    SW_EXPECT_EQUAL( sw::string( "12" ), scoreText );
}

/**
 * @brief [XmlDocumentTest] 대소문자 구분 키 옵트아웃
 */
SW_TEST_CASE( XmlDocumentTest, CaseSensitiveKeyOptOut )
{
    sw::XmlDocument doc;
    SW_EXPECT_TRUE( doc.parse( R"(<Root><Child>ok</Child></Root>)" ) );

    sw::XmlNode root = doc.getRoot( "Root", false );
    SW_EXPECT_TRUE( root.isValid() );
    SW_EXPECT_TRUE( root.findChild( "child", false ).isValid() == false );
    SW_EXPECT_TRUE( root.findChild( "Child", false ).isValid() );
    SW_EXPECT_STREQ( "ok", root.findChildText( "Child", false ) );
}

/**
 * @brief [XmlDocumentTest] 긴 줄 접기는 시작 태그의 속성만 접는다 — 따옴표 든 요소 텍스트는 저장 · 읽기를 지나도 그대로다
 * @details 예전에는 줄 끝까지 따옴표를 세어, 120 자를 넘는 `<item>The "Fire" … "Ice" …</item>` 의 텍스트 속 공백에서 줄을 접었다. 읽을 때는
 *          양 끝만 다듬어 그 줄바꿈과 들여쓰기가 값에 남았다.
 */
SW_TEST_CASE( XmlDocumentTest, LongTextWithQuotesSurvivesSave )
{
    const sw::string longText = "The \"Fire\" spell burns, the \"Ice\" spell freezes, and the \"Storm\" spell does both while "
                                "the caster keeps \"Focus\" for long enough to finish the incantation.";
    SW_ASSERT_TRUE( longText.size() > 120 );

    sw::XmlDocument doc;
    sw::XmlNode     root = doc.appendRoot( "Root" );
    root.appendChild( "item", sw::string_view{ longText.c_str(), longText.size() } );
    // 속성이 있는 요소의 텍스트 — 시작 태그 뒤까지 따옴표를 세면 여기서 접힌다.
    sw::XmlNode tagged = root.appendChild( "tagged", sw::string_view{ longText.c_str(), longText.size() } );
    tagged.setAttribute( "id", "7" );
    sw::XmlNode wide = root.appendChild( "wide" );
    wide.setAttribute( "first", "a long attribute value that keeps going and going" );
    wide.setAttribute( "second", "another long attribute value so the line passes the wrap column" );
    wide.setAttribute( "third", "yet another one" );

    const sw::string saved = doc.saveToString();
    sw::XmlDocument  reloaded;
    SW_ASSERT_TRUE( reloaded.parse( saved ) );
    sw::XmlNode reloadedRoot = reloaded.getRoot( "Root" );
    SW_ASSERT_TRUE( reloadedRoot.isValid() );
    SW_EXPECT_STREQ( longText.c_str(), reloadedRoot.findChildText( "item" ) );
    SW_EXPECT_STREQ( longText.c_str(), reloadedRoot.findChildText( "tagged" ) );
    SW_EXPECT_STREQ( "7", reloadedRoot.findChild( "tagged" ).findAttribute( "id" ) );

    // 속성 접기는 그대로 돈다 — 긴 속성 줄은 여러 줄이 되고 값은 같다.
    sw::XmlNode reloadedWide = reloadedRoot.findChild( "wide" );
    SW_ASSERT_TRUE( reloadedWide.isValid() );
    SW_EXPECT_STREQ( "yet another one", reloadedWide.findAttribute( "third" ) );
    SW_EXPECT_TRUE( saved.find( "\n" ) != sw::string::npos );
}

/**
 * @brief [XmlDocumentTest] 구문 오류는 `이름:줄:열: 이유` 로, 없는 파일은 `not found` 로 알린다 — 성공하면 비워진다
 * @details 예전에는 로그에 오프셋만 남고 어느 파일인지가 없었고, 부르는 쪽은 둘을 가를 수 없어 구문 오류도 "File not found" 로 알렸다.
 */
SW_TEST_CASE( XmlDocumentTest, ParseErrorNamesSourceLineAndColumn )
{
    sw::XmlDocument doc;
    {
        test::ScopedDefensiveTestLog expected( "malformed XML" );
        SW_EXPECT_FALSE( doc.parse( "<Root>\n  <A>\n</Root>\n", "scene.xml" ) );
    }
    SW_EXPECT_TRUE_MSG( sw::StringUtil::startsWith( doc.getLastError(), "scene.xml:3:" ), doc.getLastError().c_str() );

    SW_EXPECT_FALSE( doc.loadPath( "no/such/dir/missing.xml" ) );
    SW_EXPECT_TRUE_MSG( doc.getLastError().find( "missing.xml: not found" ) != sw::string::npos, doc.getLastError().c_str() );

    SW_EXPECT_TRUE( doc.parse( "<Root/>" ) );
    SW_EXPECT_TRUE( doc.getLastError().empty() );
}
