// DataCatalogReferencesTest — 카탈로그 참조 규칙 표: 목록 · 자식 원소 경로 · `id:수` 값 · 선택 참조 · 빈 값 · 다른 파일, 표 오류, 앞검사, 엔진 팩의 실제 표가 읽히는 것,
// 카탈로그 키 칸(`<CatalogKey>` · `<Field>`)의 상속 · 다른 팩 · 깨진 값과 리플렉션 대조.
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/unordered_set.h"
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
#include "Engine/Serialization/XML/XMLFields.h"

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
    SW_EXPECT_TRUE( AssetCookRoot::hasRoot( DataCatalogReferenceRules::kTableAssetID ) );
    DataCatalogReferenceRules rules;
    SW_REQUIRE_TRUE( rules.loadFromAsset() );
    SW_EXPECT_TRUE( rules.hasRulesFor( "JRPGCatalog" ) );
    SW_EXPECT_TRUE( rules.hasRulesFor( "ConquestCatalog" ) );
    SW_EXPECT_TRUE( rules.isCookedRoot( "QuestCatalog" ) );
    SW_EXPECT_TRUE( rules.isCookedRootMentioned( "<?xml version=\"1.0\"?><QuestCatalog/>" ) );
    SW_EXPECT_FALSE( rules.isCookedRoot( "Minigame" ) ); // 카탈로그 프레임의 루트가 아니다
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

/** @brief [DataCatalogReferencesTest] 지운 항목의 id(`absent`)는 이 파일에 없어야 한다 — 아직 있으면 하나마다 알리고 false 다. `optional` 과 함께 적으면 표 오류다 */
SW_TEST_CASE( DataCatalogReferencesTest, AbsentReferencesMustNameRemovedEntries )
{
    DataCatalogReferenceRules rules;
    SW_REQUIRE_TRUE( rules.loadFromXMLText( R"(<DataCatalogReferenceTable><Catalog root="Parts">
  <Reference owner="Part" attribute="replaces" target="Part" absent="true"/>
</Catalog></DataCatalogReferenceTable>)",
                                            "table" ) );
    SW_EXPECT_TRUE( validateText( rules, R"(<Parts><Part id="bed" replaces="cot"/><Part id="wall"/></Parts>)" ) );
    SW_EXPECT_LOG( "XMLCatalog.RemovedIDStillPresent" );
    SW_EXPECT_FALSE( validateText( rules, R"(<Parts><Part id="bed" replaces="cot"/><Part id="cot"/></Parts>)" ) );
    SW_EXPECT_LOG( "DataCatalogReferences.RuleInvalid" );
    DataCatalogReferenceRules broken;
    SW_EXPECT_FALSE( broken.loadFromXMLText( R"(<DataCatalogReferenceTable><Catalog root="A"><Reference owner="X" attribute="a" target="Y" absent="true" optional="true"/></Catalog></DataCatalogReferenceTable>)",
                                             "table" ) );
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

/** @brief [DataCatalogReferencesTest] 물려받은 형식(`<Derived>`)의 원소는 바탕 · 바탕의 바탕이 가진 키 칸을 대조한다 — 예전에는 원소 이름이 바탕 이름이 아니라 놓쳤다. 자기에게 돌아오는 줄은 표 오류다 */
SW_TEST_CASE( DataCatalogReferencesTest, DerivedRowsFollowTheBaseType )
{
    DataCatalogReferenceRules rules;
    SW_REQUIRE_TRUE( rules.loadFromXMLText( R"(<DataCatalogReferenceTable>
  <CatalogKey tag="ItemDef" catalog="ItemCatalog" entry="Item"/>
  <Field owner="LootComponent" field="_item" tag="ItemDef"/>
  <Derived type="ChestComponent" base="LootComponent"/>
  <Derived type="MimicComponent" base="ChestComponent"/>
</DataCatalogReferenceTable>)",
                                            "table" ) );
    DataCatalogKeyCheck check( rules );
    SW_EXPECT_TRUE( check.isMentioned( R"(<Scene><MimicComponent _item="gold"/></Scene>)" ) );
    addDataText( check, R"(<ItemCatalog><Item id="gold"/></ItemCatalog>)", "items" );
    addDataText( check, R"(<Scene><ChestComponent _item="gold"/><MimicComponent _item="gone"/></Scene>)", "scene" );
    SW_EXPECT_LOG( "DataCatalogKey.UnknownEntry" ); // 미믹의 gone 하나
    SW_EXPECT_FALSE( check.validate() );

    SW_EXPECT_LOG( "DataCatalogReferences.RuleInvalid" );
    SW_EXPECT_LOG( "DataCatalogReferences.RuleInvalid" );
    DataCatalogReferenceRules looped;
    SW_EXPECT_FALSE( looped.loadFromXMLText( R"(<DataCatalogReferenceTable><Derived type="A" base="B"/><Derived type="B" base="A"/></DataCatalogReferenceTable>)", "table" ) );
}

