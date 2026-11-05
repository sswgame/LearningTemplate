#include "pch.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Kits/Genre/Action/ActionAdventure/Rule/AdventureElementGrid.h"

// AdventureElementGridTest — 화학 엔진: 불이 번지는 방향(바람 · 옆바람 · 지연), 타는 시간과 기류, 얼음 녹기 · 얼리기 · 끄기, 전기 흐름과 얼음 막기, 복사가 규칙 테이블을 다시 가리키는지, 고정 스텝, 상태 왕복.
// 결함은 찾지 못했다. 키트 GF_ActionAdventure(nogpu).

namespace
{
    struct AdventureElementGridTestUtil
    {
        static void fill( sw::AdventureElementGrid& grid, sw::AdventureMaterial material )
        {
            for ( int32 y = 0; y < grid.getHeight(); ++y )
            {
                for ( int32 x = 0; x < grid.getWidth(); ++x )
                {
                    grid.setMaterial( sw::int2{ x, y }, material );
                }
            }
        }
    };
} // namespace

/**
 * @brief [AdventureElementGridTest] 크기가 1 보다 작거나 설정이 범위 밖이면 거절하고 앞의 격자를 그대로 둔다. 초기화 전의 호출은 아무 일도 하지 않는다
 */
SW_TEST_CASE( AdventureElementGridTest, InitializeRefusesBadInput )
{
    SW_EXPECT_LOG( "AdventureElementGrid.AdventureElementGridRefusedSizeX" );
    SW_EXPECT_LOG( "AdventureElementGrid.AdventureElementGridRefusedSettings" );

    sw::AdventureElementGrid empty;
    SW_EXPECT_FALSE( empty.applyFire( sw::int2{ 0, 0 } ) );
    SW_EXPECT_EQUAL( 0, empty.applyElectric( sw::int2{ 0, 0 } ) );
    SW_EXPECT_FALSE( empty.isBurning( sw::int2{ 0, 0 } ) );
    empty.step();
    SW_EXPECT_EQUAL( 0u, empty.getStepCount() );

    const sw::AdventureElementSettings settings{};
    sw::AdventureElementGrid           grid;
    SW_REQUIRE_TRUE( grid.initialize( 3, 2, settings ) );
    SW_EXPECT_FALSE( grid.initialize( 0, 2, settings ) );
    SW_EXPECT_FALSE( grid.initialize( 2, -1, settings ) );
    sw::AdventureElementSettings noBurn;
    noBurn._grassBurnSteps = 0;
    SW_EXPECT_FALSE( grid.initialize( 2, 2, noBurn ) );
    SW_EXPECT_EQUAL( 3, grid.getWidth() );
    SW_EXPECT_EQUAL( 2, grid.getHeight() );
}

/**
 * @brief [AdventureElementGridTest] 바람이 없으면 불은 네 이웃에만 번지고 대각선에는 붙지 않는다. 이미 타는 칸에는 다시 붙지 않는다
 */
SW_TEST_CASE( AdventureElementGridTest, FireSpreadsToFourNeighborsWithoutWind )
{
    sw::AdventureElementGrid grid;
    SW_REQUIRE_TRUE( grid.initialize( 5, 5, sw::AdventureElementSettings{} ) );
    AdventureElementGridTestUtil::fill( grid, sw::AdventureMaterial::Grass );

    const sw::int2 center{ 2, 2 };
    SW_EXPECT_TRUE( grid.applyFire( center ) );
    SW_EXPECT_FALSE( grid.applyFire( center ) );
    SW_EXPECT_FALSE( grid.isBurning( sw::int2{ 3, 2 } ) ); // 붙인 그 자리에서는 아직 번지지 않는다

    grid.step();
    SW_EXPECT_EQUAL( 5, grid.countBurning() );
    SW_EXPECT_TRUE( grid.isBurning( sw::int2{ 3, 2 } ) );
    SW_EXPECT_TRUE( grid.isBurning( sw::int2{ 1, 2 } ) );
    SW_EXPECT_TRUE( grid.isBurning( sw::int2{ 2, 3 } ) );
    SW_EXPECT_TRUE( grid.isBurning( sw::int2{ 2, 1 } ) );
    SW_EXPECT_FALSE( grid.isBurning( sw::int2{ 3, 3 } ) );

    sw::vector<sw::AdventureElementEvent> listEvent;
    grid.drainEvents( listEvent );
    SW_EXPECT_EQUAL( 5, sw::VectorUtil::countBy<&sw::AdventureElementEvent::_type>( listEvent, sw::AdventureElementEventType::Ignited ) );
}

