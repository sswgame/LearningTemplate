#include "pch.h"

#include "Core/Container/unordered_map.h"
#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/AssetRegistry.h"
#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"
#include "Engine/Serialization/Inherit/ResourceComposition.h"
#include "Engine/Serialization/XML/XMLDocument.h"

using namespace sw;

namespace
{
    /** @brief 부모를 글 표에서 읽습니다. */
    class DataInheritanceTestSource final : public DataInheritanceSource
    {
    public:
        /** @brief 깊이 상한 @p maxDepth 를 싣습니다. 기본은 설정(`EngineConfig.json`)과 같은 값입니다. */
        explicit DataInheritanceTestSource( optional<uint32> maxDepth = 8u )
            : DataInheritanceSource{ maxDepth }
        {
        }

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

    struct DataInheritanceTestUtil
    {
        static constexpr const utf8* kBase = R"(<Doc _name="Base" _speed="1" _color="red">
            <_listPass>
                <Pass _name="A" _cull="Back"><_listInput><item>x</item></_listInput></Pass>
                <Pass _name="B" _cull="Back"><_listInput><item>y</item></_listInput></Pass>
                <Pass _name="C" _cull="None" />
            </_listPass>
            <_inner _a="1" _b="2" />
            <_tags><item>t1</item><item>t2</item></_tags>
        </Doc>)";

        /** @brief `_listPass` 는 `_name` 으로 합치고 `_inner` 는 안으로 들어가며 `_tags` 는 통째로 바뀝니다. */
        static DataInheritanceSchema makeSchema()
        {
            DataInheritanceSchema schema{ "Doc" };
            DataInheritanceSchema pass{ "_listPass" };
            pass._keyAttribute = "_name";
            schema._listField.push_back( std::move( pass ) );
            schema._listField.emplace_back( "_inner" );
            return schema;
        }

        [[nodiscard]] static bool flatten( DataInheritanceTestSource& source, const utf8* pText, string& outText, DataInheritanceProvenance* pProvenance = nullptr )
        {
            return DataInheritance::flatten( pText, "child", makeSchema(), source, outText, pProvenance );
        }

        /** @brief 합친 문서의 `_listPass` 이름을 쉼표로 잇습니다. */
        static string passOrder( const XMLDocument& doc )
        {
            string order;
            for ( XMLNode pass = doc.getRoot().findChild( "_listPass" ).findChild( "Pass" ); pass.isValid(); pass = pass.findNextSibling( "Pass" ) )
            {
                order += order.empty() ? "" : ",";
                order += pass.getAttributeText( "_name" );
            }
            return order;
        }

        static XMLNode findPass( const XMLDocument& doc, string_view name )
        {
            for ( XMLNode pass = doc.getRoot().findChild( "_listPass" ).findChild( "Pass" ); pass.isValid(); pass = pass.findNextSibling( "Pass" ) )
            {
                if ( pass.getAttributeText( "_name" ) == name )
                    return pass;
            }
            return {};
        }
    };
    using Util = DataInheritanceTestUtil;
} // namespace

/** @brief [DataInheritanceTest] 자식이 적은 칸만 덮고 나머지는 부모 값이며, `_base` 는 결과에 남지 않는다 */
SW_TEST_CASE( DataInheritanceTest, PropertiesMergeOnlyWhatChildWrites )
{
    DataInheritanceTestSource source;
    source.add( "base.xml", Util::kBase );
    string flat;
    SW_REQUIRE_TRUE( Util::flatten( source, R"(<Doc _base="base.xml" _name="Child" _speed="2"><_inner _b="9" /></Doc>)", flat ) );
    XMLDocument doc;
    SW_REQUIRE_TRUE( doc.parse( flat ) );
    const XMLNode root = doc.getRoot( "Doc" );
    SW_EXPECT_EQUAL( string( "Child" ), string( root.getAttributeText( "_name" ) ) );
    SW_EXPECT_EQUAL( string( "2" ), string( root.getAttributeText( "_speed" ) ) );
    SW_EXPECT_EQUAL( string( "red" ), string( root.getAttributeText( "_color" ) ) );
    SW_EXPECT_FALSE( root.hasAttribute( DataInheritance::kBaseAttribute ) );
    SW_EXPECT_EQUAL( string( "1" ), string( root.findChild( "_inner" ).getAttributeText( "_a" ) ) ); // 구조체 칸은 안으로 들어가 합친다
    SW_EXPECT_EQUAL( string( "9" ), string( root.findChild( "_inner" ).getAttributeText( "_b" ) ) );
    SW_EXPECT_EQUAL( string( "A,B,C" ), Util::passOrder( doc ) );
}

