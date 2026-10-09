#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/Actor/Navigation/FlowField.h"
#include "GameFramework/Base/Actor/Navigation/GridPathfinder.h"
#include "GameFramework/Base/Actor/Navigation/GridReachability.h"
#include "GameFramework/Base/Actor/Navigation/NavAgent.h"
#include "GameFramework/Base/Actor/Navigation/NavGrid.h"
#include "GameFramework/Base/Actor/Navigation/NavGridMover.h"

#include "TestFramework/TestFramework.h"

// 장르 공통 내비게이션 — 격자 · 시선, A*(값 가중 · 모서리 깎기 금지 · 부분 경로 · 예산 · 다듬기), 흐름장, 경로 · 흐름장을 따라 걷는 행위자(분리 · 미끄러짐 · 끼임).

using namespace sw;

namespace
{
    /** @brief 20 × 20, 칸 1 m. x = 10 에 벽(y 0..15), 위쪽 y 16..19 가 틈이다. */
    void makeWallGrid( NavGrid& grid )
    {
        grid.initialize( 20, 20, 1.0f, float3{ 0.0f, 0.0f, 0.0f } );
        grid.setAreaCost( 10, 0, 10, 15, kNavBlockedCost );
    }

    /** @brief 경로가 이웃 칸으로만 이어지고(다듬지 않은 경로) 막힌 칸 · 모서리 깎기가 없으면 true 입니다. */
    bool isValidCellPath( const NavGrid& grid, const vector<int2>& listCell )
    {
        for ( size_t cellIndex = 0; cellIndex < listCell.size(); ++cellIndex )
        {
            if ( grid.isWalkable( listCell[cellIndex] ) == false )
                return false;
            if ( cellIndex == 0 )
                continue;
            const int2 delta = listCell[cellIndex] - listCell[cellIndex - 1];
            if ( MathUtil::abs( delta._x ) > 1 || MathUtil::abs( delta._y ) > 1 )
                return false;
            if ( delta._x != 0 && delta._y != 0 &&
                 ( grid.isWalkable( listCell[cellIndex - 1]._x + delta._x, listCell[cellIndex - 1]._y ) == false ||
                   grid.isWalkable( listCell[cellIndex - 1]._x, listCell[cellIndex - 1]._y + delta._y ) == false ) )
                return false;
        }
        return true;
    }

    float32 computeCellPathCost( const NavGrid& grid, const vector<int2>& listCell )
    {
        float32 cost = 0.0f;
        for ( size_t cellIndex = 1; cellIndex < listCell.size(); ++cellIndex )
        {
            const int2 delta = listCell[cellIndex] - listCell[cellIndex - 1];
            cost += static_cast<float32>( grid.getCost( listCell[cellIndex]._x, listCell[cellIndex]._y ) ) * ( delta._x != 0 && delta._y != 0 ? 1.41421356f : 1.0f );
        }
        return cost;
    }
} // namespace

/**
 * @brief [NavigationTest] 월드 ↔ 칸 · 영역 값 바꾸기는 리비전을 한 번 올린다 · 시선은 벽에 막히고 대각선 틈으로 새지 않는다 · 막힌 칸의 가장 가까운 걸을 칸
 */
