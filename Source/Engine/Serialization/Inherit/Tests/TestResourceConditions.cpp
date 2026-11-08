// 리소스 조건(`_when`) · 문맥 태그 · 덮어쓰기 층 · 합성 기록(nogpu). 태그는 프로세스 값이라 케이스마다 세우고 되돌린다.
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"
#include "Engine/Serialization/Inherit/ResourceComposition.h"
#include "Engine/Serialization/Inherit/ResourceRemap.h"
#include "Engine/Serialization/Inherit/ResourceTags.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include <initializer_list>

using namespace sw;
using sw::test::ScopedScratchDomain;

namespace
{
    /** @brief 부모를 글 표에서 읽습니다. */
    class XTestSource final : public IDataInheritanceSource
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

    /** @brief 케이스 동안 프로세스 태그 · 층을 바꾸고 소멸자에서 되돌립니다. */
    class XTestTagScope
    {
    public:
        explicit XTestTagScope( const ResourceTagSet& tags )
            : _previous{ engine::getSubsystem<ResourceTags>().exchange( tags ) }
            , _listPreviousLayer{ engine::getSubsystem<ResourceComposition>().getLayers() }
        {
        }
        ~XTestTagScope()
        {
            (void)engine::getSubsystem<ResourceTags>().exchange( _previous );
            (void)engine::getSubsystem<ResourceComposition>().setLayers( _listPreviousLayer );
            vector<string> listPending;
            engine::getSubsystem<ResourceComposition>().takePendingReload( listPending ); // 케이스가 만든 다시 읽기 목록을 다음 케이스에 남기지 않는다
        }
        XTestTagScope( const XTestTagScope& )            = delete;
        XTestTagScope& operator=( const XTestTagScope& ) = delete;

    private:
        ResourceTagSet _previous;
        vector<string> _listPreviousLayer;
    };

    struct XTestUtil
    {
        static ResourceTagSet makeTags( std::initializer_list<const utf8*> listTag )
        {
            ResourceTagSet tags;
            for ( const utf8* pTag : listTag )
            {
                tags.add( pTag );
            }
            return tags;
        }

        /** @brief @p text 의 조건을 @p timing 까지 풉니다. 바뀌지 않았으면 원문입니다. 실패하면 빈 글입니다. */
        static string resolve( string_view text, ResourceTagTiming timing, const ResourceTagSet& tags )
        {
            string         outText;
            bool           bChanged = false;
            vector<string> listLoadTag;
            if ( ResourceComposer::resolveConditions( text, "test", timing, tags, outText, bChanged, listLoadTag ) == false )
                return {};
            return bChanged ? outText : string{ text };
        }

        /** @brief 루트 속성 @p pName 의 값입니다(없으면 빈 글). */
        static string readRootAttribute( string_view text, const utf8* pName )
        {
            XMLDocument doc;
            if ( doc.parse( text ) == false )
                return {};
            return string{ doc.getRoot().getAttributeText( pName ) };
        }

        /** @brief 루트 아래 이름이 @p pName 인 원소 수입니다. */
        static uint32 countChildren( string_view text, const utf8* pName )
        {
            XMLDocument doc;
            if ( doc.parse( text ) == false )
                return 0;
            uint32 count = 0;
            for ( XMLNode node = doc.getRoot().findChild( pName ); node.isValid(); node = node.findNextSibling( pName ) )
            {
                ++count;
            }
            return count;
        }

        /** @brief 층 시험의 규칙입니다 — `<Item name>` 목록 하나. */
        static DataInheritanceSchema makeListSchema()
        {
            DataInheritanceSchema schema{ "Doc" };
            DataInheritanceSchema items{ "Items" };
            items._keyAttribute = "name";
            schema._listField.push_back( std::move( items ) );
            return schema;
        }
    };
} // namespace

/** @brief [ResourceTagsTest] 묻는 태그가 모두 쿠킹 축이면 쿠킹 조건이고, 로드 축이 하나라도 있으면 로드 조건이며 그 태그를 든다 */
SW_TEST_CASE( ResourceTagsTest, TestTimingFollowsTheAxesItAsks )
{
    ResourceTagTest test;
    SW_REQUIRE_TRUE( ResourceTags::compileTest( "platform.windows && !target.server", "test", test ) );
    SW_EXPECT_TRUE( test._timing == ResourceTagTiming::Cook );
    SW_EXPECT_TRUE( test._listLoadTag.empty() );

    SW_REQUIRE_TRUE( ResourceTags::compileTest( "quality.low || platform.linux", "test", test ) );
    SW_EXPECT_TRUE( test._timing == ResourceTagTiming::Load );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 1 ), test._listLoadTag.size() );
    SW_EXPECT_EQUAL( string( "quality.low" ), test._listLoadTag[0] );
    SW_EXPECT_TRUE( test.evaluate( XTestUtil::makeTags( { "quality.low" } ) ) );
    SW_EXPECT_FALSE( test.evaluate( XTestUtil::makeTags( { "quality.high" } ) ) );
}