/** @brief [DataInheritanceTest] 열쇠 없는 칸 원소는 자식이 적으면 통째로 바뀌고, 빈 원소는 비운다 */
SW_TEST_CASE( DataInheritanceTest, ElementWithoutSchemaIsReplacedWhole )
{
    DataInheritanceTestSource source;
    source.add( "base.xml", Util::kBase );
    string flat;
    SW_REQUIRE_TRUE( Util::flatten( source, R"(<Doc _base="base.xml"><_tags><item>t9</item></_tags></Doc>)", flat ) );
    XMLDocument doc;
    SW_REQUIRE_TRUE( doc.parse( flat ) );
    const XMLNode tags = doc.getRoot().findChild( "_tags" );
    SW_EXPECT_EQUAL( string( "t9" ), string( tags.findChild( "item" ).getText() ) );
    SW_EXPECT_FALSE( tags.findChild( "item" ).findNextSibling( "item" ).isValid() );
}

/** @brief [DataInheritanceTest] 목록은 열쇠로 합친다 — 합치기 · 더하기 · 교체 · 삭제 · 자리 옮김 */
SW_TEST_CASE( DataInheritanceTest, KeyedListMergesByName )
{
    DataInheritanceTestSource source;
    source.add( "base.xml", Util::kBase );
    string flat;
    SW_REQUIRE_TRUE( Util::flatten( source, R"(<Doc _base="base.xml"><_listPass>
        <Pass _name="B" _op="Remove" />
        <Pass _name="A" _cull="Front" />
        <Pass _name="C" _op="Replace" _cull="Front" />
        <Pass _name="D" _cull="None" _before="C" />
        <Pass _name="E" _cull="None" />
    </_listPass></Doc>)",
                                    flat ) );
    XMLDocument doc;
    SW_REQUIRE_TRUE( doc.parse( flat ) );
    SW_EXPECT_EQUAL( string( "A,D,C,E" ), Util::passOrder( doc ) );
    const XMLNode passA = Util::findPass( doc, "A" );
    SW_EXPECT_EQUAL( string( "Front" ), string( passA.getAttributeText( "_cull" ) ) );
    SW_EXPECT_TRUE( passA.findChild( "_listInput" ).isValid() ); // 합치기는 적지 않은 칸을 남긴다
    SW_EXPECT_FALSE( Util::findPass( doc, "C" ).hasAttribute( DataInheritance::kOpAttribute ) );
    SW_EXPECT_FALSE( Util::findPass( doc, "D" ).hasAttribute( DataInheritance::kBeforeAttribute ) );
}

/** @brief [DataInheritanceTest] 교체는 부모 칸을 물려받지 않고, 옮기기만 하는 줄은 값을 바꾸지 않는다 */
SW_TEST_CASE( DataInheritanceTest, ReplaceDropsParentPropertiesAndMoveKeepsThem )
{
    DataInheritanceTestSource source;
    source.add( "base.xml", Util::kBase );
    string flat;
    SW_REQUIRE_TRUE( Util::flatten( source, R"(<Doc _base="base.xml"><_listPass>
        <Pass _name="A" _op="Replace" _cull="None" />
        <Pass _name="B" _after="C" />
    </_listPass></Doc>)",
                                    flat ) );
    XMLDocument doc;
    SW_REQUIRE_TRUE( doc.parse( flat ) );
    SW_EXPECT_EQUAL( string( "A,C,B" ), Util::passOrder( doc ) );
    SW_EXPECT_FALSE( Util::findPass( doc, "A" ).findChild( "_listInput" ).isValid() );
    SW_EXPECT_TRUE( Util::findPass( doc, "B" ).findChild( "_listInput" ).isValid() );
}

/** @brief [DataInheritanceTest] 여러 단의 사슬은 위부터 차례로 합친다 */
SW_TEST_CASE( DataInheritanceTest, MultiLevelChain )
{
    DataInheritanceTestSource source;
    source.add( "base.xml", Util::kBase );
    source.add( "mid.xml", R"(<Doc _base="base.xml" _speed="2" _color="blue"><_listPass><Pass _name="B" _op="Remove" /></_listPass></Doc>)" );
    string flat;
    SW_REQUIRE_TRUE( Util::flatten( source, R"(<Doc _base="mid.xml" _speed="3"><_listPass><Pass _name="Z" /></_listPass></Doc>)", flat ) );
    XMLDocument doc;
    SW_REQUIRE_TRUE( doc.parse( flat ) );
    SW_EXPECT_EQUAL( string( "3" ), string( doc.getRoot().getAttributeText( "_speed" ) ) );
    SW_EXPECT_EQUAL( string( "blue" ), string( doc.getRoot().getAttributeText( "_color" ) ) );
    SW_EXPECT_EQUAL( string( "A,C,Z" ), Util::passOrder( doc ) );
}