SW_TEST_CASE( NavigationTest, GridConvertsCellsAndChecksLineOfSight )
{
    NavGrid grid;
    grid.initialize( 10, 8, 2.0f, float3{ -10.0f, 1.0f, 4.0f } );
    SW_EXPECT_TRUE( grid.computeCell( float3{ -9.5f, 0.0f, 4.1f } ) == ( int2{ 0, 0 } ) );
    SW_EXPECT_TRUE( grid.computeCell( float3{ -11.0f, 0.0f, 4.0f } ) == ( int2{ -1, 0 } ) );
    const float3 center = grid.computeCellCenter( int2{ 3, 2 } );
    SW_EXPECT_NEAR_EQUAL( -3.0f, center._x, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, center._y, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 9.0f, center._z, 1.0e-5f );

    const uint32 revision = grid.getRevision();
    grid.setAreaCost( 4, 0, 4, 6, kNavBlockedCost );
    SW_EXPECT_EQUAL( revision + 1, grid.getRevision() );
    grid.setAreaCost( 4, 0, 4, 6, kNavBlockedCost ); // 그대로면 오르지 않는다
    SW_EXPECT_EQUAL( revision + 1, grid.getRevision() );

    SW_EXPECT_FALSE( grid.hasLineOfSight( int2{ 1, 3 }, int2{ 8, 3 } ) );
    SW_EXPECT_TRUE( grid.hasLineOfSight( int2{ 1, 7 }, int2{ 8, 7 } ) ); // y = 7 은 열려 있다
    SW_EXPECT_TRUE( grid.hasLineOfSight( int2{ 1, 1 }, int2{ 3, 5 } ) );

    // 대각선으로 붙은 두 막힌 칸 사이는 지나지 못한다.
    NavGrid diagonal;
    diagonal.initialize( 4, 4, 1.0f, float3{} );
    diagonal.setBlocked( 1, 2, true );
    diagonal.setBlocked( 2, 1, true );
    SW_EXPECT_FALSE( diagonal.hasLineOfSight( int2{ 1, 1 }, int2{ 2, 2 } ) );
    SW_EXPECT_FALSE( diagonal.hasLineOfSight( int2{ 0, 0 }, int2{ 3, 3 } ) );

    int2 nearest;
    SW_ASSERT_TRUE( grid.findNearestWalkable( int2{ 4, 3 }, 3, nearest ) );
    SW_EXPECT_TRUE( nearest == ( int2{ 3, 3 } ) || nearest == ( int2{ 5, 3 } ) );
    SW_EXPECT_FALSE( grid.findNearestWalkable( int2{ 40, 40 }, 2, nearest ) );
}

/**
 * @brief [NavigationTest] A* 는 벽의 틈으로 돌아가는 가장 싼 길을 찾고, 싼 길(도로)이 있으면 돌아가더라도 그 길을 탄다 · 다듬은 경로는 시선이 이어진다
 */
SW_TEST_CASE( NavigationTest, PathfinderFindsCheapestPathAroundWalls )
{
    NavGrid grid;
    makeWallGrid( grid );
    GridPathfinder pathfinder;
    GridPathQuery  query;
    query._start   = int2{ 2, 2 };
    query._goal    = int2{ 17, 2 };
    query._bSmooth = SW_FALSE;
    vector<int2> listCell;
    SW_ASSERT_TRUE( pathfinder.findPath( grid, query, listCell ) == GridPathResult::Found );
    SW_EXPECT_TRUE( listCell.front() == query._start && listCell.back() == query._goal );
    SW_EXPECT_TRUE( isValidCellPath( grid, listCell ) );
    bool bUsedGap = false;
    for ( const int2& cell : listCell )
    {
        bUsedGap = bUsedGap || ( cell._x == 10 && cell._y >= 16 );
    }
    SW_EXPECT_TRUE( bUsedGap );
    // 가장 싼 길: (2,2)→(9,15) 대각 7 + 직 6, 틈 (10,16) 대각, (11,15)→(17,2) … 비용 = 값 10 × 거리. 옥타일 하한보다 작을 수 없다.
    const float32 cost = computeCellPathCost( grid, listCell );
    SW_EXPECT_TRUE( cost >= 10.0f * ( 14.0f * 1.41421356f ) - 1.0e-3f );
    SW_EXPECT_TRUE( cost < 10.0f * 36.0f );

    // 다듬으면 꺾이는 점만 남고, 잇는 선이 모두 열려 있다.
    query._bSmooth = SW_TRUE;
    vector<int2> listSmooth;
    SW_ASSERT_TRUE( pathfinder.findPath( grid, query, listSmooth ) == GridPathResult::Found );
    SW_EXPECT_TRUE( listSmooth.size() < listCell.size() );
    bool bVisible = true;
    for ( size_t cellIndex = 1; cellIndex < listSmooth.size(); ++cellIndex )
    {
        bVisible = bVisible && grid.hasLineOfSight( listSmooth[cellIndex - 1], listSmooth[cellIndex] );
    }
    SW_EXPECT_TRUE( bVisible );

    // 도로 — 값 2 인 띠(y = 0)를 깔면 곧은 길(값 10)보다 그쪽으로 간다.
    NavGrid road;
    road.initialize( 20, 6, 1.0f, float3{} );
    road.setAreaCost( 0, 0, 19, 0, 2 );
    query._start   = int2{ 0, 3 };
    query._goal    = int2{ 19, 3 };
    query._bSmooth = SW_FALSE;
    SW_ASSERT_TRUE( pathfinder.findPath( road, query, listCell ) == GridPathResult::Found );
    int32 roadCellCount = 0;
    for ( const int2& cell : listCell )
    {
        roadCellCount += cell._y == 0 ? 1 : 0;
    }
    SW_EXPECT_TRUE( roadCellCount >= 12 );
}