/** @brief [ResourceTagsTest] 빈 식 · 모르는 축 · 닫힌 값 목록 밖의 값은 읽기 오류다 — 잘못 쓴 태그가 늘 거짓으로 지나가지 않는다 */
SW_TEST_CASE( ResourceTagsTest, InvalidTestsAreLoadErrors )
{
    ResourceTagTest test;
    SW_EXPECT_LOG( "ResourceCondition.Invalid" );
    SW_EXPECT_FALSE( ResourceTags::compileTest( "  ", "test", test ) );
    SW_EXPECT_LOG( "ResourceCondition.Invalid2" );
    SW_EXPECT_FALSE( ResourceTags::compileTest( "colour.red", "test", test ) );
    SW_EXPECT_LOG( "ResourceCondition.Invalid2" );
    SW_EXPECT_FALSE( ResourceTags::compileTest( "platform.amiga", "test", test ) );
}

/** @brief [ResourceTagsTest] 값 하나 축은 값을 바꾸고, 여러 값 축은 켜고 끈다. 틀린 축 · 값은 오류이고 값을 바꾸지 않는다 */
SW_TEST_CASE( ResourceTagsTest, SettersKeepAxisArity )
{
    const XTestTagScope scope{ ResourceTagSet{} };
    SW_REQUIRE_TRUE( engine::getSubsystem<ResourceTags>().setAxisValue( "quality", "low" ) );
    SW_REQUIRE_TRUE( engine::getSubsystem<ResourceTags>().setAxisValue( "quality", "high" ) );
    SW_EXPECT_FALSE( engine::getSubsystem<ResourceTags>().capture().contains( "quality.low" ) );
    SW_EXPECT_TRUE( engine::getSubsystem<ResourceTags>().capture().contains( "quality.high" ) );

    SW_REQUIRE_TRUE( engine::getSubsystem<ResourceTags>().setTagActive( "layer", "Winter", true ) );
    SW_REQUIRE_TRUE( engine::getSubsystem<ResourceTags>().setTagActive( "layer", "Night", true ) );
    SW_REQUIRE_TRUE( engine::getSubsystem<ResourceTags>().setTagActive( "layer", "Winter", false ) );
    SW_EXPECT_TRUE( engine::getSubsystem<ResourceTags>().capture().contains( "layer.Night" ) );
    SW_EXPECT_FALSE( engine::getSubsystem<ResourceTags>().capture().contains( "layer.Winter" ) );

    SW_EXPECT_LOG( "ResourceTags.WrongArity" );
    SW_EXPECT_FALSE( engine::getSubsystem<ResourceTags>().setAxisValue( "layer", "Winter" ) );
    SW_EXPECT_LOG( "ResourceTags.InvalidValue" );
    SW_EXPECT_FALSE( engine::getSubsystem<ResourceTags>().setAxisValue( "platform", "amiga" ) );
    SW_EXPECT_LOG( "ResourceTags.UnknownAxis" );
    SW_EXPECT_FALSE( engine::getSubsystem<ResourceTags>().setAxisValue( "colour", "red" ) );
    SW_EXPECT_TRUE( engine::getSubsystem<ResourceTags>().capture().contains( "quality.high" ) );
}

/** @brief [ResourceConditionTest] 참인 조건은 자기를 품은 원소의 속성을 적힌 차례로 바꾸고(나중이 이긴다), `_op="Remove"` 는 그 원소를 뺀다. 조건 원소는 결과에 남지 않는다 */
SW_TEST_CASE( ResourceConditionTest, WhenSetsAttributesInOrderAndRemovesElements )
{
    constexpr const utf8* kText = R"(<Doc a="1">
        <_when _if="quality.low" a="2" />
        <_when _if="quality.low &amp;&amp; layer.Night" a="3" />
        <Item name="day"><_when _if="layer.Night" _op="Remove" /></Item>
        <Item name="always" />
    </Doc>)";

    const string low = XTestUtil::resolve( kText, ResourceTagTiming::Load, XTestUtil::makeTags( { "quality.low" } ) );
    SW_EXPECT_EQUAL( string( "2" ), XTestUtil::readRootAttribute( low, "a" ) );
    SW_EXPECT_EQUAL( static_cast<uint32>( 2 ), XTestUtil::countChildren( low, "Item" ) );
    SW_EXPECT_EQUAL( static_cast<uint32>( 0 ), XTestUtil::countChildren( low, DataInheritance::kWhenElement ) );

    const string lowNight = XTestUtil::resolve( kText, ResourceTagTiming::Load, XTestUtil::makeTags( { "quality.low", "layer.Night" } ) );
    SW_EXPECT_EQUAL( string( "3" ), XTestUtil::readRootAttribute( lowNight, "a" ) );
    SW_EXPECT_EQUAL( static_cast<uint32>( 1 ), XTestUtil::countChildren( lowNight, "Item" ) );

    const string none = XTestUtil::resolve( kText, ResourceTagTiming::Load, ResourceTagSet{} );
    SW_EXPECT_EQUAL( string( "1" ), XTestUtil::readRootAttribute( none, "a" ) );
}

/**
 * @brief [ResourceConditionTest] 팩 쿠킹은 쿠킹 축 조건만 풀고, 로드 축 조건과 그 뒤의 조건은 남긴다 — 쿠킹본을 런타임이 푼 결과는 소스를 바로 푼 결과와 같다
 * @details 차례를 지키는 규칙이 없으면 뒤의 쿠킹 축 조건이 앞의 로드 축 조건보다 먼저 얹혀 같은 칸의 값이 뒤집힌다(셋째 조건).
 */
