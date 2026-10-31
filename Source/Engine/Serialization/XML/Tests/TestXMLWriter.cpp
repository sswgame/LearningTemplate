// XMLWriter — 속성 · 글 이스케이프, 금지 바이트 바꾸기, 빈 원소, 중첩 · 배치, 이름 검사, 주석, 조각 옮기기(nogpu).
#include "pch.h"

#include "Core/Testing/ScopedAssertCapture.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLWriter.h"

/**
 * @brief [XMLWriterTest] 속성 값과 글은 이스케이프되고, 다시 읽으면 같은 값이다
 */
SW_TEST_CASE( XMLWriterTest, EscapesAttributesAndText )
{
    sw::string    xml;
    sw::XMLWriter writer{ xml };
    writer.beginElement( "Root" );
    writer.attribute( "name", "a\"b&c<d>e" );
    writer.attribute( "lines", "x\ty\nz" );
    writer.attributeInt( "count", -3 );
    writer.text( "1 < 2 & \"ok\"\n" );
    writer.endElement();
    SW_EXPECT_TRUE( writer.isComplete() );
    SW_EXPECT_EQUAL( sw::string( "<Root name=\"a&quot;b&amp;c&lt;d&gt;e\" lines=\"x&#9;y&#10;z\" count=\"-3\">1 &lt; 2 &amp; \"ok\"\n</Root>" ), xml );

    sw::XMLDocument document;
    SW_REQUIRE_TRUE( document.parse( xml ) );
    const sw::XMLNode root = document.getRoot( "Root" );
    SW_REQUIRE_TRUE( root.isValid() );
    const utf8* pName  = root.findAttribute( "name" );
    const utf8* pLines = root.findAttribute( "lines" );
    SW_REQUIRE_TRUE( pName != nullptr && pLines != nullptr );
    SW_EXPECT_EQUAL( sw::string( "a\"b&c<d>e" ), sw::string( pName ) );
    SW_EXPECT_EQUAL( sw::string( "x\ty\nz" ), sw::string( pLines ) );
}

/**
 * @brief [XMLWriterTest] XML 1.0 에 없는 제어 문자와 잘못된 UTF-8 은 U+FFFD 로 바뀌고 올바른 UTF-8 은 그대로다
 */
SW_TEST_CASE( XMLWriterTest, ReplacesForbiddenBytes )
{
    sw::string    xml;
    sw::XMLWriter writer{ xml };
    writer.beginElement( "T" );
    writer.attribute( "v", sw::string_view{ "a\x01"
                                            "b\xFF"
                                            "c\xEA\xB0\x80",
                                            8 } );
    writer.endElement();
    SW_EXPECT_EQUAL( sw::string( "<T v=\"a\xEF\xBF\xBD"
                                 "b\xEF\xBF\xBD"
                                 "c\xEA\xB0\x80\"/>" ),
                     xml );
}

/**
 * @brief [XMLWriterTest] 내용이 없는 원소는 `/>` 로 닫고, 중첩 · 줄 배치 · 선언 · 주석이 차례대로 나간다
 */
SW_TEST_CASE( XMLWriterTest, NestsAndLaysOut )
{
    sw::string    xml;
    sw::XMLWriter writer{ xml };
    writer.declaration();
    writer.comment( "note" );
    writer.whitespace( "\n" );
    writer.beginElement( "A" );
    writer.whitespace( "\n\t" );
    writer.beginElement( "B" );
    writer.beginElement( "C" );
    writer.endElement();
    writer.endElement();
    writer.whitespace( "\n" );
    writer.endElement();
    SW_EXPECT_TRUE( writer.isComplete() );
    SW_EXPECT_EQUAL( sw::string( "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<!-- note -->\n<A>\n\t<B><C/></B>\n</A>" ), xml );
}

/**
 * @brief [XMLWriterTest] 따로 쓴 조각을 옮길 수 있고, 다 쓰지 않은 조각은 받지 않는다
 */
SW_TEST_CASE( XMLWriterTest, MovesCompleteFragments )
{
    sw::XMLWriter piece;
    SW_EXPECT_TRUE( piece.isEmpty() );
    piece.beginElement( "Step" );
    piece.attribute( "k", "v" );
    piece.endElement();
    SW_EXPECT_FALSE( piece.isEmpty() );

    sw::string    xml;
    sw::XMLWriter writer{ xml };
    writer.beginElement( "At" );
    writer.fragment( piece );
    writer.endElement();
    SW_EXPECT_TRUE( writer.isComplete() );
    SW_EXPECT_EQUAL( sw::string( "<At><Step k=\"v\"/></At>" ), xml );

    sw::test::ScopedAssertCapture assertCapture;
    sw::XMLWriter                 open;
    open.beginElement( "Open" );
    sw::string    other;
    sw::XMLWriter otherWriter{ other };
    otherWriter.beginElement( "At" );
    otherWriter.fragment( open );
    otherWriter.endElement();
    SW_EXPECT_FALSE( otherWriter.isComplete() );
}

/**
 * @brief [XMLWriterTest] 이름 글자가 아닌 이름 · 원소 밖의 속성 · 글 뒤의 속성 · `--` 주석 · 글자가 든 배치 · 짝 없는 닫기는 단언이고 완성되지 않는다
 */
SW_TEST_CASE( XMLWriterTest, MisuseIsReported )
{
    sw::test::ScopedAssertCapture assertCapture;
    for ( uint32 caseIndex = 0; caseIndex < 6; ++caseIndex )
    {
        sw::string    xml;
        sw::XMLWriter writer{ xml };
        switch ( caseIndex )
        {
            case 0:
            {
                writer.beginElement( "A\" onload=\"x" );
                break;
            }
            case 1:
            {
                writer.attribute( "a", "b" );
                break;
            }
            case 2:
            {
                writer.beginElement( "A" );
                writer.text( "t" );
                writer.attribute( "a", "b" );
                writer.endElement();
                break;
            }
            case 3:
            {
                writer.comment( "a -- b" );
                break;
            }
            case 4:
            {
                writer.whitespace( " x " );
                break;
            }
            default:
            {
                writer.endElement();
                break;
            }
        }
        SW_EXPECT_FALSE( writer.isComplete() );
    }
    if ( sw::test::ScopedAssertCapture::kAssertsAreActive )
        SW_EXPECT_EQUAL( 6u, assertCapture.getCount() );
}
