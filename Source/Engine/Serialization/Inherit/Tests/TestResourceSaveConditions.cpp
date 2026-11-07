// 저장이 리소스 조건(`_when`)을 지키는가(nogpu). 편집기 사본은 지금 태그로 푼 값을 들고, 저장 입구가 있던 파일의 조건을 되돌린다.
#include "pch.h"

#define SW_TEST_INTERNAL

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"
#include "Engine/Serialization/Inherit/ResourceComposition.h"
#include "Engine/Serialization/Inherit/ResourceTags.h"
#include "Engine/Serialization/XML/XMLDocument.h"

using namespace sw;
using sw::test::ScopedScratchDomain;

namespace
{
    struct XSaveConditionTestUtil
    {
        static ResourceTagSet makeLow()
        {
            ResourceTagSet tags;
            tags.add( "quality.low" );
            return tags;
        }

        static string restore( string_view savedText, string_view sourceText )
        {
            string outText;
            SW_EXPECT_TRUE( ResourceComposer::restoreConditions( savedText, sourceText, "test", makeLow(), outText ) );
            return outText;
        }

        static string readAttribute( const XMLNode& node, const utf8* pName ) { return string( node.getAttributeText( pName ) ); }

        /** @brief 저장한 글을 지금 태그(low)로 다시 풀면 사본과 같은 값인지 볼 때 쓴다. */
        static string resolveLow( string_view text )
        {
            string         outText;
            bool           bChanged = false;
            vector<string> listLoadTag;
            SW_EXPECT_TRUE( ResourceComposer::resolveConditions( text, "test", ResourceTagTiming::Load, makeLow(), outText, bChanged, listLoadTag ) );
            return bChanged ? outText : string( text );
        }
    };
    using Util = XSaveConditionTestUtil;

    /** @brief 케이스 동안 프로세스 태그를 low 로 두고 되돌립니다. */
    class XSaveTagScope
    {
    public:
        XSaveTagScope()
            : _previous{ ResourceTags::exchange( Util::makeLow() ) }
        {
        }
        ~XSaveTagScope()
        {
            (void)ResourceTags::exchange( _previous );
            vector<string> listPending;
            ResourceComposition::takePendingReload( listPending );
        }
        XSaveTagScope( const XSaveTagScope& )            = delete;
        XSaveTagScope& operator=( const XSaveTagScope& ) = delete;

    private:
        ResourceTagSet _previous;
    };
} // namespace

/** @brief [ResourceSaveConditionTest] 원본에 조건이 없으면 저장할 글 그대로다 */
SW_TEST_CASE( ResourceSaveConditionTest, TextWithoutConditionsIsLeftAlone )
{
    constexpr const utf8* kSaved = R"(<Doc a="2" />)";
    SW_EXPECT_EQUAL( string( kSaved ), Util::restore( kSaved, R"(<Doc a="1" />)" ) );
    SW_EXPECT_EQUAL( string( kSaved ), Util::restore( kSaved, R"(<Doc note="_when in a value" a="1" />)" ) );
}

/** @brief [ResourceSaveConditionTest] 참인 조건이 정한 칸을 그대로 두면 원본의 값이 돌아오고 조건이 남는다 — 지금 태그로 푼 값이 파일에 박히지 않는다 */
SW_TEST_CASE( ResourceSaveConditionTest, UnchangedWinningValueRestoresTheBase )
{
    XMLDocument doc;
    SW_REQUIRE_TRUE( doc.parse( Util::restore( R"(<Doc a="2" />)", R"(<Doc a="1"><_when _if="quality.low" a="2" /></Doc>)" ) ) );
    SW_EXPECT_EQUAL( string( "1" ), Util::readAttribute( doc.getRoot(), "a" ) );
    const XMLNode when = doc.getRoot().findChild( DataInheritance::kWhenElement );
    SW_REQUIRE_TRUE( when.isValid() );
    SW_EXPECT_EQUAL( string( "2" ), Util::readAttribute( when, "a" ) );
}

/** @brief [ResourceSaveConditionTest] 참인 조건이 정한 칸의 편집은 그 조건의 값이다 — 같은 태그로 다시 읽으면 편집한 값이고, 다른 태그의 값은 그대로다 */
SW_TEST_CASE( ResourceSaveConditionTest, EditOfAWinningValueGoesToTheCondition )
{
    const string restored = Util::restore( R"(<Doc a="9" />)", R"(<Doc a="1"><_when _if="quality.low" a="2" /></Doc>)" );
    XMLDocument  doc;
    SW_REQUIRE_TRUE( doc.parse( restored ) );
    SW_EXPECT_EQUAL( string( "1" ), Util::readAttribute( doc.getRoot(), "a" ) );
    SW_EXPECT_EQUAL( string( "9" ), Util::readAttribute( doc.getRoot().findChild( DataInheritance::kWhenElement ), "a" ) );
    XMLDocument resolved;
    SW_REQUIRE_TRUE( resolved.parse( Util::resolveLow( restored ) ) );
    SW_EXPECT_EQUAL( string( "9" ), Util::readAttribute( resolved.getRoot(), "a" ) );
}

