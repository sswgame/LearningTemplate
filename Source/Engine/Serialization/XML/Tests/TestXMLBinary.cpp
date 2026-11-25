// XMLBinaryTest — 이진 쿠킹본: 글 문서와 같은 핸들로 같은 값을 읽고, 글로 되살리면 같은 문서이며, 깨진 바이트는 거절하고, 쓰기는 아무것도 바꾸지 않는다(nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"
#include "Engine/Serialization/Inherit/ResourceComposition.h"
#include "Engine/Serialization/XML/XMLBinary.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include <cstring>

using namespace sw;
using sw::test::ScopedScratchDomain;

namespace
{
    /** @brief 케이스 동안 덮어쓰기 층을 바꾸고 소멸자에서 되돌립니다. */
    class XMLBinaryTestLayers
    {
    public:
        explicit XMLBinaryTestLayers( const string& root )
            : _listPrevious{ engine::getSubsystem<ResourceComposition>().getLayers() }
        {
            (void)engine::getSubsystem<ResourceComposition>().setLayers( { root } );
        }
        ~XMLBinaryTestLayers()
        {
            (void)engine::getSubsystem<ResourceComposition>().setLayers( _listPrevious );
            vector<string> listPending;
            engine::getSubsystem<ResourceComposition>().takePendingReload( listPending );
        }
        XMLBinaryTestLayers( const XMLBinaryTestLayers& )            = delete;
        XMLBinaryTestLayers& operator=( const XMLBinaryTestLayers& ) = delete;

    private:
        vector<string> _listPrevious;
    };

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

/** @brief [XMLBinaryTest] 이진 쿠킹본인 카탈로그에 층(모드 · DLC)이 행을 더한다 — 층 파일이 글이든 이진 쿠킹본이든 원본 행이 남고, 쿠킹본의 항목을 부모(`_base`)로 삼는다 */
SW_TEST_CASE( XMLBinaryTest, LayersAddRowsToACookedCatalog )
{
    const ScopedScratchDomain domain;
    const AssetID             id   = ScopedScratchDomain::makeID( "data/items.xml" );
    const string              root = ScopedScratchDomain::makeID( "layers/mod" ).str();
    (void)domain.makeDiskFolder( "data" );
    (void)domain.makeDiskFolder( "layers/mod/testproject/data" );
    const auto makeCooked = []( const utf8* pText )
    {
        XMLDocument   document;
        vector<uint8> bytes;
        SW_EXPECT_TRUE( document.parse( pText ) && XMLBinary::encode( document.getRoot(), bytes ) );
        return bytes;
    };
    const vector<uint8> baseBytes = makeCooked( R"(<ItemCatalog><Item id="a" name="n" category="C"/><Item id="b" name="m" category="D"/></ItemCatalog>)" );
    SW_REQUIRE_TRUE( FileUtil::writeFile( domain.makeDiskPath( "data/items.xml" ), baseBytes.data(), baseBytes.size() ) );

    const AbsolutePath    layerPath  = domain.makeDiskPath( "layers/mod/testproject/data/items.xml" );
    constexpr const utf8* kLayerText = R"(<ItemCatalog><Item id="b" name="changed"/><Item id="c" _base="a" name="added"/></ItemCatalog>)";
    const vector<uint8>   layerBytes = makeCooked( R"(<ItemCatalog><Item id="b" name="changed"/><Item id="c" name="added" category="C"/></ItemCatalog>)" );
    const auto            expectRows = [&id]()
    {
        XMLDocument document;
        SW_REQUIRE_TRUE( document.loadResource( id ) );
        uint32 rowCount = 0;
        for ( XMLNode item = document.getRoot( "ItemCatalog" ).findChild( "Item" ); item.isValid(); item = item.findNextSibling( "Item" ) )
        {
            const string_view itemID = item.getAttributeText( "id" );
            ++rowCount;
            SW_EXPECT_EQUAL( string( itemID == "a" ? "n" : ( itemID == "b" ? "changed" : "added" ) ), string( item.getAttributeText( "name" ) ) );
            SW_EXPECT_EQUAL( string( itemID == "b" ? "D" : "C" ), string( item.getAttributeText( "category" ) ) );
        }
        SW_EXPECT_EQUAL( 3u, rowCount );
    };
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( layerPath, kLayerText ) );
    const XMLBinaryTestLayers layers( root );
    expectRows();
    SW_REQUIRE_TRUE( FileUtil::writeFile( layerPath, layerBytes.data(), layerBytes.size() ) );
    expectRows();
}

/** @brief [XMLBinaryTest] 상한보다 깊은 문서는 글도 이진 쿠킹본도 읽지 않는다 — 재귀로 도는 코드의 C 스택을 지킨다 */
SW_TEST_CASE( XMLBinaryTest, TooDeepDocumentsAreRefused )
{
    const auto makeNested = []( uint32 depth )
    {
        string text;
        for ( uint32 index = 0; index < depth; ++index )
            text.append( "<a>" );
        text.append( "x" );
        for ( uint32 index = 0; index < depth; ++index )
            text.append( "</a>" );
        return text;
    };
    XMLDocument limit;
    SW_REQUIRE_TRUE( limit.parse( makeNested( XMLDocument::kMaxDepth ) ) );
    vector<uint8> bytes;
    SW_REQUIRE_TRUE( XMLBinary::encode( limit.getRoot(), bytes ) );
    SW_EXPECT_TRUE( XMLBinary::validate( viewBytes( bytes ) ) );

    SW_EXPECT_LOG( "Xml.TooDeep" );
    XMLDocument deep;
    SW_EXPECT_FALSE( deep.parse( makeNested( XMLDocument::kMaxDepth + 1 ) ) );
    SW_EXPECT_FALSE( deep.getRoot().isValid() );

    // 쿠킹본은 글에서 만들 수 없는 깊이라 손으로 잇는다 — 앞 순서 레코드마다 다음 레코드가 첫 자식이다.
    const uint32          nodeCount = XMLDocument::kMaxDepth + 1;
    const XMLBinaryHeader header{ XMLBinary::kMagic, XMLBinary::kVersion, nodeCount, 0, 2 };
    vector<uint8>         chain( sizeof( header ) + nodeCount * sizeof( XMLBinaryNode ) + header._stringBytes, uint8{ 0 } );
    std::memcpy( chain.data(), &header, sizeof( header ) );
    chain[chain.size() - 2] = 'a';
    for ( uint32 index = 0; index < nodeCount; ++index )
    {
        const size_t        offset = sizeof( header ) + index * sizeof( XMLBinaryNode );
        const XMLBinaryNode node{ static_cast<uint32>( offset ), 0, 1, XMLBinary::kNone, index + 1 < nodeCount ? index + 1 : XMLBinary::kNone, XMLBinary::kNone };
        std::memcpy( chain.data() + offset, &node, sizeof( node ) );
    }
    SW_EXPECT_FALSE( XMLBinary::validate( viewBytes( chain ) ) );
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
