#include "pch.h"

#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

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
    std::optional<int32> missingValue{ 12 };
    SW_EXPECT_TRUE( root.readOptionalAttribute( "missing", missingValue ) );
    SW_EXPECT_FALSE( missingValue.has_value() );

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

    SW_EXPECT_FALSE( doc.loadResource( "no/such/dir/missing.xml" ) );
    SW_EXPECT_TRUE_MSG( doc.getLastError().find( "missing.xml: not found" ) != sw::string::npos, doc.getLastError().c_str() );

    SW_EXPECT_TRUE( doc.parse( "<Root/>" ) );
    SW_EXPECT_TRUE( doc.getLastError().empty() );
}

/**
 * @brief [XMLDocumentTest] 불리언이 아닌 글은 폴백이 되지 않고 읽기 실패다 — 속성 · 자식 텍스트 모두, 빈 글도
 * @details 폴백을 받던 `getAttributeBool` · `getChildBool` 은 `enabled="ture"` 를 경고 한 줄과 함께 폴백으로 읽었다(D9 에서 지웠다).
 *          선택 읽기는 없음(빈 optional · true)과 깨짐(false · 오류)을 가르고, 기본값은 부르는 쪽 Def 의 초기값이 정한다.
 */
SW_TEST_CASE( XMLDocumentTest, UnreadableBooleanIsAReadFailure )
{
    sw::XMLDocument doc;
    SW_ASSERT_TRUE( doc.parse( R"(<Root on="yes" off="0" typo="ture" empty=""><flag>nope</flag><good>TRUE</good></Root>)" ) );
    const sw::XMLNode root = doc.getRoot( "Root" );
    SW_ASSERT_TRUE( root.isValid() );

    test::ScopedLogCollector logs;
    std::optional<bool>      value;
    SW_EXPECT_TRUE( root.readOptionalAttribute( "on", value ) && value == true );
    SW_EXPECT_TRUE( root.readOptionalAttribute( "off", value ) && value == false );
    SW_EXPECT_TRUE( root.readOptionalAttribute( "missing", value ) && value.has_value() == false );
    SW_EXPECT_TRUE( root.readOptionalChild( "good", value ) && value == true );
    SW_EXPECT_TRUE_MSG( logs.countContaining( "is not a boolean" ) == 0, logs.joined().c_str() );

    {
        test::ScopedDefensiveTestLog expected( "non-boolean attribute and element text" );
        SW_EXPECT_FALSE( root.readOptionalAttribute( "typo", value ) );
        SW_EXPECT_FALSE( root.readOptionalAttribute( "empty", value ) );
        SW_EXPECT_FALSE( root.readOptionalChild( "flag", value ) );
    }
    SW_EXPECT_TRUE_MSG( logs.countContaining( "<Root> attribute 'typo' is not a boolean: 'ture'" ) == 1, logs.joined().c_str() );
    SW_EXPECT_TRUE_MSG( logs.countContaining( "<Root> child 'flag' is not a boolean: 'nope'" ) == 1, logs.joined().c_str() );
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
 *          `readOptionalAttributeInRange` 는 없으면 넣어 둔 값(Def 초기값) 그대로 성공, 범위 안이면 그 값, 아니면 오류와 false(값은 그대로 — 자르지 않는다)다.
 */
SW_TEST_CASE( XMLDocumentTest, RangeCheckedIntegerRejectsWhatDoesNotFit )
{
    sw::XMLDocument doc;
    SW_ASSERT_TRUE( doc.parse( R"(<bind low="0" high="3" over="256" under="-1" text="two" huge="99999999999"/>)" ) );
    const sw::XMLNode node = doc.getRoot( "bind" );
    SW_ASSERT_TRUE( node.isValid() );

    test::ScopedLogCollector logs;
    int32                    value{ 1 };
    SW_EXPECT_TRUE( node.readOptionalAttributeInRange( "missing", 0, 3, value ) );
    SW_EXPECT_EQUAL( 1, value );
    SW_EXPECT_TRUE( node.readOptionalAttributeInRange( "low", 0, 3, value ) );
    SW_EXPECT_EQUAL( 0, value );
    SW_EXPECT_TRUE( node.readOptionalAttributeInRange( "high", 0, 3, value ) );
    SW_EXPECT_EQUAL( 3, value );
    SW_EXPECT_TRUE_MSG( logs.countContaining( "outside" ) == 0, logs.joined().c_str() );

    {
        test::ScopedDefensiveTestLog expected( "out-of-range and non-integer attributes" );
        SW_EXPECT_FALSE( node.readOptionalAttributeInRange( "over", 0, 255, value ) );
        SW_EXPECT_EQUAL( 3, value );
        SW_EXPECT_FALSE( node.readOptionalAttributeInRange( "under", 0, 255, value ) );
        SW_EXPECT_FALSE( node.readOptionalAttributeInRange( "text", 0, 255, value ) );
        SW_EXPECT_FALSE( node.readOptionalAttributeInRange( "huge", 0, 255, value ) );
    }
    SW_EXPECT_TRUE_MSG( logs.countContaining( "<bind over=" ) == 1, logs.joined().c_str() );
    SW_EXPECT_TRUE_MSG( logs.countContaining( "outside" ) == 2, logs.joined().c_str() );
    SW_EXPECT_TRUE_MSG( logs.countContaining( "is not a integer" ) == 2, logs.joined().c_str() );
}

/**
 * @brief [XMLReadUtilTest] 루트 찾기는 "읽지 못함" 과 "루트 없음" 을 가르고, 이름 읽기는 앞뒤 공백을 뺀다
 * @details 캐릭터 데이터와 게임 데이터 표가 이 결과로 각자 오류를 모으거나 경고를 남긴다. 둘을 가르지 못하면 구문 오류를 "루트가 없다" 로 알린다.
 */
SW_TEST_CASE( XMLReadUtilTest, RootStatusAndTrimmedName )
{
    sw::XMLDocument doc;
    sw::XMLNode     root;
    SW_EXPECT_TRUE( sw::XMLReadUtil::parseRoot( doc, R"(<Table><row name="  sword  " blank="   "/></Table>)", "test", "table", root ) ==
                    sw::XMLRootStatus::Found );
    SW_ASSERT_TRUE( root.isValid() );

    const sw::XMLNode row = root.findChild( "row" );
    SW_EXPECT_TRUE( sw::XMLReadUtil::readName( row, "name" ) == sw::hashed_string( "sword" ) );
    SW_EXPECT_TRUE( sw::XMLReadUtil::readName( row, "blank" ).empty() );
    SW_EXPECT_TRUE( sw::XMLReadUtil::readName( row, "missing" ).empty() );

    // 글 "none" 은 빈 이름과 같아 이름으로 담지 못한다 — 오류로 알리고, 글 그대로는 속성에 남아 있다(C-59).
    sw::XMLDocument noneDoc;
    sw::XMLNode     noneRoot;
    SW_EXPECT_TRUE( sw::XMLReadUtil::parseRoot( noneDoc, R"(<Table><row align=" None "/></Table>)", "test", "Table", noneRoot ) == sw::XMLRootStatus::Found );
    const sw::XMLNode noneRow = noneRoot.findChild( "row" );
    {
        test::ScopedDefensiveTestLog expected( "a name attribute spelled 'none' is a data error" );
        SW_EXPECT_TRUE( sw::XMLReadUtil::readName( noneRow, "align" ).empty() );
    }
    SW_EXPECT_TRUE( sw::string_view( noneRow.getAttributeText( "align" ) ).find( "None" ) != sw::string_view::npos );

    SW_EXPECT_TRUE( sw::XMLReadUtil::parseRoot( doc, "<Other/>", "test", "Table", root ) == sw::XMLRootStatus::RootMissing );
    SW_EXPECT_FALSE( root.isValid() );
    {
        test::ScopedDefensiveTestLog expected( "unclosed root element" );
        SW_EXPECT_TRUE( sw::XMLReadUtil::parseRoot( doc, "<Table>", "test", "Table", root ) == sw::XMLRootStatus::NotLoaded );
    }
    SW_EXPECT_FALSE( doc.getLastError().empty() );
}
