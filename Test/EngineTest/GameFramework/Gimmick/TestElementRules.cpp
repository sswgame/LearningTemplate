// 원소 규칙표 — 데이터로 적은 불 번짐 · 물로 끄기 · 바람, 액션 어드벤처 키트의 격자가 같은 규칙표 위에서 같은 결과를 낸다, 모르는 이름은 읽기 오류.
#include "pch.h"

#include "Engine/Resource/ResourceUtil.h"

#include "GameFramework/Base/Gameplay/Gimmick/ElementGrid.h"
#include "GameFramework/Base/Gameplay/Gimmick/ElementRuleTable.h"
#include "GameFramework/Kits/Genre/Action/ActionAdventure/Rule/AdventureElementGrid.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    struct ElementRuleTestInternal
    {
        static constexpr const utf8* kDefaultTablePath = "common/data/elements/default.elements.xml";

        [[nodiscard]] static bool loadDefaultTable( ElementRuleTable& outTable ) { return ResourceUtil::initialize() && outTable.loadFromResource( kDefaultTablePath ); }

        /** @brief 풀 들판에 나무 · 얼음 · 물을 섞은 같은 판을 두 격자에 깝니다(재질 이름 = 키트 열거 이름). */
        static void fillField( ElementGrid& grid, const ElementRuleTable& table )
        {
            grid.initialize( 8, 3, &table );
            for ( int32 y = 0; y < 3; ++y )
            {
                for ( int32 x = 0; x < 8; ++x )
                {
                    grid.setMaterial( int2{ x, y }, table.findMaterial( "Grass" ) );
                }
            }
            grid.setMaterial( int2{ 3, 2 }, table.findMaterial( "Wood" ) );
            grid.setMaterial( int2{ 5, 0 }, table.findMaterial( "Ice" ) );
            grid.setMaterial( int2{ 6, 1 }, table.findMaterial( "Water" ) );
        }

        static void fillField( AdventureElementGrid& grid )
        {
            grid.initialize( 8, 3, AdventureElementSettings{} );
            for ( int32 y = 0; y < 3; ++y )
            {
                for ( int32 x = 0; x < 8; ++x )
                {
                    grid.setMaterial( int2{ x, y }, AdventureMaterial::Grass );
                }
            }
            grid.setMaterial( int2{ 3, 2 }, AdventureMaterial::Wood );
            grid.setMaterial( int2{ 5, 0 }, AdventureMaterial::Ice );
            grid.setMaterial( int2{ 6, 1 }, AdventureMaterial::Water );
        }
    };
} // namespace

/**
 * @brief [ElementRuleTest] 데이터 표 — 불은 이웃 풀로 한 걸음에 한 칸씩 번지고(N 걸음 뒤 N 칸째), 바람이 불면 바람 쪽으로만, 물을 대면 꺼진다
 */
SW_TEST_CASE( ElementRuleTest, FireSpreadsToAdjacentGrassAfterSteps )
{
    ElementRuleTable table;
    SW_ASSERT_TRUE( ElementRuleTestInternal::loadDefaultTable( table ) );
    const int32 grass   = table.findMaterial( "Grass" );
    const int32 burning = table.findStatus( "Burning" );
    const int32 fire    = table.findStimulus( "Fire" );
    const int32 water   = table.findStimulus( "Water" );
    SW_ASSERT_TRUE( grass >= 0 && burning >= 0 && fire >= 0 && water >= 0 );

    ElementGrid row;
    row.initialize( 6, 1, &table );
    for ( int32 x = 0; x < 6; ++x )
    {
        row.setMaterial( int2{ x, 0 }, grass );
    }
    SW_EXPECT_EQUAL( 1, row.applyStimulus( int2{ 0, 0 }, fire ) );
    for ( int32 stepIndex = 1; stepIndex <= 3; ++stepIndex )
    {
        row.step();
        SW_EXPECT_TRUE( row.hasStatus( int2{ stepIndex, 0 }, burning ) );      // N 걸음 뒤 N 칸째가 붙었다
        SW_EXPECT_FALSE( row.hasStatus( int2{ stepIndex + 1, 0 }, burning ) ); // 그 다음 칸은 아직
    }
    row.step(); // 풀은 4 걸음 타고 맨땅
    SW_EXPECT_EQUAL( table.findMaterial( "Empty" ), row.getMaterial( int2{ 0, 0 } ) );

    // 물은 불을 끈다 — 끈 칸은 풀로 남는다.
    SW_EXPECT_EQUAL( 1, row.applyStimulus( int2{ 4, 0 }, water ) );
    SW_EXPECT_FALSE( row.hasStatus( int2{ 4, 0 }, burning ) );
    SW_EXPECT_EQUAL( grass, row.getMaterial( int2{ 4, 0 } ) );
    SW_EXPECT_EQUAL( 0, row.applyStimulus( int2{ 5, 0 }, water ) ); // 타지 않는 칸에는 아무 일 없음

    // 바람이 불을 민다 — 바람 쪽 이웃만 붙고 거슬러서는 붙지 않는다.
    ElementGrid windy;
    windy.initialize( 5, 1, &table );
    for ( int32 x = 0; x < 5; ++x )
    {
        windy.setMaterial( int2{ x, 0 }, grass );
    }
    windy.setWind( int2{ -1, 0 } );
    (void)windy.applyStimulus( int2{ 2, 0 }, fire ); // 시험 준비 — 결과는 아래 단언이 번진 칸으로 확인한다
    windy.step();
    SW_EXPECT_TRUE( windy.hasStatus( int2{ 1, 0 }, burning ) );
    SW_EXPECT_FALSE( windy.hasStatus( int2{ 3, 0 }, burning ) );
}