/** @brief [DataInheritanceTest] 부모를 가리키지 않는 글은 그대로 나오고, 글 값 안의 `_base` 를 부모로 읽지 않는다 */
SW_TEST_CASE( DataInheritanceTest, TextWithoutBaseIsUntouched )
{
    SW_EXPECT_TRUE( DataInheritance::findBasePath( "<Doc note=\"_base\" />" ).empty() );
    SW_EXPECT_TRUE( DataInheritance::findBasePath( "float _base = 1;" ).empty() ); // XML 이 아니다 — 파서 오류를 내지 않는다
    SW_EXPECT_EQUAL( string( "x.xml" ), DataInheritance::findBasePath( "<Doc _base=\"x.xml\" />" ) );
}

/** @brief [DataInheritanceTest] 순환은 오류다 */
SW_TEST_CASE( DataInheritanceTest, CycleIsAnError )
{
    DataInheritanceTestSource source;
    source.add( "a.xml", R"(<Doc _base="b.xml" />)" );
    source.add( "b.xml", R"(<Doc _base="a.xml" />)" );
    string flat;
    SW_EXPECT_LOG( "DataInheritance.Cycle" );
    SW_EXPECT_FALSE( Util::flatten( source, R"(<Doc _base="a.xml" />)", flat ) );
}

/** @brief [DataInheritanceTest] 없는 부모는 오류다 — 자식 칸만으로 읽지 않는다 */
SW_TEST_CASE( DataInheritanceTest, MissingBaseIsAnError )
{
    DataInheritanceTestSource source;
    string                    flat;
    SW_EXPECT_LOG( "DataInheritance.BaseNotFound" );
    SW_EXPECT_FALSE( Util::flatten( source, R"(<Doc _base="missing.xml" />)", flat ) );
}

/** @brief [DataInheritanceTest] 형식이 다른 부모는 오류다 */
SW_TEST_CASE( DataInheritanceTest, BaseOfOtherFormatIsAnError )
{
    DataInheritanceTestSource source;
    source.add( "other.xml", "<Other />" );
    string flat;
    SW_EXPECT_LOG( "DataInheritance.BaseFormatDiffers" );
    SW_EXPECT_FALSE( Util::flatten( source, R"(<Doc _base="other.xml" />)", flat ) );
}

/** @brief [DataInheritanceTest] 없는 열쇠의 삭제 · 교체, 없는 자리, 같은 열쇠 두 번, 모르는 작업은 오류다 */
SW_TEST_CASE( DataInheritanceTest, BadListEntriesAreErrors )
{
    DataInheritanceTestSource source;
    source.add( "base.xml", Util::kBase );
    string flat;
    {
        SW_EXPECT_LOG( "DataInheritance.OpKeyMissing" );
        SW_EXPECT_FALSE( Util::flatten( source, R"(<Doc _base="base.xml"><_listPass><Pass _name="Q" _op="Remove" /></_listPass></Doc>)", flat ) );
    }
    {
        SW_EXPECT_FALSE( Util::flatten( source, R"(<Doc _base="base.xml"><_listPass><Pass _name="Q" _op="Replace" /></_listPass></Doc>)", flat ) );
    }
    {
        SW_EXPECT_LOG( "DataInheritance.AnchorMissing" );
        SW_EXPECT_FALSE( Util::flatten( source, R"(<Doc _base="base.xml"><_listPass><Pass _name="A" _after="Q" /></_listPass></Doc>)", flat ) );
    }
    {
        SW_EXPECT_LOG( "DataInheritance.DuplicateKey" );
        SW_EXPECT_FALSE( Util::flatten( source, R"(<Doc _base="base.xml"><_listPass><Pass _name="A" /><Pass _name="A" /></_listPass></Doc>)", flat ) );
    }
    {
        // 부모 안의 겹침도 오류다 — 자식이 고친 값은 첫 원소에만 닿고 읽기는 뒤의 것을 쓴다
        source.add( "twice.xml", R"(<Doc><_listPass><Pass _name="A" _cull="Back" /><Pass _name="A" _cull="None" /></_listPass></Doc>)" );
        SW_EXPECT_FALSE( Util::flatten( source, R"(<Doc _base="twice.xml"><_listPass><Pass _name="A" _cull="Front" /></_listPass></Doc>)", flat ) );
    }
    {
        // 저장할 목록이 겹치면 차이를 적지 않는다(결함: 맞출 자리가 목록 끝을 넘어 밖을 읽었다)
        string delta;
        SW_EXPECT_FALSE( DataInheritance::makeDelta( R"(<Doc><_listPass><Pass _name="A" /><Pass _name="A" /></_listPass></Doc>)", R"(<Doc><_listPass><Pass _name="A" /></_listPass></Doc>)",
                                                     Util::makeSchema(), "base.xml", delta ) );
    }
    {
        SW_EXPECT_LOG( "DataInheritance.UnknownOp" );
        SW_EXPECT_FALSE( Util::flatten( source, R"(<Doc _base="base.xml"><_listPass><Pass _name="A" _op="Delete" /></_listPass></Doc>)", flat ) );
    }
}

