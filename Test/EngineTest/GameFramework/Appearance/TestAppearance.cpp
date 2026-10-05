#include "pch.h"

#include "Engine/Resource/ResourceUtil.h"

#include "EngineTest/AppearanceTestFixture.h"

#include "GameFramework/Base/Appearance/AppearanceDatabase.h"
#include "GameFramework/Base/Appearance/AppearanceResolver.h"
#include "GameFramework/Base/Appearance/AppearanceXmlUtil.h"
#include "GameFramework/Base/Appearance/CharacterAppearanceState.h"
#include "GameFramework/Base/Inventory/Equipment.h"
#include "GameFramework/Base/Inventory/ItemCatalog.h"

#include "TestFramework/TestFramework.h"

// 캐릭터 외형 데이터와 해석 — 데이터 검사(모르는 이름 · 규칙 충돌 · 순환), 프리셋 상속 · 씨앗, 칸 점유, 세트 완성 표현, 규칙, 형상 변경, 꾸미기,
// 피해 단계 · 떨어져 나감, 외형 상태, 해시, 다시 해석하는 때, 2D 종이 인형.

using namespace sw;
using appearancetest::Fixture;

namespace
{
    struct AppearanceTestInternal
    {
        /** @brief 규칙 표 하나만 바꿔 읽고 오류 보고를 돌려줍니다. */
        static bool loadWithRules( const utf8* pRuleXml, Fixture& outFixture ) { return outFixture.load( pRuleXml ); }

        /** @brief 한 덩이를 더 읽고 검사한 결과입니다(모르는 이름 시험). */
        static bool loadWithExtra( const utf8* pExtraXml, Fixture& outFixture )
        {
            if ( outFixture.load() == false )
                return false;
            (void)outFixture._database.loadSectionFromXmlText( pExtraXml, "AppearanceTest.extra" );
            return outFixture._database.finishLoad( &outFixture._items );
        }

        static bool hasPlacementSocket( const ResolvedPart* pPart, const utf8* pSocket )
        {
            return pPart != nullptr && pPart->_placement._listSocket.empty() == false && pPart->_placement._listSocket.front() == hashed_string( pSocket );
        }

        static const ResolvedMaterialValue* findMaterial( const ResolvedAppearance& resolved, const utf8* pOwner, const utf8* pName )
        {
            for ( const ResolvedMaterialValue& value : resolved._listMaterialValue )
            {
                if ( value._owner == hashed_string( pOwner ) && value._name == hashed_string( pName ) )
                    return &value;
            }
            return nullptr;
        }
    };
} // namespace

/**
 * @brief [AppearanceTest] 사람이 고치는 데이터의 모르는 이름은 로드 오류다 — 속성 · 원소 · 칸 · 외형 · 세트 · 매개변수 · 항목 · 변형 · 부모
 */
SW_TEST_CASE( AppearanceTest, UnknownNamesAreLoadErrors )
{
    using Internal = AppearanceTestInternal;
    {
        Fixture fixture;
        SW_ASSERT_TRUE_MSG( fixture.load(), fixture._database.getReport().joined().c_str() );
    }
    struct Case
    {
        const utf8* _pXml;
        const utf8* _pExpected;
    };
    const Case arrCase[] = {
        {                                                                 R"(<ItemVisualCatalog><ItemVisual id="x" colour="red"><Part name="A" kind="Skinned" mesh="m"/></ItemVisual></ItemVisualCatalog>)","unknown attribute 'colour'"                                                                                                                                                                                                            },
        {                                                                      R"(<ItemVisualCatalog><ItemVisual id="x"><Part name="A" kind="Skinned" mesh="m"/><Partt/></ItemVisual></ItemVisualCatalog>)",     "unknown element <Partt>"},
        {                                                                                 R"(<ItemVisualCatalog><ItemVisual id="x"><Part name="A" kind="Mesh" mesh="m"/></ItemVisual></ItemVisualCatalog>)",         "unknown kind 'Mesh'"},
        {                                                      R"(<ItemVisualCatalog><ItemVisual id="x" occupancy="ThreeHanded"><Part name="A" kind="Skinned" mesh="m"/></ItemVisual></ItemVisualCatalog>)",           "unknown occupancy"},
        {                          R"(<ItemVisualCatalog><ItemVisual id="x"><Part name="A" kind="Skinned" mesh="m"/><State name="S"><Place part="B" socket="s"/></State></ItemVisual></ItemVisualCatalog>)",
         "unknown part 'B'"                                                                                                                                                                                                               },
        {                                                            R"(<ItemVisualCatalog><ItemVisual id="x"><Part name="A" kind="Skinned" mesh="m" breakStage="Gone"/></ItemVisual></ItemVisualCatalog>)", "unknown damage stage 'Gone'"},
        {R"(<ItemVisualCatalog><ItemVisual id="x"><Part name="A" kind="Skinned" mesh="m"/><DamageStage name="B" threshold="0.5"/><DamageStage name="C" threshold="0.4"/></ItemVisual></ItemVisualCatalog>)",
         "threshold in (previous, 1]"                                                                                                                                                                                                     },
        {                                                                                                                    R"(<SlotTable><Slot name="A"/><Occupancy id="O" slots="A,Nope"/></SlotTable>)",         "unknown slot 'Nope'"},
        {                                                                                               R"(<EquipSetCatalog><Set id="S"><Piece slot="Head" items="no_such_item"/></Set></EquipSetCatalog>)", "unknown item 'no_such_item'"},
        {                      R"(<CustomizationSchemaCatalog><Schema id="S"><Slider name="A"><Condition parameter="B" min="0" max="1"/></Slider><Slider name="B"/></Schema></CustomizationSchemaCatalog>)",
         "declared before it"                                                                                                                                                                                                             },
        {                                                    R"(<CustomizationSchemaCatalog><Schema id="S"><Choice name="A" default="Z"><Option name="X"/></Choice></Schema></CustomizationSchemaCatalog>)",   "is not one of its options"},
        {                                             R"(<CustomizationSchemaCatalog><Schema id="S"><Slider name="A"><Drive kind="DyeChannel" target="T"/></Slider></Schema></CustomizationSchemaCatalog>)",                "cannot drive"},
        {                                                                               R"(<AppearanceRuleTable><Rule id="R"><When target="Nope" tag="T"/><Hide region="X"/></Rule></AppearanceRuleTable>)",         "unknown slot 'Nope'"},
        {                                                                  R"(<AppearanceRuleTable><Rule id="R"><When tag="T"/><Variant target="Body" name="NoSuchVariant"/></Rule></AppearanceRuleTable>)",          "no visual declares"},
        {                                                                                        R"(<CharacterAppearanceCatalog><CharacterAppearance id="P" parent="Ghost"/></CharacterAppearanceCatalog>)",      "unknown parent 'Ghost'"},
        {                                    R"(<CharacterAppearanceCatalog><CharacterAppearance id="P" schema="Human"><Value name="Wings" value="1"/></CharacterAppearance></CharacterAppearanceCatalog>)",
         "unknown parameter 'Wings'"                                                                                                                                                                                                      },
        {                               R"(<CharacterAppearanceCatalog><CharacterAppearance id="P" schema="Human"><Value name="Hair" option="Mohawk"/></CharacterAppearance></CharacterAppearanceCatalog>)",
         "unknown option 'Mohawk'"                                                                                                                                                                                                        },
        {                                                 R"(<CharacterAppearanceCatalog><CharacterAppearance id="P"><Equip slot="Tail" item="sword"/></CharacterAppearance></CharacterAppearanceCatalog>)",         "unknown slot 'Tail'"},
        {                                                    R"(<CharacterAppearanceCatalog><CharacterAppearance id="A" parent="B"/><CharacterAppearance id="B" parent="A"/></CharacterAppearanceCatalog>)",               "forms a cycle"},
    };
    for ( const Case& testCase : arrCase )
    {
        Fixture fixture;
        SW_EXPECT_FALSE_MSG( Internal::loadWithExtra( testCase._pXml, fixture ), testCase._pXml );
        SW_EXPECT_TRUE_MSG( fixture._database.getReport().countContaining( testCase._pExpected ) >= 1,
                            ( string( testCase._pExpected ) + " missing in:" + fixture._database.getReport().joined() ).c_str() );
    }
}

