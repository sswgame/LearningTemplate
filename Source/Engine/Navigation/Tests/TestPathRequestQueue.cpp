// 비동기 경로 요청 줄(`PathRequestQueue`)의 순수 규칙 — 결과는 수확 뒤에만, 표를 놓으면 버려진다, 넘기기는 상한까지, 비우면 모두 사라진다, 결과는 요청 순서 · 구간과 상관없다.
// 타일 교체 순서는 형식이 지킨다 — 증표(`Fence` · `NavMeshWriteAccess`)는 줄 · 내비게이션 밖에서 만들 수 없다(아래 static_assert). 엔진 서비스 없이 부르므로 넘기기는 그 자리에서 푼다.
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

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

// 표는 이동만 된다 — 같은 요청을 둘이 쥐거나 핸들 값을 들고 다니다 취소를 빠뜨리는 길이 없다(감사 S-9).
static_assert( std::is_copy_constructible_v<DoublingQueue::Ticket> == false && std::is_copy_assignable_v<DoublingQueue::Ticket> == false, "a path request ticket is move-only" );
static_assert( std::is_nothrow_move_constructible_v<DoublingQueue::Ticket>, "tickets live in containers" );

namespace
{
    /** @brief 상한 없이 넘깁니다(이 파일의 테스트 대부분은 묶음 하나). */
    constexpr uint32 kAll = DoublingQueue::kNoLaunchLimit;
} // namespace

/**
 * @brief [PathRequestQueueTest] 결과는 넘기기(`launch`) 뒤가 아니라 수확(`complete`) 뒤에만 보인다 — 워커가 얼마나 빨랐는지와 상관없이 같은 자리에서 Ready 가 된다
 */
SW_TEST_CASE( PathRequestQueueTest, ResultIsReadyOnlyAfterComplete )
{
    DoublingSolver        solver;
    DoublingQueue         queue{ solver };
    DoublingQueue::Ticket ticket = queue.request( makeJob( 21 ) );
    SW_REQUIRE_TRUE( ticket.isValid() );
    SW_EXPECT_TRUE( queue.getState( ticket ) == sw::PathRequestState::Pending );

    queue.launch( "Test", nullptr, kAll );
    SW_EXPECT_EQUAL( 1u, solver._preparedCount );
    SW_EXPECT_TRUE( queue.getState( ticket ) == sw::PathRequestState::Pending ); // 그 자리에서 풀었어도 칸에는 아직 들지 않았다
    DoublingJob job;
    SW_EXPECT_TRUE( queue.takeResult( ticket, job ) == sw::PathRequestState::Pending );
    SW_EXPECT_TRUE( ticket.isValid() ); // 아직이면 표는 그대로다

    (void)queue.complete();
    SW_EXPECT_TRUE( queue.getState( ticket ) == sw::PathRequestState::Ready );
    SW_REQUIRE_TRUE( queue.takeResult( ticket, job ) == sw::PathRequestState::Ready );
    SW_EXPECT_EQUAL( 42, job._result );
    SW_EXPECT_EQUAL( uint64{ 1 }, queue.getCompletedCount() );
    // 받은 표는 비었다.
    SW_EXPECT_FALSE( ticket.isValid() );
    SW_EXPECT_TRUE( queue.takeResult( ticket, job ) == sw::PathRequestState::Unknown );
    SW_EXPECT_EQUAL( 0u, queue.getPendingCount() );
}

/**
 * @brief [PathRequestQueueTest] 넘기기 전에 버린 요청은 워커에 가지 않고, 넘긴 뒤에 버린 요청의 결과는 수확에서 버린다(요청자가 사라진 경우)
 */