/** @brief [DataCatalogReferencesTest] 루트가 항목 원소 하나인 파일(에디터가 내보낸 프리셋)도 그 항목을 정의한다 — 카탈로그 루트만 보면 런타임이 읽는 항목을 모른다 */
SW_TEST_CASE( DataCatalogReferencesTest, SingleEntryFileDefinesItsEntry )
{
    DataCatalogReferenceRules rules;
    SW_REQUIRE_TRUE( rules.loadFromXMLText( kKeyTable, "table" ) );
    DataCatalogKeyCheck check( rules );
    addDataText( check, R"(<ItemCatalog><Item id="cape_rogue"/><Item id="my_extra"/></ItemCatalog>)", "game/test/data/items.xml" );
    addDataText( check, R"(<Item id="helmet_knight"/>)", "game/test/data/helmet_knight.xml" );
    addDataText( check, kLootScene, "game/test/maps/loot.scene.xml" );
    SW_EXPECT_TRUE( check.validate() );
    XMLDocument doc;
    SW_REQUIRE_TRUE( doc.parse( R"(<Item id="helmet_knight"/>)", "test" ) );
    vector<string_view> listID;
    DataCatalogReferenceRules::collectEntryIDs( doc.getRoot(), *rules.findKeyTarget( "ItemDef" ), listID );
    SW_EXPECT_EQUAL( size_t( 1 ), listID.size() );
}

/**
 * @brief [DataCatalogReferencesTest] 구조체 칸 하나(`<Struct>`)는 칸 이름의 원소로 적혀도 그 형식의 키 칸으로 본다 — 예전에는 원소 이름이 형식 이름이 아니라 놓쳤다
 */
SW_TEST_CASE( DataCatalogReferencesTest, StructRowFindsSingleStructField )
{
    DataCatalogReferenceRules rules;
    SW_REQUIRE_TRUE( rules.loadFromXMLText( R"(<DataCatalogReferenceTable>
  <CatalogKey tag="ItemDef" catalog="ItemCatalog" entry="Item"/>
  <Field owner="LootDrop" field="_item" tag="ItemDef"/>
  <Struct owner="LootComponent" field="_drop" type="LootDrop"/>
  <Struct owner="LootDrop" field="_bonus" type="LootDrop"/>
</DataCatalogReferenceTable>)",
                                            "table" ) );
    SW_REQUIRE_TRUE( rules.findKeyStruct( "LootComponent", "_drop" ) != nullptr );
    DataCatalogKeyCheck check( rules );
    SW_EXPECT_TRUE( check.isMentioned( "<LootComponent/>" ) );
    addDataText( check, R"(<ItemCatalog><Item id="coin"/></ItemCatalog>)", "game/test/data/items.xml" );
    addDataText( check, R"(<Scene><LootComponent><_drop _item="coin"><_bonus _item="gem"/></_drop></LootComponent></Scene>)", "game/test/maps/loot.scene.xml" );
    SW_EXPECT_LOG( "DataCatalogKey.UnknownEntry" );
    SW_EXPECT_FALSE( check.validate() ); // 구조체 안 구조체(`_bonus`)의 gem 하나만 없다
}

/**
 * @brief [DataCatalogReferencesTest] 카탈로그 정의의 키 칸은 `<Element>` 줄이 이은 원소에서 속성(목록은 쉼표 목록)으로 찾고, 항목 경로(`Honor/Action`)도 따라간다
 * @details 무기 → 탄처럼 카탈로그 사이의 참조다. 속성 이름은 칸 이름에서 `_` 를 뗀 것이고, `PROPERTY( Name )` 으로 바꾼 칸은 줄의 `attribute` 가 적는다.
 */