/**
 * @brief [AppearanceTest] 장착 조건끼리의 순환(A 가 B 의 태그를, B 가 A 의 태그를 요구)과 모르는 세트는 로드 오류다
 */
SW_TEST_CASE( AppearanceTest, EquipConditionCycleIsALoadError )
{
    Fixture fixture;
    SW_ASSERT_TRUE( fixture.load() );
    ItemCatalog cyclic;
    SW_ASSERT_TRUE( cyclic.loadFromXmlText( R"(
<ItemCatalog>
  <Item id="a" slot="Ring" tags="Mark.A"><Requires equippedTag="Mark.B"/></Item>
  <Item id="b" slot="Belt" tags="Mark.B"><Requires equippedTag="Mark.A"/></Item>
  <Item id="c" slot="Back"><Requires set="NoSuchSet"/></Item>
</ItemCatalog>)",
                                            "AppearanceTest.cycle" ) );
    SW_EXPECT_FALSE( fixture._database.finishLoad( &cyclic ) );
    SW_EXPECT_TRUE_MSG( fixture._database.getReport().countContaining( "equip conditions form a cycle: a -> b -> a" ) == 1, fixture._database.getReport().joined().c_str() );
    SW_EXPECT_TRUE( fixture._database.getReport().countContaining( "unknown set 'NoSuchSet'" ) == 1 );

    // 한쪽만 요구하면 순환이 아니다.
    vector<hashed_string> listCycle;
    ItemCatalog           chain;
    SW_ASSERT_TRUE( chain.loadFromXmlText( R"(<ItemCatalog><Item id="a" tags="Mark.A"><Requires equippedTag="Mark.B"/></Item><Item id="b" tags="Mark.B"/></ItemCatalog>)", "chain" ) );
    SW_EXPECT_FALSE( EquipConditionUtil::findConditionCycle( chain, nullptr, listCycle ) );
}

/**
 * @brief [AppearanceTest] 같은 대상을 다르게 바꾸는 두 규칙이 같은 우선순위면 로드 오류 — 우선순위가 다르거나, 같은 값이거나, 글로 배타(not)면 괜찮다
 */
SW_TEST_CASE( AppearanceTest, EqualPriorityRuleConflictIsALoadError )
{
    using Internal = AppearanceTestInternal;
    {
        Fixture fixture;
        SW_EXPECT_FALSE( Internal::loadWithRules( R"(<AppearanceRuleTable>
  <Rule id="A" priority="3"><When tag="Hat"/><Variant target="Hair" name="UnderHat"/></Rule>
  <Rule id="B" priority="3"><When occupied="Back"/><Variant target="Hair" name="Lean"/></Rule>
</AppearanceRuleTable>)",
                                                  fixture ) );
        SW_EXPECT_TRUE_MSG( fixture._database.getReport().countContaining( "rules 'A' and 'B' both ChooseVariant 'Hair' differently at priority 3" ) == 1,
                            fixture._database.getReport().joined().c_str() );
    }
    const utf8* arrAllowed[] = {
        R"(<AppearanceRuleTable><Rule id="A" priority="3"><When tag="Hat"/><Variant target="Hair" name="UnderHat"/></Rule><Rule id="B" priority="4"><When occupied="Back"/><Variant target="Hair" name="Lean"/></Rule></AppearanceRuleTable>)",
        R"(<AppearanceRuleTable><Rule id="A" priority="3"><When tag="Hat"/><Variant target="Hair" name="UnderHat"/></Rule><Rule id="B" priority="3"><When occupied="Back"/><Variant target="Hair" name="UnderHat"/></Rule></AppearanceRuleTable>)",
        R"(<AppearanceRuleTable><Rule id="A" priority="3"><When tag="Hat"/><Variant target="Hair" name="UnderHat"/></Rule><Rule id="B" priority="3"><When tag="Hat" not="true"/><Variant target="Hair" name="Lean"/></Rule></AppearanceRuleTable>)",
        R"(<AppearanceRuleTable><Rule id="A" priority="3"><When tag="Hat"/><Hide target="Hair"/></Rule><Rule id="B" priority="3"><When occupied="Back"/><Hide target="Hair"/></Rule></AppearanceRuleTable>)",
    };
    for ( const utf8* pRuleXml : arrAllowed )
    {
        Fixture fixture;
        SW_EXPECT_TRUE_MSG( Internal::loadWithRules( pRuleXml, fixture ), ( string( pRuleXml ) + fixture._database.getReport().joined() ).c_str() );
    }
}

/**
 * @brief [AppearanceTest] 프리셋 상속(아래가 덮음 · 세트 → 칸 순서 · 칸 비우기)과 씨앗 뽑기(같은 씨앗 = 같은 결과, 씨앗마다 다름, 범위 안)
 */
SW_TEST_CASE( AppearanceTest, PresetInheritanceAndSeededCandidates )
{
    Fixture fixture;
    SW_ASSERT_TRUE_MSG( fixture.load(), fixture._database.getReport().joined().c_str() );

    CharacterAppearanceSpec knight;
    SW_ASSERT_TRUE( fixture.expand( hashed_string( "Knight" ), 0u, knight ) );
    SW_EXPECT_TRUE( knight._schema == hashed_string( "Human" ) );            // 부모에서
    SW_EXPECT_TRUE( knight._bodyVisual == hashed_string( "body_human" ) );   // 부모에서
    SW_EXPECT_TRUE( knight._tags.hasTag( TagID::request( "Race.Human" ) ) ); // 태그는 합쳐진다
    SW_EXPECT_TRUE( knight._tags.hasTag( TagID::request( "Class.Knight" ) ) );
    SW_EXPECT_NEAR_EQUAL( 1.2f, knight._customization.findValue( hashed_string( "Height" ) )->_number._x, 1.0e-5f );
    SW_EXPECT_TRUE( knight.findSlot( hashed_string( "Head" ) )->_itemId == hashed_string( "helm" ) ); // 세트
    SW_EXPECT_TRUE( knight.findSlot( hashed_string( "MainHand" ) )->_customization.findValue( hashed_string( "Gem" ) ) != nullptr );
    SW_EXPECT_EQUAL( knight._listSlot.size(), fixture._database.getSlotTable().getSlots().size() );

    CharacterAppearanceSpec squire;
    SW_ASSERT_TRUE( fixture.expand( hashed_string( "Squire" ), 0u, squire ) );
    SW_EXPECT_NEAR_EQUAL( 0.9f, squire._customization.findValue( hashed_string( "Height" ) )->_number._x, 1.0e-5f );
    SW_EXPECT_TRUE( squire.findSlot( hashed_string( "Head" ) )->_itemId.empty() );                     // item="" 은 비운다
    SW_EXPECT_TRUE( squire.findSlot( hashed_string( "Body" ) )->_itemId == hashed_string( "shirt" ) ); // 아래 층의 칸이 세트를 덮는다
    SW_EXPECT_TRUE( squire.findSlot( hashed_string( "Legs" ) )->_itemId == hashed_string( "greaves" ) );

    // 씨앗 — 같으면 같고, 여럿이면 후보가 고루 나온다.
    CharacterAppearanceSpec first;
    CharacterAppearanceSpec again;
    SW_ASSERT_TRUE( fixture.expand( hashed_string( "Villager" ), 1234u, first ) );
    SW_ASSERT_TRUE( fixture.expand( hashed_string( "Villager" ), 1234u, again ) );
    ResolvedAppearance firstResolved;
    ResolvedAppearance againResolved;
    AppearanceResolver::resolve( fixture._database, first, firstResolved );
    AppearanceResolver::resolve( fixture._database, again, againResolved );
    SW_EXPECT_EQUAL( firstResolved._hash, againResolved._hash );

    vector<hashed_string> listShape;
    vector<hashed_string> listBody;
    vector<float32>       listHeight;
    uint32                baldCount = 0;
    for ( uint32 seed = 0; seed < 64; ++seed )
    {
        CharacterAppearanceSpec villager;
        SW_ASSERT_TRUE( fixture.expand( hashed_string( "Villager" ), seed, villager ) );
        const float32 height = villager._customization.findValue( hashed_string( "Height" ) )->_number._x;
        SW_EXPECT_TRUE( 0.8f <= height && height <= 1.4f );
        if ( std::find( listHeight.begin(), listHeight.end(), height ) == listHeight.end() )
            listHeight.push_back( height );
        if ( AppearanceXmlUtil::containsName( listShape, villager._bodyShape ) == false )
            listShape.push_back( villager._bodyShape );
        const hashed_string body = villager.findSlot( hashed_string( "Body" ) )->_itemId;
        if ( AppearanceXmlUtil::containsName( listBody, body ) == false )
            listBody.push_back( body );
        if ( villager._customization.findValue( hashed_string( "Hair" ) )->_option == hashed_string( "Bald" ) )
            ++baldCount;
    }
    SW_EXPECT_EQUAL( size_t( 3 ), listShape.size() );
    SW_EXPECT_EQUAL( size_t( 2 ), listBody.size() );
    SW_EXPECT_TRUE( 0 < baldCount && baldCount < 64 );
    SW_EXPECT_TRUE( listHeight.size() > 32u ); // 범위는 씨앗마다 다른 값
}