/**
 * @brief [DataInheritanceTest] 정체 속성(`_listIdentityAttribute`)이 다른 같은 열쇠는 고침이 아니라 겹침이라 오류다. `Replace` 로만 바꾸고, 차이 저장은 그 모양으로 적는다
 * @details 애니메이션 그래프 전이(id 가 숫자)가 이 규칙을 쓴다. 예전에는 자식이 새로 더한 전이가 부모의 같은 id 전이에 조용히 합쳐졌다.
 */
SW_TEST_CASE( DataInheritanceTest, IdentityChangeUnderSameKeyIsACollision )
{
    DataInheritanceSchema schema{ "Doc" };
    DataInheritanceSchema pass{ "_listPass" };
    pass._keyAttribute          = "_name";
    pass._listIdentityAttribute = { "_cull" };
    schema._listField.push_back( std::move( pass ) );
    DataInheritanceTestSource source;
    source.add( "base.xml", Util::kBase );
    string flat;
    {
        SW_EXPECT_LOG( "DataInheritance.KeyCollision" );
        SW_EXPECT_FALSE( DataInheritance::flatten( R"(<Doc _base="base.xml"><_listPass><Pass _name="A" _cull="None" /></_listPass></Doc>)", "child", schema, source, flat ) );
    }
    // 같은 정체에 다른 칸을 고치는 것은 고침이다
    SW_REQUIRE_TRUE( DataInheritance::flatten( R"(<Doc _base="base.xml"><_listPass><Pass _name="A" _cull="Back" _extra="1" /></_listPass></Doc>)", "child", schema, source, flat ) );
    // 일부러 바꾸면 Replace 다
    SW_REQUIRE_TRUE( DataInheritance::flatten( R"(<Doc _base="base.xml"><_listPass><Pass _name="A" _cull="None" _op="Replace" /></_listPass></Doc>)", "child", schema, source, flat ) );

    static constexpr const utf8* kFull = R"(<Doc _name="Base" _speed="1" _color="red">
        <_listPass>
            <Pass _name="A" _cull="None"><_listInput><item>x</item></_listInput></Pass>
            <Pass _name="B" _cull="Back"><_listInput><item>y</item></_listInput></Pass>
            <Pass _name="C" _cull="None" />
        </_listPass>
        <_inner _a="1" _b="2" />
        <_tags><item>t1</item><item>t2</item></_tags>
    </Doc>)";
    string                       delta;
    SW_REQUIRE_TRUE( DataInheritance::makeDelta( kFull, Util::kBase, schema, "base.xml", delta ) );
    SW_EXPECT_TRUE( delta.find( "Replace" ) != string::npos );
    SW_REQUIRE_TRUE( DataInheritance::flatten( delta, "child", schema, source, flat ) );
    XMLDocument doc;
    SW_REQUIRE_TRUE( doc.parse( flat ) );
    SW_EXPECT_EQUAL( string( "None" ), string( Util::findPass( doc, "A" ).getAttributeText( "_cull" ) ) );
    SW_EXPECT_EQUAL( string( "A,B,C" ), Util::passOrder( doc ) );
}

/** @brief [DataInheritanceTest] 사슬이 읽기가 실어 온 상한을 넘으면 오류다 */
SW_TEST_CASE( DataInheritanceTest, ChainDeeperThanCarriedLimitIsAnError )
{
    DataInheritanceTestSource source;
    const uint32              maxDepth = *source.findMaxDepth();
    for ( uint32 index = 0; index <= maxDepth; ++index )
    {
        StringBuilder<constant::buffer::kMax256> idBuilder;
        idBuilder.appendFormat( "d%#.xml", index );
        const string                             id{ idBuilder.view() };
        StringBuilder<constant::buffer::kMax256> textBuilder;
        textBuilder.appendFormat( "<Doc _base=\"d%#.xml\" />", index + 1 );
        const string text{ textBuilder.view() };
        source.add( id.c_str(), text.c_str() );
    }
    fixed_string<constant::buffer::kMax64> text;
    formatstring( text.data(), text.capacity(), "d%#.xml", maxDepth + 1 );
    source.add( text.c_str(), "<Doc />" );
    string flat;
    SW_EXPECT_LOG( "DataInheritance.TooDeep" );
    SW_EXPECT_FALSE( Util::flatten( source, R"(<Doc _base="d0.xml" />)", flat ) );
}