/** @brief [ResourceSaveConditionTest] 거짓인 조건은 남고, 그 칸의 편집은 조건 밖 값이다. 조건과 상관없는 칸도 저장할 글이 맞다 */
SW_TEST_CASE( ResourceSaveConditionTest, FalseConditionsStayAndBaseEditsApply )
{
    XMLDocument doc;
    SW_REQUIRE_TRUE( doc.parse( Util::restore( R"(<Doc a="7" b="8" />)", R"(<Doc a="1" b="2"><_when _if="quality.high" a="5" /></Doc>)" ) ) );
    SW_EXPECT_EQUAL( string( "7" ), Util::readAttribute( doc.getRoot(), "a" ) );
    SW_EXPECT_EQUAL( string( "8" ), Util::readAttribute( doc.getRoot(), "b" ) );
    SW_EXPECT_EQUAL( string( "5" ), Util::readAttribute( doc.getRoot().findChild( DataInheritance::kWhenElement ), "a" ) );
}

/** @brief [ResourceSaveConditionTest] 원소 짝은 값으로 찾는다 — 사본의 차례가 바뀌고 다른 원소가 더해져도 조건은 제 원소에 남는다 */
SW_TEST_CASE( ResourceSaveConditionTest, ConditionsFollowTheirElements )
{
    constexpr const utf8* kSource = R"(<Doc><Item name="x" v="1"><_when _if="quality.high" v="5" /></Item><Item name="y" v="2" /></Doc>)";
    constexpr const utf8* kSaved  = R"(<Doc><Item name="new" v="0" /><Item name="y" v="3" /><Item name="x" v="4" /></Doc>)";
    XMLDocument           doc;
    SW_REQUIRE_TRUE( doc.parse( Util::restore( kSaved, kSource ) ) );
    uint32 itemCount = 0;
    for ( XMLNode item = doc.getRoot().findChild( "Item" ); item.isValid(); item = item.findNextSibling( "Item" ) )
    {
        ++itemCount;
        const bool bHasWhen = item.findChild( DataInheritance::kWhenElement ).isValid();
        SW_EXPECT_EQUAL( item.getAttributeText( "name" ) == "x", bHasWhen );
    }
    SW_EXPECT_EQUAL( 3u, itemCount );
}

/** @brief [ResourceSaveConditionTest] 참인 조건이 뺀 원소는 사본에 없어도 돌아온다 */
SW_TEST_CASE( ResourceSaveConditionTest, RemovedElementsComeBack )
{
    const string restored = Util::restore( R"(<Doc><Item name="y" /></Doc>)", R"(<Doc><Item name="x"><_when _if="quality.low" _op="Remove" /></Item><Item name="y" /></Doc>)" );
    XMLDocument  doc;
    SW_REQUIRE_TRUE( doc.parse( restored ) );
    const XMLNode first = doc.getRoot().findChild( "Item" );
    SW_REQUIRE_TRUE( first.isValid() );
    SW_EXPECT_EQUAL( string( "x" ), Util::readAttribute( first, "name" ) ); // 원본의 차례다
    SW_EXPECT_TRUE( first.findNextSibling( "Item" ).isValid() );
    SW_EXPECT_TRUE( Util::resolveLow( restored ).find( "\"x\"" ) == string::npos );
}

/** @brief [ResourceSaveConditionTest] 사본에서 지운 목록 원소의 조건은 함께 사라진다. 차이 저장은 부모와 같아 빠진 원소일 수 있어 남긴다 */
SW_TEST_CASE( ResourceSaveConditionTest, UnpairedElementsDependOnTheShape )
{
    constexpr const utf8* kSource = R"(<Doc><Item name="x"><_when _if="quality.high" v="1" /></Item><Item name="y" /></Doc>)";
    SW_EXPECT_TRUE( Util::restore( R"(<Doc><Item name="y" /></Doc>)", kSource ).find( "\"x\"" ) == string::npos );
    constexpr const utf8* kDeltaSource = R"(<Doc _base="p.xml"><Item name="x"><_when _if="quality.high" v="1" /></Item></Doc>)";
    SW_EXPECT_TRUE( Util::restore( R"(<Doc _base="p.xml" />)", kDeltaSource ).find( "\"x\"" ) != string::npos );
    // 구조체 칸: 모든 값이 기본값이면 저장이 원소째 뺀다 - 조건은 남아야 한다.
    SW_EXPECT_TRUE( Util::restore( R"(<Doc />)", R"(<Doc><_occlusion><_when _if="quality.high" v="1" /></_occlusion></Doc>)" ).find( "_occlusion" ) != string::npos );
}