/**
 * @brief [AppearanceTest] 칸 점유 — 로브는 하의를, 양손검은 보조 손을 가린다(설명이 남는다)
 */
SW_TEST_CASE( AppearanceTest, SlotOccupancyHidesDisplacedItems )
{
    Fixture fixture;
    SW_ASSERT_TRUE_MSG( fixture.load(), fixture._database.getReport().joined().c_str() );
    CharacterAppearanceSpec spec;
    SW_ASSERT_TRUE( fixture.expand( hashed_string( "Human" ), 0u, spec ) );
    Fixture::setSlot( spec, hashed_string( "Body" ), hashed_string( "robe" ) );
    Fixture::setSlot( spec, hashed_string( "Legs" ), hashed_string( "pants" ) );
    Fixture::setSlot( spec, hashed_string( "MainHand" ), hashed_string( "greatsword" ) );
    Fixture::setSlot( spec, hashed_string( "OffHand" ), hashed_string( "shield" ) );
    ResolvedAppearance resolved;
    AppearanceResolver::resolve( fixture._database, spec, resolved );
    SW_EXPECT_TRUE( resolved.hasOwner( hashed_string( "Body" ) ) );
    SW_EXPECT_FALSE( resolved.hasOwner( hashed_string( "Legs" ) ) );
    SW_EXPECT_TRUE( resolved.hasOwner( hashed_string( "MainHand" ) ) );
    SW_EXPECT_FALSE( resolved.hasOwner( hashed_string( "OffHand" ) ) );
    SW_EXPECT_TRUE( resolved.countTrace( hashed_string( "Body" ) ) >= 1 );

    // 셔츠는 아무것도 차지하지 않는다.
    Fixture::setSlot( spec, hashed_string( "Body" ), hashed_string( "shirt" ) );
    AppearanceResolver::resolve( fixture._database, spec, resolved );
    SW_EXPECT_TRUE( resolved.hasOwner( hashed_string( "Legs" ) ) );
}

/**
 * @brief [AppearanceTest] 세트를 다 입으면 완성 표현이 그 칸들을 대신한다 — 몸 종류 변형 조각으로도, 조각이 숨김 정책으로 꺼졌으면 아니다
 */
SW_TEST_CASE( AppearanceTest, CompleteSetSwapsToItsRepresentation )
{
    Fixture fixture;
    SW_ASSERT_TRUE_MSG( fixture.load(), fixture._database.getReport().joined().c_str() );
    CharacterAppearanceSpec spec;
    SW_ASSERT_TRUE( fixture.expand( hashed_string( "Knight" ), 0u, spec ) );
    ResolvedAppearance resolved;
    AppearanceResolver::resolve( fixture._database, spec, resolved );
    const ResolvedPart* pSuit = resolved.findPart( hashed_string( "Body" ), hashed_string( "Suit" ) );
    SW_ASSERT_NOT_NULL( pSuit );
    SW_EXPECT_TRUE( pSuit->_visualId == hashed_string( "knight_full" ) );
    SW_EXPECT_TRUE( resolved.findPart( hashed_string( "Body" ), hashed_string( "Plate" ) ) == nullptr );
    SW_EXPECT_FALSE( resolved.hasOwner( hashed_string( "Legs" ) ) );
    SW_EXPECT_TRUE( resolved.hasOwner( hashed_string( "Head" ) ) ); // 완성 표현 칸이 아니다
    SW_EXPECT_EQUAL( 1u, resolved.countTrace( hashed_string( "Knight" ) ) );

    // 여성 몸은 plate_f 가 조각이다 — 남성 기준 조각(plate)이면 미완성.
    spec._bodyType = hashed_string( "Female" );
    AppearanceResolver::resolve( fixture._database, spec, resolved );
    SW_EXPECT_TRUE( resolved.findPart( hashed_string( "Body" ), hashed_string( "Suit" ) ) == nullptr );
    Fixture::setSlot( spec, hashed_string( "Body" ), hashed_string( "plate_f" ) );
    AppearanceResolver::resolve( fixture._database, spec, resolved );
    SW_EXPECT_TRUE( resolved.findPart( hashed_string( "Body" ), hashed_string( "Suit" ) ) != nullptr );

    spec.findSlot( hashed_string( "Legs" ) )->_bSuppressed = SW_TRUE;
    AppearanceResolver::resolve( fixture._database, spec, resolved );
    SW_EXPECT_TRUE( resolved.findPart( hashed_string( "Body" ), hashed_string( "Suit" ) ) == nullptr );
    SW_EXPECT_FALSE( resolved.hasOwner( hashed_string( "Legs" ) ) ); // 숨김 정책으로 꺼진 장비는 안 보인다
}

/**
 * @brief [AppearanceTest] 규칙 — 숨기기 · 변형(우선순위가 이기고 진 쪽도 설명에) · 메시 · 머티리얼 바꾸기(배타 짝) · 모프 · 소켓 덮어쓰기 · 몸 수정 부품
 */
