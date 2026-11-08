#include "pch.h"

#include "Core/Container/unordered_map.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Inherit/DataCatalogInheritance.h"
#include "Engine/Serialization/XML/XMLDocument.h"

using namespace sw;

namespace
{
    /** @brief 부모 파일을 글 표에서 읽습니다. */
    class DataCatalogTestSource final : public IDataInheritanceSource
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

    struct DataCatalogInheritanceTestUtil
    {
        /** @brief `Visual` 항목의 `Part` 는 이름으로 합치고 `Stats` 는 안으로 들어가며, 나머지 되풀이 원소(`Requires`)는 통째로 바뀝니다. */
        static DataInheritanceSchema makeSchema()
        {
            DataInheritanceSchema part{ "Part" };
            part._keyAttribute = "name";
            DataInheritanceSchema visual{ "Visual" };
            visual._keyAttribute = "id";
            visual._listField.push_back( std::move( part ) );
            visual._listField.emplace_back( "Stats" );
            DataInheritanceSchema other{ "Other" };
            other._keyAttribute = "id";
            DataInheritanceSchema schema{ "Catalog" };
            schema._listField.push_back( std::move( visual ) );
            schema._listField.push_back( std::move( other ) );
            return schema;
        }

        [[nodiscard]] static bool flatten( DataCatalogTestSource& source, const utf8* pText, XMLDocument& outDoc )
        {
            string flatText;
            return DataCatalogInheritance::flatten( pText, "test.xml", makeSchema(), source, flatText ) && outDoc.parse( flatText );
        }

        static XMLNode findEntry( const XMLDocument& doc, const utf8* pKind, string_view id )
        {
            for ( XMLNode entry = doc.getRoot().findChild( pKind ); entry.isValid(); entry = entry.findNextSibling( pKind ) )
            {
                if ( entry.getAttributeText( "id" ) == id )
                    return entry;
            }
            return {};
        }

        static XMLNode findPart( const XMLNode& entry, string_view name )
        {
            for ( XMLNode part = entry.findChild( "Part" ); part.isValid(); part = part.findNextSibling( "Part" ) )
            {
                if ( part.getAttributeText( "name" ) == name )
                    return part;
            }
            return {};
        }

        static string partOrder( const XMLNode& entry )
        {
            string order;
            for ( XMLNode part = entry.findChild( "Part" ); part.isValid(); part = part.findNextSibling( "Part" ) )
            {
                order += order.empty() ? "" : ",";
                order += part.getAttributeText( "name" );
            }
            return order;
        }

        static uint32 countChildren( const XMLNode& entry, const utf8* pName )
        {
            uint32 count = 0;
            for ( XMLNode child = entry.findChild( pName ); child.isValid(); child = child.findNextSibling( pName ) )
            {
                ++count;
            }
            return count;
        }
    };

    using Util = DataCatalogInheritanceTestUtil;
} // namespace

