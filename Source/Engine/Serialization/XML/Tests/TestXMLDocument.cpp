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
        listName.push_back( "_attribute" + sw::to_string( index ) );
        child.setAttribute( listName.back().c_str(), index == 7 ? sw::string( kTrickyValue ) : "value_number_" + sw::to_string( index ) );
    }
    const sw::string text = document.saveToString();
    SW_EXPECT_TRUE( text.find( "\n" ) != sw::string::npos );

    sw::XMLDocument reread;
    SW_REQUIRE_TRUE( reread.parse( text ) );
    const sw::XMLNode readChild = reread.getRoot( "Root" ).findChild( "Pass" );
    SW_REQUIRE_TRUE( readChild.isValid() );
    for ( int32 index = 0; index < 24; ++index )
    {
        const sw::string expected = index == 7 ? sw::string( kTrickyValue ) : "value_number_" + sw::to_string( index );
        SW_EXPECT_EQUAL( expected, sw::string( readChild.getAttributeText( listName[static_cast<size_t>( index )].c_str() ) ) );
    }
    SW_EXPECT_EQUAL( text, reread.saveToString() ); // 접은 글은 다시 저장해도 같다(diff 가 흔들리지 않는다)
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