/**
 * @brief [AdventureElementGridTest] 바람이 있으면 바람 쪽으로만 번지고(대각선 바람 포함) 거슬러 가지 않는다. 옆바람 설정은 옆 두 칸을 더한다. 바람 성분은 −1..1 로 자른다
 */
SW_TEST_CASE( AdventureElementGridTest, WindDecidesTheSpreadDirection )
{
    sw::AdventureElementGrid row;
    SW_REQUIRE_TRUE( row.initialize( 5, 1, sw::AdventureElementSettings{} ) );
    AdventureElementGridTestUtil::fill( row, sw::AdventureMaterial::Grass );
    row.setWind( sw::int2{ 5, -9 } );
    SW_EXPECT_EQUAL( 1, row.getWind()._x );
    SW_EXPECT_EQUAL( -1, row.getWind()._y );
    row.setWind( sw::int2{ 1, 0 } );
    SW_REQUIRE_TRUE( row.applyFire( sw::int2{ 2, 0 } ) );
    row.step();
    SW_EXPECT_TRUE( row.isBurning( sw::int2{ 3, 0 } ) );
    row.step();
    SW_EXPECT_TRUE( row.isBurning( sw::int2{ 4, 0 } ) );
    row.step();
    SW_EXPECT_FALSE( row.isBurning( sw::int2{ 1, 0 } ) );

    sw::AdventureElementGrid diagonal;
    SW_REQUIRE_TRUE( diagonal.initialize( 3, 3, sw::AdventureElementSettings{} ) );
    AdventureElementGridTestUtil::fill( diagonal, sw::AdventureMaterial::Grass );
    diagonal.setWind( sw::int2{ 1, 1 } );
    SW_REQUIRE_TRUE( diagonal.applyFire( sw::int2{ 0, 0 } ) );
    diagonal.step();
    SW_EXPECT_TRUE( diagonal.isBurning( sw::int2{ 1, 1 } ) );
    SW_EXPECT_FALSE( diagonal.isBurning( sw::int2{ 1, 0 } ) );
    SW_EXPECT_FALSE( diagonal.isBurning( sw::int2{ 0, 1 } ) );

    sw::AdventureElementSettings crosswindSettings;
    crosswindSettings._bCrosswindSpread = SW_TRUE;
    sw::AdventureElementGrid crosswind;
    SW_REQUIRE_TRUE( crosswind.initialize( 3, 3, crosswindSettings ) );
    AdventureElementGridTestUtil::fill( crosswind, sw::AdventureMaterial::Grass );
    crosswind.setWind( sw::int2{ 1, 0 } );
    SW_REQUIRE_TRUE( crosswind.applyFire( sw::int2{ 1, 1 } ) );
    crosswind.step();
    SW_EXPECT_TRUE( crosswind.isBurning( sw::int2{ 2, 1 } ) );
    SW_EXPECT_TRUE( crosswind.isBurning( sw::int2{ 1, 2 } ) );
    SW_EXPECT_TRUE( crosswind.isBurning( sw::int2{ 1, 0 } ) );
    SW_EXPECT_FALSE( crosswind.isBurning( sw::int2{ 0, 1 } ) );
}

/**
 * @brief [AdventureElementGridTest] 번지는 지연 스텝 전에는 옆으로 옮겨 붙지 않는다
 */
SW_TEST_CASE( AdventureElementGridTest, SpreadWaitsForTheDelay )
{
    sw::AdventureElementSettings settings;
    settings._spreadDelaySteps = 3;
    sw::AdventureElementGrid grid;
    SW_REQUIRE_TRUE( grid.initialize( 2, 1, settings ) );
    AdventureElementGridTestUtil::fill( grid, sw::AdventureMaterial::Grass );
    SW_REQUIRE_TRUE( grid.applyFire( sw::int2{ 0, 0 } ) );
    grid.step();
    grid.step();
    SW_EXPECT_FALSE( grid.isBurning( sw::int2{ 1, 0 } ) );
    grid.step();
    SW_EXPECT_TRUE( grid.isBurning( sw::int2{ 1, 0 } ) );
}

/**
 * @brief [AdventureElementGridTest] 풀과 나무는 정해진 스텝을 타고 빈 칸이 된다(다시 붙지 않는다). 기류는 타는 풀에만 있고 행 우선으로 모은다
 */
