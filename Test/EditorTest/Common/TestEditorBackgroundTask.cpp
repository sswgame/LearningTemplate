#include "pch.h"

#include "Core/Task/TaskHandleTracker.h"
#include "Core/Task/TaskManager.h"

#include "Editor/Common/EditorBackgroundTask.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 테스트용 입력 — 워커가 실제로 받아 쓰는지 확인할 값 하나. */
    struct TestJobInput
    {
        int32 _seed{ 0 };
    };

    /**
     * @class TestBackgroundJob
     * @brief 워커를 흉내 내어 규약(세대·완료 플래그)만 동기적으로 검증하는 잡.
     * @details 실제 잡은 TaskManager 워커에서 runJob 을 돌리지만, 검증 대상은 스케줄링이 아니라
     *          잠금·세대 처리이므로 여기서는 워커 단계를 손으로 부른다.
     */
    class TestBackgroundJob final : public EditorBackgroundTask<TestJobInput, vector<int32>>
    {
    public:
        /** @brief 게임 스레드: 요청을 올리고 워커에 넘길 세대를 돌려줍니다. */
        uint32 request( int32 seed )
        {
            TestJobInput input;
            input._seed = seed;
            return beginRequest( input );
        }

        /** @brief 워커: 이 세대의 입력을 읽습니다. 낡은 세대면 false. */
        bool workerReadInput( uint32 generation, TestJobInput& outInput ) const
        {
            return readInput( _pState, generation, outInput );
        }

        /** @brief 워커: 이 세대의 결과를 싣습니다. */
        void workerPublish( uint32 generation, vector<int32> listResult )
        {
            publish( _pState, generation, std::move( listResult ) );
        }
    };

    /** @brief 테스트 태스크 본문 — 신호가 서기 전까지 돈다. */
    void waitForReleaseTask( const TaskArgs& args )
    {
        const shared_ptr<atomic<bool>> pRelease = args.get<shared_ptr<atomic<bool>>>( 0 );
        while ( pRelease->load() == false )
        {
            std::this_thread::yield();
        }
    }
} // namespace

/**
 * @brief [EditorBackgroundTaskTest] 요청 → 워커 발행 → 수거의 정상 흐름과 플래그 전이 검증
 */
SW_TEST_CASE( EditorBackgroundTaskTest, RequestPublishTakeRoundTrip )
{
    TestBackgroundJob job;

    vector<int32> taken;
    SW_EXPECT_FALSE( job.isPending() );
    SW_EXPECT_TRUE( job.take( taken ) == EditorTaskPoll::Idle );

    const uint32 generation = job.request( 7 );
    SW_EXPECT_TRUE( job.isPending() );
    // 아직 워커가 싣지 않았으므로 가져갈 것이 없고, 도는 중임이 같은 반환값으로 보인다.
    SW_EXPECT_TRUE( job.take( taken ) == EditorTaskPoll::Running );

    TestJobInput input;
    SW_ASSERT_TRUE( job.workerReadInput( generation, input ) );
    SW_EXPECT_EQUAL( 7, input._seed );

    job.workerPublish( generation, vector<int32>{ input._seed, input._seed * 2 } );
    SW_EXPECT_FALSE( job.isPending() );

    SW_ASSERT_TRUE( job.take( taken ) == EditorTaskPoll::Ready );
    SW_ASSERT_EQUAL( size_t( 2 ), taken.size() );
    SW_EXPECT_EQUAL( 7, taken[0] );
    SW_EXPECT_EQUAL( 14, taken[1] );

    // 같은 결과를 두 번 주지 않는다 — 가져간 뒤에는 쉬는 상태다.
    SW_EXPECT_TRUE( job.take( taken ) == EditorTaskPoll::Idle );
}

/**
 * @brief [EditorBackgroundTaskTest] 재요청이 낡은 세대의 워커 결과를 버리는지 검증
 * @details 폴더를 빠르게 옮겨 다니면 이전 스캔이 나중에 끝나 도착한다. 세대가 이것을 막지 못하면
 *          Content Browser 가 방금 연 폴더 대신 이전 폴더의 목록을 보여준다.
 */
SW_TEST_CASE( EditorBackgroundTaskTest, StaleGenerationResultIsDropped )
{
    TestBackgroundJob job;

    const uint32 staleGeneration = job.request( 1 );
    const uint32 freshGeneration = job.request( 2 );
    SW_EXPECT_TRUE( staleGeneration != freshGeneration );

    // 낡은 워커는 입력조차 읽지 못한다.
    TestJobInput staleInput;
    SW_EXPECT_FALSE( job.workerReadInput( staleGeneration, staleInput ) );

    // 늦게 도착한 낡은 결과는 버려지고, 요청은 여전히 대기 상태로 남는다.
    job.workerPublish( staleGeneration, vector<int32>{ 111 } );
    vector<int32> taken;
    SW_EXPECT_TRUE( job.take( taken ) == EditorTaskPoll::Running );
    SW_EXPECT_TRUE( job.isPending() );

    // 새 세대의 결과만 통과한다.
    TestJobInput freshInput;
    SW_ASSERT_TRUE( job.workerReadInput( freshGeneration, freshInput ) );
    SW_EXPECT_EQUAL( 2, freshInput._seed );

    job.workerPublish( freshGeneration, vector<int32>{ 222 } );
    SW_ASSERT_TRUE( job.take( taken ) == EditorTaskPoll::Ready );
    SW_ASSERT_EQUAL( size_t( 1 ), taken.size() );
    SW_EXPECT_EQUAL( 222, taken[0] );
}

