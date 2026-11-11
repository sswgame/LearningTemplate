#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceTypes.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearance.h"
#include "GameFramework/Runtime/Gameplay/Appearance/ItemVisual.h"
#include "GameFramework/Runtime/Gameplay/Appearance/ResolvedAppearance.h"

using namespace sw;

namespace
{
    struct AppearanceSectionsTestUtil
    {
        static constexpr const utf8* kSlotsXML = R"(<SlotCatalog><Slot name="Head"/><Slot name="Torso"/><Slot name="Shoulder"/></SlotCatalog>)";

        // 구간이 둘인 옷: 메시 변형(피해 단계가 고른다)과 머티리얼 변형(꾸미기가 고른다)이 구간 하나씩 바꾸고, 변형 · 피해 단계가 구간 하나에 값을 건다.
        static constexpr const utf8* kVisualsXML = R"(<ItemVisualCatalog>
            <ItemVisual id="body"><Part name="Body" kind="Skinned" mesh="body.mesh" material="body.material"/></ItemVisual>
            <ItemVisual id="suit" customization="Suit">
              <Part name="Suit" kind="Skinned" mesh="suit.mesh">
                <Section name="cloth" material="cloth.material"/>
                <Section name="trim" material="trim.material"/>
                <Variant name="Worn"><Section name="trim" material="trim_worn.material"/><Material section="cloth" name="_dirt" value="0.5"/></Variant>
                <MaterialVariant name="Gold" material="gold.material"><Section name="trim" material="trim_gold.material"/></MaterialVariant>
              </Part>
              <DamageStage name="Hit" threshold="0.5"><Variant part="Suit" name="Worn"/><Material part="Suit" section="trim" name="_scorch" value="1"/></DamageStage>
            </ItemVisual>
            <ItemVisual id="pauldron"><Part name="Plate" kind="Skinned" deforms="false" mesh="plate.mesh" socket="Shoulder"/></ItemVisual>
            <ItemVisual id="paper"><Part name="Cap" kind="Sprite" sprite="cap.sprite.xml" socket="Head" layer="3"/></ItemVisual>
        </ItemVisualCatalog>)";

        static constexpr const utf8* kSchemaXML = R"(<CustomizationSchemaCatalog><Schema id="Suit">
            <Choice name="Finish" default="Plain"><Option name="Plain"/><Option name="Gold" materialVariant="Gold"/></Choice>
        </Schema></CustomizationSchemaCatalog>)";

        static void load( AppearanceDatabase& database, const utf8* pXML ) { SW_REQUIRE_TRUE( database.loadSectionFromXMLText( pXML, "test" ) ); }

        static void loadAll( AppearanceDatabase& database )
        {
            load( database, kSlotsXML );
            load( database, kVisualsXML );
            load( database, kSchemaXML );
        }

        static AppearanceSlotRequest& equip( CharacterAppearanceSpec& inoutSpec, const utf8* pSlot, const utf8* pVisual )
        {
            AppearanceSlotRequest request;
            request._slot          = hashed_string( pSlot );
            request._itemID        = hashed_string( pVisual );
            request._visibleVisual = hashed_string( pVisual );
            inoutSpec._listSlot.push_back( request );
            return inoutSpec._listSlot.back();
        }

        static hashed_string findSectionMaterial( const ResolvedPart& part, const utf8* pSection )
        {
            for ( const AppearanceSectionDef& section : part._listSection )
            {
                if ( section._name == hashed_string( pSection ) )
                    return section._material;
            }
            return hashed_string{};
        }

        static bool loadCatalog( const utf8* pText, ItemVisualCatalog& outCatalog, AppearanceLoadReport& outReport )
        {
            XMLDocument document;
            if ( document.parse( pText ) == false )
                return false;
            return outCatalog.loadFromXML( document.getRoot( "ItemVisualCatalog" ), outReport, "test" );
        }
    };
} // namespace