SW_TEST_CASE( AdventureElementGridTest, BurnTimesAndUpdrafts )
{
    sw::AdventureElementGrid grass;
    SW_REQUIRE_TRUE( grass.initialize( 1, 1, sw::AdventureElementSettings{} ) );
    grass.setMaterial( sw::int2{ 0, 0 }, sw::AdventureMaterial::Grass );
    SW_EXPECT_FALSE( grass.hasUpdraft( sw::int2{ 0, 0 } ) );
    SW_REQUIRE_TRUE( grass.applyFire( sw::int2{ 0, 0 } ) );
    SW_EXPECT_TRUE( grass.hasUpdraft( sw::int2{ 0, 0 } ) );
    for ( int32 step = 0; step < 3; ++step )
    {
        grass.step();
        SW_EXPECT_TRUE( grass.isBurning( sw::int2{ 0, 0 } ) );
    }
    grass.step();
    SW_EXPECT_FALSE( grass.isBurning( sw::int2{ 0, 0 } ) );
    SW_EXPECT_FALSE( grass.hasUpdraft( sw::int2{ 0, 0 } ) );
    SW_EXPECT_TRUE( grass.getMaterial( sw::int2{ 0, 0 } ) == sw::AdventureMaterial::Empty );
    SW_EXPECT_FALSE( grass.applyFire( sw::int2{ 0, 0 } ) );
    sw::vector<sw::AdventureElementEvent> listEvent;
    grass.drainEvents( listEvent );
    SW_EXPECT_EQUAL( 1, sw::VectorUtil::countBy<&sw::AdventureElementEvent::_type>( listEvent, sw::AdventureElementEventType::BurnedOut ) );

    sw::AdventureElementGrid wood;
    SW_REQUIRE_TRUE( wood.initialize( 1, 1, sw::AdventureElementSettings{} ) );
    wood.setMaterial( sw::int2{ 0, 0 }, sw::AdventureMaterial::Wood );
    SW_REQUIRE_TRUE( wood.applyFire( sw::int2{ 0, 0 } ) );
    SW_EXPECT_FALSE( wood.hasUpdraft( sw::int2{ 0, 0 } ) );
    for ( int32 step = 0; step < 11; ++step )
    {
        wood.step();
    }
    SW_EXPECT_TRUE( wood.isBurning( sw::int2{ 0, 0 } ) );
    wood.step();
    SW_EXPECT_TRUE( wood.getMaterial( sw::int2{ 0, 0 } ) == sw::AdventureMaterial::Empty );

    sw::AdventureElementGrid mixed;
    SW_REQUIRE_TRUE( mixed.initialize( 3, 2, sw::AdventureElementSettings{} ) );
    mixed.setMaterial( sw::int2{ 2, 1 }, sw::AdventureMaterial::Grass );
    mixed.setMaterial( sw::int2{ 0, 1 }, sw::AdventureMaterial::Grass );
    mixed.setMaterial( sw::int2{ 1, 0 }, sw::AdventureMaterial::Wood );
    SW_REQUIRE_TRUE( mixed.applyFire( sw::int2{ 2, 1 } ) );
    SW_REQUIRE_TRUE( mixed.applyFire( sw::int2{ 0, 1 } ) );
    SW_REQUIRE_TRUE( mixed.applyFire( sw::int2{ 1, 0 } ) );
    sw::vector<sw::int2> listUpdraft;
    listUpdraft.push_back( sw::int2{ 9, 9 } );
    mixed.collectUpdraft( listUpdraft );
    SW_REQUIRE_EQUAL( size_t( 2 ), listUpdraft.size() );
    SW_EXPECT_EQUAL( 0, listUpdraft[0]._x );
    SW_EXPECT_EQUAL( 2, listUpdraft[1]._x );
}

/**
 * @brief [AdventureElementGridTest] 금속 · 물 · 빈 칸은 타지 않고, 격자 밖 자극은 무시한다. 얼음은 불을 맞거나 타는 칸 옆이면(대각선 제외) 물이 된다
 */