/** @brief [DataInheritanceTest] 깊이 상한은 읽기가 실어 온 사본을 쓴다 — 설정(8)보다 작은 2 를 실으면 부모 셋은 지고 둘은 된다 */
SW_TEST_CASE( DataInheritanceTest, CarriedDepthLimitIsUsed )
{
    DataInheritanceTestSource source{ 2u };
    source.add( "d0.xml", R"(<Doc _base="d1.xml" />)" );
    source.add( "d1.xml", R"(<Doc _base="d2.xml" />)" );
    source.add( "d2.xml", "<Doc />" );
    SW_EXPECT_LOG( "DataInheritance.TooDeep" );
    string flat;
    SW_EXPECT_TRUE( Util::flatten( source, R"(<Doc _base="d1.xml" />)", flat ) );
    SW_EXPECT_FALSE( Util::flatten( source, R"(<Doc _base="d0.xml" />)", flat ) );
    vector<DataInheritanceLayer> listLayer;
    SW_EXPECT_TRUE( DataInheritance::collectChain( R"(<Doc _base="d1.xml" />)", "child", source, listLayer ) );
    SW_EXPECT_FALSE( DataInheritance::collectChain( R"(<Doc _base="d0.xml" />)", "child", source, listLayer ) );
}

/** @brief [DataInheritanceTest] 깊이 상한을 싣지 않은 읽기는 오류다 — 펼치기가 설정을 대신 읽지 않는다 */
SW_TEST_CASE( DataInheritanceTest, ReadWithoutDepthLimitIsAnError )
{
    DataInheritanceTestSource source{ nullopt };
    source.add( "base.xml", "<Doc />" );
    SW_EXPECT_LOG( "DataInheritance.NoDepthLimit" );
    string flat;
    SW_EXPECT_FALSE( Util::flatten( source, R"(<Doc _base="base.xml" />)", flat ) );
    vector<DataInheritanceLayer> listLayer;
    SW_EXPECT_FALSE( DataInheritance::collectChain( R"(<Doc _base="base.xml" />)", "child", source, listLayer ) );
}

/** @brief [DataInheritanceTest] 엔진의 깊이 사본은 뜨기 전엔 없고, 리소스 읽기는 만들 때 엔진의 사본을 싣는다 */
SW_TEST_CASE( DataInheritanceTest, ResourceSourceCarriesTheEngineCopy )
{
    ResourceComposition composition;
    SW_EXPECT_FALSE( composition.findInheritanceMaxDepth().has_value() );
    composition.setInheritanceMaxDepth( 3u );
    SW_EXPECT_EQUAL( 3u, composition.findInheritanceMaxDepth().value_or( 0u ) );
    const optional<uint32> engineCopy = engine::getSubsystem<ResourceComposition>().findInheritanceMaxDepth();
    SW_REQUIRE_TRUE( engineCopy.has_value() ); // 에셋 매니저가 기동에서 떴다
    SW_EXPECT_EQUAL( *engineCopy, DataInheritanceResourceSource{}.findMaxDepth().value_or( 0u ) );
}