/** @brief [AppearanceSectionsTest] 메시 변형(피해 단계)과 머티리얼 변형(꾸미기)이 이름이 같은 구간의 머티리얼만 바꾸고, 변형 · 피해 단계의 값은 구간을 단 채로 나온다 */
SW_TEST_CASE( AppearanceSectionsTest, VariantsChangeNamedSections )
{
    using Util = AppearanceSectionsTestUtil;
    AppearanceDatabase database;
    Util::loadAll( database );
    CharacterAppearanceSpec spec;
    spec._bodyVisual            = hashed_string( "body" );
    AppearanceSlotRequest& suit = Util::equip( spec, "Torso", "suit" );
    suit._damage                = 0.5f;
    ResolvedAppearance resolved;
    ResolvedAppearance::resolve( database, spec, resolved );

    const ResolvedPart* pSuit = resolved.findPart( hashed_string( "Torso" ), hashed_string( "Suit" ) );
    SW_REQUIRE_NOT_NULL( pSuit );
    SW_EXPECT_TRUE( Util::findSectionMaterial( *pSuit, "cloth" ) == hashed_string( "cloth.material" ) ); // 변형이 적지 않은 구간은 그대로다
    SW_EXPECT_TRUE( Util::findSectionMaterial( *pSuit, "trim" ) == hashed_string( "trim_worn.material" ) );
    SW_REQUIRE_EQUAL( resolved._listMaterialValue.size(), size_t{ 2 } );
    SW_EXPECT_TRUE( resolved._listMaterialValue[0]._name == hashed_string( "_dirt" ) ); // 변형의 값이 먼저, 피해 단계의 값이 뒤다
    SW_EXPECT_TRUE( resolved._listMaterialValue[0]._section == hashed_string( "cloth" ) );
    SW_EXPECT_TRUE( resolved._listMaterialValue[0]._part == hashed_string( "Suit" ) );
    SW_EXPECT_TRUE( resolved._listMaterialValue[0]._owner == hashed_string( "Torso" ) );
    SW_EXPECT_TRUE( resolved._listMaterialValue[1]._section == hashed_string( "trim" ) );
    const uint64 wornHash = resolved._meshHash;

    // 머티리얼 변형은 메시 변형 위에 걸린다: 부품 머티리얼이 모든 구간을 바꾸고, 그 위에 구간 하나를 덮는다.
    spec.findSlot( hashed_string( "Torso" ) )->_customization.setOption( hashed_string( "Finish" ), hashed_string( "Gold" ) );
    ResolvedAppearance::resolve( database, spec, resolved );
    pSuit = resolved.findPart( hashed_string( "Torso" ), hashed_string( "Suit" ) );
    SW_REQUIRE_NOT_NULL( pSuit );
    SW_EXPECT_TRUE( Util::findSectionMaterial( *pSuit, "cloth" ) == hashed_string( "gold.material" ) );
    SW_EXPECT_TRUE( Util::findSectionMaterial( *pSuit, "trim" ) == hashed_string( "trim_gold.material" ) );
    SW_EXPECT_TRUE( pSuit->_material.empty() );          // 구간이 있는 부품은 부품 머티리얼을 쓰지 않는다
    SW_EXPECT_NOT_EQUAL( resolved._meshHash, wornHash ); // 구간 머티리얼이 병합 캐시 키(메시 해시)에 든다
}

/** @brief [AppearanceSectionsTest] 규칙의 머티리얼 교체는 넓은 것(주인 전체)을 먼저, 구간 하나를 뒤에 덮고, 규칙의 머티리얼 값은 구간을 단다 */
SW_TEST_CASE( AppearanceSectionsTest, RulesSwapSectionsNarrowestLast )
{
    using Util = AppearanceSectionsTestUtil;
    AppearanceDatabase database;
    Util::loadAll( database );
    // 좁은 규칙을 먼저 적어도 넓은 규칙이 덮지 않는다. 대상이 달라 같은 우선순위끼리 충돌이 아니다.
    Util::load( database, R"(<AppearanceRuleCatalog>
        <Rule id="Trim" priority="1"><When occupied="Torso"/><SwapMaterial target="Torso" part="Suit" section="trim" material="trim_rule.material"/></Rule>
        <Rule id="All" priority="1"><When occupied="Torso"/><SwapMaterial target="Torso" material="all.material"/></Rule>
        <Rule id="Glow" priority="1"><When occupied="Torso"/><MaterialValue target="Torso" part="Suit" section="cloth" name="_glow" value="1 0.5 0 1"/></Rule>
    </AppearanceRuleCatalog>)" );
    CharacterAppearanceSpec spec;
    spec._bodyVisual = hashed_string( "body" );
    (void)Util::equip( spec, "Torso", "suit" );
    ResolvedAppearance resolved;
    ResolvedAppearance::resolve( database, spec, resolved );

    const ResolvedPart* pSuit = resolved.findPart( hashed_string( "Torso" ), hashed_string( "Suit" ) );
    SW_REQUIRE_NOT_NULL( pSuit );
    SW_EXPECT_TRUE( Util::findSectionMaterial( *pSuit, "cloth" ) == hashed_string( "all.material" ) );
    SW_EXPECT_TRUE( Util::findSectionMaterial( *pSuit, "trim" ) == hashed_string( "trim_rule.material" ) );
    SW_REQUIRE_EQUAL( resolved._listMaterialValue.size(), size_t{ 1 } );
    SW_EXPECT_TRUE( resolved._listMaterialValue[0]._section == hashed_string( "cloth" ) );
    SW_EXPECT_NEAR( resolved._listMaterialValue[0]._value._y, 0.5f, 1.0e-6f );
}