SW_TEST_CASE( ResourceConditionTest, CookedTextResolvesLikeTheSource )
{
    constexpr const utf8* kText = R"(<Doc a="0" b="0">
        <_when _if="platform.windows" a="w" />
        <_when _if="quality.low" a="q" b="q" />
        <_when _if="platform.windows" b="w" />
        <Item name="x"><_when _if="target.server" _op="Remove" /></Item>
    </Doc>)";

    const ResourceTagSet listTags[] = { XTestUtil::makeTags( { "platform.windows", "quality.low" } ), XTestUtil::makeTags( { "platform.windows" } ),
                                        XTestUtil::makeTags( { "platform.linux", "target.server", "quality.low" } ) };
    for ( const ResourceTagSet& tags : listTags )
    {
        const string cooked = XTestUtil::resolve( kText, ResourceTagTiming::Cook, tags );
        SW_REQUIRE_FALSE( cooked.empty() );
        SW_EXPECT_EQUAL( static_cast<uint32>( 2 ), XTestUtil::countChildren( cooked, DataInheritance::kWhenElement ) ); // 로드 축 조건과 그 뒤 조건
        const string runtime = XTestUtil::resolve( cooked, ResourceTagTiming::Load, tags );
        const string direct  = XTestUtil::resolve( kText, ResourceTagTiming::Load, tags );
        SW_EXPECT_EQUAL( XTestUtil::readRootAttribute( direct, "a" ), XTestUtil::readRootAttribute( runtime, "a" ) );
        SW_EXPECT_EQUAL( XTestUtil::readRootAttribute( direct, "b" ), XTestUtil::readRootAttribute( runtime, "b" ) );
        SW_EXPECT_EQUAL( XTestUtil::countChildren( direct, "Item" ), XTestUtil::countChildren( runtime, "Item" ) );
    }
    // 쿠킹 축만 쓴 파일은 쿠킹본에 조건이 남지 않는다.
    const string cookOnly = XTestUtil::resolve( R"(<Doc a="0"><_when _if="platform.windows" a="w" /></Doc>)", ResourceTagTiming::Cook, listTags[0] );
    SW_EXPECT_EQUAL( static_cast<uint32>( 0 ), XTestUtil::countChildren( cookOnly, DataInheritance::kWhenElement ) );
    SW_EXPECT_EQUAL( string( "w" ), XTestUtil::readRootAttribute( cookOnly, "a" ) );
}

/** @brief [ResourceConditionTest] 자식 원소를 품은 조건 · `_if` 없는 조건 · 아무것도 하지 않는 조건 · 루트를 빼는 조건은 읽기 오류다 */
SW_TEST_CASE( ResourceConditionTest, MalformedConditionsAreLoadErrors )
{
    const ResourceTagSet tags = XTestUtil::makeTags( { "quality.low" } );
    string               outText;
    bool                 bChanged = false;
    vector<string>       listLoadTag;
    SW_EXPECT_LOG( "ResourceCondition.WhenHasChildren" );
    SW_EXPECT_FALSE( ResourceComposer::resolveConditions( R"(<Doc><_when _if="quality.low"><Item /></_when></Doc>)", "test", ResourceTagTiming::Load, tags, outText, bChanged, listLoadTag ) );
    SW_EXPECT_LOG( "ResourceCondition.NoTest" );
    SW_EXPECT_FALSE( ResourceComposer::resolveConditions( R"(<Doc><_when a="1" /></Doc>)", "test", ResourceTagTiming::Load, tags, outText, bChanged, listLoadTag ) );
    SW_EXPECT_LOG( "ResourceCondition.DoesNothing" );
    SW_EXPECT_FALSE( ResourceComposer::resolveConditions( R"(<Doc><_when _if="quality.low" /></Doc>)", "test", ResourceTagTiming::Load, tags, outText, bChanged, listLoadTag ) );
    SW_EXPECT_LOG( "ResourceCondition.RootRemoved" );
    SW_EXPECT_FALSE( ResourceComposer::resolveConditions( R"(<Doc><_when _if="quality.low" _op="Remove" /></Doc>)", "test", ResourceTagTiming::Load, tags, outText, bChanged, listLoadTag ) );
}

/**
 * @brief [ResourceConditionTest] 부모와 자식이 같은 원소에 적은 조건은 쌓인다(자식이 적어도 부모의 조건이 남는다). 열쇠 목록 아래의 조건은 목록 원소로 세지 않는다. 차이 만들기는 자식이 더한 조건만 적는다
 */
SW_TEST_CASE( ResourceConditionTest, ConditionsStackAcrossTheBaseChain )
{
    XTestSource source;
    source.add( "base.xml", R"(<Doc a="1"><_when _if="quality.low" a="2" /><Items><_when _if="quality.low" n="1" /><Item name="x" v="1" /></Items></Doc>)" );
    string flatText;
    SW_REQUIRE_TRUE( DataInheritance::flatten( R"(<Doc _base="base.xml"><_when _if="layer.Night" a="3" /><Items><Item name="y" v="2" /></Items></Doc>)", "child", XTestUtil::makeListSchema(),
                                               source, flatText ) );
    SW_EXPECT_EQUAL( static_cast<uint32>( 2 ), XTestUtil::countChildren( flatText, DataInheritance::kWhenElement ) );
    const string night = XTestUtil::resolve( flatText, ResourceTagTiming::Load, XTestUtil::makeTags( { "quality.low", "layer.Night" } ) );
    SW_EXPECT_EQUAL( string( "3" ), XTestUtil::readRootAttribute( night, "a" ) ); // 자식의 조건이 부모의 조건 뒤에 얹힌다

    string parentFlat;
    SW_REQUIRE_TRUE( DataInheritance::flatten( R"(<Doc _base="base.xml" />)", "parent", XTestUtil::makeListSchema(), source, parentFlat ) );
    string delta;
    SW_REQUIRE_TRUE( DataInheritance::makeDelta( flatText, parentFlat, XTestUtil::makeListSchema(), "base.xml", delta ) );
    SW_EXPECT_EQUAL( static_cast<uint32>( 1 ), XTestUtil::countChildren( delta, DataInheritance::kWhenElement ) );

    // 부모의 조건을 지운 결과는 쌓기로 적을 수 없다.
    SW_EXPECT_LOG( "DataInheritance.WhenDropped" );
    SW_EXPECT_FALSE( DataInheritance::makeDelta( R"(<Doc a="1"><Items><Item name="x" v="1" /></Items></Doc>)", parentFlat, XTestUtil::makeListSchema(), "base.xml", delta ) );
}