/** @brief [DataCatalogInheritance] 항목이 같은 파일의 항목을 물려받는다 - 적은 칸만 덮고, 부품은 이름으로 합치며, 되풀이 원소는 통째로 바뀌고, `_base` 는 결과에 남지 않는다 */
SW_TEST_CASE( DataCatalogInheritance, EntryInheritsWithinFile )
{
    DataCatalogTestSource source;
    XMLDocument           doc;
    SW_REQUIRE_TRUE( Util::flatten( source, R"(<Catalog>
        <Visual id="a" tags="T" size="1">
            <Part name="Shell" mesh="a.mesh" material="a.material" />
            <Part name="Trim" mesh="t.mesh" />
            <Stats armor="3" speed="1" />
            <Requires set="S1" />
            <Requires set="S2" />
        </Visual>
        <Visual id="b" _base="a" size="2">
            <Part name="Shell" material="b.material" />
            <Stats speed="2" />
        </Visual>
        <Visual id="c" _base="a">
            <Requires set="S3" />
        </Visual>
        <Visual id="d" _base="a">
            <Requires _op="Clear" />
        </Visual>
    </Catalog>)",
                                    doc ) );
    const XMLNode b = Util::findEntry( doc, "Visual", "b" );
    SW_REQUIRE_TRUE( b.isValid() );
    SW_EXPECT_FALSE( b.hasAttribute( DataInheritance::kBaseAttribute ) );
    SW_EXPECT_EQUAL( string( "T" ), string( b.getAttributeText( "tags" ) ) );
    SW_EXPECT_EQUAL( string( "2" ), string( b.getAttributeText( "size" ) ) );
    SW_EXPECT_EQUAL( string( "a.mesh" ), string( Util::findPart( b, "Shell" ).getAttributeText( "mesh" ) ) );
    SW_EXPECT_EQUAL( string( "b.material" ), string( Util::findPart( b, "Shell" ).getAttributeText( "material" ) ) );
    SW_EXPECT_EQUAL( string( "Shell,Trim" ), Util::partOrder( b ) );
    SW_EXPECT_EQUAL( string( "3" ), string( b.findChild( "Stats" ).getAttributeText( "armor" ) ) );
    SW_EXPECT_EQUAL( string( "2" ), string( b.findChild( "Stats" ).getAttributeText( "speed" ) ) );
    SW_EXPECT_EQUAL( 2u, Util::countChildren( b, "Requires" ) );

    const XMLNode c = Util::findEntry( doc, "Visual", "c" );
    SW_EXPECT_EQUAL( 1u, Util::countChildren( c, "Requires" ) );
    SW_EXPECT_EQUAL( string( "S3" ), string( c.findChild( "Requires" ).getAttributeText( "set" ) ) );
    SW_EXPECT_EQUAL( 0u, Util::countChildren( Util::findEntry( doc, "Visual", "d" ), "Requires" ) );
    // 부모는 그대로다.
    SW_EXPECT_EQUAL( string( "a.material" ), string( Util::findPart( Util::findEntry( doc, "Visual", "a" ), "Shell" ).getAttributeText( "material" ) ) );
}

/** @brief [DataCatalogInheritance] 부품 작업(`_op` · `_before`)이 항목에서도 같은 규칙이고, 사슬은 위부터 얹으며, 부모가 파일 뒤에 있어도 된다 */
SW_TEST_CASE( DataCatalogInheritance, MultiLevelAndListOperations )
{
    DataCatalogTestSource source;
    XMLDocument           doc;
    SW_REQUIRE_TRUE( Util::flatten( source, R"(<Catalog>
        <Visual id="leaf" _base="middle" level="3">
            <Part name="New" mesh="n.mesh" _before="Brows" />
        </Visual>
        <Visual id="middle" _base="root" level="2">
            <Part name="Hair" mesh="m.mesh" />
            <Part name="Drop" _op="Remove" />
        </Visual>
        <Visual id="root" level="1" kept="k">
            <Part name="Hair" mesh="r.mesh" kind="Skinned" />
            <Part name="Drop" mesh="d.mesh" />
            <Part name="Brows" mesh="b.mesh" />
        </Visual>
    </Catalog>)",
                                    doc ) );
    const XMLNode leaf = Util::findEntry( doc, "Visual", "leaf" );
    SW_EXPECT_EQUAL( string( "3" ), string( leaf.getAttributeText( "level" ) ) );
    SW_EXPECT_EQUAL( string( "k" ), string( leaf.getAttributeText( "kept" ) ) );
    SW_EXPECT_EQUAL( string( "Hair,New,Brows" ), Util::partOrder( leaf ) );
    SW_EXPECT_EQUAL( string( "m.mesh" ), string( Util::findPart( leaf, "Hair" ).getAttributeText( "mesh" ) ) );
    SW_EXPECT_EQUAL( string( "Skinned" ), string( Util::findPart( leaf, "Hair" ).getAttributeText( "kind" ) ) );
    SW_EXPECT_FALSE( Util::findPart( leaf, "New" ).hasAttribute( DataInheritance::kBeforeAttribute ) );
    // 파일 순서를 지킨다(가게 진열 · 블록 번호가 순서를 쓴다).
    SW_EXPECT_EQUAL( string( "leaf" ), string( doc.getRoot().findChild( "Visual" ).getAttributeText( "id" ) ) );
}

