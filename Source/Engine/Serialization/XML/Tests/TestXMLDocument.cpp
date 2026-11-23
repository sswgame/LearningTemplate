#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

// XMLDocumentTest — 에셋 XML 의 저장 · 읽기: 긴 요소 줄을 속성 단위로 접어도(손으로 자르는 `wrapLongElementLines`) 다시 읽으면 같은 값이고,
// 값 안의 공백 · 따옴표 · `=` · `>` 에서 자르지 않는다. 실수 묶음 읽기 규칙도 본다(nogpu).

namespace
{
    constexpr const utf8* kTrickyValue = "a b=\"c\" > d' /> e";
} // namespace

/**
 * @brief [XMLDocumentTest] 속성이 많아 줄을 접은 글을 다시 읽으면 모든 속성 값이 같다 — 값 안의 공백 · 따옴표 · `=` · `>` 에서 자르지 않는다
 */
SW_TEST_CASE( XMLDocumentTest, WrappedLinesRoundTrip )
{
    sw::XMLDocument        document;
    const sw::XMLNode      root  = document.appendRoot( "Root" );
    const sw::XMLNode      child = root.appendChild( "Pass" );
    sw::vector<sw::string> listName;
    for ( int32 index = 0; index < 24; ++index )
    {
        sw::fixed_string<sw::constant::buffer::kMax32> text;
        sw::formatstring( text.data(), text.capacity(), "_attribute%#", index );
        listName.push_back( sw::string( text.view() ) );
        sw::formatstring( text.data(), text.capacity(), "value_number_%#", index );
        child.setAttribute( listName.back().c_str(), index == 7 ? sw::string_view( kTrickyValue ) : text.view() );
    }
    const sw::string text = document.saveToString();
    SW_EXPECT_TRUE( text.find( "\n" ) != sw::string::npos );

    sw::XMLDocument reread;
    SW_REQUIRE_TRUE( reread.parse( text ) );
    const sw::XMLNode readChild = reread.getRoot( "Root" ).findChild( "Pass" );
    SW_REQUIRE_TRUE( readChild.isValid() );
    for ( int32 index = 0; index < 24; ++index )
    {
        sw::fixed_string<sw::constant::buffer::kMax32> numbered;
        sw::formatstring( numbered.data(), numbered.capacity(), "value_number_%#", index );
        const sw::string expected = index == 7 ? sw::string( kTrickyValue ) : sw::string( numbered.view() );
        SW_EXPECT_EQUAL( expected, sw::string( readChild.getAttributeText( listName[static_cast<size_t>( index )].c_str() ) ) );
    }
    SW_EXPECT_EQUAL( text, reread.saveToString() ); // 접은 글은 다시 저장해도 같다(diff 가 흔들리지 않는다)
}

/**
 * @brief [XMLDocumentTest] 원소 글의 양 끝 공백이 저장 · 읽기를 지나도 남고, 여러 줄 글 속의 `<` 줄은 접히지 않는다(결함: 읽기가 양 끝을 다듬어 글 목록의 공백이 사라졌다)
 */