/**
 * @brief [NavigationTest] 갇힌 목적지는 가장 가까운 곳까지(부분 경로) · 부분을 받지 않으면 없음 · 막힌 시작 · 예산이 다하면 부분 · 격자 크기가 바뀌어도 같은 찾기를 다시 쓴다
 */
SW_TEST_CASE( NavigationTest, PathfinderReturnsPartialPathsWithinBudget )
{
    NavGrid grid;
    grid.initialize( 16, 16, 1.0f, float3{} );
    // (10..14, 10..14) 를 둘러싼 고리 — 안쪽 (12,12) 는 닿을 수 없다.
    grid.setAreaCost( 10, 10, 14, 10, kNavBlockedCost );
    grid.setAreaCost( 10, 14, 14, 14, kNavBlockedCost );
    grid.setAreaCost( 10, 10, 10, 14, kNavBlockedCost );
    grid.setAreaCost( 14, 10, 14, 14, kNavBlockedCost );

    GridPathfinder pathfinder;
    GridPathQuery  query;
    query._start   = int2{ 1, 1 };
    query._goal    = int2{ 12, 12 };
    query._bSmooth = SW_FALSE;
    vector<int2> listCell;
    SW_ASSERT_TRUE( pathfinder.findPath( grid, query, listCell ) == GridPathResult::Partial );
    const int2 end = listCell.back();
    SW_EXPECT_TRUE( MathUtil::max( MathUtil::abs( end._x - 12 ), MathUtil::abs( end._y - 12 ) ) == 3 ); // 고리 바로 밖
    SW_EXPECT_TRUE( isValidCellPath( grid, listCell ) );

    query._bAcceptPartial = SW_FALSE;
    SW_EXPECT_TRUE( pathfinder.findPath( grid, query, listCell ) == GridPathResult::NoPath );
    SW_EXPECT_TRUE( listCell.empty() );

    query._start = int2{ 10, 10 };
    SW_EXPECT_TRUE( pathfinder.findPath( grid, query, listCell ) == GridPathResult::InvalidStart );

    query._start          = int2{ 0, 0 };
    query._goal           = int2{ 15, 0 };
    query._bAcceptPartial = SW_TRUE;
    query._maxExpansions  = 5;
    SW_EXPECT_TRUE( pathfinder.findPath( grid, query, listCell ) == GridPathResult::Partial );
    SW_EXPECT_TRUE( pathfinder.getLastExpansionCount() <= 5u );
    SW_EXPECT_TRUE( listCell.back()._x > 0 ); // 그래도 목적지 쪽으로 몇 칸은 간다

    NavGrid smallGrid;
    smallGrid.initialize( 5, 5, 1.0f, float3{} );
    query._goal          = int2{ 4, 4 };
    query._maxExpansions = 0;
    SW_EXPECT_TRUE( pathfinder.findPath( smallGrid, query, listCell ) == GridPathResult::Found );
    SW_EXPECT_NEAR_EQUAL( 4.0f * 1.41421356f, GridPathfinder::computePathLength( smallGrid, listCell ), 1.0e-3f );
}

/**
 * @brief [NavigationTest] 흐름장의 거리는 A* 비용과 같고, 방향을 따라가면 거리가 줄어 목적지에 닿는다 · 갇힌 칸은 닿지 못한다 · 격자가 바뀌면 낡았다
 */