/**
 * @brief [ResourceLayerTest] 리소스 id 로 읽으면 층 파일 `<뿌리>/<id>` 가 원본을 부모로 삼아 얹히고, 층의 조건은 층의 차이에 얹힌다. 층을 바꾸면 그 리소스가 다시 읽을 목록에 오른다
 */
SW_TEST_CASE( ResourceLayerTest, LayerFileLiesOverTheResource )
{
    const ScopedScratchDomain domain;
    const XTestTagScope       scope{ XTestUtil::makeTags( { "quality.low" } ) };
    const AssetID             id   = ScopedScratchDomain::makeID( "data/doc.xml" );
    const string              root = ScopedScratchDomain::makeID( "layers/night" ).str();
    // 층 파일 id 는 `<뿌리>/<id>` 다 — 도메인 폴더 안에서는 `layers/night/testproject/data/doc.xml`.
    SW_REQUIRE_EQUAL( ScopedScratchDomain::makeID( "layers/night/testproject/data/doc.xml" ).str(), ResourceLayers::makeLayerAssetID( root, id.view() ) );
    (void)domain.makeDiskFolder( "layers/night/testproject/data" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "layers/night/testproject/data/doc.xml" ),
                                              R"(<Doc a="layer"><_when _if="quality.low" b="low" /><Items><Item name="added" v="9" /></Items></Doc>)" ) );
    constexpr const utf8* kText = R"(<Doc a="base" b="base"><Items><Item name="x" v="1" /></Items></Doc>)";

    SW_REQUIRE_TRUE( engine::getSubsystem<ResourceComposition>().setLayers( { root } ) );
    string flatText;
    bool   bComposed = false;
    SW_REQUIRE_TRUE( ResourceComposer::compose( kText, id.view(), ResourceContext::forSchema( &XTestUtil::makeListSchema ).withAsset( id ), flatText, bComposed ) );
    SW_REQUIRE_TRUE( bComposed );
    SW_EXPECT_EQUAL( string( "layer" ), XTestUtil::readRootAttribute( flatText, "a" ) );
    SW_EXPECT_EQUAL( string( "low" ), XTestUtil::readRootAttribute( flatText, "b" ) );
    XMLDocument doc;
    SW_REQUIRE_TRUE( doc.parse( flatText ) );
    uint32 itemCount = 0;
    for ( XMLNode item = doc.getRoot().findChild( "Items" ).findChild( "Item" ); item.isValid(); item = item.findNextSibling( "Item" ) )
    {
        ++itemCount;
    }
    SW_EXPECT_EQUAL( static_cast<uint32>( 2 ), itemCount );

    // 글로 읽으면(리소스 id 없음) 층을 얹지 않는다.
    SW_REQUIRE_TRUE( ResourceComposer::compose( kText, "text", ResourceContext::forSchema( &XTestUtil::makeListSchema ), flatText, bComposed ) );
    SW_EXPECT_FALSE( bComposed );

    // 층을 떼면 층을 얹었던 리소스를 다시 읽는다. 층이 기댄 로드 축 태그가 바뀌어도 다시 읽는다.
    vector<string> listPending;
    engine::getSubsystem<ResourceComposition>().takePendingReload( listPending );
    SW_REQUIRE_TRUE( engine::getSubsystem<ResourceComposition>().setLayers( {} ) );
    engine::getSubsystem<ResourceComposition>().takePendingReload( listPending );
    SW_EXPECT_TRUE( VectorUtil::contains( listPending, id.str() ) );
}

/** @brief [ResourceCompositionTest] 로드 축 태그에 기댄 합성은 그 태그가 바뀔 때 다시 읽을 목록에 오르고, 접속 대조 목록에 든다. 기대지 않은 합성은 들지 않는다 */
SW_TEST_CASE( ResourceCompositionTest, LoadTagDependentsAreReloadedAndDigested )
{
    const XTestTagScope scope{ XTestUtil::makeTags( { "region.us" } ) };
    const AssetID       dependent{ "testproject/data/dependent.xml" };
    const AssetID       plain{ "testproject/data/plain.xml" };
    string              flatText;
    bool                bComposed = false;
    SW_REQUIRE_TRUE( ResourceComposer::compose( R"(<Doc a="1"><_when _if="region.us" a="2" /></Doc>)", dependent.view(), ResourceContext::forDocument().withAsset( dependent ), flatText, bComposed ) );
    SW_REQUIRE_TRUE( ResourceComposer::compose( R"(<Doc a="1" />)", plain.view(), ResourceContext::forDocument().withAsset( plain ), flatText, bComposed ) );

    vector<ResourceDigestEntry> listEntry;
    engine::getSubsystem<ResourceComposition>().collectDigest( listEntry );
    const bool bDependentListed = VectorUtil::containsIf( listEntry, [&dependent]( const ResourceDigestEntry& entry )
    { return entry._assetID == dependent.str(); } );
    const bool bPlainListed     = VectorUtil::containsIf( listEntry, [&plain]( const ResourceDigestEntry& entry )
        { return entry._assetID == plain.str(); } );
    SW_EXPECT_TRUE( bDependentListed );
    SW_EXPECT_FALSE( bPlainListed );

    vector<string> listPending;
    engine::getSubsystem<ResourceComposition>().takePendingReload( listPending );
    SW_REQUIRE_TRUE( engine::getSubsystem<ResourceTags>().setAxisValue( "region", "kr" ) );
    engine::getSubsystem<ResourceComposition>().takePendingReload( listPending );
    SW_EXPECT_TRUE( VectorUtil::contains( listPending, dependent.str() ) );
    SW_EXPECT_FALSE( VectorUtil::contains( listPending, plain.str() ) );
}