SW_TEST_CASE( AdventureElementGridTest, FireOnNonFlammablesAndMelting )
{
    sw::AdventureElementGrid grid;
    SW_REQUIRE_TRUE( grid.initialize( 4, 1, sw::AdventureElementSettings{} ) );
    grid.setMaterial( sw::int2{ 0, 0 }, sw::AdventureMaterial::Metal );
    grid.setMaterial( sw::int2{ 1, 0 }, sw::AdventureMaterial::Water );
    grid.setMaterial( sw::int2{ 3, 0 }, sw::AdventureMaterial::Ice );
    SW_EXPECT_FALSE( grid.applyFire( sw::int2{ 0, 0 } ) );
    SW_EXPECT_FALSE( grid.applyFire( sw::int2{ 1, 0 } ) );
    SW_EXPECT_FALSE( grid.applyFire( sw::int2{ 2, 0 } ) );
    SW_EXPECT_FALSE( grid.applyFire( sw::int2{ 4, 0 } ) );
    SW_EXPECT_FALSE( grid.applyFire( sw::int2{ -1, 0 } ) );
    SW_EXPECT_TRUE( grid.getMaterial( sw::int2{ -1, 0 } ) == sw::AdventureMaterial::Empty );
    SW_EXPECT_TRUE( grid.applyFire( sw::int2{ 3, 0 } ) );
    SW_EXPECT_TRUE( grid.getMaterial( sw::int2{ 3, 0 } ) == sw::AdventureMaterial::Water );
    SW_EXPECT_FALSE( grid.isBurning( sw::int2{ 3, 0 } ) );

    sw::AdventureElementGrid neighbor;
    SW_REQUIRE_TRUE( neighbor.initialize( 2, 2, sw::AdventureElementSettings{} ) );
    neighbor.setMaterial( sw::int2{ 0, 0 }, sw::AdventureMaterial::Grass );
    neighbor.setMaterial( sw::int2{ 1, 0 }, sw::AdventureMaterial::Ice );
    neighbor.setMaterial( sw::int2{ 1, 1 }, sw::AdventureMaterial::Ice );
    SW_REQUIRE_TRUE( neighbor.applyFire( sw::int2{ 0, 0 } ) );
    neighbor.step();
    SW_EXPECT_TRUE( neighbor.getMaterial( sw::int2{ 1, 0 } ) == sw::AdventureMaterial::Water );
    SW_EXPECT_TRUE( neighbor.getMaterial( sw::int2{ 1, 1 } ) == sw::AdventureMaterial::Ice );
    sw::vector<sw::AdventureElementEvent> listEvent;
    neighbor.drainEvents( listEvent );
    SW_EXPECT_EQUAL( 1, sw::VectorUtil::countBy<&sw::AdventureElementEvent::_type>( listEvent, sw::AdventureElementEventType::Melted ) );
}

/**
 * @brief [AdventureElementGridTest] 냉기는 타는 칸을 끄고(재질은 그대로라 다시 붙는다), 물을 얼리며 대전을 지운다. 그 밖에는 아무 일도 없다
 */
SW_TEST_CASE( AdventureElementGridTest, IceExtinguishesAndFreezes )
{
    sw::AdventureElementGrid grid;
    SW_REQUIRE_TRUE( grid.initialize( 3, 1, sw::AdventureElementSettings{} ) );
    grid.setMaterial( sw::int2{ 0, 0 }, sw::AdventureMaterial::Grass );
    grid.setMaterial( sw::int2{ 1, 0 }, sw::AdventureMaterial::Metal );
    grid.setMaterial( sw::int2{ 2, 0 }, sw::AdventureMaterial::Water );

    SW_EXPECT_FALSE( grid.applyIce( sw::int2{ 0, 0 } ) ); // 타지 않는 풀
    SW_REQUIRE_TRUE( grid.applyFire( sw::int2{ 0, 0 } ) );
    SW_EXPECT_TRUE( grid.applyIce( sw::int2{ 0, 0 } ) );
    SW_EXPECT_FALSE( grid.isBurning( sw::int2{ 0, 0 } ) );
    SW_EXPECT_TRUE( grid.getMaterial( sw::int2{ 0, 0 } ) == sw::AdventureMaterial::Grass );
    SW_EXPECT_TRUE( grid.applyFire( sw::int2{ 0, 0 } ) );

    SW_EXPECT_FALSE( grid.applyIce( sw::int2{ 1, 0 } ) );
    SW_EXPECT_FALSE( grid.applyIce( sw::int2{ 9, 0 } ) );

    SW_EXPECT_EQUAL( 2, grid.applyElectric( sw::int2{ 2, 0 } ) );
    SW_EXPECT_TRUE( grid.isCharged( sw::int2{ 2, 0 } ) );
    SW_EXPECT_TRUE( grid.applyIce( sw::int2{ 2, 0 } ) );
    SW_EXPECT_TRUE( grid.getMaterial( sw::int2{ 2, 0 } ) == sw::AdventureMaterial::Ice );
    SW_EXPECT_FALSE( grid.isCharged( sw::int2{ 2, 0 } ) );
    SW_EXPECT_FALSE( grid.applyIce( sw::int2{ 2, 0 } ) );

    sw::vector<sw::AdventureElementEvent> listEvent;
    grid.drainEvents( listEvent );
    SW_EXPECT_EQUAL( 1, sw::VectorUtil::countBy<&sw::AdventureElementEvent::_type>( listEvent, sw::AdventureElementEventType::Extinguished ) );
    SW_EXPECT_EQUAL( 1, sw::VectorUtil::countBy<&sw::AdventureElementEvent::_type>( listEvent, sw::AdventureElementEventType::Frozen ) );
}