SW_TEST_CASE( PathRequestQueueTest, CancelledRequestsNeverDeliver )
{
    DoublingSolver solver;
    DoublingQueue  queue{ solver };

    DoublingQueue::Ticket beforeLaunch = queue.request( makeJob( 1 ) );
    beforeLaunch.cancel();
    DoublingQueue::Ticket afterLaunch = queue.request( makeJob( 2 ) );
    queue.launch( "Test", nullptr, kAll );
    SW_EXPECT_EQUAL( 1u, solver._preparedCount ); // 버린 요청은 넘기지 않았다
    afterLaunch.cancel();
    (void)queue.complete();

    DoublingJob job;
    SW_EXPECT_TRUE( queue.takeResult( beforeLaunch, job ) == sw::PathRequestState::Unknown );
    SW_EXPECT_TRUE( queue.takeResult( afterLaunch, job ) == sw::PathRequestState::Unknown );
    SW_EXPECT_EQUAL( uint64{ 0 }, queue.getCompletedCount() );
    // 버린 표를 다시 버려도 할 일이 없다.
    afterLaunch.cancel();
    SW_EXPECT_EQUAL( 0u, queue.getPendingCount() );
}

/**
 * @brief [PathRequestQueueTest] 표를 놓으면(소멸 · 덮어쓰기) 요청이 버려지고 칸이 풀린다 — 핸들 값이면 요청자가 취소를 빠뜨린 칸이 수확 뒤에도 Ready 로 남아 칸을 끝까지 쥐었다(감사 S-9).
 *        놓인 칸은 다음 요청이 다시 쓴다.
 */
SW_TEST_CASE( PathRequestQueueTest, DroppedTicketFreesSlot )
{
    DoublingSolver solver;
    DoublingQueue  queue{ solver };
    {
        DoublingQueue::Ticket dropped = queue.request( makeJob( 3 ) );
        queue.launch( "Test", nullptr, kAll );
    } // 받지 않고 사라진다
    (void)queue.complete();
    SW_EXPECT_EQUAL( uint64{ 0 }, queue.getCompletedCount() ); // 수확에서 버렸다
    SW_EXPECT_EQUAL( 0u, queue.getPendingCount() );

    DoublingQueue::Ticket overwritten = queue.request( makeJob( 4 ) );
    overwritten                       = queue.request( makeJob( 5 ) ); // 앞 요청을 덮으면 버려진다
    const uint32 preparedBefore       = solver._preparedCount;         // 앞 구간의 넘기기는 누적 수에 이미 들었다
    queue.launch( "Test", nullptr, kAll );
    SW_EXPECT_EQUAL( preparedBefore + 1u, solver._preparedCount ); // 덮인 요청은 넘기지 않았다
    (void)queue.complete();
    DoublingJob job;
    SW_REQUIRE_TRUE( queue.takeResult( overwritten, job ) == sw::PathRequestState::Ready );
    SW_EXPECT_EQUAL( 10, job._result );
}

/**
 * @brief [PathRequestQueueTest] 한 번의 넘기기는 상한까지만 워커에 넘기고, 남은 요청은 다음 넘기기가 요청 순서대로 잇는다 — 몰린 요청이 한 프레임을 붙들던 결함(감사 S-9).
 *        상한 앞에서 버린 요청은 상한에 세지 않는다.
 */
SW_TEST_CASE( PathRequestQueueTest, LaunchStopsAtTheBudget )
{
    DoublingSolver                    solver;
    DoublingQueue                     queue{ solver };
    sw::vector<DoublingQueue::Ticket> listTicket;
    for ( int32 query = 0; query < 5; ++query )
    {
        listTicket.push_back( queue.request( makeJob( query ) ) );
    }
    listTicket[0].cancel();

    queue.launch( "Test", nullptr, 2 );
    SW_EXPECT_EQUAL( 2u, solver._preparedCount );
    (void)queue.complete();
    SW_EXPECT_TRUE( queue.getState( listTicket[1] ) == sw::PathRequestState::Ready );
    SW_EXPECT_TRUE( queue.getState( listTicket[2] ) == sw::PathRequestState::Ready );
    SW_EXPECT_TRUE( queue.getState( listTicket[3] ) == sw::PathRequestState::Pending );
    SW_EXPECT_EQUAL( 2u, queue.getPendingCount() );

    queue.launch( "Test", nullptr, 2 );
    (void)queue.complete();
    SW_EXPECT_TRUE( queue.getState( listTicket[4] ) == sw::PathRequestState::Ready );
    SW_EXPECT_EQUAL( 4u, solver._preparedCount );
    SW_EXPECT_EQUAL( 0u, queue.getPendingCount() );
}