/** @brief [ResourceRemapTest] 같은 id 의 줄은 위에서부터 보고 처음 참인 줄의 파일을 읽는다. 리소스 입구(읽기 · 있는지 묻기)가 한 번 바꾸고, 로드 축 태그가 바뀌면 그 id 를 다시 읽는다 */
SW_TEST_CASE( ResourceRemapTest, ReadsTheFirstMatchingTarget )
{
    const ScopedScratchDomain domain;
    const XTestTagScope       scope{ XTestUtil::makeTags( { "locale.ko" } ) };
    const AssetID             logo   = ScopedScratchDomain::makeID( "ui/logo.txt" );
    const AssetID             logoKo = ScopedScratchDomain::makeID( "ui/logo_ko.txt" );
    (void)domain.makeDiskFolder( "ui" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "ui/logo.txt" ), "base" ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "ui/logo_ko.txt" ), "korean" ) );
    const string table = "<ResourceRemap>"
                         "<Remap id=\"" +
                         logo.str() + "\" when=\"locale.ja\" to=\"" + logo.str() + ".ja\" />"
                                                                                   "<Remap id=\"" +
                         logo.str() + "\" when=\"locale.ko\" to=\"" + logoKo.str() + "\" />"
                                                                                     "</ResourceRemap>";
    SW_REQUIRE_TRUE( ResourceRemap::loadFromXMLText( table, "test" ) );

    string text;
    SW_REQUIRE_TRUE( AssetIO::readTextAsset( logo, text ) );
    SW_EXPECT_EQUAL( string( "korean" ), text );
    SW_EXPECT_TRUE( AssetIO::hasAsset( logo ) );

    vector<string> listPending;
    engine::getSubsystem<ResourceComposition>().takePendingReload( listPending );
    SW_REQUIRE_TRUE( engine::getSubsystem<ResourceTags>().setAxisValue( "locale", "en" ) );
    engine::getSubsystem<ResourceComposition>().takePendingReload( listPending );
    SW_EXPECT_TRUE( VectorUtil::contains( listPending, logo.str() ) );
    SW_REQUIRE_TRUE( AssetIO::readTextAsset( logo, text ) );
    SW_EXPECT_EQUAL( string( "base" ), text );
    ResourceRemap::clear();
}

/** @brief [ResourceRemapTest] 빠진 칸 · 자기 자신으로 바꾸기 · 바꾼 것을 또 바꾸기 · 틀린 조건은 표 읽기 오류이고 앞의 표가 남는다 */
SW_TEST_CASE( ResourceRemapTest, MalformedTablesAreRejected )
{
    SW_EXPECT_LOG( "ResourceRemap.MissingField" );
    SW_EXPECT_FALSE( ResourceRemap::loadFromXMLText( R"(<ResourceRemap><Remap id="a" to="b" /></ResourceRemap>)", "test" ) );
    SW_EXPECT_LOG( "ResourceRemap.SelfRemap" );
    SW_EXPECT_FALSE( ResourceRemap::loadFromXMLText( R"(<ResourceRemap><Remap id="a" when="locale.ko" to="a" /></ResourceRemap>)", "test" ) );
    SW_EXPECT_LOG( "ResourceRemap.Chained" );
    SW_EXPECT_FALSE( ResourceRemap::loadFromXMLText( R"(<ResourceRemap><Remap id="a" when="locale.ko" to="b" /><Remap id="b" when="locale.ko" to="c" /></ResourceRemap>)", "test" ) );
    SW_EXPECT_LOG( "ResourceCondition.Invalid2" );
    SW_EXPECT_FALSE( ResourceRemap::loadFromXMLText( R"(<ResourceRemap><Remap id="a" when="colour.red" to="b" /></ResourceRemap>)", "test" ) );
    string remapped;
    SW_EXPECT_FALSE( ResourceRemap::resolve( "a", remapped ) );
}

