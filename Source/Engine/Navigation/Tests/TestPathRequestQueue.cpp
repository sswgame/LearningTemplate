// 비동기 경로 요청 줄(`PathRequestQueue`)의 순수 규칙 — 결과는 수확 뒤에만, 취소 · 칸 재사용은 세대가 가린다, 비우면 모두 사라진다, 결과는 요청 순서 · 구간과 상관없다.
// 타일 교체 순서는 형식이 지킨다 — 증표(`Fence` · `NavMeshWriteAccess`)는 줄 · 내비게이션 밖에서 만들 수 없다(아래 static_assert). 엔진 서비스 없이 부르므로 넘기기는 그 자리에서 푼다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Navigation/PathRequestQueue.h"
#include "Engine/Navigation/Scene/NavMeshRuntime.h"
#include "Engine/Navigation/Scene/NavPathSolver.h"

#include <type_traits>

namespace
{
    /** @brief 질의 값의 두 배를 결과로 내고, 어느 구간에서 풀었는지 적는 풀이입니다. */
    struct DoublingJob
    {
        int32  _query{ 0 };
        int32  _result{ 0 };
        uint32 _chunkIndex{ 0 };
    };

    struct DoublingSolver
    {
        using Job = DoublingJob;

        uint32 _preparedCount{ 0 };
        uint32 _chunkCount{ 0 };

        void prepareBatch( sw::vector<DoublingJob>& inoutListJob, uint32 chunkCount )
        {
            _preparedCount += static_cast<uint32>( inoutListJob.size() );
            _chunkCount = chunkCount;
        }

        void solve( DoublingJob& inoutJob, uint32 chunkIndex ) const
        {
            inoutJob._result     = inoutJob._query * 2;
            inoutJob._chunkIndex = chunkIndex;
        }
    };

    using DoublingQueue = sw::PathRequestQueue<DoublingSolver>;

    DoublingJob makeJob( int32 query )
    {
        DoublingJob job;
        job._query = query;
        return job;
    }
} // namespace

// 증표는 줄 · 내비게이션만 만든다 — 기다림(수확)을 건너뛰고 타일을 바꾸는 코드는 컴파일되지 않는다.
static_assert( std::is_default_constructible_v<DoublingQueue::Fence> == false, "only PathRequestQueue::complete / clear may create a fence" );
static_assert( std::is_constructible_v<sw::NavMeshWriteAccess, const sw::PathRequestQueue<sw::NavPathSolver>::Fence&> == false,
               "only SceneNavigation may turn a path-query fence into navmesh write access" );
static_assert( std::is_constructible_v<sw::NavMeshWriteAccess, const DoublingQueue::Fence&> == false, "another queue's fence never grants navmesh write access" );

/**
 * @brief [PathRequestQueueTest] 결과는 넘기기(`launch`) 뒤가 아니라 수확(`complete`) 뒤에만 보인다 — 워커가 얼마나 빨랐는지와 상관없이 같은 자리에서 Ready 가 된다
 */
SW_TEST_CASE( PathRequestQueueTest, ResultIsReadyOnlyAfterComplete )
{
    DoublingSolver              solver;
    DoublingQueue               queue{ solver };
    const sw::PathRequestHandle handle = queue.request( makeJob( 21 ) );
    SW_REQUIRE_TRUE( handle.isValid() );
    SW_EXPECT_TRUE( queue.getState( handle ) == sw::PathRequestState::Pending );

    queue.launch( "Test", nullptr );
    SW_EXPECT_EQUAL( 1u, solver._preparedCount );
    SW_EXPECT_TRUE( queue.getState( handle ) == sw::PathRequestState::Pending ); // 그 자리에서 풀었어도 칸에는 아직 들지 않았다
    DoublingJob job;
    SW_EXPECT_TRUE( queue.takeResult( handle, job ) == sw::PathRequestState::Pending );

    (void)queue.complete();
    SW_EXPECT_TRUE( queue.getState( handle ) == sw::PathRequestState::Ready );
    SW_REQUIRE_TRUE( queue.takeResult( handle, job ) == sw::PathRequestState::Ready );
    SW_EXPECT_EQUAL( 42, job._result );
    SW_EXPECT_EQUAL( uint64{ 1 }, queue.getCompletedCount() );
    // 받은 핸들은 끝났다.
    SW_EXPECT_TRUE( queue.takeResult( handle, job ) == sw::PathRequestState::Unknown );
    SW_EXPECT_EQUAL( 0u, queue.getPendingCount() );
}

/**
 * @brief [PathRequestQueueTest] 넘기기 전에 버린 요청은 워커에 가지 않고, 넘긴 뒤에 버린 요청의 결과는 수확에서 버린다(요청자가 사라진 경우)
 */
SW_TEST_CASE( PathRequestQueueTest, CancelledRequestsNeverDeliver )
{
    DoublingSolver solver;
    DoublingQueue  queue{ solver };

    const sw::PathRequestHandle beforeLaunch = queue.request( makeJob( 1 ) );
    queue.cancel( beforeLaunch );
    const sw::PathRequestHandle afterLaunch = queue.request( makeJob( 2 ) );
    queue.launch( "Test", nullptr );
    SW_EXPECT_EQUAL( 1u, solver._preparedCount ); // 버린 요청은 넘기지 않았다
    queue.cancel( afterLaunch );
    (void)queue.complete();

    DoublingJob job;
    SW_EXPECT_TRUE( queue.takeResult( beforeLaunch, job ) == sw::PathRequestState::Unknown );
    SW_EXPECT_TRUE( queue.takeResult( afterLaunch, job ) == sw::PathRequestState::Unknown );
    SW_EXPECT_EQUAL( uint64{ 0 }, queue.getCompletedCount() );
    // 버린 핸들을 다시 버려도 할 일이 없다.
    queue.cancel( afterLaunch );
    SW_EXPECT_EQUAL( 0u, queue.getPendingCount() );
}

