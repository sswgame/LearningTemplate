#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Foundation/Framework/Presentation/RuleViewCatalog.h"

// RuleViewCatalogTest — 규칙 id → 뷰 표: 메시는 타입 있는 참조로 읽고(코드가 경로를 조립하지 않는다, 감사 K-7), 빠진 칸은 그 줄을 넣지 않는다(nogpu).

/**
 * @brief [RuleViewCatalogTest] 줄의 메시 경로 · 폭을 그대로 읽고, 모든 메시 경로는 겹치지 않게 한 번씩 낸다. 폭이 빠진 줄은 거절한다
 */
SW_TEST_CASE( RuleViewCatalogTest, ReadsViewsAndRefusesBrokenRows )
{
    sw::RuleViewCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<RuleViewCatalog>
  <View id="minerals" mesh="game/playground/starskirmish/models/rock_crystals.mesh" width="0.85"/>
  <View id="rich_minerals" mesh="game/playground/starskirmish/models/rock_crystals.mesh" width="0.85"/>
  <View id="marine" mesh="game/playground/starskirmish/models/craft_speeder_a.mesh" width="2.1"/>
</RuleViewCatalog>)",
                                              "RuleViewCatalogTest" ) );
    const sw::RuleViewDef* pMarine = catalog.find( sw::hashed_string( "marine" ) );
    SW_REQUIRE_NOT_NULL( pMarine );
    SW_EXPECT_EQUAL( sw::string( "game/playground/starskirmish/models/craft_speeder_a.mesh" ), pMarine->_mesh.path() );
    SW_EXPECT_NEAR( 2.1f, pMarine->_width, 1.0e-5f );
    SW_EXPECT_EQUAL( size_t( 2 ), catalog.makeAllMeshPaths().size() );

    SW_EXPECT_LOG( "RuleViewCatalog.ViewInvalidFieldsSkipped" );
    SW_EXPECT_LOG( "XMLFields.RequiredFieldMissing" );
    sw::RuleViewCatalog broken;
    SW_EXPECT_FALSE( broken.loadFromXMLText( R"(<RuleViewCatalog>
  <View id="marine" mesh="game/playground/starskirmish/models/craft_speeder_a.mesh"/>
</RuleViewCatalog>)",
                                             "RuleViewCatalogTest" ) );
}