/** @brief [ResourceCompositionTest] 품질 · 언어에만 기댄 합성은 플레이어끼리 달라도 되어 접속 대조에 오르지 않고, 함께 쓰는 축이나 층에 기대면 오른다. */
SW_TEST_CASE( ResourceCompositionTest, PerClientAxesAreNotCompared )
{
    const XTestTagScope scope{ XTestUtil::makeTags( { "quality.low", "feature.night" } ) };
    const AssetID       localOnly{ "testproject/data/perclient-local.xml" };
    const AssetID       shared{ "testproject/data/perclient-shared.xml" };
    string              flatText;
    bool                bComposed = false;
    SW_REQUIRE_TRUE( ResourceComposer::compose( R"(<Doc a="1"><_when _if="quality.low" a="2" /></Doc>)", localOnly.view(), ResourceContext::forDocument().withAsset( localOnly ), flatText, bComposed ) );
    SW_REQUIRE_TRUE( ResourceComposer::compose( R"(<Doc a="1"><_when _if="feature.night" a="2" /></Doc>)", shared.view(), ResourceContext::forDocument().withAsset( shared ), flatText, bComposed ) );
    vector<ResourceDigestEntry> listEntry;
    engine::getSubsystem<ResourceComposition>().collectDigest( listEntry );
    const auto hasEntry = [&listEntry]( const AssetID& assetID )
    {
        return VectorUtil::containsIf( listEntry, [&assetID]( const ResourceDigestEntry& entry )
        { return entry._assetID == assetID.str(); } );
    };
    SW_EXPECT_FALSE( hasEntry( localOnly ) );
    SW_EXPECT_TRUE( hasEntry( shared ) );
}

/** @brief [ResourceCompositionTest] 접속 대조는 이 프로세스가 로드 축 · 층에 기대 합성한 리소스만 본다 — 같은 해시는 통과, 다른 해시는 어긋남, 모르는 id 는 대조하지 않는다 */
SW_TEST_CASE( ResourceCompositionTest, DigestMismatchComparesOnlyKnownCompositions )
{
    const XTestTagScope   scope{ XTestUtil::makeTags( { "region.us" } ) };
    const AssetID         id{ "testproject/data/digest.xml" };
    constexpr const utf8* kText = R"(<Doc a="1"><_when _if="region.us" a="2" /></Doc>)";
    string                flatText;
    bool                  bComposed = false;
    SW_REQUIRE_TRUE( ResourceComposer::compose( kText, id.view(), ResourceContext::forDocument().withAsset( id ), flatText, bComposed ) );
    SW_REQUIRE_TRUE( bComposed );

    SW_EXPECT_FALSE( engine::getSubsystem<ResourceComposition>().isDigestMismatch( ResourceDigestEntry{ id.str(), ResourceComposition::hashText( flatText ) } ) );
    // 상대가 품질 high 로 합성했다 — 같은 파일이 다른 값이다.
    const string highText = XTestUtil::resolve( kText, ResourceTagTiming::Load, XTestUtil::makeTags( { "region.kr" } ) );
    SW_EXPECT_TRUE( engine::getSubsystem<ResourceComposition>().isDigestMismatch( ResourceDigestEntry{ id.str(), ResourceComposition::hashText( highText ) } ) );
    SW_EXPECT_FALSE( engine::getSubsystem<ResourceComposition>().isDigestMismatch( ResourceDigestEntry{ "testproject/data/never-composed.xml", 1 } ) );
}

/** @brief [ResourceCompositionTest] 서버가 층 · 조건 없이 읽은 표도 대조한다 — 클라이언트만 층을 얹으면 서버 쪽에 기댄 것이 없어도 어긋남이다 */
SW_TEST_CASE( ResourceCompositionTest, DigestComparesPlainCompositionsToo )
{
    const AssetID id{ "testproject/data/digest-plain.xml" };
    string        flatText;
    bool          bComposed = false;
    SW_REQUIRE_TRUE( ResourceComposer::compose( R"(<Doc a="1" />)", id.view(), ResourceContext::forDocument().withAsset( id ), flatText, bComposed ) );
    SW_EXPECT_FALSE( engine::getSubsystem<ResourceComposition>().isDigestMismatch( ResourceDigestEntry{ id.str(), ResourceComposition::hashText( R"(<Doc a="1" />)" ) } ) );
    SW_EXPECT_TRUE( engine::getSubsystem<ResourceComposition>().isDigestMismatch( ResourceDigestEntry{ id.str(), ResourceComposition::hashText( R"(<Doc a="layer" />)" ) } ) );
}

/** @brief [ResourceCompositionTest] 번호 뒤의 합성만 나온다 — 접속 뒤에 새로 읽은 리소스와 태그로 바뀐 결과가 서버에 간다. 기대지 않은 합성은 보낼 줄이 아니다 */
SW_TEST_CASE( ResourceCompositionTest, ChangesSinceARevisionAreCollected )
{
    const XTestTagScope         scope{ XTestUtil::makeTags( { "region.us" } ) };
    uint64                      revision = 0;
    vector<ResourceDigestEntry> listEntry;
    engine::getSubsystem<ResourceComposition>().collectTagDependentChanges( revision, listEntry );
    engine::getSubsystem<ResourceComposition>().collectTagDependentChanges( revision, listEntry );
    SW_EXPECT_TRUE( listEntry.empty() );

    const AssetID         varying{ "testproject/data/since-varying.xml" };
    const AssetID         plain{ "testproject/data/since-plain.xml" };
    constexpr const utf8* kText = R"(<Doc a="1"><_when _if="region.us" a="2" /></Doc>)";
    string                flatText;
    bool                  bComposed = false;
    SW_REQUIRE_TRUE( ResourceComposer::compose( kText, varying.view(), ResourceContext::forDocument().withAsset( varying ), flatText, bComposed ) );
    SW_REQUIRE_TRUE( ResourceComposer::compose( R"(<Doc a="1" />)", plain.view(), ResourceContext::forDocument().withAsset( plain ), flatText, bComposed ) );
    engine::getSubsystem<ResourceComposition>().collectTagDependentChanges( revision, listEntry );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 1 ), listEntry.size() );
    SW_EXPECT_EQUAL( varying.str(), listEntry[0]._assetID );

    // 같은 결과로 다시 읽으면 보낼 것이 없다.
    SW_REQUIRE_TRUE( ResourceComposer::compose( kText, varying.view(), ResourceContext::forDocument().withAsset( varying ), flatText, bComposed ) );
    engine::getSubsystem<ResourceComposition>().collectTagDependentChanges( revision, listEntry );
    SW_EXPECT_TRUE( listEntry.empty() );

    // 태그가 바뀌어 다시 읽으면 새 해시가 나간다.
    SW_REQUIRE_TRUE( engine::getSubsystem<ResourceTags>().setAxisValue( "region", "kr" ) );
    SW_REQUIRE_TRUE( ResourceComposer::compose( kText, varying.view(), ResourceContext::forDocument().withAsset( varying ), flatText, bComposed ) );
    engine::getSubsystem<ResourceComposition>().collectTagDependentChanges( revision, listEntry );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 1 ), listEntry.size() );
    SW_EXPECT_EQUAL( ResourceComposition::hashText( flatText ), listEntry[0]._hash );
}