/** @brief [AppearanceSectionsTest] 휘지 않는 스킨드 조각과 스프라이트는 소켓에 붙는 부품이고 병합 메시 해시에 들지 않으며, 스프라이트는 그리기 순서를 가진다 */
SW_TEST_CASE( AppearanceSectionsTest, RigidAndSpritePartsAreSocketBound )
{
    using Util = AppearanceSectionsTestUtil;
    AppearanceDatabase database;
    Util::loadAll( database );
    CharacterAppearanceSpec spec;
    spec._bodyVisual = hashed_string( "body" );
    ResolvedAppearance bare;
    ResolvedAppearance::resolve( database, spec, bare );

    (void)Util::equip( spec, "Shoulder", "pauldron" );
    (void)Util::equip( spec, "Head", "paper" );
    ResolvedAppearance resolved;
    ResolvedAppearance::resolve( database, spec, resolved );
    const ResolvedPart* pPlate = resolved.findPart( hashed_string( "Shoulder" ), hashed_string( "Plate" ) );
    const ResolvedPart* pCap   = resolved.findPart( hashed_string( "Head" ), hashed_string( "Cap" ) );
    SW_REQUIRE_NOT_NULL( pPlate );
    SW_REQUIRE_NOT_NULL( pCap );
    SW_EXPECT_FALSE( pPlate->isSkinnedFollower() );
    SW_EXPECT_TRUE( pPlate->isSocketBound() );
    SW_EXPECT_TRUE( pCap->isSocketBound() );
    SW_EXPECT_EQUAL( pCap->_layer, 3 );
    SW_EXPECT_TRUE( resolved._listPart[0].isSkinnedFollower() ); // 몸
    SW_EXPECT_EQUAL( resolved._meshHash, bare._meshHash );
    SW_EXPECT_NOT_EQUAL( resolved._hash, bare._hash );
}

/** @brief [AppearanceSectionsTest] 읽지 못할 칸은 로드 오류다: 휘는 스프라이트, 소켓 없는 단단한 조각, 스프라이트가 아닌 부품의 layer, 부품에 없는 구간을 짚는 변형 */
SW_TEST_CASE( AppearanceSectionsTest, UnreadablePropertiesFailToLoad )
{
    using Util = AppearanceSectionsTestUtil;
    ItemVisualCatalog    catalog;
    AppearanceLoadReport report;
    SW_EXPECT_LOG( "Appearance.PartDeformsInvalid" );
    SW_EXPECT_LOG( "Appearance.PartSocketMissing" );
    SW_EXPECT_LOG( "Appearance.PartAttributeNotAllowed" );
    SW_EXPECT_LOG( "Appearance.SectionUnknown" );
    (void)Util::loadCatalog( R"(<ItemVisualCatalog>
        <ItemVisual id="a"><Part name="Cap" kind="Sprite" sprite="cap.sprite.xml" socket="Head" deforms="true"/></ItemVisual>
        <ItemVisual id="b"><Part name="Plate" kind="Skinned" mesh="plate.mesh" deforms="false"/></ItemVisual>
        <ItemVisual id="c"><Part name="Gun" kind="SocketPrefab" prefab="gun.prefab" socket="Grip" layer="1"/></ItemVisual>
        <ItemVisual id="d"><Part name="Suit" kind="Skinned" mesh="suit.mesh"><Section name="cloth" material="cloth.material"/><Variant name="V"><Section name="trim" material="trim.material"/></Variant></Part></ItemVisual>
    </ItemVisualCatalog>)",
                             catalog, report );
    SW_EXPECT_EQUAL( report.countWithID( "Appearance.PartDeformsInvalid" ), uint32{ 1 } );
    SW_EXPECT_EQUAL( report.countWithID( "Appearance.PartSocketMissing" ), uint32{ 1 } );
    SW_EXPECT_EQUAL( report.countWithID( "Appearance.PartAttributeNotAllowed" ), uint32{ 1 } );
    SW_EXPECT_EQUAL( report.countWithID( "Appearance.SectionUnknown" ), uint32{ 1 } );
}
