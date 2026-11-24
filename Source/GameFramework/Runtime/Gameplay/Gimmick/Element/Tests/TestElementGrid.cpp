// ElementGridTest — 규칙표 원소 격자: 표 읽기의 거절, 자극 규칙의 차례, 번짐(이웃 · 바람 · 옆바람 · 지연), 다 탐 · 이웃 바꾸기, 대전의 수명과 이어 퍼지기, 고정 스텝, 상태 왕복(nogpu).
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Element/ElementGrid.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Element/ElementRuleCatalog.h"

using namespace sw;

namespace
{
    /** @brief 작은 규칙표와 그 번호입니다. 풀은 두 스텝이면 다 타고, 불은 한 스텝 뒤 바람 쪽 · 옆바람 쪽으로 번진다. */
    struct ElementGridTestTable
    {
        static constexpr string_view kXML = R"(<ElementRules stepTime="0.25">
            <Flag id="Flammable"/><Flag id="Conductive"/>
            <Status id="Burning" kind="Age"/><Status id="Charged" kind="Countdown" steps="2"/>
            <Material id="Empty"/><Material id="Grass" flags="Flammable" burnSteps="2"/><Material id="Wood" flags="Flammable" burnSteps="9"/>
            <Material id="Metal" flags="Conductive"/><Material id="Ice"/><Material id="Water"/>
            <Stimulus id="Fire"><On material="Ice" setMaterial="Water" event="Melted"/><On flag="Flammable" without="Burning" addStatus="Burning" event="Ignited"/></Stimulus>
            <Stimulus id="Electric"><On flag="Conductive" floodStatus="Charged" through="Conductive" event="Electrified"/></Stimulus>
            <Step>
                <Expire status="Burning" stepsParam="burnSteps" setMaterial="Empty" event="BurnedOut"/>
                <Convert status="Burning" neighbor="Ice" setMaterial="Water" event="Melted"/>
                <Spread status="Burning" after="1" to="Flammable" pattern="Wind" crosswind="true" event="Ignited"/>
            </Step>
        </ElementRules>)";

        ElementRuleCatalog _table{};
        int32              _fire{ 0 };
        int32              _electric{ 0 };
        int32              _burning{ 0 };
        int32              _charged{ 0 };

        ElementGridTestTable()
        {
            SW_EXPECT_TRUE( _table.loadFromXMLText( kXML, "test" ) );
            _fire     = _table.findStimulus( "Fire" ).value_or( -1 );
            _electric = _table.findStimulus( "Electric" ).value_or( -1 );
            _burning  = _table.findStatus( "Burning" ).value_or( -1 );
            _charged  = _table.findStatus( "Charged" ).value_or( -1 );
        }

        int32 material( const utf8* pName ) const { return _table.findMaterial( hashed_string( pName ) ).value_or( -1 ); }

        /** @brief 모든 칸이 @p pName 재질인 격자를 세웁니다. */
        [[nodiscard]] bool makeGrid( int32 width, int32 height, const utf8* pName, ElementGrid& outGrid ) const
        {
            if ( outGrid.initialize( width, height, &_table ) == false )
                return false;
            for ( int32 y = 0; y < height; ++y )
            {
                for ( int32 x = 0; x < width; ++x )
                {
                    outGrid.setMaterial( int2{ x, y }, material( pName ) );
                }
            }
            return true;
        }
    };
} // namespace

/** @brief [ElementGridTest] 표가 없거나 재질이 없거나 크기가 1 보다 작으면 세우지 않고, 표에 없는 재질은 칸에 놓지 않는다 */
SW_TEST_CASE( ElementGridTest, RefusesBadSizeTableAndMaterial )
{
    const ElementGridTestTable rules;
    ElementGrid                grid;
    SW_EXPECT_LOG( "ElementGrid.ElementGridRefusedSizeX" );
    SW_EXPECT_FALSE( grid.initialize( 0, 3, &rules._table ) );
    SW_EXPECT_FALSE( grid.initialize( 3, 3, nullptr ) );
    const ElementRuleCatalog empty{};
    SW_EXPECT_FALSE( grid.initialize( 3, 3, &empty ) );
    SW_REQUIRE_TRUE( rules.makeGrid( 2, 1, "Grass", grid ) );
    SW_EXPECT_LOG( "ElementGrid.MaterialNotInTable" );
    grid.setMaterial( int2{ 0, 0 }, 6 ); // 재질은 여섯 개(0 ~ 5)
    grid.setMaterial( int2{ 0, 0 }, -1 );
    SW_EXPECT_EQUAL( rules.material( "Grass" ), grid.getMaterial( int2{ 0, 0 } ) );
    grid.setMaterial( int2{ 9, 9 }, rules.material( "Wood" ) ); // 격자 밖은 아무 일도 없다
    SW_EXPECT_EQUAL( 0, grid.getMaterial( int2{ 9, 9 } ) );
}