/** @brief [ResourceDigestComparerTest] 상대 줄이 먼저 오고 서버가 나중에 읽어도 만난다 — 접속 뒤에 합성한 리소스의 어긋남을 유예가 지난 뒤 알린다 */
SW_TEST_CASE( ResourceDigestComparerTest, ServerReadingLaterStillCompares )
{
    const XTestTagScope    scope{ XTestUtil::makeTags( { "quality.low" } ) };
    const AssetID          id{ "testproject/data/compare-later.xml" };
    ResourceDigestComparer comparer{ engine::getSubsystem<ResourceComposition>() };
    SW_REQUIRE_TRUE( comparer.addPeerLine( 7, ResourceDigestEntry{ id.str(), 12345u }, 0 ) ); // 서버는 아직 이 리소스를 읽지 않았다

    vector<ResourceDigestMismatch> listMismatch;
    comparer.update( 1, 100, listMismatch );
    SW_EXPECT_TRUE( listMismatch.empty() );

    string flatText;
    bool   bComposed = false;
    SW_REQUIRE_TRUE( ResourceComposer::compose( R"(<Doc a="1"><_when _if="quality.low" a="2" /></Doc>)", id.view(), ResourceContext::forDocument().withAsset( id ), flatText, bComposed ) );
    comparer.update( 10, 100, listMismatch );
    SW_EXPECT_TRUE( listMismatch.empty() ); // 유예 안이다
    comparer.update( 110, 100, listMismatch );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 1 ), listMismatch.size() );
    SW_EXPECT_EQUAL( 7, listMismatch[0]._peer );
    SW_EXPECT_EQUAL( id.str(), listMismatch[0]._assetID );
}

/** @brief [ResourceDigestComparerTest] 유예 안에 맞으면 끊지 않는다 — 복제되는 태그는 양쪽이 다른 프레임에 바뀐다. 닫힌 연결의 줄은 맞대지 않는다 */
SW_TEST_CASE( ResourceDigestComparerTest, GraceHealsAndClosedPeersAreForgotten )
{
    const XTestTagScope   scope{ XTestUtil::makeTags( { "quality.low" } ) };
    const AssetID         id{ "testproject/data/compare-grace.xml" };
    constexpr const utf8* kText = R"(<Doc a="1"><_when _if="quality.low" a="2" /></Doc>)";
    string                flatText;
    bool                  bComposed = false;
    SW_REQUIRE_TRUE( ResourceComposer::compose( kText, id.view(), ResourceContext::forDocument().withAsset( id ), flatText, bComposed ) );
    const uint64 lowHash = ResourceComposition::hashText( flatText );

    ResourceDigestComparer comparer{ engine::getSubsystem<ResourceComposition>() };
    SW_REQUIRE_TRUE( comparer.addPeerLine( 1, ResourceDigestEntry{ id.str(), lowHash }, 0 ) );
    SW_REQUIRE_TRUE( comparer.addPeerLine( 2, ResourceDigestEntry{ id.str(), lowHash }, 0 ) );
    vector<ResourceDigestMismatch> listMismatch;
    comparer.update( 1, 100, listMismatch );
    SW_EXPECT_TRUE( listMismatch.empty() );

    // 서버의 태그가 먼저 바뀌었다 — 두 연결 모두 잠깐 어긋난다.
    SW_REQUIRE_TRUE( engine::getSubsystem<ResourceTags>().setAxisValue( "quality", "high" ) );
    SW_REQUIRE_TRUE( ResourceComposer::compose( kText, id.view(), ResourceContext::forDocument().withAsset( id ), flatText, bComposed ) );
    comparer.update( 10, 100, listMismatch );
    SW_EXPECT_TRUE( listMismatch.empty() );

    // 연결 1 은 유예 안에 따라왔고, 연결 2 는 닫혔다.
    SW_REQUIRE_TRUE( comparer.addPeerLine( 1, ResourceDigestEntry{ id.str(), ResourceComposition::hashText( flatText ) }, 20 ) );
    comparer.removePeer( 2 );
    comparer.update( 1000, 100, listMismatch );
    SW_EXPECT_TRUE( listMismatch.empty() );
}