/** @brief [ResourceSaveConditionTest] 조건 든 원소가 사본의 두 원소와 똑같이 닮으면 짐작하지 않고 저장을 거절한다 — 조건이 엉뚱한 원소에 붙으면 데이터가 조용히 틀어진다. */
SW_TEST_CASE( ResourceSaveConditionTest, AmbiguousElementIsNotSaved )
{
    SW_EXPECT_LOG( "ResourceCondition.SaveAmbiguousElement" );
    constexpr const utf8* kSource = R"(<Doc><item a="1"><_when _if="quality.low" a="2" /></item></Doc>)";
    constexpr const utf8* kSaved  = R"(<Doc><item a="1" b="x" /><item a="1" b="y" /></Doc>)";
    string                outText;
    SW_EXPECT_FALSE( ResourceComposer::restoreConditions( kSaved, kSource, "test", XSaveConditionTestUtil::makeLow(), outText ) );
}

/** @brief [ResourceSaveConditionTest] 원본의 조건이 틀리거나 사본의 루트가 다르면 쓰지 않는다 — 조건을 잃는 저장보다 실패가 낫다 */
SW_TEST_CASE( ResourceSaveConditionTest, BrokenConditionsAndOtherRootsAreNotSaved )
{
    string outText;
    SW_EXPECT_LOG( "ResourceCondition.SaveShapeChanged" );
    SW_EXPECT_FALSE( ResourceComposer::restoreConditions( "<Other />", R"(<Doc><_when _if="quality.low" a="1" /></Doc>)", "test", Util::makeLow(), outText ) );
    SW_EXPECT_LOG( "ResourceCondition.NoTest" );
    SW_EXPECT_FALSE( ResourceComposer::restoreConditions( "<Doc />", R"(<Doc><_when a="1" /></Doc>)", "test", Util::makeLow(), outText ) );
}

/** @brief [ResourceSaveConditionTest] 덮어쓰기 층을 얹어 읽은 리소스는 원본 자리에 쓰지 않는다 — 층 값이 원본에 박힌다 */
SW_TEST_CASE( ResourceSaveConditionTest, LayeredResourceIsNotSavedOverTheOriginal )
{
    const ScopedScratchDomain domain;
    const XSaveTagScope       scope;
    const vector<string>      listPreviousLayer = ResourceLayers::getLayers();
    (void)domain.makeDiskFolder( "layers/night/testproject" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "layers/night/testproject/doc.xml" ), R"(<Doc a="layer" />)" ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "doc.xml" ), R"(<Doc a="base" />)" ) );
    SW_REQUIRE_TRUE( ResourceLayers::setLayers( { ScopedScratchDomain::makeID( "layers/night" ).str() } ) );

    const AssetID id = ScopedScratchDomain::makeID( "doc.xml" );
    string        flatText;
    bool          bComposed = false;
    SW_REQUIRE_TRUE( ResourceComposer::compose( R"(<Doc a="base" />)", id.view(), ResourceContext::forSchema( []()
    { return DataInheritanceSchema{ "Doc" }; } ).withAsset( id ),
                                                flatText, bComposed ) );
    SW_EXPECT_LOG( "ResourceLayer.SaveWouldBake" );
    SW_EXPECT_FALSE( ResourceComposer::saveText( domain.makeDiskPath( "doc.xml" ), flatText ) );
    string onDisk;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( domain.makeDiskPath( "doc.xml" ), onDisk ) );
    SW_EXPECT_TRUE( onDisk.find( "base" ) != string::npos );

    SW_REQUIRE_TRUE( ResourceLayers::setLayers( listPreviousLayer ) );
}

/**
 * @brief [ResourceSaveDeltaTest] `_base` 저장 한 벌이 폴더를 만들어 차이만 쓰고, 다시 읽어 다르면 쓰지 않고 있던 파일을 지키는지 검증
 */
SW_TEST_CASE( ResourceSaveDeltaTest, WritesTheDeltaOnlyWhenItReadsBack )
{
    const ScopedScratchDomain   domain;
    const string                path = domain.makeDiskPath( "new/folder/child.xml" );
    const DataInheritanceSchema schema{ "Doc" };
    constexpr string_view       kParent = R"(<Doc a="1" b="2" />)";
    constexpr string_view       kFull   = R"(<Doc a="1" b="3" />)";

    SW_REQUIRE_TRUE( ResourceComposer::saveDelta( path, kFull, kParent, schema, "parent.xml", []( const string& )
    { return string{ kFull }; } ) );
    string written;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( path, written ) );
    SW_EXPECT_TRUE( written.find( "_base" ) != string::npos );
    SW_EXPECT_TRUE( written.find( "a=" ) == string::npos ); // 부모와 같은 칸은 적지 않는다

    SW_EXPECT_LOG( "ResourceSave.DeltaNotSaved" );
    SW_EXPECT_FALSE( ResourceComposer::saveDelta( path, R"(<Doc a="1" />)", kParent, schema, "parent.xml", []( const string& )
    { return string{ "different" }; } ) );
    string kept;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( path, kept ) );
    SW_EXPECT_EQUAL( written, kept );
}