/** @brief [DataCatalogInheritance] 순환 · 자기 자신 · 없는 부모 · 다른 종류의 부모는 오류이고 자식 칸만으로 읽지 않는다 */
SW_TEST_CASE( DataCatalogInheritance, BrokenChainsAreErrors )
{
    DataCatalogTestSource       source;
    string                      flatText;
    const DataInheritanceSchema schema = Util::makeSchema();
    {
        SW_EXPECT_LOG( "DataInheritance.Cycle" );
        SW_EXPECT_FALSE( DataCatalogInheritance::flatten( R"(<Catalog><Visual id="a" _base="b" /><Visual id="b" _base="a" /></Catalog>)", "test.xml", schema, source, flatText ) );
    }
    {
        SW_EXPECT_FALSE( DataCatalogInheritance::flatten( R"(<Catalog><Visual id="a" _base="a" /></Catalog>)", "test.xml", schema, source, flatText ) );
    }
    {
        SW_EXPECT_LOG( "DataInheritance.BaseNotFound" );
        SW_EXPECT_FALSE( DataCatalogInheritance::flatten( R"(<Catalog><Visual id="a" _base="missing" /></Catalog>)", "test.xml", schema, source, flatText ) );
    }
    {
        SW_EXPECT_FALSE( DataCatalogInheritance::flatten( R"(<Catalog><Other id="o" /><Visual id="a" _base="o" /></Catalog>)", "test.xml", schema, source, flatText ) );
    }
    {
        SW_EXPECT_FALSE( DataCatalogInheritance::flatten( R"(<Catalog><Visual id="a" _base="common/none.xml#x" /></Catalog>)", "test.xml", schema, source, flatText ) );
    }
    {
        SW_EXPECT_LOG( "DataCatalogInheritance.KindNotListed" );
        SW_EXPECT_FALSE( DataCatalogInheritance::flatten( R"(<Catalog><Unlisted id="a" _base="b" /></Catalog>)", "test.xml", schema, source, flatText ) );
    }
    {
        SW_EXPECT_LOG( "DataCatalogInheritance.FieldRepeated" );
        SW_EXPECT_FALSE( DataCatalogInheritance::flatten( R"(<Catalog><Visual id="a" /><Visual id="b" _base="a"><Stats /><Stats /></Visual></Catalog>)", "test.xml", schema, source, flatText ) );
    }
}

/** @brief [DataCatalogInheritance] 다른 파일의 항목을 부모로 쓴다 - 다른 게임 팩이 common 카탈로그를 물려받는다. 그 항목의 `_base` 는 그 파일 기준이고, 그 파일의 파일 상속도 펼친다 */
SW_TEST_CASE( DataCatalogInheritance, ParentInAnotherFile )
{
    DataCatalogTestSource source;
    source.add( "common/root.xml", R"(<Catalog>
        <Visual id="far" tags="Far"><Part name="Shell" mesh="far.mesh" /></Visual>
    </Catalog>)" );
    source.add( "common/items.xml", R"(<Catalog _base="common/root.xml">
        <Visual id="near" _base="far" size="1" />
    </Catalog>)" );
    XMLDocument doc;
    SW_REQUIRE_TRUE( Util::flatten( source, R"(<Catalog>
        <Visual id="near" size="99" />
        <Visual id="mine" _base="common/items.xml#near"><Part name="Shell" material="m.material" /></Visual>
    </Catalog>)",
                                    doc ) );
    const XMLNode mine = Util::findEntry( doc, "Visual", "mine" );
    SW_REQUIRE_TRUE( mine.isValid() );
    SW_EXPECT_EQUAL( string( "Far" ), string( mine.getAttributeText( "tags" ) ) );
    SW_EXPECT_EQUAL( string( "1" ), string( mine.getAttributeText( "size" ) ) ); // 이 파일의 near(99)가 아니다
    SW_EXPECT_EQUAL( string( "far.mesh" ), string( Util::findPart( mine, "Shell" ).getAttributeText( "mesh" ) ) );
    SW_EXPECT_EQUAL( string( "m.material" ), string( Util::findPart( mine, "Shell" ).getAttributeText( "material" ) ) );
}

/** @brief [DataCatalogInheritance] 파일 상속 - 게임 팩이 카탈로그 전체를 물려받아 항목을 더하고 고치고 지운다. 자기 항목은 물려받은 항목을 부모로 쓴다 */
SW_TEST_CASE( DataCatalogInheritance, WholeFileInheritance )
{
    DataCatalogTestSource source;
    source.add( "common/items.xml", R"(<Catalog version="1">
        <Visual id="a" size="1"><Part name="Shell" mesh="a.mesh" /></Visual>
        <Visual id="gone" size="1" />
        <Other id="o" note="n"><Requires set="S" /></Other>
    </Catalog>)" );
    XMLDocument doc;
    SW_REQUIRE_TRUE( Util::flatten( source, R"(<Catalog _base="common/items.xml">
        <Visual id="a" size="5" />
        <Visual id="gone" _op="Remove" />
        <Visual id="mine" _base="a" extra="e" />
    </Catalog>)",
                                    doc ) );
    SW_EXPECT_FALSE( doc.getRoot().hasAttribute( DataInheritance::kBaseAttribute ) );
    SW_EXPECT_EQUAL( string( "1" ), string( doc.getRoot().getAttributeText( "version" ) ) );
    SW_EXPECT_FALSE( Util::findEntry( doc, "Visual", "gone" ).isValid() );
    SW_EXPECT_TRUE( Util::findEntry( doc, "Other", "o" ).findChild( "Requires" ).isValid() );
    const XMLNode mine = Util::findEntry( doc, "Visual", "mine" );
    SW_REQUIRE_TRUE( mine.isValid() );
    SW_EXPECT_EQUAL( string( "5" ), string( mine.getAttributeText( "size" ) ) ); // 이 파일이 고친 a 를 물려받는다
    SW_EXPECT_EQUAL( string( "a.mesh" ), string( Util::findPart( mine, "Shell" ).getAttributeText( "mesh" ) ) );
    SW_EXPECT_EQUAL( string( "e" ), string( mine.getAttributeText( "extra" ) ) );
}