/**
 * @brief [AdventureElementGridTest] 전기는 이어진 금속과 물을 따라 퍼져 그 수를 돌려주고, 얼음을 넘지 못하며, 정해진 스텝 뒤 사라진다. 부도체에 떨어지면 그 칸에만 남는다
 */
SW_TEST_CASE( AdventureElementGridTest, ElectricityFloodsConductorsAndIceBlocks )
{
    sw::AdventureElementGrid grid;
    SW_REQUIRE_TRUE( grid.initialize( 6, 1, sw::AdventureElementSettings{} ) );
    grid.setMaterial( sw::int2{ 0, 0 }, sw::AdventureMaterial::Metal );
    grid.setMaterial( sw::int2{ 1, 0 }, sw::AdventureMaterial::Metal );
    grid.setMaterial( sw::int2{ 2, 0 }, sw::AdventureMaterial::Water );
    grid.setMaterial( sw::int2{ 3, 0 }, sw::AdventureMaterial::Ice );
    grid.setMaterial( sw::int2{ 4, 0 }, sw::AdventureMaterial::Metal );

    SW_EXPECT_EQUAL( 3, grid.applyElectric( sw::int2{ 0, 0 } ) );
    SW_EXPECT_TRUE( grid.isCharged( sw::int2{ 2, 0 } ) );
    SW_EXPECT_FALSE( grid.isCharged( sw::int2{ 3, 0 } ) );
    SW_EXPECT_FALSE( grid.isCharged( sw::int2{ 4, 0 } ) );
    sw::vector<sw::AdventureElementEvent> listEvent;
    grid.drainEvents( listEvent );
    SW_EXPECT_EQUAL( 3, sw::VectorUtil::countBy<&sw::AdventureElementEvent::_type>( listEvent, sw::AdventureElementEventType::Electrified ) );

    grid.step();
    SW_EXPECT_TRUE( grid.isCharged( sw::int2{ 0, 0 } ) );
    grid.step();
    SW_EXPECT_FALSE( grid.isCharged( sw::int2{ 0, 0 } ) );

    SW_EXPECT_EQUAL( 1, grid.applyElectric( sw::int2{ 5, 0 } ) ); // 빈 칸
    SW_EXPECT_TRUE( grid.isCharged( sw::int2{ 5, 0 } ) );
    SW_EXPECT_FALSE( grid.isCharged( sw::int2{ 4, 0 } ) ); // 부도체에 떨어진 번개는 옆 금속으로 가지 않는다
    SW_EXPECT_EQUAL( 0, grid.applyElectric( sw::int2{ 6, 0 } ) );
}

/**
 * @brief [AdventureElementGridTest] 복사본은 자기 규칙 테이블을 가리켜 원본이 사라져도 돌고, 서로 영향을 주지 않는다
 */
SW_TEST_CASE( AdventureElementGridTest, CopiesOutliveTheOriginal )
{
    sw::AdventureElementGrid assigned;
    {
        sw::AdventureElementGrid source;
        SW_REQUIRE_TRUE( source.initialize( 3, 1, sw::AdventureElementSettings{} ) );
        AdventureElementGridTestUtil::fill( source, sw::AdventureMaterial::Grass );
        SW_REQUIRE_TRUE( source.applyFire( sw::int2{ 0, 0 } ) );
        assigned = source;
    }
    assigned.step();
    SW_EXPECT_TRUE( assigned.isBurning( sw::int2{ 1, 0 } ) );

    sw::AdventureElementGrid constructed( assigned );
    constructed.step();
    SW_EXPECT_TRUE( constructed.isBurning( sw::int2{ 2, 0 } ) );
    SW_EXPECT_FALSE( assigned.isBurning( sw::int2{ 2, 0 } ) );
    SW_EXPECT_TRUE( constructed.applyIce( sw::int2{ 0, 0 } ) );
    SW_EXPECT_TRUE( assigned.isBurning( sw::int2{ 0, 0 } ) );
}