SW_TEST_CASE( NavigationTest, FlowFieldMatchesPathCostAndLeadsToGoal )
{
    NavGrid grid;
    makeWallGrid( grid );
    grid.setAreaCost( 3, 17, 7, 18, 30 ); // 늪 — 값이 다른 칸에서도 두 거리가 같아야 한다
    FlowField  field;
    const int2 goal{ 17, 2 };
    SW_EXPECT_TRUE( field.computeToCell( grid, goal ) > 300 );
    SW_EXPECT_NEAR_EQUAL( 0.0f, field.getDistance( goal ), 1.0e-6f );
    SW_EXPECT_TRUE( field.getDirectionIndex( goal ) == FlowField::kNoDirection );

    GridPathfinder pathfinder;
    GridPathQuery  query;
    query._goal    = goal;
    query._bSmooth = SW_FALSE;
    vector<int2> listCell;
    const int2   arrStart[] = {
        int2{ 2,  2},
        int2{ 5, 18},
        int2{12, 10},
        int2{ 0,  0}
    };
    bool bSameCost = true;
    bool bLeads    = true;
    for ( const int2& start : arrStart )
    {
        query._start = start;
        SW_ASSERT_TRUE( pathfinder.findPath( grid, query, listCell ) == GridPathResult::Found );
        bSameCost = bSameCost && MathUtil::abs( computeCellPathCost( grid, listCell ) - field.getDistance( start ) ) < 1.0e-2f;

        int2    cell     = start;
        float32 previous = field.getDistance( cell );
        for ( int32 stepIndex = 0; stepIndex < 100 && cell != goal; ++stepIndex )
        {
            cell                  = field.getNextCell( cell );
            const float32 current = field.getDistance( cell );
            bLeads                = bLeads && current < previous;
            previous              = current;
        }
        bLeads = bLeads && cell == goal;
    }
    SW_EXPECT_TRUE( bSameCost );
    SW_EXPECT_TRUE( bLeads );

    // 갇힌 칸.
    NavGrid boxed;
    boxed.initialize( 6, 6, 1.0f, float3{} );
    boxed.setAreaCost( 3, 0, 3, 5, kNavBlockedCost );
    FlowField boxedField;
    (void)boxedField.computeToCell( boxed, int2{ 0, 0 } );
    SW_EXPECT_FALSE( boxedField.isReachable( int2{ 5, 5 } ) );
    SW_EXPECT_TRUE( boxedField.sampleDirection( boxed, boxed.computeCellCenter( int2{ 5, 5 } ) ).getLengthSquared() < 1.0e-6f );
    SW_EXPECT_FALSE( boxedField.isStale( boxed ) );
    boxed.setBlocked( 3, 2, false );
    SW_EXPECT_TRUE( boxedField.isStale( boxed ) );
}

/**
 * @brief [NavigationTest] 행위자는 경로로 벽을 돌아 도착하고 막힌 칸에 들어가지 않는다 · 흐름장 하나로 열 명이 함께 와서 서로 겹치지 않는다 · 벽으로 밀면 끼인다
 */
