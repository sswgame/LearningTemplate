#include "pch.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 1) Core_XML — 대소문자 무시 키·옵트아웃
// ------------------------------------------------------------------------------
/**
 * @brief [XMLDocumentTest] 파싱·탐색과 대소문자 무시 키
 */
SW_TEST_CASE( XMLDocumentTest, ParseAndNavigateIgnoreCaseKeys )
{
    sw::XMLDocument doc;
    const bool      bParsed = doc.parse(
        R"(<Root Name="Demo">
			<_score>12</_score>
			<item id="1">A</item>
			<item id="2">B</item>
		</Root>)" );
    SW_EXPECT_TRUE( bParsed );

    sw::XMLNode missing = doc.getRoot( "Missing" );
    SW_EXPECT_TRUE( missing.isValid() == false );

    sw::XMLNode root = doc.getRoot( "root" ); // 기본은 대소문자 무시
    SW_EXPECT_TRUE( root.isValid() );
    SW_EXPECT_STREQ( "Root", root.getName() );
    SW_EXPECT_STREQ( "Demo", root.findAttribute( "name" ) );
    SW_EXPECT_EQUAL( 12, root.getAttributeInt( "missing", 12 ) );

    SW_EXPECT_STREQ( "12", root.findChildText( "_score" ) );
    SW_EXPECT_STREQ( "12", root.findChildText( "_SCORE" ) );

    sw::XMLNode firstItem = root.findChild( "ITEM" );
    SW_EXPECT_TRUE( firstItem.isValid() );
    SW_EXPECT_STREQ( "1", firstItem.findAttribute( "ID" ) );
    SW_EXPECT_STREQ( "A", firstItem.getText() );

    sw::XMLNode secondItem = firstItem.findNextSibling( "item" );
    SW_EXPECT_TRUE( secondItem.isValid() );
    SW_EXPECT_STREQ( "2", secondItem.findAttribute( "id" ) );
    SW_EXPECT_STREQ( "B", secondItem.getText() );

    sw::string scoreText;
    SW_EXPECT_TRUE( root.takeChildText( "_score", scoreText ) );
    SW_EXPECT_EQUAL( sw::string( "12" ), scoreText );
}

/**
 * @brief [XMLDocumentTest] 대소문자 구분 키 옵트아웃
 */
SW_TEST_CASE( XMLDocumentTest, CaseSensitiveKeyOptOut )
{
    sw::XMLDocument doc;
    SW_EXPECT_TRUE( doc.parse( R"(<Root><Child>ok</Child></Root>)" ) );

    sw::XMLNode root = doc.getRoot( "Root", false );
    SW_EXPECT_TRUE( root.isValid() );
    SW_EXPECT_TRUE( root.findChild( "child", false ).isValid() == false );
    SW_EXPECT_TRUE( root.findChild( "Child", false ).isValid() );
    SW_EXPECT_STREQ( "ok", root.findChildText( "Child", false ) );
}

/**
 * @brief [XMLDocumentTest] 긴 줄 접기는 시작 태그의 속성만 접는다 — 따옴표 든 요소 텍스트는 저장 · 읽기를 지나도 그대로다
 * @details 줄 끝까지 따옴표를 세면 120 자를 넘는 `<item>The "Fire" … "Ice" …</item>` 의 텍스트 속 공백에서 줄을 접고, 읽을 때는
 *          양 끝만 다듬으므로 그 줄바꿈과 들여쓰기가 값에 남는다.
 */