/**
 * @brief [ElementRuleTest] 키트 격자(코드로 지은 표) · 데이터 표가 같은 판 · 같은 조작에서 걸음마다 같은 재질 · 상태 · 알림을 낸다
 * @details 액션 어드벤처 키트는 공용 격자 위로 옮겼다 — 기본 설정으로 지은 표가 `default.elements.xml` 과 같은 규칙인지 본다.
 */
SW_TEST_CASE( ElementRuleTest, AdventureGridMatchesDefaultTable )
{
    ElementRuleTable table;
    SW_ASSERT_TRUE( ElementRuleTestInternal::loadDefaultTable( table ) );
    ElementGrid          shared;
    AdventureElementGrid kit;
    ElementRuleTestInternal::fillField( shared, table );
    ElementRuleTestInternal::fillField( kit );
    shared.setWind( int2{ 1, 0 } );
    kit.setWind( int2{ 1, 0 } );
    SW_EXPECT_TRUE( shared.applyStimulus( int2{ 0, 1 }, table.findStimulus( "Fire" ) ) > 0 );
    SW_EXPECT_TRUE( kit.applyFire( int2{ 0, 1 } ) );
    SW_EXPECT_EQUAL( 1, shared.applyStimulus( int2{ 6, 1 }, table.findStimulus( "Electric" ) ) ); // 이웃이 모두 풀이라 물 한 칸만
    (void)kit.applyElectric( int2{ 6, 1 } );                                                      // 공용 격자 쪽에서 칸 수를 단언했다 — 여기는 같은 입력을 넣기만 한다

    vector<ElementEvent>          listSharedEvent;
    vector<AdventureElementEvent> listKitEvent;
    for ( int32 stepIndex = 0; stepIndex < 16; ++stepIndex )
    {
        shared.step();
        kit.step();
        for ( int32 y = 0; y < 3; ++y )
        {
            for ( int32 x = 0; x < 8; ++x )
            {
                const int2 cell{ x, y };
                SW_EXPECT_EQUAL( static_cast<int32>( kit.getMaterial( cell ) ), shared.getMaterial( cell ) );
                SW_EXPECT_EQUAL( kit.isBurning( cell ), shared.hasStatus( cell, table.findStatus( "Burning" ) ) );
                SW_EXPECT_EQUAL( kit.isCharged( cell ), shared.hasStatus( cell, table.findStatus( "Charged" ) ) );
            }
        }
    }
    shared.drainEvents( listSharedEvent );
    kit.drainEvents( listKitEvent );
    SW_ASSERT_EQUAL( listSharedEvent.size(), listKitEvent.size() );
    SW_EXPECT_TRUE( listSharedEvent.size() > 10 );
    SW_EXPECT_EQUAL( shared.computeStateHash(), kit.computeStateHash() );
}

/**
 * @brief [ElementRuleTest] 표 안에서 선언하지 않은 깃발 · 재질 · 상태 이름, 모르는 규칙 · 모양 · 원소는 읽기 오류다
 */
SW_TEST_CASE( ElementRuleTest, UnknownNamesAreLoadErrors )
{
    ElementRuleTable table;
    {
        SW_TEST_DEFENSIVE_SCOPE( "element table with undeclared names" );
        SW_EXPECT_FALSE( table.loadFromXMLText( R"(<ElementRules><Flag id="Flammable"/><Material id="Grass" flags="Flamable"/></ElementRules>)", "flag.elements.xml" ) );
        SW_EXPECT_FALSE( table.loadFromXMLText( R"(<ElementRules><Material id="Grass"/><Stimulus id="Fire"><On material="Gras" event="X"/></Stimulus></ElementRules>)",
                                                "material.elements.xml" ) );
        SW_EXPECT_FALSE( table.loadFromXMLText( R"(<ElementRules><Material id="Grass"/><Step><Spread status="Burning"/></Step></ElementRules>)", "status.elements.xml" ) );
        SW_EXPECT_FALSE( table.loadFromXMLText( R"(<ElementRules><Status id="Burning" kind="Age"/><Step><Explode status="Burning"/></Step></ElementRules>)", "rule.elements.xml" ) );
        SW_EXPECT_FALSE( table.loadFromXMLText( R"(<ElementRules><Status id="Burning" kind="Age"/><Step><Spread status="Burning" pattern="Spiral"/></Step></ElementRules>)",
                                                "pattern.elements.xml" ) );
        SW_EXPECT_FALSE( table.loadFromXMLText( R"(<ElementRules><Weather id="Rain"/></ElementRules>)", "element.elements.xml" ) );
    }
    SW_EXPECT_TRUE( table.loadFromXMLText( R"(<ElementRules><Flag id="Flammable"/><Status id="Burning" kind="Age"/><Material id="Grass" flags="Flammable" burnSteps="2"/></ElementRules>)",
                                           "ok.elements.xml" ) );
    SW_EXPECT_EQUAL( 2, table.getMaterials()[0].getParam( "burnSteps", 0 ) );
}