SW_TEST_CASE( NavigationTest, AgentsFollowPathsAndFlowFieldsWithoutOverlapping )
{
    NavGrid grid;
    makeWallGrid( grid );
    GridPathfinder pathfinder;
    NavAgent       agent;
    agent.setPosition( float3{ 2.5f, 0.0f, 2.5f } );
    SW_ASSERT_TRUE( agent.moveTo( grid, pathfinder, float3{ 17.3f, 0.0f, 2.7f } ) );
    const vector<float3> listNoNeighbor;
    bool                 bAlwaysWalkable = true;
    for ( int32 frameIndex = 0; frameIndex < 600 && agent.getState() == NavAgentState::FollowingPath; ++frameIndex )
    {
        agent.update( grid, listNoNeighbor, 1.0f / 60.0f );
        bAlwaysWalkable = bAlwaysWalkable && grid.isWalkable( grid.computeCell( agent.getPosition() ) );
    }
    SW_EXPECT_TRUE( agent.getState() == NavAgentState::Arrived );
    SW_EXPECT_TRUE( bAlwaysWalkable );
    SW_EXPECT_TRUE( float3::getDistance( agent.getPosition(), float3{ 17.3f, 0.0f, 2.7f } ) < 0.3f );

    // 흐름장 — 열 명이 같은 곳으로.
    FlowField field;
    (void)field.computeToCell( grid, int2{ 17, 3 } );
    vector<NavAgent> listAgent( 10 );
    for ( size_t agentIndex = 0; agentIndex < listAgent.size(); ++agentIndex )
    {
        listAgent[agentIndex].setPosition( float3{ 1.5f + static_cast<float32>( agentIndex % 5 ), 0.0f, 1.5f + static_cast<float32>( agentIndex / 5 ) } );
        listAgent[agentIndex].followFlowField( &field, float3{ 17.5f, 0.0f, 3.5f } );
    }
    vector<float3> listNeighbor;
    float32        minSeparation = 100.0f;
    for ( int32 frameIndex = 0; frameIndex < 900; ++frameIndex )
    {
        for ( size_t agentIndex = 0; agentIndex < listAgent.size(); ++agentIndex )
        {
            listNeighbor.clear();
            for ( size_t otherIndex = 0; otherIndex < listAgent.size(); ++otherIndex )
            {
                if ( otherIndex != agentIndex )
                    listNeighbor.push_back( listAgent[otherIndex].getPosition() );
            }
            listAgent[agentIndex].update( grid, listNeighbor, 1.0f / 60.0f );
        }
        if ( frameIndex > 600 )
        {
            for ( size_t agentIndex = 0; agentIndex < listAgent.size(); ++agentIndex )
            {
                for ( size_t otherIndex = agentIndex + 1; otherIndex < listAgent.size(); ++otherIndex )
                {
                    minSeparation = MathUtil::min( minSeparation, float3::getDistance( listAgent[agentIndex].getPosition(), listAgent[otherIndex].getPosition() ) );
                }
            }
        }
    }
    int32 nearGoalCount = 0;
    for ( const NavAgent& member : listAgent )
    {
        nearGoalCount += float3::getDistance( member.getPosition(), float3{ 17.5f, 0.0f, 3.5f } ) < 3.0f ? 1 : 0;
    }
    SW_EXPECT_EQUAL( 10, nearGoalCount );
    SW_EXPECT_TRUE( minSeparation > 0.35f ); // 지름 0.8 — 조금 겹쳐도 겹쳐 쌓이지는 않는다

    // 벽 속으로 곧게 가라고 하면 미끄러지다 끼인다.
    NavAgent pushed;
    pushed.setPosition( float3{ 9.5f, 0.0f, 5.5f } );
    vector<float3> listThroughWall;
    listThroughWall.push_back( float3{ 12.5f, 0.0f, 5.5f } );
    pushed.followPath( listThroughWall );
    for ( int32 frameIndex = 0; frameIndex < 240; ++frameIndex )
    {
        pushed.update( grid, listNoNeighbor, 1.0f / 60.0f );
    }
    SW_EXPECT_TRUE( pushed.getState() == NavAgentState::Stuck );
    SW_EXPECT_TRUE( grid.isWalkable( grid.computeCell( pushed.getPosition() ) ) );
}

/**
 * @brief [NavigationTest] 격자 행위자를 공통 이동 창구(`INavMover`)로 — 내비메시 에이전트와 같은 호출로 걷고, 도착 · 길 없음(막힌 칸에서 출발)을 같은 상태로 알린다
 */