/** @brief [DataInheritanceTest] 차이를 부모 위에 다시 펼치면 처음 칸이 나온다 — 삭제 · 추가 · 순서 · 빈 칸까지 */
SW_TEST_CASE( DataInheritanceTest, DeltaRoundTrip )
{
    static constexpr const utf8* kFull = R"(<Doc _name="Base" _speed="5">
        <_listPass>
            <Pass _name="N" _cull="None" />
            <Pass _name="C" _cull="None" />
            <Pass _name="A" _cull="Front"><_listInput><item>x</item></_listInput></Pass>
        </_listPass>
        <_inner _a="1" _b="3" />
    </Doc>)";
    string                       delta;
    SW_REQUIRE_TRUE( DataInheritance::makeDelta( kFull, Util::kBase, Util::makeSchema(), "base.xml", delta ) );

    XMLDocument deltaDoc;
    SW_REQUIRE_TRUE( deltaDoc.parse( delta ) );
    const XMLNode deltaRoot = deltaDoc.getRoot();
    SW_EXPECT_EQUAL( string( "base.xml" ), string( deltaRoot.getAttributeText( DataInheritance::kBaseAttribute ) ) );
    SW_EXPECT_FALSE( deltaRoot.hasAttribute( "_name" ) );                              // 같은 칸은 쓰지 않는다
    SW_EXPECT_EQUAL( string( "" ), string( deltaRoot.getAttributeText( "_color" ) ) ); // 부모에만 있는 칸은 빈 값으로 지운다
    SW_EXPECT_TRUE( deltaRoot.hasAttribute( "_color" ) );
    SW_EXPECT_FALSE( deltaRoot.findChild( "_inner" ).hasAttribute( "_a" ) );

    DataInheritanceTestSource source;
    source.add( "base.xml", Util::kBase );
    string flat;
    SW_REQUIRE_TRUE( Util::flatten( source, delta.c_str(), flat ) );
    XMLDocument doc;
    SW_REQUIRE_TRUE( doc.parse( flat ) );
    SW_EXPECT_EQUAL( string( "N,C,A" ), Util::passOrder( doc ) );
    SW_EXPECT_EQUAL( string( "Front" ), string( Util::findPass( doc, "A" ).getAttributeText( "_cull" ) ) );
    SW_EXPECT_EQUAL( string( "5" ), string( doc.getRoot().getAttributeText( "_speed" ) ) );
    SW_EXPECT_EQUAL( string( "3" ), string( doc.getRoot().findChild( "_inner" ).getAttributeText( "_b" ) ) );
    SW_EXPECT_EQUAL( string( "1" ), string( doc.getRoot().findChild( "_inner" ).getAttributeText( "_a" ) ) );

    // 같은 문서의 차이는 비어 있다.
    string noDelta;
    SW_REQUIRE_TRUE( DataInheritance::makeDelta( Util::kBase, Util::kBase, Util::makeSchema(), {}, noDelta ) );
    XMLDocument noDeltaDoc;
    SW_REQUIRE_TRUE( noDeltaDoc.parse( noDelta ) );
    SW_EXPECT_FALSE( noDeltaDoc.getRoot().getFirstAttribute().isValid() );
    SW_EXPECT_FALSE( noDeltaDoc.getRoot().findChild().isValid() );
}

/**
 * @brief [DataInheritanceTest] 글이 열쇠인 목록(`kTextKey`)은 글 하나씩 더하고 지우며, 차이를 다시 펼치면 처음 글 목록이 나온다
 * @details 변이: 펼치기가 글을 옮기지 않으면 새 줄이 빈 원소가 되고, 차이가 글뿐인 새 원소를 "차이 없음" 으로 보면 더한 줄이 저장에서 사라진다.
 */
SW_TEST_CASE( DataInheritanceTest, TextKeyedListMergesByText )
{
    static constexpr const utf8* kTextBase = R"(<Doc><_tags><item>t1</item><item>t2</item></_tags></Doc>)";
    DataInheritanceSchema        schema{ "Doc" };
    schema._listField.push_back( DataInheritanceSchema::makeKeyedList( "_tags", DataInheritanceSchema::kTextKey ) );
    const auto joinTags = []( const string& text )
    {
        XMLDocument doc;
        string      tags;
        if ( doc.parse( text ) == false )
            return tags;
        for ( XMLNode item = doc.getRoot().findChild( "_tags" ).findChild( "item" ); item; item = item.findNextSibling( "item" ) )
        {
            tags += item.getText();
            tags.push_back( ',' );
        }
        return tags;
    };

    DataInheritanceTestSource source;
    source.add( "base.xml", kTextBase );
    string flat;
    SW_REQUIRE_TRUE( DataInheritance::flatten( R"(<Doc _base="base.xml"><_tags><item>t3</item><item _op="Remove">t1</item><item>t2</item></_tags></Doc>)", "child.xml", schema, source, flat ) );
    SW_EXPECT_EQUAL( string( "t2,t3," ), joinTags( flat ) );

    string delta;
    SW_REQUIRE_TRUE( DataInheritance::makeDelta( flat, kTextBase, schema, "base.xml", delta ) );
    SW_EXPECT_TRUE( delta.find( "t2" ) == string::npos ); // 물려받은 줄은 적지 않는다
    string roundTrip;
    SW_REQUIRE_TRUE( DataInheritance::flatten( delta, "child.xml", schema, source, roundTrip ) );
    SW_EXPECT_EQUAL( joinTags( flat ), joinTags( roundTrip ) );

    // 빈 글은 열쇠가 없다.
    SW_EXPECT_LOG( "DataInheritance.ListKeyMissing2" );
    SW_EXPECT_FALSE( DataInheritance::flatten( R"(<Doc _base="base.xml"><_tags><item/></_tags></Doc>)", "child.xml", schema, source, flat ) );
}

/** @brief [DataInheritanceTest] 저장이 지우지 않는 칸(필수 · 판)은 부모와 같아도 남는다 */
SW_TEST_CASE( DataInheritanceTest, DeltaKeepsKeptAttributes )
{
    DataInheritanceSchema schema{ "Doc" };
    schema._listKeptAttribute.emplace_back( "_id" );
    string delta;
    SW_REQUIRE_TRUE( DataInheritance::makeDelta( R"(<Doc _id="1" _x="1" />)", R"(<Doc _id="1" _x="1" />)", schema, {}, delta ) );
    XMLDocument doc;
    SW_REQUIRE_TRUE( doc.parse( delta ) );
    SW_EXPECT_TRUE( doc.getRoot().hasAttribute( "_id" ) );
    SW_EXPECT_FALSE( doc.getRoot().hasAttribute( "_x" ) );
}