SW_TEST_CASE( AppearanceTest, RulesHideChooseSwapMorphAndOverride )
{
    Fixture fixture;
    SW_ASSERT_TRUE_MSG( fixture.load(), fixture._database.getReport().joined().c_str() );
    CharacterAppearanceSpec spec;
    SW_ASSERT_TRUE( fixture.expand( hashed_string( "Human" ), 0u, spec ) );
    spec._customization.setOption( hashed_string( "Beard" ), hashed_string( "Full" ) );
    Fixture::setSlot( spec, hashed_string( "Body" ), hashed_string( "plate" ) );
    Fixture::setSlot( spec, hashed_string( "Back" ), hashed_string( "cape_hidden" ) );
    ResolvedAppearance resolved;
    AppearanceResolver::resolve( fixture._database, spec, resolved );
    SW_EXPECT_TRUE( resolved.hasOwner( hashed_string( "Hair" ) ) );
    SW_EXPECT_TRUE( resolved.hasOwner( hashed_string( "Beard" ) ) );
    SW_EXPECT_TRUE( resolved.findPart( hashed_string( "Body" ), hashed_string( "Plate" ) )->_variant == hashed_string( "CapeHole" ) );
    SW_EXPECT_TRUE( resolved.findPart( hashed_string( "Body" ), hashed_string( "Plate" ) )->_asset == hashed_string( "m/plate_capehole.mesh" ) );
    SW_ASSERT_EQUAL( size_t( 1 ), resolved._listSocketOverride.size() );
    SW_EXPECT_TRUE( resolved._listSocketOverride[0]._name == hashed_string( "Belt.Hook" ) );

    // 드워프면 짧은 소매(우선순위 2)가 망토 구멍(1)을 이긴다 — 진 규칙도 설명에 남는다.
    spec._tags.addTag( TagID::request( "Race.Dwarf" ) );
    AppearanceResolver::resolve( fixture._database, spec, resolved );
    SW_EXPECT_TRUE( resolved.findPart( hashed_string( "Body" ), hashed_string( "Plate" ) )->_variant == hashed_string( "Short" ) );
    SW_EXPECT_TRUE( resolved.countTrace( hashed_string( "CapeCutsPlate" ) ) >= 1 );

    // 투구 — 머리 · 수염을 숨기고(10), 몸 수정이 모프 · 영역 숨김을 걸고, 메시를 바꾼다.
    Fixture::setSlot( spec, hashed_string( "Head" ), hashed_string( "helm" ) );
    AppearanceResolver::resolve( fixture._database, spec, resolved );
    SW_EXPECT_FALSE( resolved.hasOwner( hashed_string( "Hair" ) ) );
    SW_EXPECT_FALSE( resolved.hasOwner( hashed_string( "Beard" ) ) );
    SW_EXPECT_TRUE( resolved.isRegionHidden( hashed_string( "Scalp" ) ) );
    SW_ASSERT_NOT_NULL( resolved.findMorph( hashed_string{}, hashed_string( "HeadSquash" ) ) );
    SW_EXPECT_TRUE( resolved.findPart( hashed_string( "Head" ), hashed_string( "Shell" ) )->_asset == hashed_string( "m/helm_skull.mesh" ) );
    SW_EXPECT_TRUE( resolved.findPart( hashed_string( "Head" ), hashed_string( "Shell" ) )->_bDeforms == SW_FALSE );

    // 체형 · 몸 종류 조건, 배타 짝(로브면 다른 머티리얼).
    spec._bodyShape = hashed_string( "Heavy" );
    spec._bodyType  = hashed_string( "Female" );
    AppearanceResolver::resolve( fixture._database, spec, resolved );
    SW_ASSERT_NOT_NULL( resolved.findMorph( hashed_string( "Body" ), hashed_string( "Loosen" ) ) );
    SW_EXPECT_TRUE( resolved.findPart( hashed_string( "Body" ), hashed_string( "Plate" ) )->_material == hashed_string( "mat/fem.material" ) );
    Fixture::setSlot( spec, hashed_string( "Body" ), hashed_string( "robe" ) );
    AppearanceResolver::resolve( fixture._database, spec, resolved );
    SW_EXPECT_TRUE( resolved.findPart( hashed_string( "Body" ), hashed_string( "Robe" ) )->_material == hashed_string( "mat/fem_robe.material" ) );
}

/**
 * @brief [AppearanceTest] 규칙은 요청된 목록만 보고 판정한다 — 앞 규칙이 망토를 숨겨도 "등 칸 점유" 규칙은 그대로 맞는다(규칙끼리 사슬이 없다)
 */
SW_TEST_CASE( AppearanceTest, RulesSeeTheRequestedListOnly )
{
    Fixture fixture;
    SW_ASSERT_TRUE_MSG( fixture.load(), fixture._database.getReport().joined().c_str() );
    CharacterAppearanceSpec spec;
    SW_ASSERT_TRUE( fixture.expand( hashed_string( "Human" ), 0u, spec ) );
    Fixture::setSlot( spec, hashed_string( "Head" ), hashed_string( "helm" ) );
    Fixture::setSlot( spec, hashed_string( "Body" ), hashed_string( "plate" ) );
    Fixture::setSlot( spec, hashed_string( "Back" ), hashed_string( "cape_hidden" ) );
    ResolvedAppearance resolved;
    AppearanceResolver::resolve( fixture._database, spec, resolved );
    SW_EXPECT_FALSE( resolved.hasOwner( hashed_string( "Back" ) ) ); // HelmHidesCape(표의 첫 규칙)
    SW_EXPECT_TRUE( resolved.findPart( hashed_string( "Body" ), hashed_string( "Plate" ) )->_variant == hashed_string( "CapeHole" ) );
    SW_EXPECT_EQUAL( size_t( 1 ), resolved._listSocketOverride.size() );
}

/**
 * @brief [AppearanceTest] 형상 변경 — 칸의 보이는 외형만 바뀌고, 규칙은 보이는 외형의 태그를 본다(투구를 모자로 보이면 머리가 숨지 않고 눌린다)
 */
SW_TEST_CASE( AppearanceTest, TransmogShowsTheOverrideAndItsTags )
{
    Fixture fixture;
    SW_ASSERT_TRUE_MSG( fixture.load(), fixture._database.getReport().joined().c_str() );
    CharacterAppearanceSpec spec;
    SW_ASSERT_TRUE( fixture.expand( hashed_string( "Human" ), 0u, spec ) );
    Fixture::setSlot( spec, hashed_string( "Head" ), hashed_string( "helm" ) );
    spec.findSlot( hashed_string( "Head" ) )->_visibleVisual = hashed_string( "cap" );
    ResolvedAppearance resolved;
    AppearanceResolver::resolve( fixture._database, spec, resolved );
    const ResolvedPart* pCap = resolved.findPart( hashed_string( "Head" ), hashed_string( "Cap" ) );
    SW_ASSERT_NOT_NULL( pCap );
    SW_EXPECT_TRUE( pCap->_itemId == hashed_string( "helm" ) ); // 아이템은 그대로
    SW_EXPECT_TRUE( resolved.findPart( hashed_string( "Head" ), hashed_string( "Shell" ) ) == nullptr );
    SW_EXPECT_TRUE( resolved.findPart( hashed_string( "Hair" ), hashed_string( "Hair" ) )->_variant == hashed_string( "UnderHat" ) );
    SW_EXPECT_FALSE( resolved.isRegionHidden( hashed_string( "Scalp" ) ) );
}

/**
 * @brief [AppearanceTest] 꾸미기 — 범위로 자르고 격자로 맞추며, 조건이 꺼진 매개변수는 쓰이지 않고, 대칭 묶음은 함께 바뀌고, 구동 범위를 옮긴다
 */