/** @brief [ElementGridTest] 선언하지 않은 이름 · 이어 갈 플래그 없는 번짐은 표 읽기가 거절한다 */
SW_TEST_CASE( ElementGridTest, CatalogRefusesUndeclaredNames )
{
    ElementRuleCatalog table;
    SW_EXPECT_LOG( "ElementRuleCatalog.NamesNothingDeclaredTable" );
    SW_EXPECT_FALSE( table.loadFromXMLText( R"(<ElementRules><Material id="A"/><Stimulus id="S"><On material="B" setMaterial="A"/></Stimulus></ElementRules>)", "test" ) );
    SW_EXPECT_LOG( "ElementRuleCatalog.FloodStatusNeedsThroughFlag" );
    SW_EXPECT_FALSE( table.loadFromXMLText( R"(<ElementRules><Status id="X"/><Material id="A"/><Stimulus id="S"><On floodStatus="X"/></Stimulus></ElementRules>)", "test" ) );
}

/** @brief [ElementGridTest] 자극은 위에서부터 처음 맞는 규칙 하나만 적용한다 — 얼음은 녹기만 하고, 타는 칸에는 다시 붙지 않는다 */
SW_TEST_CASE( ElementGridTest, StimulusAppliesTheFirstMatchingRule )
{
    const ElementGridTestTable rules;
    ElementGrid                grid;
    SW_REQUIRE_TRUE( rules.makeGrid( 2, 1, "Grass", grid ) );
    grid.setMaterial( int2{ 1, 0 }, rules.material( "Ice" ) );
    SW_EXPECT_EQUAL( 1, grid.applyStimulus( int2{ 1, 0 }, rules._fire ) );
    SW_EXPECT_EQUAL( rules.material( "Water" ), grid.getMaterial( int2{ 1, 0 } ) );
    SW_EXPECT_FALSE( grid.hasStatus( int2{ 1, 0 }, rules._burning ) );
    SW_EXPECT_EQUAL( 1, grid.applyStimulus( int2{ 0, 0 }, rules._fire ) );
    SW_EXPECT_EQUAL( 0, grid.applyStimulus( int2{ 0, 0 }, rules._fire ) );
    SW_EXPECT_EQUAL( 0, grid.applyStimulus( int2{ 5, 0 }, rules._fire ) );
    SW_EXPECT_EQUAL( 0, grid.applyStimulus( int2{ 0, 0 }, 99 ) );
    vector<ElementEvent> listEvent;
    grid.pollEvents( listEvent );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listEvent.size() );
    SW_EXPECT_TRUE( listEvent[0]._name == hashed_string( "Melted" ) && listEvent[0]._cell == int2( 1, 0 ) );
    SW_EXPECT_TRUE( listEvent[1]._name == hashed_string( "Ignited" ) && listEvent[1]._cell == int2( 0, 0 ) );
}

/** @brief [ElementGridTest] 바람이 없으면 네 이웃으로, 바람이 있으면 바람 쪽과 그 양옆으로만 번진다. 번진 칸은 그 스텝에 다시 번지지 않는다 */
SW_TEST_CASE( ElementGridTest, FireSpreadsByWind )
{
    const ElementGridTestTable rules;
    ElementGrid                calm;
    SW_REQUIRE_TRUE( rules.makeGrid( 5, 5, "Wood", calm ) );
    SW_REQUIRE_EQUAL( 1, calm.applyStimulus( int2{ 2, 2 }, rules._fire ) );
    calm.step();
    SW_EXPECT_EQUAL( 5, calm.countStatus( rules._burning ) );
    SW_EXPECT_TRUE( calm.hasStatus( int2{ 2, 1 }, rules._burning ) && calm.hasStatus( int2{ 1, 2 }, rules._burning ) );

    ElementGrid withWind;
    SW_REQUIRE_TRUE( rules.makeGrid( 5, 5, "Wood", withWind ) );
    withWind.setWind( int2{ 7, 0 } ); // 성분은 -1..1 로 자른다
    SW_EXPECT_EQUAL( 1, withWind.getWind()._x );
    SW_REQUIRE_EQUAL( 1, withWind.applyStimulus( int2{ 2, 2 }, rules._fire ) );
    withWind.step();
    SW_EXPECT_EQUAL( 4, withWind.countStatus( rules._burning ) );
    SW_EXPECT_TRUE( withWind.hasStatus( int2{ 3, 2 }, rules._burning ) && withWind.hasStatus( int2{ 2, 1 }, rules._burning ) && withWind.hasStatus( int2{ 2, 3 }, rules._burning ) );
    SW_EXPECT_FALSE( withWind.hasStatus( int2{ 1, 2 }, rules._burning ) );
}