/** @brief [DataInheritanceTest] 칸마다 그 값을 마지막으로 적은 파일을 물을 수 있다. 지운 원소의 칸은 답이 없다 */
SW_TEST_CASE( DataInheritanceTest, ProvenanceAnswersWhereFieldCameFrom )
{
    DataInheritanceTestSource source;
    source.add( "base.xml", Util::kBase );
    source.add( "mid.xml", R"(<Doc _base="base.xml" _color="blue"><_listPass><Pass _name="B" _op="Remove" /></_listPass></Doc>)" );
    string                    flat;
    DataInheritanceProvenance provenance;
    SW_REQUIRE_TRUE( Util::flatten( source, R"(<Doc _base="mid.xml" _speed="3"><_listPass><Pass _name="A" _cull="Front" /></_listPass></Doc>)", flat, &provenance ) );

    const auto origin = [&provenance]( string_view fieldPath )
    {
        const string* pOrigin = provenance.findOrigin( fieldPath );
        return pOrigin != nullptr ? *pOrigin : string( "<none>" );
    };
    SW_EXPECT_EQUAL( string( "child" ), origin( "_speed" ) );
    SW_EXPECT_EQUAL( string( "mid.xml" ), origin( "_color" ) );
    SW_EXPECT_EQUAL( string( "base.xml" ), origin( "_name" ) );
    SW_EXPECT_EQUAL( string( "child" ), origin( "_listPass[A]._cull" ) );
    SW_EXPECT_EQUAL( string( "base.xml" ), origin( "_listPass[A]._listInput" ) );
    SW_EXPECT_EQUAL( string( "base.xml" ), origin( "_inner._a" ) );
    SW_EXPECT_EQUAL( string( "<none>" ), origin( "_listPass[B]._cull" ) );
}

/** @brief [DataInheritanceTest] 부모의 자식 목록을 물을 수 있다 — 곧바로 가리키는 파일만 */
SW_TEST_CASE( DataInheritanceTest, CollectChildrenFindsDirectChildren )
{
    const test::ScopedScratchDomain domain;
    const string                    idBase = test::ScopedScratchDomain::makeID( "base.xml" ).str();
    const string                    idMid  = test::ScopedScratchDomain::makeID( "mid.xml" ).str();
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "base.xml" ), "<Doc />" ) );
    StringBuilder<constant::buffer::kMax256> body;
    body.appendFormat( "<Doc _base=\"%#\" />", idBase );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "mid.xml" ), body.view() ) );
    body.clear();
    body.appendFormat( "<Doc _base=\"%#\" />", idMid );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "leaf.xml" ), body.view() ) );

    vector<string> listChild;
    SW_REQUIRE_TRUE( DataInheritance::collectChildren( domain.getFolder(), idBase, { ".xml" }, listChild ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listChild.size() );
    SW_EXPECT_EQUAL( string( "mid.xml" ), listChild[0] );
}

/** @brief [DataInheritanceTest] 규칙에 없는 이름은 같은 이름의 형제까지 묶음이다 — 자식이 적으면 부모의 묶음을 통째로 바꾸고, 안 적으면 부모 자리 그대로다 */
SW_TEST_CASE( DataInheritanceTest, RepeatedSiblingsAreReplacedAsAGroup )
{
    DataInheritanceTestSource source;
    source.add( "base.xml", R"(<Doc><Tag v="a" /><Other /><Tag v="b" /></Doc>)" );

    const auto collectTags = []( const string& text )
    {
        XMLDocument doc;
        string      tags;
        if ( doc.parse( text ) )
        {
            for ( XMLNode tag = doc.getRoot().findChild( "Tag" ); tag.isValid(); tag = tag.findNextSibling( "Tag" ) )
            {
                tags += tag.getAttributeText( "v" );
            }
        }
        return tags;
    };

    string flat;
    SW_REQUIRE_TRUE( Util::flatten( source, R"(<Doc _base="base.xml"><Tag v="c" /></Doc>)", flat ) );
    SW_EXPECT_EQUAL( string( "c" ), collectTags( flat ) );
    SW_REQUIRE_TRUE( Util::flatten( source, R"(<Doc _base="base.xml"><Tag v="c" /><Tag v="d" /><Tag v="e" /></Doc>)", flat ) );
    SW_EXPECT_EQUAL( string( "cde" ), collectTags( flat ) );
    SW_REQUIRE_TRUE( Util::flatten( source, R"(<Doc _base="base.xml" _x="1" />)", flat ) );
    SW_EXPECT_EQUAL( string( "ab" ), collectTags( flat ) );
    XMLDocument kept;
    SW_REQUIRE_TRUE( kept.parse( flat ) );
    SW_EXPECT_TRUE( kept.getRoot().findChild( "Other" ).isValid() );
}