SW_TEST_CASE( XMLDocumentTest, ElementTextKeepsEdgeSpaces )
{
    const sw::string longTagLine( 100, 'x' );
    sw::string       multiLine = "first\n<tag a=\"1\" b=\"";
    multiLine.append( longTagLine );
    multiLine.append( "\">\n" );
    // 결함: `\r` 은 어느 글에서도 읽을 때 `\n` 이 되고, 양 끝에 공백이 있고 `]]>` 가 든 글은 공백을 잃었다.
    const sw::string arrText[] = { "  lead", "tail \t", " ", "\n", "plain", "a <b> & \"c\" ", "ends ]]> here", multiLine, " both ]]> ends ", "]]>", "a\r\nb", "tail\r", "\r", " \r\n", " a]]\r>b " };

    sw::XMLDocument   document;
    const sw::XMLNode root = document.appendRoot( "Root" );
    for ( const sw::string& text : arrText )
    {
        root.appendChild( "item" ).setValue( sw::string_view( text ) );
    }
    const sw::string saved = document.saveToString();
    sw::XMLDocument  reread;
    SW_REQUIRE_TRUE( reread.parse( saved ) );
    sw::XMLNode item = reread.getRoot( "Root" ).findChild( "item" );
    for ( const sw::string& text : arrText )
    {
        SW_REQUIRE_TRUE( item.isValid() );
        SW_EXPECT_EQUAL( text, sw::string( item.getText() ) );
        item = item.findNextSibling( "item" );
    }
    SW_EXPECT_EQUAL( saved, reread.saveToString() ); // 다시 저장해도 같다

    const sw::XMLNode first = reread.getRoot( "Root" ).findChild( "item" );
    first.setValue( "short" ); // CDATA 였던 글을 바꿔도 하나다
    SW_EXPECT_EQUAL( sw::string( "short" ), sw::string( first.getText() ) );

    // 결함: CDATA 였던 원소에 `]]>` 가 든 글을 쓰면 CDATA 가 둘로 나뉘어 저장되고 읽기는 앞의 것만 돌려주었다.
    const sw::XMLNode second = first.findNextSibling( "item" );
    second.setValue( "was ]]> raw" );
    SW_EXPECT_EQUAL( sw::string( "was ]]> raw" ), sw::string( second.getText() ) );
    first.setValue( " raw" );
    first.setValue( first.getText() ); // 제 글을 다시 쓴다
    sw::XMLDocument rewritten;
    SW_REQUIRE_TRUE( rewritten.parse( reread.saveToString() ) );
    const sw::XMLNode rewrittenFirst = rewritten.getRoot( "Root" ).findChild( "item" );
    SW_EXPECT_EQUAL( sw::string( " raw" ), sw::string( rewrittenFirst.getText() ) );
    SW_EXPECT_EQUAL( sw::string( "was ]]> raw" ), sw::string( rewrittenFirst.findNextSibling( "item" ).getText() ) );
}

/**
 * @brief [XMLDocumentTest] 실수 묶음은 정확히 그 개수만 읽고, 쓴 글을 다시 읽으면 같은 값이다
 */
SW_TEST_CASE( XMLDocumentTest, FloatListRules )
{
    float32 arrValue[3]{};
    SW_EXPECT_TRUE( sw::XMLReadUtil::readFloatList( "1, 2.5  -3", arrValue, 3 ) );
    SW_EXPECT_EQUAL( -3.0f, arrValue[2] );
    SW_EXPECT_FALSE( sw::XMLReadUtil::readFloatList( "1,2", arrValue, 3 ) );
    SW_EXPECT_FALSE( sw::XMLReadUtil::readFloatList( "1,2,3,4", arrValue, 3 ) );
    SW_EXPECT_FALSE( sw::XMLReadUtil::readFloatList( "1,x,3", arrValue, 3 ) );

    const float32    arrOriginal[3] = { 0.1f, -1234.5f, 1.0e-7f };
    const sw::string written        = sw::XMLReadUtil::makeFloatListText( arrOriginal, 3 );
    SW_REQUIRE_TRUE( sw::XMLReadUtil::readFloatList( written, arrValue, 3 ) );
    for ( int32 index = 0; index < 3; ++index )
    {
        SW_EXPECT_EQUAL( arrOriginal[index], arrValue[index] );
    }
}

/**
 * @brief [XMLDocumentTest] 목록 읽기는 모두 한 구분자(`kListSeparators`)로 가른다 — 이름 목록 · "이름:숫자" 목록 · 실수 묶음이 같은 글을 같은 낱말로 나눈다
 * @details 변이: 어느 읽기든 제 구분자로 돌리면(예전 `", "` · `",; "`) 세미콜론 · 세로줄 · 줄바꿈 줄이 진다.
 */