/** @brief [ElementGridTest] 재질의 수치만큼 타면 맨땅이 되고, 타는 동안 이웃 얼음이 녹는다. 그 수치를 적지 않은 재질의 불은 끝나지 않는다 */
SW_TEST_CASE( ElementGridTest, FireBurnsOutAndMeltsIce )
{
    const ElementGridTestTable rules;
    ElementGrid                grid;
    SW_REQUIRE_TRUE( rules.makeGrid( 2, 1, "Grass", grid ) );
    grid.setMaterial( int2{ 1, 0 }, rules.material( "Ice" ) );
    SW_REQUIRE_EQUAL( 1, grid.applyStimulus( int2{ 0, 0 }, rules._fire ) );
    grid.step();
    SW_EXPECT_EQUAL( rules.material( "Water" ), grid.getMaterial( int2{ 1, 0 } ) );
    SW_EXPECT_EQUAL( 1, grid.getStatusValue( int2{ 0, 0 }, rules._burning ) );
    grid.step();
    SW_EXPECT_EQUAL( rules.material( "Empty" ), grid.getMaterial( int2{ 0, 0 } ) );
    SW_EXPECT_EQUAL( 0, grid.countStatus( rules._burning ) );
}

/** @brief [ElementGridTest] 전기는 이어진 도체 모두에 붙어 그 수를 돌려주고, 정한 스텝 뒤 사라진다 */
SW_TEST_CASE( ElementGridTest, ElectricityFloodsAndCountsDown )
{
    const ElementGridTestTable rules;
    ElementGrid                grid;
    SW_REQUIRE_TRUE( rules.makeGrid( 4, 1, "Metal", grid ) );
    grid.setMaterial( int2{ 2, 0 }, rules.material( "Wood" ) );
    SW_EXPECT_EQUAL( 2, grid.applyStimulus( int2{ 0, 0 }, rules._electric ) );
    SW_EXPECT_FALSE( grid.hasStatus( int2{ 3, 0 }, rules._charged ) );
    SW_EXPECT_EQUAL( 0, grid.applyStimulus( int2{ 2, 0 }, rules._electric ) );
    grid.step();
    SW_EXPECT_EQUAL( 2, grid.countStatus( rules._charged ) );
    grid.step();
    SW_EXPECT_EQUAL( 0, grid.countStatus( rules._charged ) );
}

/** @brief [ElementGridTest] 시간은 고정 스텝으로 흐르고 나머지를 잇는다. 해시는 칸 · 바람 · 스텝 수를 모두 본다 */
SW_TEST_CASE( ElementGridTest, FixedStepAndHash )
{
    const ElementGridTestTable rules;
    ElementGrid                grid;
    SW_REQUIRE_TRUE( rules.makeGrid( 3, 1, "Wood", grid ) );
    SW_EXPECT_EQUAL( 0, grid.update( 0.2f ) );
    const uint32 still = grid.computeStateHash();
    SW_EXPECT_EQUAL( 1, grid.update( 0.1f ) );
    SW_EXPECT_EQUAL( 1u, grid.getStepCount() );
    const uint32 stepped = grid.computeStateHash();
    SW_EXPECT_NOT_EQUAL( still, stepped );
    grid.setWind( int2{ 0, 1 } );
    SW_EXPECT_NOT_EQUAL( stepped, grid.computeStateHash() );
}

/** @brief [ElementGridTest] 쓰고 읽은 격자는 원본과 같이 구른다. 크기가 다르거나 잘린 바이트, 이 표에 없는 재질 번호는 거절하고 상태는 그대로다 */
SW_TEST_CASE( ElementGridTest, StateRoundTripAndRejects )
{
    const ElementGridTestTable rules;
    ElementGrid                original;
    SW_REQUIRE_TRUE( rules.makeGrid( 4, 2, "Grass", original ) );
    original.setWind( int2{ 1, 0 } );
    original.setMaterial( int2{ 3, 1 }, rules.material( "Water" ) );
    SW_REQUIRE_EQUAL( 1, original.applyStimulus( int2{ 0, 0 }, rules._fire ) );
    SW_EXPECT_EQUAL( 1, original.update( 0.3f ) );
    Archive written;
    original.writeState( written );

    ElementGrid restored;
    SW_REQUIRE_TRUE( rules.makeGrid( 4, 2, "Wood", restored ) );
    Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( original.computeStateHash(), restored.computeStateHash() );
    SW_EXPECT_EQUAL( original.update( 0.2f ), restored.update( 0.2f ) ); // 남은 시간도 이어진다
    SW_EXPECT_EQUAL( original.computeStateHash(), restored.computeStateHash() );

    ElementGrid other;
    ElementGrid narrow;
    SW_REQUIRE_TRUE( rules.makeGrid( 3, 2, "Wood", narrow ) );
    Archive forNarrow( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( narrow.readState( forNarrow ) );
    SW_REQUIRE_TRUE( rules.makeGrid( 4, 2, "Wood", other ) );
    const uint32 before = other.computeStateHash();
    Archive      truncated( written.getData(), written.getSize() - 1 );
    SW_EXPECT_FALSE( other.readState( truncated ) );
    SW_EXPECT_EQUAL( before, other.computeStateHash() );

    ElementRuleCatalog small;
    SW_REQUIRE_TRUE( small.loadFromXMLText( R"(<ElementRules><Material id="Empty"/></ElementRules>)", "small" ) );
    ElementGrid reduced;
    SW_REQUIRE_TRUE( reduced.initialize( 4, 2, &small ) );
    Archive forReduced( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( reduced.readState( forReduced ) ); // 풀 · 물 번호가 이 표에 없다
}