/** @brief [DataCatalogInheritance] 표는 프로젝트의 설정 파일이고, 표에 없는 문서는 건드리지 않으며, `_base` 가 없는 글은 파싱조차 하지 않는다 */
SW_TEST_CASE( DataCatalogInheritance, RuleTableAndPassThrough )
{
    DataInheritanceSchema schema{ "x" };
    bool                  bListed = false;
    SW_REQUIRE_TRUE( DataCatalogInheritance::loadSchema( "ItemVisualCatalog", schema, bListed ) );
    SW_EXPECT_TRUE( bListed );
    const DataInheritanceSchema* pVisual = schema.findField( "ItemVisual" );
    SW_REQUIRE_TRUE( pVisual != nullptr );
    SW_EXPECT_EQUAL( string( "id" ), pVisual->_keyAttribute );
    SW_REQUIRE_TRUE( pVisual->findField( "Part" ) != nullptr );
    SW_EXPECT_EQUAL( string( "name" ), pVisual->findField( "Part" )->_keyAttribute );

    SW_REQUIRE_TRUE( DataCatalogInheritance::loadSchema( "NoSuchCatalog", schema, bListed ) );
    SW_EXPECT_FALSE( bListed );

    DataCatalogTestSource source;
    string                flatText;
    bool                  bFlattened = true;
    SW_REQUIRE_TRUE( DataCatalogInheritance::flattenIfListed( "not xml, no base", "a", source, bFlattened, flatText ) );
    SW_EXPECT_FALSE( bFlattened );
    SW_REQUIRE_TRUE( DataCatalogInheritance::flattenIfListed( R"(<RenderPipelineDesc _base="x.xml" />)", "a", source, bFlattened, flatText ) );
    SW_EXPECT_FALSE( bFlattened ); // 자기 입구가 펼친다
    SW_REQUIRE_TRUE( DataCatalogInheritance::flattenIfListed( R"(<ItemCatalog><Item id="a" /></ItemCatalog><!-- _base -->)", "a", source, bFlattened, flatText ) );
    SW_EXPECT_FALSE( bFlattened );
    SW_REQUIRE_TRUE( DataCatalogInheritance::flattenIfListed( R"(<ItemCatalog><Item id="a" name="n" /><Item id="b" _base="a" /></ItemCatalog>)", "a", source, bFlattened, flatText ) );
    SW_EXPECT_TRUE( bFlattened );
    XMLDocument doc;
    SW_REQUIRE_TRUE( doc.parse( flatText ) );
    SW_EXPECT_EQUAL( string( "n" ), string( Util::findEntry( doc, "Item", "b" ).getAttributeText( "name" ) ) );
}

/**
 * @brief [DataCatalogInheritance] 입력 맵은 표의 줄로 파일 상속을 받는다 — 레이어 정의는 이름으로 합치고(우선순위만 고침 · 지움), 루트 액션은 이름이 열쇠다
 * @details 레이어 정의는 래퍼(`<layers>`) 안의 목록이라 항목이 아닌 루트 칸(`<Catalog>` 아래 `<Field>`)이다.
 */