SW_TEST_CASE( PathRequestQueueTest, RelaunchHarvestsThePreviousBatch )
{
    DoublingSolver        solver;
    DoublingQueue         queue{ solver };
    DoublingQueue::Ticket first = queue.request( makeJob( 5 ) );
    queue.launch( "Test", nullptr, kAll );
    DoublingQueue::Ticket second = queue.request( makeJob( 6 ) );
    queue.launch( "Test", nullptr, kAll ); // 앞 묶음을 수확한 뒤 둘째를 넘긴다
    SW_EXPECT_TRUE( queue.getState( first ) == sw::PathRequestState::Ready );
    SW_EXPECT_TRUE( queue.getState( second ) == sw::PathRequestState::Pending );
    (void)queue.complete();
    SW_EXPECT_TRUE( queue.getState( second ) == sw::PathRequestState::Ready );
}

/**
 * @brief [PathRequestQueueTest] 비우면 줄에 선 · 구하는 중 · 받지 않은 요청이 모두 사라진다(씬을 비울 때). 남은 표는 `Unknown` 이고 놓아도 할 일이 없다
 */
SW_TEST_CASE( PathRequestQueueTest, ClearDropsEveryRequest )
{
    DoublingSolver        solver;
    DoublingQueue         queue{ solver };
    DoublingQueue::Ticket ready = queue.request( makeJob( 7 ) );
    queue.launch( "Test", nullptr, kAll );
    (void)queue.complete();
    DoublingQueue::Ticket inFlight = queue.request( makeJob( 8 ) );
    queue.launch( "Test", nullptr, kAll );
    DoublingQueue::Ticket queued = queue.request( makeJob( 9 ) );

    (void)queue.clear();
    SW_EXPECT_TRUE( queue.getState( ready ) == sw::PathRequestState::Unknown );
    SW_EXPECT_TRUE( queue.getState( inFlight ) == sw::PathRequestState::Unknown );
    SW_EXPECT_TRUE( queue.getState( queued ) == sw::PathRequestState::Unknown );
    SW_EXPECT_EQUAL( 0u, queue.getPendingCount() );
    // 비운 뒤에도 줄은 다시 쓴다 — 옛 표의 칸을 새 요청이 다시 써도 옛 표는 새 결과를 받지 못한다(세대).
    DoublingQueue::Ticket fresh = queue.request( makeJob( 10 ) );
    queue.launch( "Test", nullptr, kAll );
    (void)queue.complete();
    DoublingJob job;
    SW_EXPECT_TRUE( queue.takeResult( ready, job ) == sw::PathRequestState::Unknown );
    SW_REQUIRE_TRUE( queue.takeResult( fresh, job ) == sw::PathRequestState::Ready );
    SW_EXPECT_EQUAL( 20, job._result );
}

/**
 * @brief [PathRequestQueueTest] 요청 수백 개를 한 묶음으로 넘겨도 표마다 자기 결과를 받는다(구간 나누기는 결과에 들지 않는다)
 */
SW_TEST_CASE( PathRequestQueueTest, EveryHandleGetsItsOwnResult )
{
    DoublingSolver                    solver;
    DoublingQueue                     queue{ solver };
    sw::vector<DoublingQueue::Ticket> listTicket;
    for ( int32 query = 0; query < 500; ++query )
    {
        listTicket.push_back( queue.request( makeJob( query ) ) );
    }
    queue.launch( "Test", nullptr, kAll );
    (void)queue.complete();
    SW_EXPECT_TRUE( solver._chunkCount >= 1u );
    for ( size_t index = 0; index < listTicket.size(); ++index )
    {
        DoublingJob job;
        SW_REQUIRE_TRUE( queue.takeResult( listTicket[index], job ) == sw::PathRequestState::Ready );
        SW_EXPECT_EQUAL( static_cast<int32>( index ) * 2, job._result );
        SW_EXPECT_TRUE( job._chunkIndex < solver._chunkCount );
    }
}