/**
 * @brief [AdventureElementGridTest] 시간은 고정 스텝으로 쌓아 쓰고, 긴 프레임은 상한(1 초)까지만 낸다. 0 · 음수 시간은 쌓지 않는다
 */
SW_TEST_CASE( AdventureElementGridTest, UpdateConsumesFixedSteps )
{
    sw::AdventureElementGrid grid;
    SW_REQUIRE_TRUE( grid.initialize( 1, 1, sw::AdventureElementSettings{} ) ); // 스텝 0.25 초
    SW_EXPECT_EQUAL( 1, grid.update( 0.25f ) );
    SW_EXPECT_EQUAL( 0, grid.update( 0.125f ) );
    SW_EXPECT_EQUAL( 1, grid.update( 0.125f ) );
    SW_EXPECT_EQUAL( 0, grid.update( 0.0f ) );
    SW_EXPECT_EQUAL( 0, grid.update( -5.0f ) );
    SW_EXPECT_EQUAL( 4, grid.update( 100.0f ) );
    SW_EXPECT_EQUAL( 6u, grid.getStepCount() );
}

/**
 * @brief [AdventureElementGridTest] 같은 조작은 같은 해시를 내고, 쓰고 읽은 격자는 원본과 같이 굴러간다. 크기가 다르거나 잘린 바이트는 거절하고, 읽으면 쌓인 알림을 비운다
 */
SW_TEST_CASE( AdventureElementGridTest, StateRoundTripIsDeterministic )
{
    const auto run = []( sw::AdventureElementGrid& grid )
    {
        SW_EXPECT_TRUE( grid.initialize( 4, 3, sw::AdventureElementSettings{} ) );
        AdventureElementGridTestUtil::fill( grid, sw::AdventureMaterial::Grass );
        grid.setMaterial( sw::int2{ 3, 2 }, sw::AdventureMaterial::Ice );
        grid.setWind( sw::int2{ 1, 0 } );
        SW_EXPECT_TRUE( grid.applyFire( sw::int2{ 0, 1 } ) );
        grid.step();
    };
    sw::AdventureElementGrid original;
    run( original );
    sw::AdventureElementGrid twin;
    run( twin );
    SW_EXPECT_EQUAL( original.computeStateHash(), twin.computeStateHash() );
    twin.step();
    SW_EXPECT_NOT_EQUAL( original.computeStateHash(), twin.computeStateHash() );

    sw::Archive written;
    original.writeState( written );

    sw::AdventureElementGrid restored;
    SW_REQUIRE_TRUE( restored.initialize( 4, 3, sw::AdventureElementSettings{} ) );
    SW_REQUIRE_TRUE( restored.applyElectric( sw::int2{ 0, 0 } ) == 1 ); // 읽으면 사라질 알림
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( original.computeStateHash(), restored.computeStateHash() );
    SW_EXPECT_EQUAL( original.getStepCount(), restored.getStepCount() );
    SW_EXPECT_EQUAL( 1, restored.getWind()._x );
    sw::vector<sw::AdventureElementEvent> listEvent;
    restored.drainEvents( listEvent );
    SW_EXPECT_TRUE( listEvent.empty() );

    for ( int32 step = 0; step < 5; ++step )
    {
        original.step();
        restored.step();
        SW_EXPECT_EQUAL( original.computeStateHash(), restored.computeStateHash() );
    }

    sw::AdventureElementGrid wider;
    SW_REQUIRE_TRUE( wider.initialize( 5, 3, sw::AdventureElementSettings{} ) );
    sw::Archive forWider( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( wider.readState( forWider ) );

    sw::AdventureElementGrid truncatedTarget;
    SW_REQUIRE_TRUE( truncatedTarget.initialize( 4, 3, sw::AdventureElementSettings{} ) );
    const uint32 hashBefore = truncatedTarget.computeStateHash();
    sw::Archive  truncated( written.getData(), written.getSize() - 1 );
    SW_EXPECT_FALSE( truncatedTarget.readState( truncated ) );
    SW_EXPECT_EQUAL( hashBefore, truncatedTarget.computeStateHash() );
}