/**
 * @brief [PathRequestQueueTest] 칸을 다시 써도 옛 핸들은 새 요청의 결과를 받지 못한다(세대)
 */
SW_TEST_CASE( PathRequestQueueTest, ReusedSlotRejectsOldHandle )
{
    DoublingSolver              solver;
    DoublingQueue               queue{ solver };
    const sw::PathRequestHandle first = queue.request( makeJob( 3 ) );
    queue.cancel( first );
    const sw::PathRequestHandle second = queue.request( makeJob( 4 ) );
    SW_EXPECT_EQUAL( first.index(), second.index() ); // 같은 칸을 다시 쓴다
    SW_EXPECT_TRUE( first.generation() != second.generation() );

    queue.launch( "Test", nullptr );
    (void)queue.complete();
    DoublingJob job;
    SW_EXPECT_TRUE( queue.takeResult( first, job ) == sw::PathRequestState::Unknown );
    SW_REQUIRE_TRUE( queue.takeResult( second, job ) == sw::PathRequestState::Ready );
    SW_EXPECT_EQUAL( 8, job._result );
}

/**
 * @brief [PathRequestQueueTest] 넘긴 일이 수확되지 않은 채 다시 넘기면 먼저 수확한다 — 비행 줄은 한 벌이고 결과는 잃지 않는다
 */
SW_TEST_CASE( PathRequestQueueTest, RelaunchHarvestsThePreviousBatch )
{
    DoublingSolver              solver;
    DoublingQueue               queue{ solver };
    const sw::PathRequestHandle first = queue.request( makeJob( 5 ) );
    queue.launch( "Test", nullptr );
    const sw::PathRequestHandle second = queue.request( makeJob( 6 ) );
    queue.launch( "Test", nullptr ); // 앞 묶음을 수확한 뒤 둘째를 넘긴다
    SW_EXPECT_TRUE( queue.getState( first ) == sw::PathRequestState::Ready );
    SW_EXPECT_TRUE( queue.getState( second ) == sw::PathRequestState::Pending );
    (void)queue.complete();
    SW_EXPECT_TRUE( queue.getState( second ) == sw::PathRequestState::Ready );
}

/**
 * @brief [PathRequestQueueTest] 비우면 줄에 선 · 구하는 중 · 받지 않은 요청이 모두 사라진다(씬을 비울 때)
 */
SW_TEST_CASE( PathRequestQueueTest, ClearDropsEveryRequest )
{
    DoublingSolver              solver;
    DoublingQueue               queue{ solver };
    const sw::PathRequestHandle ready = queue.request( makeJob( 7 ) );
    queue.launch( "Test", nullptr );
    (void)queue.complete();
    const sw::PathRequestHandle inFlight = queue.request( makeJob( 8 ) );
    queue.launch( "Test", nullptr );
    const sw::PathRequestHandle queued = queue.request( makeJob( 9 ) );

    (void)queue.clear();
    SW_EXPECT_TRUE( queue.getState( ready ) == sw::PathRequestState::Unknown );
    SW_EXPECT_TRUE( queue.getState( inFlight ) == sw::PathRequestState::Unknown );
    SW_EXPECT_TRUE( queue.getState( queued ) == sw::PathRequestState::Unknown );
    SW_EXPECT_EQUAL( 0u, queue.getPendingCount() );
    // 비운 뒤에도 줄은 다시 쓴다.
    const sw::PathRequestHandle fresh = queue.request( makeJob( 10 ) );
    queue.launch( "Test", nullptr );
    (void)queue.complete();
    DoublingJob job;
    SW_REQUIRE_TRUE( queue.takeResult( fresh, job ) == sw::PathRequestState::Ready );
    SW_EXPECT_EQUAL( 20, job._result );
}

/**
 * @brief [PathRequestQueueTest] 요청 수백 개를 한 묶음으로 넘겨도 핸들마다 자기 결과를 받는다(구간 나누기는 결과에 들지 않는다)
 */
SW_TEST_CASE( PathRequestQueueTest, EveryHandleGetsItsOwnResult )
{
    DoublingSolver                    solver;
    DoublingQueue                     queue{ solver };
    sw::vector<sw::PathRequestHandle> listHandle;
    for ( int32 query = 0; query < 500; ++query )
    {
        listHandle.push_back( queue.request( makeJob( query ) ) );
    }
    queue.launch( "Test", nullptr );
    (void)queue.complete();
    SW_EXPECT_TRUE( solver._chunkCount >= 1u );
    for ( size_t index = 0; index < listHandle.size(); ++index )
    {
        DoublingJob job;
        SW_REQUIRE_TRUE( queue.takeResult( listHandle[index], job ) == sw::PathRequestState::Ready );
        SW_EXPECT_EQUAL( static_cast<int32>( index ) * 2, job._result );
        SW_EXPECT_TRUE( job._chunkIndex < solver._chunkCount );
    }
}
