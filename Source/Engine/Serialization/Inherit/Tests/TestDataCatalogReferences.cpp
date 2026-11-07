// DataCatalogReferencesTest — 카탈로그 참조 규칙 표: 목록 · 자식 원소 경로 · `id:수` 값 · 선택 참조 · 빈 값, 표 오류, 앞검사, 엔진 팩의 실제 표가 읽히는 것.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Content/AssetIO.h"
#include "Engine/Content/Pack/AssetCookRoot.h"
#include "Engine/Serialization/Inherit/DataCatalogReferences.h"
#include "Engine/Serialization/XML/XMLDocument.h"

using namespace sw;

namespace
{
    constexpr const utf8* kTestTable = R"(<DataCatalogReferenceTable>
  <Catalog root="TestCatalog">
    <Reference owner="Room" attribute="ghosts" target="Ghost"/>
    <Reference owner="Class/Learn" attribute="spell" target="Spell"/>
    <Reference owner="Site" attribute="garrison" target="Ghost" named="true"/>
    <Reference owner="Hint" attribute="topic" target="Spell" optional="true"/>
  </Catalog>
</DataCatalogReferenceTable>)";

    /** @brief 표 규칙으로 글 @p xmlText 를 봅니다. */
    bool validateText( const DataCatalogReferenceRules& rules, string_view xmlText )
    {
        XMLDocument doc;
        SW_EXPECT_TRUE( doc.parse( xmlText, "test" ) );
        return rules.validateDocument( doc.getRoot(), "test" );
    }
} // namespace

/** @brief [DataCatalogReferencesTest] 목록 · 자식 원소 · `id:수` 값을 따라가 모두 있으면 통과하고, 뒤에 적힌 대상도 찾는다. 빈 값은 적지 않은 것이다 */
SW_TEST_CASE( DataCatalogReferencesTest, CleanReferencesPass )
{
    DataCatalogReferenceRules rules;
    SW_REQUIRE_TRUE( rules.loadFromXMLText( kTestTable, "table" ) );
    SW_EXPECT_EQUAL( size_t( 4 ), rules.getRules().size() );
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
