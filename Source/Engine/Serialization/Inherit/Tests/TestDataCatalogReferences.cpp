// DataCatalogReferencesTest — 카탈로그 참조 규칙 표: 목록 · 자식 원소 경로 · `id:수` 값 · 선택 참조 · 빈 값 · 다른 파일, 표 오류, 앞검사, 엔진 팩의 실제 표가 읽히는 것,
// 카탈로그 키 칸(`<CatalogKey>` · `<Field>`)의 상속 · 다른 팩 · 깨진 값과 리플렉션 대조.
#include "pch.h"

#include "Core/Log/Logger.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/Pack/AssetCookRoot.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Reflection/PropertyInfo.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Inherit/DataCatalogReferences.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include <algorithm>

using namespace sw;

namespace
{
    constexpr const utf8* kTestTable = R"(<DataCatalogReferenceTable>
  <Catalog root="TestCatalog">
    <Reference owner="Room" attribute="ghosts" target="Ghost"/>
    <Reference owner="Class/Learn" attribute="spell" target="Spell"/>
    <Reference owner="Site" attribute="garrison" target="Ghost" named="true"/>
    <Reference owner="Hint" attribute="topic" target="Spell" optional="true"/>
    <Unique owner="Spell" attribute="id"/>
  </Catalog>
</DataCatalogReferenceTable>)";

    /** @brief 표 규칙으로 글 @p xmlText 를 봅니다. */
    bool validateText( const DataCatalogReferenceRules& rules, string_view xmlText )
    {
        XMLDocument doc;
        SW_EXPECT_TRUE( doc.parse( xmlText, "test" ) );
        return rules.validateDocument( doc.getRoot(), "test" );
    }

    constexpr const utf8* kKeyTable = R"(<DataCatalogReferenceTable>
  <CatalogKey tag="ItemDef" catalog="ItemCatalog" entry="Item"/>
  <Field owner="LootComponent" field="_item" tag="ItemDef"/>
  <Field owner="LootComponent" field="_listItem" tag="ItemDef"/>
</DataCatalogReferenceTable>)";

    constexpr const utf8* kLootScene = R"(<Scene><entities><entity id="1"><GameObject><_listComponent>
  <LootComponent _item="helmet_knight"><_listItem><item>cape_rogue</item><item> my_extra </item></_listItem></LootComponent>
</_listComponent></GameObject></entity></entities></Scene>)";

    /** @brief 글 @p xmlText 를 데이터 문서로 읽어(`_base` 를 펼친다) @p inoutCheck 에 넘깁니다. */
    void addDataText( DataCatalogKeyCheck& inoutCheck, string_view xmlText, string_view sourceName )
    {
        XMLDocument doc;
        const bool  bParsed = doc.parseData( xmlText, sourceName );
        SW_EXPECT_TRUE( bParsed );
        if ( bParsed )
            inoutCheck.addDocument( doc.getRoot(), sourceName );
    }
} // namespace

