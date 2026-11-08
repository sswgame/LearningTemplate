#include "pch.h"

#include "Core/Container/unordered_map.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/AssetIO.h"
#include "Engine/Content/Pack/AssetCookRoot.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Reflection/Reflection.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Inherit/DataCatalogInheritance.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"
#include "Engine/Serialization/XML/XMLDocument.h"

using namespace sw;

namespace
{
    /** @brief 부모를 글 표에서 읽습니다. */
    class ResourceComposerTestSource final : public IDataInheritanceSource
    {
    public:
        void add( const utf8* pID, const utf8* pText ) { _mapText[pID] = pText; }

        [[nodiscard]] bool readText( string_view assetID, string& outText ) override
        {
            const auto it = _mapText.find( string{ assetID } );
            if ( it == _mapText.end() )
                return false;
            outText = it->second;
            return true;
        }

    private:
        unordered_map<string, string> _mapText;
    };

    struct ResourceComposerTestUtil
    {
        /** @brief 두 규칙이 합치는 데 쓰는 칸(이름 · 열쇠 · 자식)이 같으면 true 입니다. 칸 순서는 보지 않습니다. */
        static bool isSameMergeRule( const DataInheritanceSchema& left, const DataInheritanceSchema& right )
        {
            if ( left._keyAttribute != right._keyAttribute || left._listField.size() != right._listField.size() )
                return false;
            for ( const DataInheritanceSchema& field : left._listField )
            {
                const DataInheritanceSchema* pOther = right.findField( field._name );
                if ( pOther == nullptr || isSameMergeRule( field, *pOther ) == false )
                    return false;
            }
            return true;
        }
    };
} // namespace

/** @brief [ResourceComposerTest] 얹을 차이가 없는 글은 어느 문맥에서도 그대로 지나간다 */
SW_TEST_CASE( ResourceComposerTest, TextWithoutBaseIsLeftAlone )
{
    ResourceComposerTestSource source;
    string                     outText;
    bool                       bComposed = true;
    SW_REQUIRE_TRUE( ResourceComposer::compose( "<Doc />", "a", ResourceContext::forDocument(), outText, bComposed ) );
    SW_EXPECT_FALSE( bComposed );
    SW_REQUIRE_TRUE( ResourceComposer::compose( "not xml", "a", ResourceContext::forPackCook( source ), outText, bComposed ) );
    SW_EXPECT_FALSE( bComposed );
}

/** @brief [ResourceComposerTest] 범용 문서 읽기는 카탈로그 표에 없는 루트의 `_base` 를 건드리지 않는다(프리팹처럼 자기 입구가 합치는 형식) */
SW_TEST_CASE( ResourceComposerTest, DocumentReadLeavesUnlistedRootsToTheirReader )
{
    string outText;
    bool   bComposed = true;
    SW_REQUIRE_TRUE( ResourceComposer::compose( R"(<Prefab _base="x.prefab.xml" />)", "a", ResourceContext::forDocument(), outText, bComposed ) );
    SW_EXPECT_FALSE( bComposed );
}

/** @brief [ResourceComposerTest] 쿠킹에서 규칙을 찾지 못한 루트는 표에 적으라는 오류로 멈춘다 — 게임 · 키트 형식은 쿠킹 때 코드가 올라와 있지 않다 */
SW_TEST_CASE( ResourceComposerTest, CookStopsWhenNoRuleIsVisible )
{
    ResourceComposerTestSource source;
    source.add( "base.xml", "<NoSuchFormatAnywhere />" );
    string outText;
    bool   bComposed = false;
    SW_EXPECT_LOG( "DataInheritance.NoSchema" );
    SW_EXPECT_FALSE( ResourceComposer::compose( R"(<NoSuchFormatAnywhere _base="base.xml" />)", "a", ResourceContext::forPackCook( source ), outText, bComposed ) );
}

/** @brief [ResourceComposerTest] 문서 표의 줄이 규칙이 되고, 그 규칙으로 쿠킹이 펼친다 */
SW_TEST_CASE( ResourceComposerTest, DocumentTableRowParses )
{
    constexpr const utf8* kTable = R"(<Table><Document root="Kit"><Field name="_listX" key="_name" /></Document></Table>)";

    DataInheritanceSchema schema{ "Kit" };
    bool                  bListed = false;
    SW_REQUIRE_TRUE( DataCatalogInheritance::parseDocumentRule( kTable, "Kit", schema, bListed ) );
    SW_EXPECT_TRUE( bListed );
    SW_REQUIRE_TRUE( schema.findField( "_listX" ) != nullptr );
    SW_EXPECT_EQUAL( string( "_name" ), schema.findField( "_listX" )->_keyAttribute );

    SW_REQUIRE_TRUE( DataCatalogInheritance::parseDocumentRule( kTable, "Other", schema, bListed ) );
    SW_EXPECT_FALSE( bListed );
}