SW_TEST_CASE( XMLDocumentTest, LongTextWithQuotesSurvivesSave )
{
    const sw::string longText = "The \"Fire\" spell burns, the \"Ice\" spell freezes, and the \"Storm\" spell does both while "
                                "the caster keeps \"Focus\" for long enough to finish the incantation.";
    SW_ASSERT_TRUE( longText.size() > 120 );

    sw::XMLDocument doc;
    sw::XMLNode     root = doc.appendRoot( "Root" );
    root.appendChild( "item", sw::string_view{ longText.c_str(), longText.size() } );
    // 속성이 있는 요소의 텍스트 — 시작 태그 뒤까지 따옴표를 세면 여기서 접힌다.
    sw::XMLNode tagged = root.appendChild( "tagged", sw::string_view{ longText.c_str(), longText.size() } );
    tagged.setAttribute( "id", "7" );
    sw::XMLNode wide = root.appendChild( "wide" );
    wide.setAttribute( "first", "a long attribute value that keeps going and going" );
    wide.setAttribute( "second", "another long attribute value so the line passes the wrap column" );
    wide.setAttribute( "third", "yet another one" );

    const sw::string saved = doc.saveToString();
    sw::XMLDocument  reloaded;
    SW_ASSERT_TRUE( reloaded.parse( saved ) );
    sw::XMLNode reloadedRoot = reloaded.getRoot( "Root" );
    SW_ASSERT_TRUE( reloadedRoot.isValid() );
    SW_EXPECT_STREQ( longText.c_str(), reloadedRoot.findChildText( "item" ) );
    SW_EXPECT_STREQ( longText.c_str(), reloadedRoot.findChildText( "tagged" ) );
    SW_EXPECT_STREQ( "7", reloadedRoot.findChild( "tagged" ).findAttribute( "id" ) );

    // 속성 접기는 그대로 돈다 — 긴 속성 줄은 여러 줄이 되고 값은 같다.
    sw::XMLNode reloadedWide = reloadedRoot.findChild( "wide" );
    SW_ASSERT_TRUE( reloadedWide.isValid() );
    SW_EXPECT_STREQ( "yet another one", reloadedWide.findAttribute( "third" ) );
    SW_EXPECT_TRUE( saved.find( "\n" ) != sw::string::npos );
}

/**
 * @brief [XMLDocumentTest] 구문 오류는 `이름:줄:열: 이유` 로, 없는 파일은 `not found` 로 알린다 — 성공하면 비워진다
 * @details 오프셋만 남기면 어느 파일인지 알 수 없고, 부르는 쪽이 둘을 가를 수 없으면 구문 오류도 "File not found" 로 알린다.
 */
