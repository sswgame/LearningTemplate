#include "pch.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 1) Core_Xml — 대소문자 무시 키·옵트아웃
// ------------------------------------------------------------------------------
/**
 * @brief [Core_Xml] 파싱·탐색과 대소문자 무시 키
 */

SW_TEST_CASE( Core_Xml, ParseAndNavigateIgnoreCaseKeys )
{
	sw::XmlDocument doc;
	const bool		bParsed = doc.parse(
		R"(<Root Name="Demo">
			<_score>12</_score>
			<item id="1">A</item>
			<item id="2">B</item>
		</Root>)" );
	SW_EXPECT_TRUE( bParsed );

	sw::XmlNode missing = doc.root( "Missing" );
	SW_EXPECT_TRUE( missing.isValid() == false );

	sw::XmlNode root = doc.root( "root" ); // 기본은 대소문자 무시
	SW_EXPECT_TRUE( root.isValid() );
	SW_EXPECT_STREQ( "Root", root.name() );
	SW_EXPECT_STREQ( "Demo", root.attr( "name" ) );
	SW_EXPECT_EQUAL( 12, root.attrInt( "missing", 12 ) );

	SW_EXPECT_STREQ( "12", root.childText( "_score" ) );
	SW_EXPECT_STREQ( "12", root.childText( "_SCORE" ) );

	sw::XmlNode firstItem = root.child( "ITEM" );
	SW_EXPECT_TRUE( firstItem.isValid() );
	SW_EXPECT_STREQ( "1", firstItem.attr( "ID" ) );
	SW_EXPECT_STREQ( "A", firstItem.text() );

	sw::XmlNode secondItem = firstItem.next( "item" );
	SW_EXPECT_TRUE( secondItem.isValid() );
	SW_EXPECT_STREQ( "2", secondItem.attr( "id" ) );
	SW_EXPECT_STREQ( "B", secondItem.text() );

	sw::string scoreText;
	SW_EXPECT_TRUE( root.takeChildText( "_score", scoreText ) );
	SW_EXPECT_EQUAL( sw::string( "12" ), scoreText );
}

/**
 * @brief [Core_Xml] 대소문자 구분 키 옵트아웃
 */
SW_TEST_CASE( Core_Xml, CaseSensitiveKeyOptOut )
{
	sw::XmlDocument doc;
	SW_EXPECT_TRUE( doc.parse( R"(<Root><Child>ok</Child></Root>)" ) );

	sw::XmlNode root = doc.root( "Root", false );
	SW_EXPECT_TRUE( root.isValid() );
	SW_EXPECT_TRUE( root.child( "child", false ).isValid() == false );
	SW_EXPECT_TRUE( root.child( "Child", false ).isValid() );
	SW_EXPECT_STREQ( "ok", root.childText( "Child", false ) );
}
