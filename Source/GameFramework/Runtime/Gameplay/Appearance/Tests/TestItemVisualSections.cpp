#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceTypes.h"
#include "GameFramework/Runtime/Gameplay/Appearance/ItemVisual.h"

// ItemVisualSectionsTest — 스킨드 부품의 <Section name material>: 구간마다 머티리얼(머티리얼이 여럿인 몸 — 몸 · 머리카락 · 눈), 부품 material 과 함께 쓰면 오류.

namespace
{
    bool loadCatalog( const char* pText, sw::ItemVisualCatalog& outCatalog, sw::AppearanceLoadReport& outReport )
    {
        sw::XMLDocument document;
        if ( document.parse( pText ) == false )
            return false;
        return outCatalog.loadFromNode( document.getRoot( "ItemVisualCatalog" ), outReport, "test" );
    }
} // namespace

/**
 * @brief [ItemVisualSectionsTest] 구간을 적은 순서대로 읽는다 — 첫 구간은 주 메시가, 나머지는 구간 컴포넌트가 그린다
 */
SW_TEST_CASE( ItemVisualSectionsTest, ReadsSectionMaterialsInOrder )
{
    sw::ItemVisualCatalog    catalog;
    sw::AppearanceLoadReport report;
    SW_REQUIRE_TRUE( loadCatalog( R"(<ItemVisualCatalog>
        <ItemVisual id="body_ubc">
          <Part name="Body" kind="Skinned" mesh="body.mesh">
            <Section name="mi_body" material="body.material"/>
            <Section name="mi_hair" material="hair.material"/>
          </Part>
        </ItemVisual>
      </ItemVisualCatalog>)",
                                  catalog, report ) );
    SW_REQUIRE_FALSE( report.hasErrors() );
    const sw::ItemVisualDef* pVisual = catalog.findVisual( sw::hashed_string( "body_ubc" ) );
    SW_REQUIRE_TRUE( pVisual != nullptr );
    const sw::AppearancePartDef& part = pVisual->_listPart.front();
    SW_REQUIRE_EQUAL( size_t{ 2 }, part._listSection.size() );
    SW_EXPECT_TRUE( part._listSection[0]._name == sw::hashed_string( "mi_body" ) );
    SW_EXPECT_TRUE( part._listSection[1]._material == sw::hashed_string( "hair.material" ) );
}

/**
 * @brief [ItemVisualSectionsTest] 부품 material 과 구간을 함께 적으면 오류다 — 어느 구간의 머티리얼인지 모른다
 */
SW_TEST_CASE( ItemVisualSectionsTest, SectionsWithPartMaterialFail )
{
    sw::ItemVisualCatalog    catalog;
    sw::AppearanceLoadReport report;
    SW_EXPECT_LOG( "Appearance.SectionWithPartMaterial" );
    (void)loadCatalog( R"(<ItemVisualCatalog>
        <ItemVisual id="body_ubc">
          <Part name="Body" kind="Skinned" mesh="body.mesh" material="body.material">
            <Section name="mi_body" material="body.material"/>
          </Part>
        </ItemVisual>
      </ItemVisualCatalog>)",
                       catalog, report );
    SW_EXPECT_EQUAL( uint32{ 1 }, report.countWithID( "Appearance.SectionWithPartMaterial" ) );
}