SW_TEST_CASE( DataCatalogInheritance, InputMapLayersMergeByName )
{
    DataInheritanceSchema schema{ "x" };
    bool                  bListed = false;
    SW_REQUIRE_TRUE( DataCatalogInheritance::loadSchema( "InputMap", schema, bListed ) );
    SW_REQUIRE_TRUE( bListed );
    const DataInheritanceSchema* pLayers = schema.findField( "layers" );
    SW_REQUIRE_TRUE( pLayers != nullptr );
    SW_EXPECT_TRUE( pLayers->_keyAttribute.empty() );

    DataCatalogTestSource source;
    source.add( "common/input/base.input.xml", R"(<InputMap>
        <layers><layer name="Movement" priority="1" /><layer name="Menu" enabled="0" /></layers>
        <action name="Move" layer="Movement"><key key="W" /></action>
        <action name="Jump" layer="Movement"><key key="Space" /></action>
    </InputMap>)" );
    string flatText;
    SW_REQUIRE_TRUE( DataCatalogInheritance::flatten( R"(<InputMap _base="common/input/base.input.xml">
        <layers><layer name="Movement" priority="5" /><layer name="Menu" _op="Remove" /></layers>
        <action name="Jump" _op="Remove" />
    </InputMap>)",
                                                      "child.input.xml", schema, source, flatText ) );
    XMLDocument doc;
    SW_REQUIRE_TRUE( doc.parse( flatText ) );
    const XMLNode layers = doc.getRoot().findChild( "layers" );
    SW_REQUIRE_TRUE( layers.isValid() );
    const XMLNode movement = layers.findChild( "layer" );
    SW_REQUIRE_TRUE( movement.isValid() );
    SW_EXPECT_EQUAL( string( "Movement" ), string( movement.getAttributeText( "name" ) ) );
    SW_EXPECT_EQUAL( string( "5" ), string( movement.getAttributeText( "priority" ) ) );
    SW_EXPECT_FALSE( movement.findNextSibling( "layer" ).isValid() ); // Menu 는 지웠다
    const XMLNode move = doc.getRoot().findChild( "action" );
    SW_REQUIRE_TRUE( move.isValid() );
    SW_EXPECT_EQUAL( string( "Move" ), string( move.getAttributeText( "name" ) ) );
    SW_EXPECT_TRUE( move.findChild( "key" ).isValid() );
    SW_EXPECT_FALSE( move.findNextSibling( "action" ).isValid() ); // Jump 는 지웠다
}

/** @brief [DataCatalogInheritance] 문서 읽기(`parseData`)가 펼친다 - 카탈로그 코드가 상속을 모르고, 저장본의 차이 글에서 다시 읽어도 같다 */
SW_TEST_CASE( DataCatalogInheritance, DocumentReadFlattens )
{
    XMLDocument doc;
    SW_REQUIRE_TRUE( doc.parseData( R"(<ItemCatalog><Item id="a" name="n" category="C"><Stats armor="3" /></Item><Item id="b" _base="a" name="m" /></ItemCatalog>)", "items.xml" ) );
    const XMLNode b = Util::findEntry( doc, "Item", "b" );
    SW_EXPECT_EQUAL( string( "m" ), string( b.getAttributeText( "name" ) ) );
    SW_EXPECT_EQUAL( string( "C" ), string( b.getAttributeText( "category" ) ) );
    SW_EXPECT_EQUAL( string( "3" ), string( b.findChild( "Stats" ).getAttributeText( "armor" ) ) );

    // 펼친 글은 `_base` 가 없어 다시 읽어도 같다 - 쿠킹본이 이 글이다.
    const string flatText = doc.saveToString();
    XMLDocument  cooked;
    SW_REQUIRE_TRUE( cooked.parseData( flatText, "items.xml" ) );
    SW_EXPECT_EQUAL( flatText, cooked.saveToString() );

    SW_EXPECT_LOG( "DataInheritance.BaseNotFound" );
    XMLDocument broken;
    SW_EXPECT_FALSE( broken.parseData( R"(<ItemCatalog><Item id="b" _base="missing" /></ItemCatalog>)", "items.xml" ) );
    SW_EXPECT_FALSE( broken.getLastError().empty() );
}