SW_TEST_CASE( AppearanceTest, CustomizationRangesConditionsAndSymmetry )
{
    Fixture fixture;
    SW_ASSERT_TRUE_MSG( fixture.load(), fixture._database.getReport().joined().c_str() );
    const CustomizationSchemaDef* pHuman = fixture._database.getSchemas().findSchema( hashed_string( "Human" ) );
    SW_ASSERT_NOT_NULL( pHuman );

    CustomizationValueSet given;
    given.setNumber( hashed_string( "Height" ), 5.0f ); // 범위 [0, 2] 밖
    given.setNumber( hashed_string( "Fat" ), 0.5f );
    given.setOption( hashed_string( "Hair" ), hashed_string( "Mohawk" ) ); // 없는 항목
    given.setNumber( hashed_string( "Tail" ), 1.0f );                      // 없는 매개변수
    CustomizationValueSet normalized;
    vector<hashed_string> listDropped;
    CustomizationUtil::normalize( *pHuman, given, normalized, &listDropped );
    SW_EXPECT_EQUAL( pHuman->_listParameter.size(), normalized.getCount() );
    SW_EXPECT_NEAR_EQUAL( 2.0f, normalized.findValue( hashed_string( "Height" ) )->_number._x, 1.0e-6f );
    SW_EXPECT_TRUE( normalized.findValue( hashed_string( "Hair" ) )->_option == hashed_string( "Long" ) );
    SW_EXPECT_EQUAL( size_t( 2 ), listDropped.size() );
    // 격자 — 정규화를 두 번 해도 같다(전송 정밀도로 저장).
    CustomizationValueSet twice;
    CustomizationUtil::normalize( *pHuman, normalized, twice, nullptr );
    SW_EXPECT_TRUE( twice.isEquivalent( normalized ) );

    // 조건 — 수염이 없으면 수염 길이는 꺼진다.
    const CustomizationParamDef* pBeardLength = pHuman->findParameter( hashed_string( "BeardLength" ) );
    SW_EXPECT_FALSE( CustomizationUtil::isActive( *pBeardLength, *pHuman, normalized ) );
    // 사슬 — 수염 길이(기본 0.5)가 범위 안이어도, 수염 길이가 꺼져 있으면 땋기도 꺼진다.
    const CustomizationParamDef* pBraid = pHuman->findParameter( hashed_string( "BeardBraid" ) );
    SW_EXPECT_FALSE( CustomizationUtil::isActive( *pBraid, *pHuman, normalized ) );
    // 슬라이더 범위 조건 — 근육은 Fat 0..0.5 에서만.
    const CustomizationParamDef* pMuscle = pHuman->findParameter( hashed_string( "Muscle" ) );
    CustomizationValueSet        lean    = normalized;
    lean.setNumber( hashed_string( "Fat" ), 0.25f );
    SW_EXPECT_TRUE( CustomizationUtil::isActive( *pMuscle, *pHuman, lean ) );
    lean.setNumber( hashed_string( "Fat" ), 0.9f );
    SW_EXPECT_FALSE( CustomizationUtil::isActive( *pMuscle, *pHuman, lean ) );
    CustomizationValueSet bearded = normalized;
    bearded.setOption( hashed_string( "Beard" ), hashed_string( "Full" ) );
    SW_EXPECT_TRUE( CustomizationUtil::isActive( *pBeardLength, *pHuman, bearded ) );
    SW_EXPECT_TRUE( CustomizationUtil::isActive( *pBraid, *pHuman, bearded ) );

    // 대칭 — 왼쪽 귀를 바꾸면 오른쪽도.
    CustomizationValue ear;
    ear._parameter = hashed_string( "Ear.L" );
    ear._number    = float4( 0.75f, 0.0f, 0.0f, 0.0f );
    SW_EXPECT_TRUE( CustomizationUtil::applyValue( *pHuman, ear, true, bearded ) );
    SW_EXPECT_NEAR_EQUAL( 0.75f, bearded.findValue( hashed_string( "Ear.R" ) )->_number._x, 1.0e-6f );
    ear._number = float4( 0.25f, 0.0f, 0.0f, 0.0f );
    SW_EXPECT_TRUE( CustomizationUtil::applyValue( *pHuman, ear, false, bearded ) );
    SW_EXPECT_NEAR_EQUAL( 0.75f, bearded.findValue( hashed_string( "Ear.R" ) )->_number._x, 1.0e-6f );

    // 해석 — 구동 범위(Fat 0..1 → 0..0.5), 꺼진 매개변수의 모프 없음, 색 → 염색 채널, 몸 변형 고르기, 몸 소켓 부착.
    CharacterAppearanceSpec spec;
    SW_ASSERT_TRUE( fixture.expand( hashed_string( "Human" ), 0u, spec ) );
    spec._customization.setNumber( hashed_string( "Fat" ), 1.0f );
    spec._customization.setOption( hashed_string( "Physique" ), hashed_string( "Lean" ) );
    spec._customization.setOption( hashed_string( "Earring" ), hashed_string( "Hoop" ) );
    ResolvedAppearance resolved;
    AppearanceResolver::resolve( fixture._database, spec, resolved );
    SW_EXPECT_NEAR_EQUAL( 0.5f, resolved.findMorph( hashed_string{}, hashed_string( "Fat" ) )->_weight, 1.0e-5f );
    SW_EXPECT_TRUE( resolved.findMorph( hashed_string{}, hashed_string( "BeardLength" ) ) == nullptr );
    SW_EXPECT_TRUE( resolved.countTrace( hashed_string( "BeardLength" ) ) == 1 );
    const ResolvedMaterialValue* pDye = AppearanceTestInternal::findMaterial( resolved, "", "Hair" );
    SW_ASSERT_NOT_NULL( pDye );
    SW_EXPECT_TRUE( pDye->_target == ResolvedMaterialTarget::DyeChannel && pDye->_channel == 2 );
    SW_EXPECT_TRUE( resolved.findPart( hashed_string{}, hashed_string( "Body" ) )->_asset == hashed_string( "m/body_lean.mesh" ) );
    SW_ASSERT_EQUAL( size_t( 1 ), resolved._listAttachment.size() );
    SW_EXPECT_TRUE( resolved._listAttachment[0]._placement._listSocket[0] == hashed_string( "ear.l" ) ); // 몸 소켓은 앞머리가 없다
}

/**
 * @brief [AppearanceTest] 아이템 인스턴스 꾸미기 — 부품 소켓 부착물은 칸 이름이 앞에 붙고(`MainHand.GemSlot`), 머티리얼 변형 · 메시 변형을 고른다
 */
SW_TEST_CASE( AppearanceTest, ItemCustomizationAttachesAndChoosesVariants )
{
    Fixture fixture;
    SW_ASSERT_TRUE_MSG( fixture.load(), fixture._database.getReport().joined().c_str() );
    CharacterAppearanceSpec spec;
    SW_ASSERT_TRUE( fixture.expand( hashed_string( "Knight" ), 0u, spec ) );
    AppearanceSlotRequest* pMainHand = spec.findSlot( hashed_string( "MainHand" ) );
    pMainHand->_customization.setOption( hashed_string( "Finish" ), hashed_string( "Gold" ) );
    ResolvedAppearance resolved;
    AppearanceResolver::resolve( fixture._database, spec, resolved );
    SW_ASSERT_EQUAL( size_t( 1 ), resolved._listAttachment.size() );
    const ResolvedAttachment& gem = resolved._listAttachment[0];
    SW_EXPECT_TRUE( gem._placement._listSocket[0] == hashed_string( "MainHand.GemSlot" ) );
    SW_EXPECT_TRUE( gem._asset == hashed_string( "p/ruby.prefab.xml" ) );
    SW_EXPECT_NEAR_EQUAL( 0.1f, gem._placement._offset._y, 1.0e-6f );
    const ResolvedPart* pBlade = resolved.findPart( hashed_string( "MainHand" ), hashed_string( "Blade" ) );
    SW_ASSERT_NOT_NULL( pBlade );
    SW_EXPECT_TRUE( pBlade->_material == hashed_string( "mat/gold.material" ) && pBlade->_materialVariant == hashed_string( "Gold" ) );
    bool bSwordSockets = false;
    for ( const ResolvedSocketSource& source : resolved._listSocketSource )
    {
        bSwordSockets = bSwordSockets || ( source._owner == hashed_string( "MainHand" ) && source._socketSet == hashed_string( "s/sword.sockets.xml" ) );
    }
    SW_EXPECT_TRUE( bSwordSockets );

    pMainHand->_customization.setOption( hashed_string( "Finish" ), hashed_string( "Chipped" ) );
    AppearanceResolver::resolve( fixture._database, spec, resolved );
    SW_EXPECT_TRUE( resolved.findPart( hashed_string( "MainHand" ), hashed_string( "Blade" ) )->_asset == hashed_string( "p/sword_chipped.prefab.xml" ) );
}

/**
 * @brief [AppearanceTest] 피해 단계 — 문턱 아래는 멀쩡, 문턱부터 그 단계의 변형 · 머티리얼 값, 내구도가 닳은 만큼도 피해다, 단계에 걸린 부품은 떨어진다
 */