SW_TEST_CASE( NavigationTest, GridMoverSpeaksTheCommonMoverInterface )
{
    NavGrid grid;
    makeWallGrid( grid );
    GridPathfinder pathfinder;
    NavAgent       agent;
    agent.setPosition( float3{ 2.5f, 0.0f, 2.5f } );
    NavGridMover gridMover{ agent, grid, pathfinder };
    INavMover&   mover = gridMover;
    SW_ASSERT_TRUE( mover.moveTo( float3{ 17.3f, 0.0f, 2.7f } ) );
    SW_EXPECT_TRUE( mover.getMoveStatus() == NavMoveStatus::Moving );
    const vector<float3> listNoNeighbor;
    for ( int32 frameIndex = 0; frameIndex < 600 && mover.getMoveStatus() == NavMoveStatus::Moving; ++frameIndex )
    {
        agent.update( grid, listNoNeighbor, 1.0f / 60.0f );
    }
    SW_EXPECT_TRUE( mover.getMoveStatus() == NavMoveStatus::Arrived );
    SW_EXPECT_TRUE( float3::getDistance( mover.getMovePosition(), float3{ 17.3f, 0.0f, 2.7f } ) < 0.3f );

    // 막힌 칸(벽 속)에서는 길이 없다 — 실패로 알린다.
    NavAgent walled;
    walled.setPosition( float3{ 10.5f, 0.0f, 5.5f } );
    NavGridMover walledMover{ walled, grid, pathfinder };
    SW_EXPECT_FALSE( walledMover.moveTo( float3{ 17.3f, 0.0f, 2.7f } ) );
    SW_EXPECT_TRUE( walledMover.getMoveStatus() == NavMoveStatus::Failed );
    walledMover.stopMoving();
    SW_EXPECT_TRUE( walledMover.getMoveStatus() == NavMoveStatus::Idle );
}

SW_TEST_CASE( NavigationTest, ReachabilityHonoursTerrainCostsAlliesAndAttackRange )
{
    // 7 × 7, 가운데 (3,3) 에서 이동력 3. (4,3) 숲(비용 2), (3,4) 적(막힘), (2,3) 아군(지나가되 서지 못함).
    const int2       forest{ 4, 3 };
    const int2       enemy{ 3, 4 };
    const int2       ally{ 2, 3 };
    GridReachability reach;
    reach.compute( 7, 7, int2{ 3, 3 }, 3,
                   [&]( const int2&, const int2& to )
    { return to == enemy ? -1 : ( to == forest ? 2 : 1 ); },
                   [&]( const int2& cell )
    { return cell != ally; } );
    SW_EXPECT_TRUE( reach.isReachable( int2{ 3, 3 } ) );
    SW_EXPECT_EQUAL( 2, reach.getCost( forest ) );
    SW_EXPECT_EQUAL( 3, reach.getCost( int2{ 5, 3 } ) );
    SW_EXPECT_FALSE( reach.isReachable( int2{ 6, 3 } ) );
    SW_EXPECT_FALSE( reach.isReachable( enemy ) );
    SW_EXPECT_FALSE( reach.isReachable( ally ) );
    SW_EXPECT_EQUAL( 1, reach.getCost( ally ) );
    SW_EXPECT_TRUE( reach.isReachable( int2{ 0, 3 } ) );                            // 아군을 지나 왼쪽 끝
    SW_EXPECT_EQUAL( GridReachability::kUnreached, reach.getCost( int2{ 3, 6 } ) ); // 적 뒤는 돌아가야 해서 3 을 넘는다
    SW_EXPECT_FALSE( reach.isReachable( int2{ 3, 6 } ) );

    vector<int2> listPath;
    SW_ASSERT_TRUE( reach.makePath( int2{ 0, 3 }, listPath ) );
    SW_EXPECT_EQUAL( 4, static_cast<int32>( listPath.size() ) );
    SW_EXPECT_TRUE( listPath.front() == int2( 3, 3 ) && listPath.back() == int2( 0, 3 ) );

    vector<int2> listStand;
    reach.collectReachable( listStand );
    vector<int2> listAttack;
    reach.collectAttackCells( 1, 1, listAttack );
    SW_EXPECT_TRUE( listAttack.size() > listStand.size() );
    bool bEnemyInRange = false;
    for ( const int2& cell : listAttack )
    {
        bEnemyInRange = bEnemyInRange || cell == enemy;
    }
    SW_EXPECT_TRUE( bEnemyInRange );

    vector<int2> listRing;
    GridReachability::collectRangeCells( int2{ 0, 0 }, 2, 2, 7, 7, listRing );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( listRing.size() ) ); // (2,0) (1,1) (0,2)
}