SW_TEST_CASE( DataCatalogReferencesTest, ElementRowFindsCatalogDefinitionKeys )
{
    DataCatalogReferenceRules rules;
    SW_REQUIRE_TRUE( rules.loadFromXMLText( R"(<DataCatalogReferenceTable>
  <CatalogKey tag="ItemDef" catalog="ItemCatalog" entry="Item"/>
  <CatalogKey tag="ActionDef" catalog="Western" entry="Honor/Action"/>
  <Element type="TurretDef" catalog="Turrets" path="Turret"/>
  <Field owner="TurretDef" field="_lootID" tag="ItemDef" attribute="loot"/>
  <Field owner="TurretDef" field="_listSpare" tag="ItemDef"/>
  <Field owner="TurretDef" field="_action" tag="ActionDef"/>
</DataCatalogReferenceTable>)",
                                            "table" ) );
    SW_EXPECT_EQUAL( string( "loot" ), DataCatalogReferenceRules::findElementAttribute( *rules.findKeyField( "TurretDef", "_lootID" ) ) );
    SW_EXPECT_EQUAL( string( "listSpare" ), DataCatalogReferenceRules::findElementAttribute( *rules.findKeyField( "TurretDef", "_listSpare" ) ) );
    DataCatalogKeyCheck check( rules );
    addDataText( check, R"(<Turrets><Turret id="t" loot="coin" listSpare="coin, gone" action="help"/></Turrets>)", "game/test/data/turrets.xml" );
    addDataText( check, R"(<ItemCatalog><Item id="coin"/></ItemCatalog>)", "game/test/data/items.xml" );
    addDataText( check, R"(<Western><Honor><Action id="help"/></Honor></Western>)", "game/test/data/western.xml" );
    SW_EXPECT_LOG( "DataCatalogKey.UnknownEntry" );
    SW_EXPECT_FALSE( check.validate() ); // gone 하나만 없다
}

/**
 * @brief [DataCatalogReferencesTest] 표의 칸 · 구조체 · 원소 줄은 이 실행 파일에 올라온 리플렉션과 같다
 * @details 게이트를 두는 까닭: 쿠커는 게임 모듈의 리플렉션을 보지 못해 표를 읽는다. 칸을 더하고 줄을 빠뜨리면 쿠킹이 그 칸을 보지 않는다. 올라오지 않은 모듈의 줄은 보지 않는다.
 *          - `CatalogKey<Tag>` 칸마다 `<Field>` 줄이 있고 종류 이름이 같다. 줄마다 그 칸이 있다.
 *          - 키를 든 구조체가 칸 하나로 들면(원소 이름이 칸 이름) `<Struct owner field type>` 줄이 있다.
 *          - 칸 줄의 주인은 쿠커가 원소 이름으로 찾을 수 있다 — 컴포넌트, 목록 원소 구조체, `<Struct>` 형식, 또는 `<Element>` 줄이 이은 카탈로그 정의다.
 *          - 키를 든 형식을 물려받은 형식마다 `<Derived type base>` 줄이 있다(쿠커는 원소 이름 — 가장 아래 형식 — 에서 이 줄로 바탕의 칸 줄을 따라간다).
 *          - 카탈로그 정의의 칸 줄 속성 이름은 `XMLFields::getAttributeName` 과 같다.
 *          타입 표는 잠근 채 다시 물을 수 없어(공유 잠금은 재귀가 아니다) 먼저 칸을 모으고 잠금 밖에서 본다.
 */