SW_TEST_CASE( XMLDocumentTest, ListReadsShareOneSeparatorSet )
{
    sw::vector<sw::hashed_string> listName;
    sw::XMLReadUtil::parseNameList( "a, b;c|d\te\r\nf", listName );
    SW_REQUIRE_EQUAL( 6u, static_cast<uint32>( listName.size() ) );
    SW_EXPECT_TRUE( listName[2] == sw::hashed_string( "c" ) );
    SW_EXPECT_TRUE( listName[5] == sw::hashed_string( "f" ) );
    sw::XMLReadUtil::appendNameList( " ;; g ", listName );
    SW_REQUIRE_EQUAL( 7u, static_cast<uint32>( listName.size() ) );
    sw::XMLReadUtil::parseNameList( "", listName );
    SW_EXPECT_TRUE( listName.empty() );

    int32 sum   = 0;
    int32 count = 0;
    sw::XMLReadUtil::forEachNamedNumber( "x:1; y:2|z:3\nw:4", 0, sw::XMLReadUtil::MissingValue::Reject,
                                         [&]( sw::string_view, sw::string_view, int32 value, bool bValid )
    {
        sum += bValid ? value : 100;
        ++count;
    } );
    SW_EXPECT_EQUAL( 4, count );
    SW_EXPECT_EQUAL( 10, sum );

    float32 arrValue[3]{};
    SW_EXPECT_TRUE( sw::XMLReadUtil::readFloatList( "1;2|3", arrValue, 3 ) );
    SW_EXPECT_EQUAL( 3.0f, arrValue[2] );
}

/**
 * @brief [XMLDocumentTest] 한 요소의 중복 속성은 읽기 실패다 — 예전에는 pugixml 이 받아 이름으로 찾을 때 첫 값만 읽히고 둘째 값이 조용히 버려졌다
 */
SW_TEST_CASE( XMLDocumentTest, DuplicateAttributeIsAParseError )
{
    sw::XMLDocument document;
    SW_EXPECT_TRUE( document.parse( R"(<Root a="1"><Child a="2" b="3"/></Root>)" ) ); // 다른 요소의 같은 이름은 중복이 아니다
    SW_EXPECT_LOG( "Xml.ParseError" );
    SW_EXPECT_FALSE( document.parse( R"(<Root><Child a="1" a="2"/></Root>)", "dup.xml" ) );
    SW_EXPECT_TRUE( document.getLastError().find( "repeats the attribute 'a'" ) != sw::string::npos );
}

/**
 * @brief [XMLDocumentTest] 필수 실수 셋 속성은 정확히 세 실수일 때만 읽힌다 — 없거나 모양이 다르면 어느 파일 · 원소 · 속성인지 알리고 false 이며 값은 그대로다
 */
SW_TEST_CASE( XMLDocumentTest, RequiredFloat3NeedsTheAttribute )
{
    sw::XMLDocument document;
    SW_REQUIRE_TRUE( document.parse( R"(<Root><Point offset="1 2.5 -3" pair="1 2"/></Root>)" ) );
    const sw::XMLNode point = document.getRoot( "Root" ).findChild( "Point" );

    sw::float3 value{ 9.0f, 9.0f, 9.0f };
    SW_REQUIRE_TRUE( sw::XMLReadUtil::readRequiredFloat3( point, "offset", "test.xml", value ) );
    SW_EXPECT_EQUAL( 1.0f, value._x );
    SW_EXPECT_EQUAL( 2.5f, value._y );
    SW_EXPECT_EQUAL( -3.0f, value._z );

    SW_EXPECT_LOG( "GameData.AttributeMissing" );
    SW_EXPECT_FALSE( sw::XMLReadUtil::readRequiredFloat3( point, "absent", "test.xml", value ) );
    SW_EXPECT_LOG( "GameData.AttributeShapeMismatch" );
    SW_EXPECT_FALSE( sw::XMLReadUtil::readRequiredFloat3( point, "pair", "test.xml", value ) );
    SW_EXPECT_EQUAL( 2.5f, value._y ); // 실패한 읽기는 값을 바꾸지 않는다
}
