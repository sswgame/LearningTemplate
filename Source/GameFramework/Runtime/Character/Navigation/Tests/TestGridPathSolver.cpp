// GridPathSolverTest — 격자 경로 요청 줄(`PathRequestQueue<GridPathSolver>`)이 동기 길(`NavAgent::planPath`)과 같은 경로를 내고, 레인을 나눠도 결과가 같다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Navigation/PathRequestQueue.h"

#include "GameFramework/Runtime/Character/Navigation/GridPathSolver.h"
#include "GameFramework/Runtime/Character/Navigation/GridPathfinder.h"
#include "GameFramework/Runtime/Character/Navigation/NavAgent.h"
#include "GameFramework/Runtime/Character/Navigation/NavGrid.h"

namespace
{
    /** @brief 12 × 12 격자, 가운데 세로 벽(맨 아래 두 줄만 열림)을 깝니다. 격자를 만들지 못하면 false 입니다. */
    [[nodiscard]] bool makeWalledGrid( sw::NavGrid& outGrid )
    {
        if ( outGrid.initialize( 12, 12, 1.0f, sw::float3{ 0.0f, 0.0f, 0.0f } ) == false )
            return false;
        for ( int32 y = 2; y < 12; ++y )
        {
            outGrid.setBlocked( 6, y, true );
        }
        return true;
    }
} // namespace

/**
 * @brief [GridPathSolverTest] 줄로 푼 경로는 동기로 구한 경로와 점마다 같다 — 요청 순서 · 레인 수와 상관없다(결정적 고정 걸음이 그대로 쓴다)
 */
SW_TEST_CASE( GridPathSolverTest, QueuedPathsMatchSynchronousPaths )
{
    sw::NavGrid grid;
    SW_REQUIRE_TRUE( makeWalledGrid( grid ) );
    sw::GridPathSolver                       solver{ grid };
    sw::PathRequestQueue<sw::GridPathSolver> queue{ solver };

    sw::vector<sw::float3>            listFrom;
    sw::vector<sw::float3>            listTarget;
    sw::vector<sw::PathRequestHandle> listHandle;
    for ( int32 index = 0; index < 40; ++index )
    {
        const sw::float3 from{ 0.5f + static_cast<float32>( index % 5 ), 0.0f, 0.5f + static_cast<float32>( index % 11 ) };
        const sw::float3 target{ 11.5f - static_cast<float32>( index % 4 ), 0.0f, 11.5f - static_cast<float32>( index % 7 ) };
        sw::GridPathJob  job;
        job._from   = from;
        job._target = target;
        listFrom.push_back( from );
        listTarget.push_back( target );
        listHandle.push_back( queue.request( std::move( job ) ) );
    }
    queue.launch( "Test", nullptr );
    (void)queue.complete();

    sw::GridPathfinder pathfinder;
    for ( size_t index = 0; index < listHandle.size(); ++index )
    {
        sw::GridPathJob job;
        SW_REQUIRE_TRUE( queue.takeResult( listHandle[index], job ) == sw::PathRequestState::Ready );
        sw::vector<sw::float3>   listExpected;
        const sw::GridPathResult expected = sw::NavAgent::planPath( grid, pathfinder, listFrom[index], listTarget[index], listExpected );
        SW_EXPECT_TRUE( job._result == expected );
        SW_REQUIRE_EQUAL( listExpected.size(), job._listPoint.size() );
        for ( size_t pointIndex = 0; pointIndex < listExpected.size(); ++pointIndex )
        {
            SW_EXPECT_NEAR( listExpected[pointIndex]._x, job._listPoint[pointIndex]._x, 1.0e-6f );
            SW_EXPECT_NEAR( listExpected[pointIndex]._z, job._listPoint[pointIndex]._z, 1.0e-6f );
        }
    }
}

/**
 * @brief [GridPathSolverTest] 막힌 칸에서 시작하면 경로가 없다(`InvalidStart`) — 요청자는 그 결과로 걸을 수 있는 칸으로 민다(RTS `snapToWalkable`)
 */
SW_TEST_CASE( GridPathSolverTest, BlockedStartReportsNoPath )
{
    sw::NavGrid grid;
    SW_REQUIRE_TRUE( makeWalledGrid( grid ) );
    sw::GridPathSolver                       solver{ grid };
    sw::PathRequestQueue<sw::GridPathSolver> queue{ solver };
    sw::GridPathJob                          request;
    request._from                      = sw::float3{ 6.5f, 0.0f, 6.5f }; // 벽 칸
    request._target                    = sw::float3{ 0.5f, 0.0f, 0.5f };
    const sw::PathRequestHandle handle = queue.request( std::move( request ) );
    queue.launch( "Test", nullptr );
    (void)queue.complete();
    sw::GridPathJob job;
    SW_REQUIRE_TRUE( queue.takeResult( handle, job ) == sw::PathRequestState::Ready );
    SW_EXPECT_TRUE( job._result == sw::GridPathResult::InvalidStart );
    SW_EXPECT_TRUE( job._listPoint.empty() );
}