SW_TEST_CASE( XMLDocumentTest, ParseErrorNamesSourceLineAndColumn )
{
    sw::XMLDocument doc;
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

/**
 * @brief [XMLDocumentTest] 불리언이 아닌 글은 폴백을 쓰되 알린다 — 속성 · 자식 텍스트 모두
 * @details `getAttributeBool` · `getChildBool` 은 정수 · 실수 형제(`getAttributeInt` · `getAttributeFloat`)와 같은 규칙이다(`StringUtil::tryParseBool`).
 *          `StringUtil::parseBool` 로 읽으면 `enabled="ture"` 가 **아무 말 없이** 폴백이 된다.
 *          없거나 빈 값은 여전히 조용한 폴백이다.
 */
SW_TEST_CASE( XMLDocumentTest, UnreadableBooleanFallsBackAndSaysSo )
{
    sw::XMLDocument doc;
    SW_ASSERT_TRUE( doc.parse( R"(<Root on="yes" off="0" typo="ture" empty=""><flag>nope</flag><good>TRUE</good></Root>)" ) );
    const sw::XMLNode root = doc.getRoot( "Root" );
    SW_ASSERT_TRUE( root.isValid() );

    test::ScopedLogCollector logs;
    SW_EXPECT_TRUE( root.getAttributeBool( "on", false ) );
    SW_EXPECT_FALSE( root.getAttributeBool( "off", true ) );
    SW_EXPECT_TRUE( root.getAttributeBool( "missing", true ) );
    SW_EXPECT_TRUE( root.getAttributeBool( "empty", true ) );
    SW_EXPECT_TRUE( root.getChildBool( "good", false ) );
    SW_EXPECT_TRUE_MSG( logs.countContaining( "unreadable boolean" ) == 0, logs.joined().c_str() );

    {
        test::ScopedDefensiveTestLog expected( "non-boolean attribute and element text" );
        SW_EXPECT_TRUE( root.getAttributeBool( "typo", true ) );
        SW_EXPECT_FALSE( root.getChildBool( "flag", false ) );
    }
    SW_EXPECT_TRUE_MSG( logs.countContaining( "Attribute 'typo' has an unreadable boolean 'ture'" ) == 1, logs.joined().c_str() );
    SW_EXPECT_TRUE_MSG( logs.countContaining( "Element 'flag' has an unreadable boolean 'nope'" ) == 1, logs.joined().c_str() );
}

/**
 * @brief [XMLDocumentTest] getAttributeText 는 없는 속성에 빈 글을 준다 — string_view 를 받는 함수에 그대로 넘겨도 된다
 */
SW_TEST_CASE( XMLDocumentTest, AttributeTextIsEmptyWhenMissing )
{
    sw::XMLDocument doc;
    SW_ASSERT_TRUE( doc.parse( R"(<bind key="A" empty=""/>)" ) );
    const sw::XMLNode node = doc.getRoot( "bind" );
    SW_ASSERT_TRUE( node.isValid() );

    SW_EXPECT_TRUE( node.getAttributeText( "key" ) == "A" );
    SW_EXPECT_TRUE( node.getAttributeText( "empty" ).empty() );
    SW_EXPECT_TRUE( node.getAttributeText( "missing" ).empty() );
    SW_EXPECT_TRUE( sw::XMLNode{}.getAttributeText( "key" ).empty() );
}

/**
 * @brief [XMLDocumentTest] 범위를 정한 정수 속성은 범위 밖 · 정수가 아닌 글을 거절하고 알린다 — 좁은 칸으로 감지 않는다
 * @details 좁은 칸에 `static_cast<uint8>( getAttributeInt( … ) )` 로 넣으면 "256" 이 0, "-1" 이 255 로 감겼다(입력 맵의 패드 번호).
 *          `tryGetAttributeIntInRange` 는 없으면 폴백으로 성공, 범위 안이면 그 값, 아니면 경고하고 false(값은 폴백)다.
 */
SW_TEST_CASE( XMLDocumentTest, RangeCheckedIntegerRejectsWhatDoesNotFit )
{
    sw::XMLDocument doc;
    SW_ASSERT_TRUE( doc.parse( R"(<bind low="0" high="3" over="256" under="-1" text="two" huge="99999999999"/>)" ) );
    const sw::XMLNode node = doc.getRoot( "bind" );
    SW_ASSERT_TRUE( node.isValid() );

    test::ScopedLogCollector logs;
    int32                    value{ -7 };
    SW_EXPECT_TRUE( node.tryGetAttributeIntInRange( "missing", 1, 0, 3, value ) );
    SW_EXPECT_EQUAL( 1, value );
    SW_EXPECT_TRUE( node.tryGetAttributeIntInRange( "low", 1, 0, 3, value ) );
    SW_EXPECT_EQUAL( 0, value );
    SW_EXPECT_TRUE( node.tryGetAttributeIntInRange( "high", 1, 0, 3, value ) );
    SW_EXPECT_EQUAL( 3, value );
    SW_EXPECT_TRUE_MSG( logs.countContaining( "rejected" ) == 0, logs.joined().c_str() );

    {
        test::ScopedDefensiveTestLog expected( "out-of-range and non-integer attributes" );
        SW_EXPECT_FALSE( node.tryGetAttributeIntInRange( "over", 1, 0, 255, value ) );
        SW_EXPECT_EQUAL( 1, value );
        SW_EXPECT_FALSE( node.tryGetAttributeIntInRange( "under", 1, 0, 255, value ) );
        SW_EXPECT_FALSE( node.tryGetAttributeIntInRange( "text", 1, 0, 255, value ) );
        SW_EXPECT_FALSE( node.tryGetAttributeIntInRange( "huge", 1, 0, 255, value ) );
    }
    SW_EXPECT_TRUE_MSG( logs.countContaining( "<bind> attribute 'over' is '256', not an integer in [0, 255]" ) == 1, logs.joined().c_str() );
    SW_EXPECT_TRUE_MSG( logs.countContaining( "rejected" ) == 4, logs.joined().c_str() );
}