SW_TEST_CASE( AppearanceTest, DamageStagesFollowDataThresholds )
{
    using Internal = AppearanceTestInternal;
    Fixture fixture;
    SW_ASSERT_TRUE_MSG( fixture.load(), fixture._database.getReport().joined().c_str() );
    CharacterAppearanceSpec spec;
    SW_ASSERT_TRUE( fixture.expand( hashed_string( "Knight" ), 0u, spec ) );
    AppearanceSlotRequest* pMainHand = spec.findSlot( hashed_string( "MainHand" ) );
    ResolvedAppearance     resolved;

    pMainHand->_damage = 0.29f;
    AppearanceResolver::resolve( fixture._database, spec, resolved );
    SW_EXPECT_TRUE( resolved.findPart( hashed_string( "MainHand" ), hashed_string( "Blade" ) )->_damageStage.empty() );
    SW_EXPECT_TRUE( Internal::findMaterial( resolved, "MainHand", "Rust" ) == nullptr );

    // 문턱과 같은 피해는 그 단계다(문턱 "이상").
    const ItemVisualDef* pSwordVisual = fixture._database.getVisuals().findVisual( hashed_string( "sword" ) );
    SW_EXPECT_EQUAL( -1, pSwordVisual->computeDamageStageIndex( 0.29f ) );
    SW_EXPECT_EQUAL( 0, pSwordVisual->computeDamageStageIndex( 0.3f ) );
    SW_EXPECT_EQUAL( 1, pSwordVisual->computeDamageStageIndex( 0.8f ) );
    pMainHand->_damage = 0.3f;
    AppearanceResolver::resolve( fixture._database, spec, resolved );
    const ResolvedPart* pBlade = resolved.findPart( hashed_string( "MainHand" ), hashed_string( "Blade" ) );
    SW_EXPECT_TRUE( pBlade->_damageStage == hashed_string( "Worn" ) && pBlade->_variant == hashed_string( "Notched" ) );
    SW_ASSERT_NOT_NULL( Internal::findMaterial( resolved, "MainHand", "Rust" ) );
    SW_EXPECT_NEAR_EQUAL( 0.4f, Internal::findMaterial( resolved, "MainHand", "Rust" )->_value._x, 1.0e-6f );
    SW_EXPECT_TRUE( resolved.findPart( hashed_string( "MainHand" ), hashed_string( "Pommel" ) ) != nullptr );
    SW_EXPECT_TRUE( resolved._listDetachedPart.empty() );

    // 내구도 15/100 → 피해 0.85 → 부서짐: 녹 1, 손잡이 머리가 단계로 떨어진다.
    Equipment equipment;
    fixture.makeEquipment( equipment );
    ItemStack sword;
    sword._itemId     = hashed_string( "sword" );
    sword._count      = 1;
    sword._durability = 15.0f;
    vector<ItemStack> listRemoved;
    SW_ASSERT_TRUE( equipment.equip( hashed_string( "MainHand" ), sword, listRemoved ) == EquipResult::Ok );
    AppearanceInputUtil::applyEquipment( equipment, spec );
    SW_EXPECT_NEAR_EQUAL( AppearanceResolver::snapDamage( 0.85f ), spec.findSlot( hashed_string( "MainHand" ) )->_damage, 1.0e-6f );
    AppearanceResolver::resolve( fixture._database, spec, resolved );
    SW_ASSERT_NOT_NULL( Internal::findMaterial( resolved, "MainHand", "Rust" ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, Internal::findMaterial( resolved, "MainHand", "Rust" )->_value._x, 1.0e-6f );
    SW_EXPECT_TRUE( resolved.findPart( hashed_string( "MainHand" ), hashed_string( "Pommel" ) ) == nullptr );
    SW_ASSERT_EQUAL( size_t( 1 ), resolved._listDetachedPart.size() );
    SW_EXPECT_TRUE( resolved._listDetachedPart[0]._partName == hashed_string( "Pommel" ) && resolved._listDetachedPart[0]._bFromDamageStage == SW_TRUE );
    SW_EXPECT_NEAR_EQUAL( 1.0f, resolved._listDetachedPart[0]._impulse._x, 1.0e-6f );
}

/**
 * @brief [AppearanceTest] 맞아서 떨어져 나간 부품은 떨어지는 그 해석에서 한 번 이벤트(충격 힌트)를 낸다 — 떨어질 수 없는 부품은 무시하고 설명에 남긴다
 */
SW_TEST_CASE( AppearanceTest, BreakOffEmitsOneDetachEvent )
{
    Fixture fixture;
    SW_ASSERT_TRUE_MSG( fixture.load(), fixture._database.getReport().joined().c_str() );
    CharacterAppearanceSpec spec;
    SW_ASSERT_TRUE( fixture.expand( hashed_string( "Knight" ), 0u, spec ) );
    Equipment equipment;
    fixture.makeEquipment( equipment );
    ItemStack sword;
    sword._itemId     = hashed_string( "sword" );
    sword._count      = 1;
    sword._durability = 100.0f;
    vector<ItemStack> listRemoved;
    SW_ASSERT_TRUE( equipment.equip( hashed_string( "MainHand" ), sword, listRemoved ) == EquipResult::Ok );

    CharacterAppearanceState state;
    state.initialize( &fixture._database, spec );
    SW_EXPECT_TRUE( state.update( &equipment ) );
    vector<AppearanceDetachEvent> listEvent;
    state.takeDetachEvents( listEvent );
    SW_EXPECT_TRUE( listEvent.empty() );
    SW_EXPECT_TRUE( state.getResolved().findPart( hashed_string( "MainHand" ), hashed_string( "Tassel" ) ) != nullptr );

    // 술이 맞아 떨어진다 + 칼날(떨어질 수 없음)도 요청에 섞는다.
    sword._listDetachedPart = { hashed_string( "Tassel" ), hashed_string( "Blade" ) };
    SW_ASSERT_TRUE( equipment.setEquippedInstance( hashed_string( "MainHand" ), sword ) );
    SW_EXPECT_TRUE( state.update( &equipment ) );
    state.takeDetachEvents( listEvent );
    SW_ASSERT_EQUAL( size_t( 1 ), listEvent.size() );
    SW_EXPECT_TRUE( listEvent[0]._part._partName == hashed_string( "Tassel" ) );
    SW_EXPECT_TRUE( listEvent[0]._part._itemId == hashed_string( "sword" ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, listEvent[0]._part._impulse._y, 1.0e-6f );
    SW_EXPECT_TRUE( listEvent[0]._part._placement._listSocket[0] == hashed_string( "MainHand.Pommel" ) );
    SW_EXPECT_TRUE( state.getResolved().findPart( hashed_string( "MainHand" ), hashed_string( "Blade" ) ) != nullptr );
    SW_EXPECT_TRUE( state.getResolved().countTrace( hashed_string( "MainHand" ) ) >= 2 );

    // 다시 해석해도 같은 부품의 이벤트는 다시 나지 않는다(술은 계속 떨어진 채다).
    CustomizationValueSet values = spec._customization;
    values.setNumber( hashed_string( "Fat" ), 0.7f );
    state.setCustomization( values );
    SW_EXPECT_TRUE( state.update( &equipment ) );
    state.takeDetachEvents( listEvent );
    SW_EXPECT_TRUE( listEvent.empty() );
    SW_EXPECT_EQUAL( size_t( 1 ), state.getResolved()._listDetachedPart.size() );
}

/**
 * @brief [AppearanceTest] 외형 상태 — 뽑음 ↔ 꽂음은 같은 부품을 다른 소켓(후보 목록)으로 옮기고 상태별로 부품을 숨긴다. 병합 메시 해시는 그대로다
 */
SW_TEST_CASE( AppearanceTest, VisualStateMovesPartsWithoutChangingTheMeshHash )
{
    using Internal = AppearanceTestInternal;
    Fixture fixture;
    SW_ASSERT_TRUE_MSG( fixture.load(), fixture._database.getReport().joined().c_str() );
    CharacterAppearanceSpec spec;
    SW_ASSERT_TRUE( fixture.expand( hashed_string( "Knight" ), 0u, spec ) );
    ResolvedAppearance drawn;
    AppearanceResolver::resolve( fixture._database, spec, drawn );
    SW_EXPECT_TRUE( Internal::hasPlacementSocket( drawn.findPart( hashed_string( "MainHand" ), hashed_string( "Blade" ) ), "hand.r" ) );
    SW_EXPECT_TRUE( drawn.findPart( hashed_string( "MainHand" ), hashed_string( "Tassel" ) ) != nullptr );

    spec.findSlot( hashed_string( "MainHand" ) )->_state = hashed_string( "Sheathed" );
    ResolvedAppearance sheathed;
    AppearanceResolver::resolve( fixture._database, spec, sheathed );
    const ResolvedPart* pBlade = sheathed.findPart( hashed_string( "MainHand" ), hashed_string( "Blade" ) );
    SW_ASSERT_NOT_NULL( pBlade );
    SW_ASSERT_EQUAL( size_t( 2 ), pBlade->_placement._listSocket.size() );
    SW_EXPECT_TRUE( pBlade->_placement._listSocket[0] == hashed_string( "Belt.Hook" ) && pBlade->_placement._listSocket[1] == hashed_string( "hip.l" ) );
    SW_EXPECT_TRUE( sheathed.findPart( hashed_string( "MainHand" ), hashed_string( "Tassel" ) ) == nullptr );
    SW_EXPECT_NOT_EQUAL( drawn._hash, sheathed._hash );
    SW_EXPECT_EQUAL( drawn._meshHash, sheathed._meshHash );

    // 배치(오프셋)만 달라도 전체 해시는 다르고 병합 메시 해시는 같다.
    ResolvedAppearance moved = drawn;
    for ( ResolvedPart& part : moved._listPart )
    {
        if ( part._partName == hashed_string( "Blade" ) )
            part._placement._offset._x += 0.05f;
    }
    SW_EXPECT_NOT_EQUAL( drawn._hash, AppearanceResolver::computeHash( moved ) );
    SW_EXPECT_EQUAL( drawn._meshHash, AppearanceResolver::computeMeshHash( moved ) );
}

/**
 * @brief [AppearanceTest] 해시 — 같은 입력이면 같고(값 순서와 상관없이), 한 칸 · 한 격자만 달라도 다르다
 */
SW_TEST_CASE( AppearanceTest, ResolveHashIsStable )
{
    Fixture fixture;
    SW_ASSERT_TRUE_MSG( fixture.load(), fixture._database.getReport().joined().c_str() );
    CharacterAppearanceSpec spec;
    SW_ASSERT_TRUE( fixture.expand( hashed_string( "Villager" ), 7u, spec ) );
    ResolvedAppearance first;
    AppearanceResolver::resolve( fixture._database, spec, first );
    SW_EXPECT_NOT_EQUAL( uint64( 0 ), first._hash );
    SW_EXPECT_EQUAL( first._hash, AppearanceResolver::computeHash( first ) );

    // 같은 값을 거꾸로 넣어도 같다.
    CharacterAppearanceSpec reordered = spec;
    reordered._customization.clear();
    const vector<CustomizationValue>& listValue = spec._customization.getValues();
    for ( size_t index = listValue.size(); index > 0; --index )
    {
        reordered._customization.setValue( listValue[index - 1] );
    }
    ResolvedAppearance second;
    AppearanceResolver::resolve( fixture._database, reordered, second );
    SW_EXPECT_EQUAL( first._hash, second._hash );

    // 슬라이더 한 격자.
    CharacterAppearanceSpec      nudged  = spec;
    const CustomizationParamDef* pHeight = fixture._database.getSchemas().findSchema( hashed_string( "Human" ) )->findParameter( hashed_string( "Height" ) );
    const uint32                 step    = CustomizationUtil::quantizeSlider( *pHeight, spec._customization.findValue( hashed_string( "Height" ) )->_number._x );
    nudged._customization.setNumber( hashed_string( "Height" ), CustomizationUtil::dequantizeSlider( *pHeight, step + 1 ) );
    ResolvedAppearance third;
    AppearanceResolver::resolve( fixture._database, nudged, third );
    SW_EXPECT_NOT_EQUAL( first._hash, third._hash );

    // 칸 하나.
    CharacterAppearanceSpec changed = spec;
    Fixture::setSlot( changed, hashed_string( "Ring" ), hashed_string( "sigil" ) );
    Fixture::setSlot( changed, hashed_string( "Head" ), hashed_string( "cap" ) );
    ResolvedAppearance fourth;
    AppearanceResolver::resolve( fixture._database, changed, fourth );
    SW_EXPECT_NOT_EQUAL( first._hash, fourth._hash );
}

/**
 * @brief [AppearanceTest] 외형 상태는 장비 판 · 데이터 판 · 입력이 바뀔 때만 다시 해석한다(Equipment::getRevision)
 */
SW_TEST_CASE( AppearanceTest, StateReresolvesOnlyWhenARevisionChanges )
{
    Fixture fixture;
    SW_ASSERT_TRUE_MSG( fixture.load(), fixture._database.getReport().joined().c_str() );
    CharacterAppearanceSpec spec;
    SW_ASSERT_TRUE( fixture.expand( hashed_string( "Human" ), 0u, spec ) );
    Equipment equipment;
    fixture.makeEquipment( equipment );

    CharacterAppearanceState state;
    state.initialize( &fixture._database, spec );
    SW_EXPECT_TRUE( state.update( &equipment ) );
    SW_EXPECT_FALSE( state.update( &equipment ) );
    SW_EXPECT_EQUAL( 1u, state.getResolveCount() );

    ItemStack cap;
    cap._itemId = hashed_string( "cap" );
    cap._count  = 1;
    vector<ItemStack> listRemoved;
    SW_ASSERT_TRUE( equipment.equip( hashed_string( "Head" ), cap, listRemoved ) == EquipResult::Ok );
    SW_EXPECT_TRUE( state.update( &equipment ) );
    SW_EXPECT_TRUE( state.getResolved().hasOwner( hashed_string( "Head" ) ) );
    SW_EXPECT_FALSE( state.update( &equipment ) );

    state.setVisibleVisual( hashed_string( "Head" ), hashed_string( "helm_full" ) );
    SW_EXPECT_TRUE( state.update( &equipment ) );
    SW_EXPECT_FALSE( state.getResolved().hasOwner( hashed_string( "Hair" ) ) );

    // 데이터를 다시 읽으면(핫 리로드) 다시 해석한다.
    SW_ASSERT_TRUE( fixture._database.loadSectionFromXmlText( appearancetest::kRuleXml, "AppearanceTest.reload" ) );
    SW_ASSERT_TRUE( fixture._database.finishLoad( &fixture._items ) );
    SW_EXPECT_TRUE( state.update( &equipment ) );
    SW_EXPECT_EQUAL( 4u, state.getResolveCount() );
}

/**
 * @brief [AppearanceTest] 실제 슈터 데이터(KayKit) — 읽히고, 플레이어는 기사 몸 · 기사 세트 완성 망토 · 투구 · 블래스터(Gun 소켓 · 소켓 에셋) · 푸른 염색,
 *        해골 전사는 고른 두건을 투구가 감춘다(HelmetHidesHood), 무작위 해골은 씨앗마다 두건이 다르다
 */
SW_TEST_CASE( AppearanceTest, ShooterSampleDataResolves )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    ItemCatalog items;
    SW_ASSERT_TRUE( items.loadFromResource( "game/shooter3d/data/items.xml" ) );
    AppearanceDatabase database;
    SW_ASSERT_TRUE_MSG( database.loadFromFolder( "game/shooter3d/data/appearance", &items ), database.getReport().joined().c_str() );

    CharacterAppearanceSpec player;
    SW_ASSERT_TRUE( database.getPresets().expand( hashed_string( "ShooterPlayer" ), 0u, database.getSlotTable(), database.getSets(), database.getSchemas(), player ) );
    ResolvedAppearance resolved;
    AppearanceResolver::resolve( database, player, resolved );
    const ResolvedPart* pBody = resolved.findPart( hashed_string{}, hashed_string( "Body" ) );
    SW_ASSERT_NOT_NULL( pBody );
    SW_EXPECT_TRUE( pBody->_asset == hashed_string( "game/shooter3d/models/kaykit/knight.mesh" ) );
    SW_EXPECT_TRUE( pBody->_socketSet == hashed_string( "game/shooter3d/data/sockets/kaykit_humanoid.sockets.xml" ) );
    const ResolvedPart* pCape = resolved.findPart( hashed_string( "Back" ), hashed_string( "Cape" ) );
    SW_ASSERT_NOT_NULL( pCape );
    SW_EXPECT_TRUE( pCape->_asset == hashed_string( "game/shooter3d/prefabs/kaykit/knight_cape.prefab.xml" ) ); // 기사 세트 완성
    SW_EXPECT_TRUE( resolved.findPart( hashed_string( "Head" ), hashed_string( "Shell" ) ) != nullptr );
    const ResolvedPart* pGun = resolved.findPart( hashed_string( "MainHand" ), hashed_string( "Body" ) );
    SW_ASSERT_NOT_NULL( pGun );
    SW_EXPECT_TRUE( pGun->_placement._listSocket[0] == hashed_string( "Gun" ) );
    SW_EXPECT_TRUE( pGun->_socketSet == hashed_string( "game/shooter3d/data/sockets/blaster_d.sockets.xml" ) );
    bool bDyed = false;
    for ( const ResolvedMaterialValue& value : resolved._listMaterialValue )
    {
        bDyed = bDyed || ( value._owner.empty() && value._name == hashed_string( "color" ) && value._value._z > value._value._x );
    }
    SW_EXPECT_TRUE( bDyed );

    // 해골 전사 — 두건을 골랐지만 투구가 덮는다(HelmetHidesHood). 무작위 해골은 씨앗마다 두건이 보이기도 안 보이기도 한다.
    CharacterAppearanceSpec warrior;
    SW_ASSERT_TRUE( database.getPresets().expand( hashed_string( "SkeletonWarrior" ), 0u, database.getSlotTable(), database.getSets(), database.getSchemas(), warrior ) );
    AppearanceResolver::resolve( database, warrior, resolved );
    SW_EXPECT_TRUE( resolved.hasOwner( hashed_string( "Head" ) ) );
    SW_EXPECT_FALSE( resolved.hasOwner( hashed_string( "Hood" ) ) );
    SW_EXPECT_TRUE( resolved.countTrace( hashed_string( "HelmetHidesHood" ) ) > 0u );
    uint32 hoodedCount = 0;
    uint32 bareCount   = 0;
    for ( uint32 seed = 0; seed < 16; ++seed )
    {
        CharacterAppearanceSpec raider;
        SW_ASSERT_TRUE( database.getPresets().expand( hashed_string( "SkeletonRaider" ), seed, database.getSlotTable(), database.getSets(), database.getSchemas(), raider ) );
        AppearanceResolver::resolve( database, raider, resolved );
        if ( resolved.hasOwner( hashed_string( "Hood" ) ) )
            ++hoodedCount;
        else
            ++bareCount;
    }
    SW_EXPECT_TRUE( hoodedCount > 0u );
    SW_EXPECT_TRUE( bareCount > 0u );

    // 게임플레이 칸 문자열도 같은 표에서 나온다.
    Equipment equipment;
    equipment.initialize( &items, database.getSlotTable().makeEquipmentLayout() );
    equipment.setSetLookup( &database.getSets() );
    SW_EXPECT_TRUE( equipment.canEquip( hashed_string( "MainHand" ), hashed_string( "blaster_rifle" ) ) );
    SW_EXPECT_FALSE( equipment.canEquip( hashed_string( "MainHand" ), hashed_string( "shield_round" ) ) );
}

/**
 * @brief [AppearanceTest] 2D 종이 인형 — 같은 해석이 스프라이트 부품(그리기 순서 · 기준 소켓)과 팔레트 바꾸기를 낸다
 */
SW_TEST_CASE( AppearanceTest, PaperDollFixtureResolvesSpritesAndPalettes )
{
    ItemCatalog items;
    SW_ASSERT_TRUE( items.loadFromXmlText( R"(<ItemCatalog><Item id="tunic" slot="Torso" visual="tunic"/><Item id="hood" slot="Head" visual="hood"/>
      <Item id="spear" slot="Hand" visual="spear"/></ItemCatalog>)",
                                           "PaperDoll.items" ) );
    AppearanceDatabase database;
    SW_ASSERT_TRUE( database.loadSectionFromXmlText( R"(<SlotTable><Slot name="Head"/><Slot name="Torso"/><Slot name="Hand"/></SlotTable>)", "PaperDoll.slots" ) );
    SW_ASSERT_TRUE( database.loadSectionFromXmlText( R"(<CustomizationSchemaCatalog><Schema id="Pixel">
        <Color name="Skin" default="1 0.8 0.6 1"><Drive kind="PaletteSwap" target="Skin"/></Color>
        <Choice name="HairStyle" default="Spiky"><Option name="Spiky" visual="hair_spiky"/><Option name="Shaved"/></Choice></Schema>
      <Schema id="Cloth"><Color name="Dye" default="0.5 0.5 0.5 1"><Drive kind="PaletteSwap" target="Cloth"/></Color></Schema></CustomizationSchemaCatalog>)",
                                                     "PaperDoll.schemas" ) );
    SW_ASSERT_TRUE( database.loadSectionFromXmlText( R"(<ItemVisualCatalog>
        <ItemVisual id="doll"><Part name="Base" kind="Sprite" sprite="t/doll.sprite.json" socket="root" layer="0"/></ItemVisual>
        <ItemVisual id="hair_spiky"><Part name="Hair" kind="Sprite" sprite="t/hair_spiky.sprite.json" socket="head" layer="3"/></ItemVisual>
        <ItemVisual id="tunic" customization="Cloth"><Part name="Tunic" kind="Sprite" sprite="t/tunic.sprite.json" socket="root" layer="1"/></ItemVisual>
        <ItemVisual id="hood" tags="Hat.Hood"><Part name="Hood" kind="Sprite" sprite="t/hood.sprite.json" socket="head" layer="4"/></ItemVisual>
        <ItemVisual id="spear" defaultState="Held"><Part name="Spear" kind="Sprite" sprite="t/spear.sprite.json" socket="hand" layer="5"/>
          <State name="Held"/><State name="Stowed"><Place part="Spear" socket="back" rotation="0 0 90"/></State></ItemVisual></ItemVisualCatalog>)",
                                                     "PaperDoll.visuals" ) );
    SW_ASSERT_TRUE( database.loadSectionFromXmlText( R"(<AppearanceRuleTable><Rule id="HoodHidesHair"><When target="Head" tag="Hat"/><Hide target="HairStyle"/></Rule></AppearanceRuleTable>)",
                                                     "PaperDoll.rules" ) );
    SW_ASSERT_TRUE( database.loadSectionFromXmlText( R"(<CharacterAppearanceCatalog><CharacterAppearance id="Peasant" schema="Pixel" body="doll">
        <Value name="Skin" color="0.4 0.3 0.2 1"/><Equip slot="Torso" item="tunic"><Value name="Dye" color="0.8 0.1 0.1 1"/></Equip><Equip slot="Hand" item="spear"/>
      </CharacterAppearance></CharacterAppearanceCatalog>)",
                                                     "PaperDoll.presets" ) );
    SW_ASSERT_TRUE_MSG( database.finishLoad( &items ), database.getReport().joined().c_str() );

    CharacterAppearanceSpec spec;
    SW_ASSERT_TRUE( database.getPresets().expand( hashed_string( "Peasant" ), 0u, database.getSlotTable(), database.getSets(), database.getSchemas(), spec ) );
    ResolvedAppearance resolved;
    AppearanceResolver::resolve( database, spec, resolved );
    SW_EXPECT_EQUAL( size_t( 4 ), resolved._listPart.size() ); // 몸 · 머리카락 · 웃옷 · 창
    for ( const ResolvedPart& part : resolved._listPart )
    {
        SW_EXPECT_TRUE( part._kind == AppearancePartKind::Sprite );
    }
    SW_EXPECT_EQUAL( 5, resolved.findPart( hashed_string( "Hand" ), hashed_string( "Spear" ) )->_layer );
    uint32 paletteCount = 0;
    for ( const ResolvedMaterialValue& value : resolved._listMaterialValue )
    {
        if ( value._target != ResolvedMaterialTarget::PaletteSwap )
            continue;
        ++paletteCount;
        if ( value._owner == hashed_string( "Torso" ) )
            SW_EXPECT_NEAR_EQUAL( CustomizationUtil::dequantizeColorChannel( CustomizationUtil::quantizeColorChannel( 0.8f ) ), value._value._x, 1.0e-6f );
    }
    SW_EXPECT_EQUAL( 2u, paletteCount );

    Fixture::setSlot( spec, hashed_string( "Head" ), hashed_string( "hood" ) );
    spec.findSlot( hashed_string( "Hand" ) )->_state = hashed_string( "Stowed" );
    AppearanceResolver::resolve( database, spec, resolved );
    SW_EXPECT_FALSE( resolved.hasOwner( hashed_string( "HairStyle" ) ) );
    SW_EXPECT_TRUE( resolved.findPart( hashed_string( "Hand" ), hashed_string( "Spear" ) )->_placement._listSocket[0] == hashed_string( "back" ) );
    SW_EXPECT_EQUAL( resolved._meshHash, AppearanceResolver::computeMeshHash( resolved ) );
}