/** @brief [ResourceComposerTest] 규칙 표는 코드 뿌리라 배포 팩에 담긴다 — 표가 `Config/Dev` 에 있던 때는 배포 구성의 카탈로그 층이 표를 읽지 못해 오류로 멈췄다 */
SW_TEST_CASE( ResourceComposerTest, RuleTableIsACookRootResource )
{
    SW_EXPECT_TRUE( AssetCookRootRegistry::hasRoot( DataCatalogInheritance::kTableAssetID ) );
    string text;
    SW_REQUIRE_TRUE( AssetIO::readTextAsset( AssetID{ DataCatalogInheritance::kTableAssetID }, text ) );
}

/**
 * @brief [ResourceComposerTest] 문서 표의 리플렉션 형식 줄은 그 형식의 `PROPERTY( InheritKey )` 와 같다
 * @details 게이트를 두는 까닭: 쿠킹은 게임 · 키트 코드를 보지 못해 표를 읽고 개발 실행은 코드에서 규칙을 만든다. 둘이 어긋나면 쿠킹본만 다른 값이 되는데 형식으로 막을 수 없다. 이 실행 파일에 올라온 형식만 본다.
 */
SW_TEST_CASE( ResourceComposerTest, DocumentTableMatchesReflection )
{
    string text;
    SW_REQUIRE_TRUE( AssetIO::readTextAsset( AssetID{ DataCatalogInheritance::kTableAssetID }, text ) );
    XMLDocument doc;
    SW_REQUIRE_TRUE( doc.parse( text, DataCatalogInheritance::kTableAssetID ) );
    SW_REQUIRE_TRUE( doc.getRoot().findChild( "Document" ).isValid() );
    for ( XMLNode row = doc.getRoot().findChild( "Document" ); row.isValid(); row = row.findNextSibling( "Document" ) )
    {
        const string          rootName{ row.getAttributeText( "root" ) };
        DataInheritanceSchema schema{ rootName };
        bool                  bListed = false;
        SW_REQUIRE_TRUE( DataCatalogInheritance::parseDocumentRule( text, rootName, schema, bListed ) );
        const TypeInfo* pTypeInfo = engine::getSubsystem<TypeRegistry>().findType( hashed_string( rootName.c_str() ) );
        if ( pTypeInfo != nullptr )
            SW_EXPECT_TRUE( ResourceComposerTestUtil::isSameMergeRule( schema, DataInheritanceSchema::makeFromType( *pTypeInfo ) ) );
    }
}

/** @brief [ResourceComposerTest] 사슬은 위부터 모이고, 순환 · 없는 부모는 공통 오류다 */
SW_TEST_CASE( ResourceComposerTest, ChainIsCollectedFromTheTop )
{
    ResourceComposerTestSource source;
    source.add( "top.xml", "<Top />" );
    source.add( "mid.xml", R"(<Mid _base="top.xml" />)" );
    source.add( "loop.xml", R"(<Mid _base="loop.xml" />)" );

    vector<DataInheritanceLayer> listLayer;
    SW_REQUIRE_TRUE( DataInheritance::collectChain( R"(<Leaf _base="mid.xml" />)", "leaf", source, listLayer ) );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 3 ), listLayer.size() );
    SW_EXPECT_EQUAL( string( "top.xml" ), listLayer[0]._id );
    SW_EXPECT_EQUAL( string( "mid.xml" ), listLayer[1]._id );
    SW_EXPECT_EQUAL( string( "leaf" ), listLayer[2]._id );

    SW_EXPECT_LOG( "DataInheritance.Cycle" );
    SW_EXPECT_FALSE( DataInheritance::collectChain( R"(<Leaf _base="loop.xml" />)", "leaf", source, listLayer ) );
    SW_EXPECT_LOG( "DataInheritance.BaseNotFound" );
    SW_EXPECT_FALSE( DataInheritance::collectChain( R"(<Leaf _base="none.xml" />)", "leaf", source, listLayer ) );
}