SW_TEST_CASE( DataCatalogReferencesTest, FieldTableMatchesReflection )
{
    struct PropertyRecord
    {
        string _owner;
        string _field;
        string _typeName;
        string _elementTypeName;
        string _tag;
        string _attribute;
        bool   _bContainer{ false };      // NOLINT(sw-bool-flag): 테스트 안의 집합체다
        bool   _bComponentOwner{ false }; // NOLINT(sw-bool-flag): 테스트 안의 집합체다
    };
    DataCatalogReferenceRules rules;
    SW_REQUIRE_TRUE( rules.loadFromAsset() );
    const hashed_string               keyTypeName( CatalogKeyBase::kTypeName );
    vector<PropertyRecord>            listRecord;
    vector<std::pair<string, string>> listParent; // (형식, 부모 형식 이름 — 이름공간 뺌)
    TypeRegistry&                     registry = engine::getSubsystem<TypeRegistry>();
    registry.forEachType( [&]( const TypeInfo& type )
    {
        const string_view parent   = type._parentFQN.view();
        const size_t      colon    = parent.rfind( ':' );
        const string_view leafName = colon == string_view::npos ? parent : parent.substr( colon + 1 );
        if ( leafName.empty() == false )
            listParent.emplace_back( string( type._name.view() ), string( leafName ) );
        for ( const PropertyInfo& prop : type._listProperty )
        {
            PropertyRecord record;
            record._owner           = string( type._name.view() );
            record._field           = string( prop._name.view() );
            record._typeName        = string( prop._typeName.view() );
            record._elementTypeName = string( prop._elementTypeName.view() );
            record._tag             = prop._metadata->_catalogKeyTag;
            record._attribute       = XMLFields::getAttributeName( prop );
            record._bContainer      = prop._bIsContainer != SW_FALSE;
            record._bComponentOwner = type._addComponent != nullptr;
            listRecord.push_back( std::move( record ) );
        }
    } );
    const auto isKey = [&keyTypeName]( const PropertyRecord& record )
    { return record._typeName == keyTypeName.view() || record._elementTypeName == keyTypeName.view(); };
    const auto findRecord = [&listRecord]( string_view owner, string_view field ) -> const PropertyRecord*
    {
        const auto iter = std::find_if( listRecord.begin(), listRecord.end(), [owner, field]( const PropertyRecord& record )
        { return record._owner == owner && record._field == field; } );
        return iter != listRecord.end() ? &*iter : nullptr;
    };
    const auto isLoadedType = [&listRecord]( string_view owner )
    {
        return std::any_of( listRecord.begin(), listRecord.end(), [owner]( const PropertyRecord& record )
        { return record._owner == owner; } );
    };
    // 키를 든 형식: 키 칸이 있거나 `<Struct>` 줄의 주인이다(구조체 안의 구조체).
    unordered_set<string> setKeyHolder;
    unordered_set<string> setComponent;
    unordered_set<string> setSequenceElement;
    for ( const PropertyRecord& record : listRecord )
    {
        if ( isKey( record ) )
            setKeyHolder.insert( record._owner );
        if ( record._bComponentOwner )
            setComponent.insert( record._owner );
        if ( record._bContainer && record._elementTypeName.empty() == false )
            setSequenceElement.insert( record._elementTypeName );
    }
    for ( const DataCatalogKeyStruct& row : rules.getStructRows() )
    {
        setKeyHolder.insert( row._owner );
    }
    vector<string>                           listProblem;
    StringBuilder<constant::buffer::kMax512> problem;
    // 키를 든 형식을 물려받은 형식도 키를 든다(바탕의 바탕까지 — 더 늘지 않을 때까지 돈다).
    for ( bool bGrown = true; bGrown; )
    {
        bGrown = false;
        for ( const auto& [typeName, parentName] : listParent )
        {
            if ( setKeyHolder.contains( parentName ) == false )
                continue;
            const DataCatalogKeyDerived* pRow = rules.findKeyDerived( typeName );
            if ( setKeyHolder.contains( typeName ) == false )
            {
                setKeyHolder.insert( typeName );
                bGrown = true;
            }
            else if ( pRow != nullptr )
                continue;
            if ( pRow == nullptr || pRow->_base != parentName )
            {
                problem.clear();
                problem.appendFormat( "%# derives from the key holder %# but has no <Derived type base> row with that base - the cook would not see the inherited key fields", typeName, parentName );
                if ( VectorUtil::contains( listProblem, string( problem.view() ) ) == false )
                    listProblem.push_back( string( problem.view() ) );
            }
        }
    }
    for ( const PropertyRecord& record : listRecord )
    {
        if ( isKey( record ) )
        {
            const DataCatalogKeyField* pRow = rules.findKeyField( record._owner, record._field );
            problem.clear();
            if ( pRow == nullptr )
                problem.appendFormat( "%#::%# has no <Field> row", record._owner, record._field );
            else if ( pRow->_tag != record._tag )
                problem.appendFormat( "%#::%# is CatalogKey<%#> but its row says %#", record._owner, record._field, record._tag, pRow->_tag );
            if ( problem.size() > 0 )
                listProblem.push_back( string( problem.view() ) );
            continue;
        }
        if ( record._bContainer == false && setKeyHolder.contains( record._typeName ) )
        {
            const DataCatalogKeyStruct* pRow = rules.findKeyStruct( record._owner, record._field );
            problem.clear();
            if ( pRow == nullptr )
                problem.appendFormat( "%#::%# holds the key struct %# but has no <Struct> row", record._owner, record._field, record._typeName );
            else if ( pRow->_type != record._typeName )
                problem.appendFormat( "%#::%# is %# but its <Struct> row says %#", record._owner, record._field, record._typeName, pRow->_type );
            if ( problem.size() > 0 )
                listProblem.push_back( string( problem.view() ) );
        }
    }
    for ( const DataCatalogKeyField& row : rules.getFieldRows() )
    {
        if ( isLoadedType( row._owner ) == false )
            continue; // 이 실행 파일에 올라오지 않은 모듈
        const PropertyRecord* pRecord = findRecord( row._owner, row._field );
        if ( pRecord == nullptr || isKey( *pRecord ) == false )
        {
            problem.clear();
            problem.appendFormat( "%#::%# row names no CatalogKey field", row._owner, row._field );
            listProblem.push_back( string( problem.view() ) );
            continue;
        }
        const bool                   bStructType = std::any_of( rules.getStructRows().begin(), rules.getStructRows().end(), [&row]( const DataCatalogKeyStruct& structRow )
                          { return structRow._type == row._owner; } );
        const DataCatalogKeyElement* pElement    = rules.findKeyElement( row._owner );
        if ( setComponent.contains( row._owner ) == false && setSequenceElement.contains( row._owner ) == false && bStructType == false && pElement == nullptr )
        {
            problem.clear();
            problem.appendFormat( "%# (field %#) is not a component, a list element, a <Struct> type or an <Element> type - the cook cannot find it", row._owner, row._field );
            listProblem.push_back( string( problem.view() ) );
        }
        if ( pElement != nullptr && DataCatalogReferenceRules::findElementAttribute( row ) != pRecord->_attribute )
        {
            problem.clear();
            problem.appendFormat( "%#::%# is written as '%#' in the catalog but the row reads '%#'", row._owner, row._field, pRecord->_attribute,
                                  DataCatalogReferenceRules::findElementAttribute( row ) );
            listProblem.push_back( string( problem.view() ) );
        }
    }
    for ( const DataCatalogKeyDerived& row : rules.getDerivedRows() )
    {
        const bool bMatches = std::any_of( listParent.begin(), listParent.end(), [&row]( const std::pair<string, string>& parent )
        { return parent.first == row._type && parent.second == row._base; } );
        if ( isLoadedType( row._type ) && bMatches == false )
        {
            problem.clear();
            problem.appendFormat( "<Derived type=\"%#\" base=\"%#\"> row does not match the reflected base type", row._type, row._base );
            listProblem.push_back( string( problem.view() ) );
        }
    }
    for ( const DataCatalogKeyStruct& row : rules.getStructRows() )
    {
        if ( isLoadedType( row._owner ) == false )
            continue;
        const PropertyRecord* pRecord = findRecord( row._owner, row._field );
        if ( pRecord == nullptr || pRecord->_bContainer || pRecord->_typeName != row._type )
        {
            problem.clear();
            problem.appendFormat( "%#::%# <Struct> row names no %# field", row._owner, row._field, row._type );
            listProblem.push_back( string( problem.view() ) );
        }
    }
    for ( const string& problem : listProblem )
    {
        SW_LOG_ERROR_ID( "DataCatalogReferencesTest.FieldRowMismatch", "%# (%#)", problem, DataCatalogReferenceRules::kTableAssetID );
    }
    SW_EXPECT_TRUE( listProblem.empty() );
}