/**
 * @brief [EditorBackgroundTaskTest] 재요청이 이전 결과를 지워 낡은 값이 새 요청에 섞이지 않는지 검증
 */
SW_TEST_CASE( EditorBackgroundTaskTest, RequestClearsPreviousResult )
{
    TestBackgroundJob job;

    const uint32 firstGeneration = job.request( 1 );
    job.workerPublish( firstGeneration, vector<int32>{ 42 } );

    // 가져가지 않은 채로 다시 요청하면 이전 결과는 사라진다.
    const uint32  secondGeneration = job.request( 2 );
    vector<int32> taken;
    SW_EXPECT_TRUE( job.take( taken ) == EditorTaskPoll::Running );
    SW_EXPECT_TRUE( job.isPending() );

    job.workerPublish( secondGeneration, vector<int32>{ 43 } );
    SW_ASSERT_TRUE( job.take( taken ) == EditorTaskPoll::Ready );
    SW_ASSERT_EQUAL( size_t( 1 ), taken.size() );
    SW_EXPECT_EQUAL( 43, taken[0] );
}

/**
 * @brief [EditorBackgroundTaskTest] 워커가 끝낸 결과를 꺼내기 전에는 '쉬는 상태' 로 보이지 않는지 검증
 * @details 결과 유무와 대기 여부를 따로 읽던 시절에는, 워커가 끝난 직후 "대기 아님" 이 보여 새 요청이 끝난 결과를 버렸다.
 *          `take` 한 번이 둘을 같은 락 안에서 읽으므로, 끝난 결과는 `Idle` 이 아니라 `Ready` 로 나와야 한다.
 */
SW_TEST_CASE( EditorBackgroundTaskTest, FinishedResultIsNeverReportedIdle )
{
    TestBackgroundJob job;

    const uint32 generation = job.request( 5 );
    job.workerPublish( generation, vector<int32>{ 9 } );

    // 워커가 끝나 isPending 은 false 지만, 결과가 아직 안 나갔으니 Idle 이 아니다.
    SW_EXPECT_FALSE( job.isPending() );
    vector<int32> taken;
    SW_ASSERT_TRUE( job.take( taken ) == EditorTaskPoll::Ready );
    SW_EXPECT_EQUAL( 9, taken[0] );
    SW_EXPECT_TRUE( job.take( taken ) == EditorTaskPoll::Idle );
}

/**
 * @brief [EditorBackgroundTaskTest] 종료 대기가 에디터가 낸 잡만 기다리는지 검증
 * @details `TaskManager::waitAll` 은 엔진의 영구 태스크까지 기다려 시간 제한에 걸렸다. 추적기는 올린 핸들만 보므로, 올리지 않은 태스크가 도는 중에도 돌아와야 한다.
 */
SW_TEST_CASE( EditorBackgroundTaskTest, JobTrackerWaitsOnlyForTrackedJobs )
{
    TaskManager manager;
    SW_ASSERT_TRUE( manager.initialize( 2 ) );

    const shared_ptr<atomic<bool>> pForeignRelease = sw::make_shared<atomic<bool>>( false ); // 에디터가 내지 않은 태스크(영구 태스크 흉내)
    TaskHandle                     foreign         = manager.emplaceTask( "TestForeign", SW_DELEGATE_FUNCTION( TaskArgsDelegate, waitForReleaseTask ), MakeTaskArgs( pForeignRelease ) );
    foreign.submit();

    const shared_ptr<atomic<bool>> pTrackedRelease = sw::make_shared<atomic<bool>>( false );
    TaskHandle                     tracked         = manager.emplaceTask( "TestTracked", SW_DELEGATE_FUNCTION( TaskArgsDelegate, waitForReleaseTask ), MakeTaskArgs( pTrackedRelease ) );

    TaskHandleTracker tracker;
    tracker.track( tracked );
    tracked.submit();

    // 올린 잡이 도는 동안은 시간이 다 되어 false 다.
    SW_EXPECT_EQUAL( 1u, tracker.getPendingCount() );
    SW_EXPECT_FALSE( tracker.waitIdle( 20, "TestTracker" ) );

    // 올린 잡이 끝나면, 올리지 않은 태스크가 아직 돌아도 true 다.
    pTrackedRelease->store( true );
    SW_EXPECT_TRUE( tracker.waitIdle( 5000, "TestTracker" ) );
    SW_EXPECT_EQUAL( 0u, tracker.getPendingCount() );
    SW_EXPECT_FALSE( foreign.isCompleted() );

    pForeignRelease->store( true );
    (void)manager.waitAll( 5000 );
    manager.shutdown();
}
