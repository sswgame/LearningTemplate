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