/** @brief [ResourceDigestComparerTest] 상대 하나가 드는 줄 수에는 상한이 있다 — 신뢰 경계다. 같은 id 는 줄을 늘리지 않는다 */
SW_TEST_CASE( ResourceDigestComparerTest, LinesPerPeerAreBounded )
{
    ResourceDigestComparer comparer{ engine::getSubsystem<ResourceComposition>() };
    for ( uint32 index = 0; index < ResourceDigestComparer::kMaxLinesPerPeer; ++index )
    {
        SW_REQUIRE_TRUE( comparer.addPeerLine( 1, ResourceDigestEntry{ "testproject/flood/" + sw::to_string( index ), 1u }, 0 ) );
    }
    SW_EXPECT_TRUE( comparer.addPeerLine( 1, ResourceDigestEntry{ "testproject/flood/0", 2u }, 0 ) );
    SW_EXPECT_FALSE( comparer.addPeerLine( 1, ResourceDigestEntry{ "testproject/flood/over", 1u }, 0 ) );
    SW_EXPECT_TRUE( comparer.addPeerLine( 2, ResourceDigestEntry{ "testproject/flood/over", 1u }, 0 ) );
}

/** @brief [ResourceConditionTest] 바꿔 끼운 태그(미리보기 문맥)로 합치면 그 축의 변형을 고르고, 엔진 태그는 그대로이며 다시 읽기 · 접속 대조 기록에 남지 않는다 */
SW_TEST_CASE( ResourceConditionTest, TagOverridePicksVariantWithoutTouchingEngineTags )
{
    const XTestTagScope   scope{ XTestUtil::makeTags( { "screen.regular", "orientation.landscape" } ) };
    const AssetID         id{ "testproject/ui/override.ui.xml" };
    constexpr const utf8* kText       = R"(<Doc a="wide"><_when _if="screen.compact" a="narrow" /></Doc>)";
    const ResourceTagSet  tagOverride = XTestUtil::makeTags( { "screen.compact" } );
    string                flatText;
    bool                  bComposed = false;
    SW_REQUIRE_TRUE( ResourceComposer::compose( kText, id.view(), ResourceContext::forDocument().withAsset( id ).withTagOverride( tagOverride ), flatText, bComposed ) );
    SW_REQUIRE_TRUE( bComposed );
    SW_EXPECT_TRUE( flatText.find( "narrow" ) != string::npos );

    const ResourceTagSet engineTags = engine::getSubsystem<ResourceTags>().capture();
    SW_EXPECT_TRUE( engineTags.contains( "screen.regular" ) );
    SW_EXPECT_FALSE( engineTags.contains( "screen.compact" ) );
    // 기록에 없으면 대조하지 않는다(어긋남 아님) — 바꿔 끼운 합성은 엔진 판의 기록이 아니다.
    SW_EXPECT_FALSE( engine::getSubsystem<ResourceComposition>().isDigestMismatch( ResourceDigestEntry{ id.str(), 1u } ) );

    SW_REQUIRE_TRUE( ResourceComposer::compose( kText, id.view(), ResourceContext::forDocument().withAsset( id ), flatText, bComposed ) );
    SW_EXPECT_TRUE( flatText.find( "wide" ) != string::npos );
    SW_EXPECT_TRUE( engine::getSubsystem<ResourceComposition>().isDigestMismatch( ResourceDigestEntry{ id.str(), 1u } ) );
}

/** @brief [ResourceTagsTest] 축 바꿔 끼우기는 그 축의 값만 바꾸고(`overrideAxes`), 엔진 값 넣기(`setAxisTags`)는 값 하나 축 · 닫힌 목록만 받는다 */
SW_TEST_CASE( ResourceTagsTest, AxisOverrideReplacesOnlyItsAxes )
{
    const XTestTagScope  scope{ XTestUtil::makeTags( { "screen.regular", "quality.low" } ) };
    const ResourceTagSet tags = engine::getSubsystem<ResourceTags>().captureWith( XTestUtil::makeTags( { "screen.compact", "orientation.portrait" } ) );
    SW_EXPECT_TRUE( tags.contains( "screen.compact" ) );
    SW_EXPECT_FALSE( tags.contains( "screen.regular" ) );
    SW_EXPECT_TRUE( tags.contains( "orientation.portrait" ) );
    SW_EXPECT_TRUE( tags.contains( "quality.low" ) );
    SW_EXPECT_TRUE( engine::getSubsystem<ResourceTags>().capture().contains( "screen.regular" ) );

    SW_REQUIRE_TRUE( engine::getSubsystem<ResourceTags>().setAxisTags( XTestUtil::makeTags( { "screen.compact" } ) ) );
    SW_EXPECT_TRUE( engine::getSubsystem<ResourceTags>().capture().contains( "screen.compact" ) );
    SW_EXPECT_LOG( "ResourceTags.InvalidValue4" );
    SW_EXPECT_FALSE( engine::getSubsystem<ResourceTags>().setAxisTags( XTestUtil::makeTags( { "screen.tiny" } ) ) );
    SW_EXPECT_LOG( "ResourceTags.WrongArity" );
    SW_EXPECT_FALSE( engine::getSubsystem<ResourceTags>().setAxisTags( XTestUtil::makeTags( { "layer.night" } ) ) );
    SW_EXPECT_TRUE( engine::getSubsystem<ResourceTags>().capture().contains( "screen.compact" ) );
}
