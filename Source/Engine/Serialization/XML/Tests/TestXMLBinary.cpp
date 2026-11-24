// XMLBinaryTest — 이진 쿠킹본: 글 문서와 같은 핸들로 같은 값을 읽고, 글로 되살리면 같은 문서이며, 깨진 바이트는 거절하고, 쓰기는 아무것도 바꾸지 않는다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Inherit/ResourceComposer.h"
#include "Engine/Serialization/XML/XMLBinary.h"
#include "Engine/Serialization/XML/XMLDocument.h"

using namespace sw;

namespace
{
    constexpr const utf8* kXMLBinaryTestText = R"(<!-- 주석은 담지 않는다 -->
<CropCatalog season="spring">
    <Crop id="turnip" days="4"><Note>a &lt;b&gt; &amp; c</Note></Crop>
    <Crop id="potato" days="12" empty=""/>
    <Rule/>
</CropCatalog>)";

    string_view viewBytes( const vector<uint8>& bytes )
    {
        return string_view( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() );
    }
} // namespace

/** @brief [XMLBinaryTest] 글 문서와 이진 쿠킹본을 같은 방법으로 읽으면 이름 · 속성 · 글 · 차례가 같다. 쓰기 함수는 쿠킹본을 바꾸지 않고, 다른 문서로 복사 · 글로 되살리기는 된다 */
SW_TEST_CASE( XMLBinaryTest, CookedDocumentReadsLikeTheText )
{
    XMLDocument text;
    SW_REQUIRE_TRUE( text.parse( kXMLBinaryTestText ) );
    vector<uint8> bytes;
    SW_REQUIRE_TRUE( XMLBinary::encode( text.getRoot(), bytes ) );
    SW_EXPECT_TRUE( XMLBinary::isBinary( viewBytes( bytes ) ) );
    SW_EXPECT_FALSE( XMLBinary::isBinary( kXMLBinaryTestText ) );

    XMLDocument cooked;
    SW_REQUIRE_TRUE( cooked.parse( viewBytes( bytes ), "cooked" ) );
    SW_EXPECT_TRUE( cooked.isCooked() && text.isCooked() == false );
    SW_EXPECT_FALSE( cooked.getRoot( "Other" ).isValid() );
    const XMLNode root = cooked.getRoot( "CropCatalog" );
    SW_REQUIRE_TRUE( root.isElement() );
    SW_EXPECT_EQUAL( string( "spring" ), string( root.getAttributeText( "season" ) ) );
    const XMLNode first = root.findChild( "Crop" );
    SW_EXPECT_EQUAL( string( "turnip" ), string( first.getAttributeText( "id" ) ) );
    SW_EXPECT_EQUAL( 4, first.readRequiredAttribute<int32>( "days" ).value_or( 0 ) );
    SW_EXPECT_EQUAL( string( "a <b> & c" ), string( first.findChildText( "Note" ) ) );
    const XMLNode second = first.findNextSibling( "Crop" );
    SW_EXPECT_EQUAL( string( "potato" ), string( second.getAttributeText( "id" ) ) );
    SW_EXPECT_TRUE( second.hasAttribute( "empty" ) && second.hasAttribute( "none" ) == false );
    SW_EXPECT_EQUAL( string( "Rule" ), string( second.findNextSibling().getName() ) );
    SW_EXPECT_FALSE( second.findNextSibling( "Crop" ).isValid() );
    uint32 attributeCount = 0;
    for ( XMLAttribute attribute = second.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
        ++attributeCount;
    SW_EXPECT_EQUAL( 3u, attributeCount );
    SW_EXPECT_FALSE( second.getSourceOffset().has_value() );

    root.setAttribute( "season", "winter" ); // 읽기 전용이다
    SW_EXPECT_FALSE( root.appendChild( "Crop" ).isValid() );
    SW_EXPECT_EQUAL( string( "spring" ), string( root.getAttributeText( "season" ) ) );

    string decoded;
    SW_REQUIRE_TRUE( XMLBinary::decodeText( viewBytes( bytes ), decoded ) );
    XMLDocument reread;
    SW_REQUIRE_TRUE( reread.parse( decoded ) );
    SW_EXPECT_EQUAL( text.getRoot().toString(), reread.getRoot().toString() );
    SW_EXPECT_EQUAL( text.getRoot().toString(), root.toString() );
    XMLDocument   target;
    const XMLNode copied = target.appendRoot( "Root" ).appendClone( first );
    SW_EXPECT_EQUAL( string( "a <b> & c" ), string( copied.findChildText( "Note" ) ) );

    // 합치기는 쿠킹본을 글로 되돌리지 않는다(층이 없을 때).
    string composedText;
    bool   bComposed = true;
    SW_EXPECT_TRUE( ResourceComposer::compose( viewBytes( bytes ), "cooked", ResourceContext::forDocument(), composedText, bComposed ) );
    SW_EXPECT_FALSE( bComposed );
}

/** @brief [XMLBinaryTest] 잘린 바이트 · 범위 밖 번호 · 뒤로 도는 번호 · 다른 판은 거절한다 — 하나씩 뒤집은 바이트는 거절되거나 범위 안에서 읽힌다 */
SW_TEST_CASE( XMLBinaryTest, BrokenBytesAreRefused )
{
    XMLDocument text;
    SW_REQUIRE_TRUE( text.parse( kXMLBinaryTestText ) );
    vector<uint8> bytes;
    SW_REQUIRE_TRUE( XMLBinary::encode( text.getRoot(), bytes ) );

    vector<uint8> truncated( bytes.begin(), bytes.end() - 1 );
    SW_EXPECT_FALSE( XMLBinary::validate( viewBytes( truncated ) ) );
    vector<uint8> otherVersion = bytes;
    reinterpret_cast<XMLBinaryHeader*>( otherVersion.data() )->_version += 1;
    SW_EXPECT_FALSE( XMLBinary::validate( viewBytes( otherVersion ) ) );
    vector<uint8> loop                                                                       = bytes;
    reinterpret_cast<XMLBinaryNode*>( loop.data() + sizeof( XMLBinaryHeader ) )->_firstChild = 0; // 자기 자신
    SW_EXPECT_FALSE( XMLBinary::validate( viewBytes( loop ) ) );
    vector<uint8> outside                                                                 = bytes;
    reinterpret_cast<XMLBinaryNode*>( outside.data() + sizeof( XMLBinaryHeader ) )->_name = 0xFFFFFF;
    SW_EXPECT_FALSE( XMLBinary::validate( viewBytes( outside ) ) );
    SW_EXPECT_LOG( "Xml.CookedBroken" );
    XMLDocument document;
    SW_EXPECT_FALSE( document.parse( viewBytes( outside ), "broken" ) );
    SW_EXPECT_FALSE( document.getRoot().isValid() );

    for ( size_t index = 0; index < bytes.size(); ++index )
    {
        vector<uint8> flipped = bytes;
        flipped[index] ^= 0xFFu;
        string decoded;
        (void)XMLBinary::decodeText( viewBytes( flipped ), decoded ); // 거절하거나 범위 안을 읽는다(ASan 이 본다)
    }
}