/** @brief [DataCatalogReferencesTest] 목록 · 자식 원소 · `id:수` 값을 따라가 모두 있으면 통과하고, 뒤에 적힌 대상도 찾는다. 빈 값은 적지 않은 것이다 */
SW_TEST_CASE( DataCatalogReferencesTest, CleanReferencesPass )
{
    DataCatalogReferenceRules rules;
    SW_REQUIRE_TRUE( rules.loadFromXMLText( kTestTable, "table" ) );
    SW_EXPECT_EQUAL( size_t( 5 ), rules.getRules().size() );
    SW_EXPECT_TRUE( rules.hasRulesFor( "TestCatalog" ) );
    SW_EXPECT_FALSE( rules.hasRulesFor( "OtherCatalog" ) );
    SW_EXPECT_TRUE( validateText( rules, R"(<TestCatalog>
  <Room id="r" ghosts="a, b;c"/>
  <Room id="empty" ghosts=""/>
  <Class id="hero"><Learn spell="fire"/><Learn spell="ice"/></Class>
  <Site id="s" garrison="a:2,b:1"/>
  <Ghost id="a"/><Ghost id="b"/><Ghost id="c"/>
  <Spell id="fire"/><Spell id="ice"/>
</TestCatalog>)" ) );
    SW_EXPECT_TRUE( validateText( rules, R"(<OtherCatalog><Room ghosts="nobody"/></OtherCatalog>)" ) ); // 규칙이 없는 루트
}

/** @brief [DataCatalogReferencesTest] 깨진 필수 참조는 목록 · 자식 경로 · `id:수` 어디에 있든 하나마다 알리고 false 다 */
SW_TEST_CASE( DataCatalogReferencesTest, BrokenRequiredReferencesFail )
{
    DataCatalogReferenceRules rules;
    SW_REQUIRE_TRUE( rules.loadFromXMLText( kTestTable, "table" ) );
    SW_EXPECT_LOG( "XMLCatalog.UnknownReference" );
    SW_EXPECT_LOG( "XMLCatalog.UnknownReference" );
    SW_EXPECT_LOG( "XMLCatalog.UnknownReference" );
    SW_EXPECT_FALSE( validateText( rules, R"(<TestCatalog>
  <Room id="r" ghosts="a,nobody"/>
  <Class id="hero"><Learn spell="nope"/></Class>
  <Site id="s" garrison="phantom:3"/>
  <Ghost id="a"/>
</TestCatalog>)" ) );
}

/** @brief [DataCatalogReferencesTest] 선택 참조는 깨져도 경고만 하고 통과한다 */
SW_TEST_CASE( DataCatalogReferencesTest, BrokenOptionalReferenceOnlyWarns )
{
    DataCatalogReferenceRules rules;
    SW_REQUIRE_TRUE( rules.loadFromXMLText( kTestTable, "table" ) );
    SW_EXPECT_LOG( "XMLCatalog.UnknownOptionalReference" );
    SW_EXPECT_TRUE( validateText( rules, R"(<TestCatalog><Hint id="h" topic="elsewhere"/></TestCatalog>)" ) );
}

/** @brief [DataCatalogReferencesTest] 표의 줄이 칸을 빠뜨리거나 모르는 속성 · 원소를 적으면 표 전체가 오류다(반만 검사하지 않는다) */
SW_TEST_CASE( DataCatalogReferencesTest, BrokenTableIsRejected )
{
    SW_EXPECT_LOG( "DataCatalogReferences.RuleInvalid" );
    SW_EXPECT_LOG( "DataCatalogReferences.TableInvalid2" );
    DataCatalogReferenceRules rules;
    SW_EXPECT_FALSE( rules.loadFromXMLText( R"(<DataCatalogReferenceTable><Catalog root="A"><Reference owner="X" target="Y"/></Catalog></DataCatalogReferenceTable>)", "table" ) );
    SW_EXPECT_TRUE( rules.getRules().empty() );
    SW_EXPECT_FALSE( rules.loadFromXMLText( R"(<DataCatalogReferenceTable><Catalog root="A"><Reference owner="X" attribute="a" target="Y" typo="1"/></Catalog></DataCatalogReferenceTable>)", "table" ) );
    SW_EXPECT_FALSE( rules.loadFromXMLText( R"(<DataCatalogReferenceTable><Row/></DataCatalogReferenceTable>)", "table" ) );
}

/** @brief [DataCatalogReferencesTest] 쿠커의 앞검사는 규칙이 있는 루트가 원소로 적힌 글만 고른다(이름이 앞부분만 같은 원소는 고르지 않는다) */
SW_TEST_CASE( DataCatalogReferencesTest, RootMentionPrefilter )
{
    DataCatalogReferenceRules rules;
    SW_REQUIRE_TRUE( rules.loadFromXMLText( kTestTable, "table" ) );
    SW_EXPECT_TRUE( rules.isRootMentioned( R"(<?xml version="1.0"?><TestCatalog><Room/></TestCatalog>)" ) );
    SW_EXPECT_TRUE( rules.isRootMentioned( "<TestCatalog/>" ) );
    SW_EXPECT_FALSE( rules.isRootMentioned( "<TestCatalogExtra/>" ) );
    SW_EXPECT_FALSE( rules.isRootMentioned( "no xml here" ) );
}

/** @brief [DataCatalogReferencesTest] 엔진 팩의 실제 표가 읽히고 엔진 팩에 담긴다(쿠커 · 배포 런타임이 같은 표를 본다) */
SW_TEST_CASE( DataCatalogReferencesTest, ShippedTableLoads )
{
    SW_EXPECT_TRUE( AssetCookRootRegistry::hasRoot( DataCatalogReferenceRules::kTableAssetID ) );
    DataCatalogReferenceRules rules;
    SW_REQUIRE_TRUE( rules.loadFromAsset() );
    SW_EXPECT_TRUE( rules.hasRulesFor( "JRPGCatalog" ) );
    SW_EXPECT_TRUE( rules.hasRulesFor( "ConquestCatalog" ) );
}

/** @brief [DataCatalogReferencesTest] 겹침 줄(`<Unique>`)은 한 파일 안의 같은 값을 하나마다 알리고 false 다. 다른 원소 종류의 같은 값은 겹침이 아니다 */
SW_TEST_CASE( DataCatalogReferencesTest, UniqueRowRejectsDuplicates )
{
    DataCatalogReferenceRules rules;
    SW_REQUIRE_TRUE( rules.loadFromXMLText( kTestTable, "table" ) );
    SW_EXPECT_TRUE( validateText( rules, R"(<TestCatalog><Spell id="fire"/><Ghost id="fire"/></TestCatalog>)" ) );
    SW_EXPECT_LOG( "XMLCatalog.DuplicateID" );
    SW_EXPECT_FALSE( validateText( rules, R"(<TestCatalog><Spell id="fire"/><Spell id="fire"/></TestCatalog>)" ) );
    SW_EXPECT_LOG( "DataCatalogReferences.RuleInvalid" );
    DataCatalogReferenceRules broken;
    SW_EXPECT_FALSE( broken.loadFromXMLText( R"(<DataCatalogReferenceTable><Catalog root="A"><Unique owner="X" attribute="id" target="Y"/></Catalog></DataCatalogReferenceTable>)", "table" ) );
}

/** @brief [DataCatalogReferencesTest] 항목 안 참조(`inEntry`)는 같은 항목의 자식에서만 찾는다 — 다른 항목에 같은 id 가 있어도 통과하지 않는다 */
SW_TEST_CASE( DataCatalogReferencesTest, InEntryReferencesStayInTheEntry )
{
    DataCatalogReferenceRules rules;
    SW_REQUIRE_TRUE( rules.loadFromXMLText( R"(<DataCatalogReferenceTable><Catalog root="Quests">
  <Reference owner="Quest/Stage" attribute="next" target="Stage" inEntry="true"/>
</Catalog></DataCatalogReferenceTable>)",
                                            "table" ) );
    SW_EXPECT_TRUE( validateText( rules, R"(<Quests><Quest id="q"><Stage id="a" next="b"/><Stage id="b"/></Quest></Quests>)" ) );
    SW_EXPECT_LOG( "XMLCatalog.UnknownReference" );
    SW_EXPECT_FALSE( validateText( rules, R"(<Quests><Quest id="q"><Stage id="a" next="b"/></Quest><Quest id="r"><Stage id="b"/></Quest></Quests>)" ) );
}

/**
 * @brief [DataCatalogReferencesTest] 다른 파일의 원소(`file`)는 루트가 적은 리소스에서 찾고, 값을 적지 않으면 `defaultAttribute` 의 값을 id 로 본다
 * @details 격투 캐릭터의 기술(`Fighter/Move`)이 루트 `moves` 의 기술 카탈로그를 가리키는 모양이다. 대상 파일로 엔진 팩의 실제 표를 쓴다(루트의 자식 `<Catalog root>`).
 */
SW_TEST_CASE( DataCatalogReferencesTest, FileReferencesLookInTheNamedResource )
{
    DataCatalogReferenceRules rules;
    SW_REQUIRE_TRUE( rules.loadFromXMLText( R"(<DataCatalogReferenceTable><Catalog root="Picker">
  <Reference owner="Pick" attribute="catalog" defaultAttribute="id" target="Catalog" targetKey="root" file="table"/>
</Catalog></DataCatalogReferenceTable>)",
                                            "table" ) );
    SW_EXPECT_TRUE( validateText( rules, R"(<Picker table="engine/settings/datacatalogreferences.xml">
  <Pick id="a" catalog="JRPGCatalog"/>
  <Pick id="ConquestCatalog"/>
</Picker>)" ) );
    SW_EXPECT_TRUE( validateText( rules, R"(<Picker><Pick id="a" catalog="NoSuchCatalog"/></Picker>)" ) ); // 파일을 적지 않은 루트는 읽는 코드가 막는다
    SW_EXPECT_LOG( "XMLCatalog.UnknownReference" );
    SW_EXPECT_LOG( "XMLCatalog.UnknownReference" );
    SW_EXPECT_FALSE( validateText( rules, R"(<Picker table="engine/settings/datacatalogreferences.xml">
  <Pick id="a" catalog="NoSuchCatalog"/>
  <Pick id="NoSuchEither"/>
</Picker>)" ) );
    SW_EXPECT_LOG( "DataCatalogReferences.RuleInvalid" );
    DataCatalogReferenceRules broken;
    SW_EXPECT_FALSE( broken.loadFromXMLText( R"(<DataCatalogReferenceTable><Catalog root="A"><Reference owner="X" attribute="a" target="Y" inEntry="true" file="f"/></Catalog></DataCatalogReferenceTable>)",
                                             "table" ) );
}

/** @brief [DataCatalogReferencesTest] 칸 줄의 종류는 표에 있어야 하고, 같은 줄을 두 번 적거나 모르는 속성 · 빠진 속성이 있으면 표 전체가 오류다 */
SW_TEST_CASE( DataCatalogReferencesTest, KeyRowsAreChecked )
{
    DataCatalogReferenceRules rules;
    SW_REQUIRE_TRUE( rules.loadFromXMLText( kKeyTable, "table" ) );
    SW_REQUIRE_TRUE( rules.findKeyTarget( "ItemDef" ) != nullptr );
    SW_EXPECT_EQUAL( string( "id" ), rules.findKeyTarget( "ItemDef" )->_targetKey );
    SW_EXPECT_TRUE( rules.findKeyField( "LootComponent", "_listItem" ) != nullptr );
    SW_EXPECT_TRUE( rules.findKeyField( "LootComponent", "_other" ) == nullptr );
    SW_EXPECT_LOG( "DataCatalogReferences.RuleInvalid" );
    SW_EXPECT_FALSE( rules.loadFromXMLText( R"(<DataCatalogReferenceTable><Field owner="A" field="_b" tag="Missing"/></DataCatalogReferenceTable>)", "table" ) );
    SW_EXPECT_TRUE( rules.getFieldRows().empty() );
    SW_EXPECT_LOG( "DataCatalogReferences.RuleInvalid" );
    SW_EXPECT_FALSE( rules.loadFromXMLText( R"(<DataCatalogReferenceTable><CatalogKey tag="T" catalog="C" entry="E"/><CatalogKey tag="T" catalog="C" entry="E"/></DataCatalogReferenceTable>)", "table" ) );
    SW_EXPECT_LOG( "DataCatalogReferences.RuleInvalid" );
    SW_EXPECT_FALSE( rules.loadFromXMLText( R"(<DataCatalogReferenceTable><CatalogKey tag="T" catalog="C" entry="E" typo="1"/></DataCatalogReferenceTable>)", "table" ) );
    SW_EXPECT_LOG( "DataCatalogReferences.RuleInvalid" );
    SW_EXPECT_FALSE( rules.loadFromXMLText( R"(<DataCatalogReferenceTable><CatalogKey tag="T" catalog="C"/></DataCatalogReferenceTable>)", "table" ) );
}

/**
 * @brief [DataCatalogReferencesTest] 카탈로그 키 칸은 물려받은 항목(루트 `_base`)과 다른 팩의 카탈로그 항목도 찾고, 넘긴 차례(씬이 먼저)에 기대지 않는다
 * @details 게임 팩 카탈로그가 `common/data/items.xml` 을 통째로 물려받고 항목 하나를 더한 모양이다 — 키는 부모에만 있는 항목(`helmet_knight`)과 자식이 더한 항목을 가리킨다.
 */
SW_TEST_CASE( DataCatalogReferencesTest, KeyValuesFindInheritedAndOtherPackEntries )
{
    DataCatalogReferenceRules rules;
    SW_REQUIRE_TRUE( rules.loadFromXMLText( kKeyTable, "table" ) );
    DataCatalogKeyCheck inherited( rules );
    SW_EXPECT_TRUE( inherited.isMentioned( kLootScene ) );
    SW_EXPECT_FALSE( inherited.isMentioned( "<LootComponentExtra/>" ) );
    addDataText( inherited, kLootScene, "game/test/maps/loot.scene.xml" );
    addDataText( inherited, R"(<ItemCatalog _base="common/data/items.xml"><Item id="my_extra" name="Extra"/></ItemCatalog>)", "game/test/data/items.xml" );
    SW_EXPECT_TRUE( inherited.validate() );

    DataCatalogKeyCheck otherPack( rules );
    addDataText( otherPack, kLootScene, "game/test/maps/loot.scene.xml" );
    addDataText( otherPack, R"(<ItemCatalog><Item id="my_extra" name="Extra"/></ItemCatalog>)", "game/test/data/items.xml" );
    XMLDocument common;
    SW_REQUIRE_TRUE( common.loadResource( AssetID{ "common/data/items.xml" } ) );
    otherPack.addDocument( common.getRoot(), "common/data/items.xml" );
    SW_EXPECT_TRUE( otherPack.validate() );
}

/** @brief [DataCatalogReferencesTest] 어느 카탈로그에도 없는 항목을 가리키는 칸 값은 값 하나 · 목록 원소 어디에 있든 하나마다 알리고 false 다(쿠킹 실패). "None" 은 빈 키다 */
SW_TEST_CASE( DataCatalogReferencesTest, BrokenKeyValuesFail )
{
    DataCatalogReferenceRules rules;
    SW_REQUIRE_TRUE( rules.loadFromXMLText( kKeyTable, "table" ) );
    DataCatalogKeyCheck check( rules );
    addDataText( check, R"(<ItemCatalog><Item id="my_extra"/></ItemCatalog>)", "game/test/data/items.xml" );
    addDataText( check, R"(<Prefab><GameObject><LootComponent _item="nope"><_listItem><item>my_extra</item><item>None</item><item>gone</item></_listItem></LootComponent></GameObject></Prefab>)",
                 "game/test/prefabs/loot.prefab.xml" );
    SW_EXPECT_LOG( "DataCatalogKey.UnknownEntry" );
    SW_EXPECT_LOG( "DataCatalogKey.UnknownEntry" );
    SW_EXPECT_FALSE( check.validate() );
}

/**
 * @brief [DataCatalogReferencesTest] 표의 칸 줄은 이 실행 파일에 올라온 리플렉션의 `CatalogKey<Tag>` 칸과 같다 — 칸마다 줄이 있고 종류 이름이 같으며, 줄마다 칸이 있다
 * @details 게이트를 두는 까닭: 쿠커는 게임 모듈의 리플렉션을 보지 못해 표를 읽는다. 칸을 더하고 줄을 빠뜨리면 쿠킹이 그 칸을 보지 않는다. 올라오지 않은 모듈의 줄은 보지 않는다.
 */
SW_TEST_CASE( DataCatalogReferencesTest, FieldTableMatchesReflection )
{
    DataCatalogReferenceRules rules;
    SW_REQUIRE_TRUE( rules.loadFromAsset() );
    const hashed_string typeName( CatalogKeyBase::kTypeName );
    vector<string>      listProblem;
    TypeRegistry&       registry = engine::getSubsystem<TypeRegistry>();
    registry.forEachType( [&]( const TypeInfo& type )
    {
        for ( const PropertyInfo& prop : type._listProperty )
        {
            if ( prop._typeName != typeName && prop._elementTypeName != typeName )
                continue;
            const string               fieldName = string( type._name.view() ) + "::" + string( prop._name.view() );
            const DataCatalogKeyField* pRow      = rules.findKeyField( type._name.view(), prop._name.view() );
            if ( pRow == nullptr )
                listProblem.push_back( fieldName + " has no <Field> row" );
            else if ( pRow->_tag != prop._metadata->_catalogKeyTag )
                listProblem.push_back( fieldName + " is CatalogKey<" + prop._metadata->_catalogKeyTag + "> but its row says " + pRow->_tag );
        }
    } );
    for ( const DataCatalogKeyField& row : rules.getFieldRows() )
    {
        const TypeInfo* pType = registry.findType( hashed_string( row._owner.c_str() ) );
        if ( pType == nullptr )
            continue; // 이 실행 파일에 올라오지 않은 모듈
        const bool bHasField = std::any_of( pType->_listProperty.begin(), pType->_listProperty.end(), [&row, &typeName]( const PropertyInfo& prop )
        { return prop._name.view() == row._field && ( prop._typeName == typeName || prop._elementTypeName == typeName ); } );
        if ( bHasField == false )
            listProblem.push_back( row._owner + "::" + row._field + " row names no CatalogKey field" );
    }
    for ( const string& problem : listProblem )
    {
        SW_LOG_ERROR_ID( "DataCatalogReferencesTest.FieldRowMismatch", "%# (%#)", problem, DataCatalogReferenceRules::kTableAssetID );
    }
    SW_EXPECT_TRUE( listProblem.empty() );
}