/** @brief [DataInheritanceTest] 차이 만들기도 묶음으로 맞대어, 되풀이되는 원소가 부모와 같으면 적지 않고 다르면 모두 적는다(펼치면 원래 글) */
SW_TEST_CASE( DataInheritanceTest, DeltaComparesRepeatedSiblingsAsAGroup )
{
    constexpr const utf8* kParent = R"(<Doc><Tag v="a" /><Tag v="b" /></Doc>)";
    string                delta;
    SW_REQUIRE_TRUE( DataInheritance::makeDelta( kParent, kParent, Util::makeSchema(), {}, delta ) );
    XMLDocument same;
    SW_REQUIRE_TRUE( same.parse( delta ) );
    SW_EXPECT_FALSE( same.getRoot().findChild( "Tag" ).isValid() );

    SW_REQUIRE_TRUE( DataInheritance::makeDelta( R"(<Doc><Tag v="a" /><Tag v="c" /></Doc>)", kParent, Util::makeSchema(), {}, delta ) );
    XMLDocument changed;
    SW_REQUIRE_TRUE( changed.parse( delta ) );
    const XMLNode first = changed.getRoot().findChild( "Tag" );
    SW_REQUIRE_TRUE( first.isValid() );
    SW_EXPECT_EQUAL( string( "a" ), string( first.getAttributeText( "v" ) ) );
    SW_EXPECT_EQUAL( string( "c" ), string( first.findNextSibling( "Tag" ).getAttributeText( "v" ) ) );
}

/** @brief [DataInheritanceTest] 규칙이 구조체 칸이라고 한 이름을 두 번 적으면 오류다 — 첫 원소만 합치면 뒤 원소가 말없이 사라진다 */
SW_TEST_CASE( DataInheritanceTest, RepeatedStructFieldIsAnError )
{
    DataInheritanceTestSource source;
    source.add( "base.xml", R"(<Doc><_inner _a="1" /></Doc>)" );
    string flat;
    SW_EXPECT_LOG( "DataInheritance.FieldRepeated" );
    SW_EXPECT_FALSE( Util::flatten( source, R"(<Doc _base="base.xml"><_inner _a="2" /><_inner _a="3" /></Doc>)", flat ) );
}

/** @brief [DataInheritanceTest] `_base` 의 파일 부분은 GUID 로 적고 지금 경로로 읽는다 — 항목 id(`#…`)는 그대로이고, 색인에 없는 GUID 는 글 그대로 남아 "부모 없음" 이 된다 */
SW_TEST_CASE( DataInheritanceTest, BaseFileIsWrittenAsAGUIDAndReadAsThePathItHasNow )
{
    const AssetGUID               guid     = *AssetGUID::tryParse( "0000000000000000000000000000ba5e" );
    const expected<AssetRegistry> registry = AssetRegistry::merge( vector<AssetRegistryLayer>{
        AssetRegistryLayer{ "one", { AssetRegistryEntry{ guid, "common/data/items.xml" } } }
    } );
    SW_REQUIRE_TRUE( registry.has_value() );
    AssetRegistry previous = AssetRegistry::install( *registry );
    SW_EXPECT_EQUAL( string( "guid:0000000000000000000000000000ba5e" ), DataInheritance::makeBaseText( "common/data/items.xml" ) );
    SW_EXPECT_EQUAL( string( "guid:0000000000000000000000000000ba5e#helmet" ), DataInheritance::makeBaseText( "common/data/items.xml#helmet" ) );
    SW_EXPECT_EQUAL( string( "helmet" ), DataInheritance::makeBaseText( "helmet" ) );
    SW_EXPECT_EQUAL( string( "common/data/items.xml" ), DataInheritance::findBasePath( "<Doc _base=\"guid:0000000000000000000000000000ba5e\" />" ) );
    SW_EXPECT_EQUAL( string( "common/data/items.xml#helmet" ), DataInheritance::resolveBaseText( "guid:0000000000000000000000000000ba5e#helmet" ) );
    SW_EXPECT_EQUAL( string( "guid:0000000000000000000000000000dead#helmet" ), DataInheritance::resolveBaseText( "guid:0000000000000000000000000000dead#helmet" ) );
    (void)AssetRegistry::install( std::move( previous ) );
}
