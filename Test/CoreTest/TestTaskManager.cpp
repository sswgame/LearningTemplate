/**
 * @file TestTaskManager.cpp
 * @brief TaskManager — 실행·의존성·병렬 분할·스테이지·메인 스레드 친화도·취소·동시 제출.
 * @details **이 파일이 없었다.** 1,355 줄짜리 동시성 핵심인데 `Test/CoreTest` 에 전용 케이스가
 *          하나도 없었고, `EngineTest` 의 다른 주제(AssetStreaming · Audio · GpuScene)를 통해
 *          간접적으로만 돌고 있었다. 그래서 스케줄러 자체의 결함은 테스트로 잡히지 않았다 —
 *          실제로 `scheduleReadyTask` 의 `notify_one` 결함(2026-09-20)이 그렇게 지나갔다.
 *
 *          여기서는 **스케줄러가 지켜야 하는 약속**만 본다: 낸 일은 반드시 돌고, 의존성 순서가
 *          지켜지고, 병렬 분할이 빠짐없이 한 번씩 덮고, 스테이지 대기가 끝을 보장하고, 메인
 *          스레드 일감은 메인 스레드에서만 돌고, 취소된 일은 돌지 않는다.
 *
 * @note 모든 대기에 **타임아웃을 건다.** 스케줄러가 멈추는 결함이 회귀하면 이 바이너리가
 *       CTest 타임아웃(30초)까지 붙잡히는 대신 그 자리에서 진다.
 */
#include "pch.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/vector.h"
#include "Core/Task/TaskManager.h"

#include "TestFramework/TestFramework.h"

namespace
{
    /** @brief 테스트가 쓰는 최대 대기 시간. 정상 동작은 수 밀리초면 끝난다. */
    constexpr uint32 kWaitTimeoutMs = 5000;

    /** @brief 워커 수를 고정해 둔다 — 코어 수에 따라 답이 갈리면 안 되는 것만 본다. */
    constexpr uint32 kWorkerCount = 4;
} // namespace

/**
 * @brief [TaskManagerTest] 낸 일은 반드시 돌고 waitAll 이 그것을 보장한다
 */
SW_TEST_CASE( TaskManagerTest, SubmittedTaskRunsBeforeWaitAllReturns )
{
    sw::TaskManager manager;
    SW_ASSERT_TRUE( manager.initialize( kWorkerCount ) );

    sw::atomic<int32> ranCount{ 0 };
    for ( int32 index = 0; index < 32; ++index )
    {
        sw::TaskHandle handle = manager.emplaceTask( "Unit", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&ranCount]()
        {
            ranCount.fetch_add( 1, std::memory_order_relaxed );
        } ) );
        handle.submit();
    }

    SW_EXPECT_TRUE( manager.waitAll( kWaitTimeoutMs ) );
    SW_EXPECT_EQUAL( 32, ranCount.load() );

    manager.shutdown();
}

/**
 * @brief [TaskManagerTest] precede 로 건 순서가 지켜진다
 * @details 후속 태스크는 선행이 **끝난 뒤에만** 돌아야 한다. 순서를 직접 보려고 선행이 깃발을
 *          세우고 후속이 그 깃발을 확인한다.
 */
SW_TEST_CASE( TaskManagerTest, PrecedeKeepsTheDependencyOrder )
{
    sw::TaskManager manager;
    SW_ASSERT_TRUE( manager.initialize( kWorkerCount ) );

    sw::atomic<bool> bFirstDone{ false };
    sw::atomic<bool> bSecondSawFirst{ false };
    sw::atomic<bool> bSecondRan{ false };

    sw::TaskHandle first = manager.emplaceTask( "First", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&bFirstDone]()
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
        bFirstDone.store( true, std::memory_order_release );
    } ) );

    sw::TaskHandle second = manager.emplaceTask( "Second", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&bFirstDone, &bSecondSawFirst, &bSecondRan]()
    {
        bSecondSawFirst.store( bFirstDone.load( std::memory_order_acquire ), std::memory_order_release );
        bSecondRan.store( true, std::memory_order_release );
    } ) );

    first.precede( second );
    first.submit();
    second.submit();

    SW_EXPECT_TRUE( manager.waitAll( kWaitTimeoutMs ) );
    SW_EXPECT_TRUE( bSecondRan.load() );
    SW_EXPECT_TRUE( bSecondSawFirst.load() );

    manager.shutdown();
}

/**
 * @brief [TaskManagerTest] 병렬 분할이 모든 인덱스를 **정확히 한 번씩** 덮는다
 * @details 빠뜨리면 조용히 일부가 계산되지 않고, 겹치면 같은 자리를 두 번 쓴다. 둘 다 결과가
 *          "대체로 맞아" 보이므로 인덱스별 횟수를 세어야 드러난다.
 */
SW_TEST_CASE( TaskManagerTest, ParallelCoversEveryIndexExactlyOnce )
{
    sw::TaskManager manager;
    SW_ASSERT_TRUE( manager.initialize( kWorkerCount ) );

    constexpr uint32              kCount = 1000;
    sw::vector<sw::atomic<int32>> listHit( kCount );

    // **워커 안에서는 `sw::vector` 를 첨자로 만지지 않는다.** `operator[]` 에 레이스 탐지기가
    // 붙어 있어서, 여러 워커가 동시에 들어오면 그것이 먼저 울린다(진짜 레이스는 없다 —
    // 원소가 원자라 원소별 접근은 안전하다). 시작 전에 주소만 한 번 받아 둔다.
    sw::atomic<int32>* pHit = listHit.data();

    sw::TaskHandle handle = manager.emplaceParallel( "Cover", kCount,
                                                     SW_DELEGATE_LAMBDA( sw::ParallelTaskDelegate, [pHit]( uint32 index )
    {
        pHit[index].fetch_add( 1, std::memory_order_relaxed );
    } ) );
    handle.submit();

    SW_EXPECT_TRUE( manager.waitAll( kWaitTimeoutMs ) );

    uint32 missedCount{ 0 };
    uint32 doubledCount{ 0 };
    for ( uint32 index = 0; index < kCount; ++index )
    {
        const int32 hitCount = pHit[index].load();
        if ( hitCount == 0 )
            ++missedCount;
        else if ( hitCount > 1 )
            ++doubledCount;
    }
    SW_EXPECT_EQUAL( 0u, missedCount );
    SW_EXPECT_EQUAL( 0u, doubledCount );

    manager.shutdown();
}

/**
 * @brief [TaskManagerTest] 블록 분할도 범위를 빠짐없이 한 번씩 덮는다
 * @details 청크 경계는 `[start, end)` 라 끝을 하나 더 세거나 덜 세기 쉬운 자리다.
 */
SW_TEST_CASE( TaskManagerTest, ParallelBlockCoversTheWholeRangeExactlyOnce )
{
    sw::TaskManager manager;
    SW_ASSERT_TRUE( manager.initialize( kWorkerCount ) );

    constexpr uint32              kBeginIndex = 7;
    constexpr uint32              kEndIndex   = 1007;
    sw::vector<sw::atomic<int32>> listHit( kEndIndex );

    // 위 케이스와 같은 이유로 주소를 미리 받는다(`operator[]` 의 레이스 탐지기를 피한다).
    sw::atomic<int32>* pHit = listHit.data();

    sw::TaskHandle handle = manager.emplaceParallelBlock( kBeginIndex, kEndIndex,
                                                          SW_DELEGATE_LAMBDA( sw::ParallelBlockDelegate, [pHit]( uint32 start, uint32 end )
    {
        for ( uint32 index = start; index < end; ++index )
            pHit[index].fetch_add( 1, std::memory_order_relaxed );
    } ) );
    handle.submit();

    SW_EXPECT_TRUE( manager.waitAll( kWaitTimeoutMs ) );

    uint32 wrongCount{ 0 };
    for ( uint32 index = 0; index < kEndIndex; ++index )
    {
        const int32 expectedCount = ( index >= kBeginIndex ) ? 1 : 0;
        if ( pHit[index].load() != expectedCount )
            ++wrongCount;
    }
    SW_EXPECT_EQUAL( 0u, wrongCount );

    manager.shutdown();
}

/**
 * @brief [TaskManagerTest] 스테이지 대기는 그 스테이지의 일이 모두 끝난 뒤에 돌아온다
 */
SW_TEST_CASE( TaskManagerTest, WaitStageReturnsOnlyAfterEveryStageTaskIsDone )
{
    sw::TaskManager manager;
    SW_ASSERT_TRUE( manager.initialize( kWorkerCount ) );

    constexpr int32   kTaskCount = 24;
    sw::atomic<int32> doneCount{ 0 };

    sw::TaskStageHandle stage = manager.createAnonymousStage( "UnitStage" );
    for ( int32 index = 0; index < kTaskCount; ++index )
    {
        sw::TaskHandle handle = manager.emplaceTask( "StageWork", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&doneCount]()
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
            doneCount.fetch_add( 1, std::memory_order_release );
        } ) );
        stage.addTask( handle );
        handle.submit();
    }

    manager.waitStage( stage );

    // 돌아온 시점에 **이미** 다 끝나 있어야 한다 — 여기서 더 기다리지 않는다.
    SW_EXPECT_EQUAL( kTaskCount, doneCount.load( std::memory_order_acquire ) );
    SW_EXPECT_TRUE( manager.isStageComplete( stage ) );

    manager.waitAll( kWaitTimeoutMs );
    manager.shutdown();
}

/**
 * @brief [TaskManagerTest] 메인 스레드 친화도 일감은 메인 스레드에서만, dispatch 로만 돈다
 * @details 워커가 집어가면 그 일감이 기대한 스레드 규약(렌더 컨텍스트 · UI 등)이 깨진다.
 *          dispatch 를 부르기 전에는 돌지 않아야 하고, 돈 스레드가 메인이어야 한다.
 */
SW_TEST_CASE( TaskManagerTest, MainThreadTaskRunsOnlyOnTheMainThread )
{
    sw::TaskManager manager;
    SW_ASSERT_TRUE( manager.initialize( kWorkerCount ) );

    sw::atomic<bool> bRan{ false };
    sw::atomic<bool> bRanOnMainThread{ false };

    sw::TaskHandle handle = manager.emplaceTask( "MainOnly",
                                                 SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&manager, &bRan, &bRanOnMainThread]()
    {
        bRanOnMainThread.store( manager.isMainThread(), std::memory_order_release );
        bRan.store( true, std::memory_order_release );
    } ),
                                                 sw::TaskThreadAffinity::MainThread );
    handle.submit();

    // 워커들이 집어갈 틈을 준다 — 그래도 돌면 안 된다.
    std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
    SW_EXPECT_FALSE( bRan.load( std::memory_order_acquire ) );

    manager.dispatchMainThreadTasks();

    SW_EXPECT_TRUE( bRan.load( std::memory_order_acquire ) );
    SW_EXPECT_TRUE( bRanOnMainThread.load( std::memory_order_acquire ) );

    manager.waitAll( kWaitTimeoutMs );
    manager.shutdown();
}

/**
 * @brief [TaskManagerTest] 취소한 일은 돌지 않고, 그래도 waitAll 이 돌아온다
 * @details 취소가 카운터를 어긋나게 하면 `waitAll` 이 영영 돌아오지 않는다 — 그쪽이 더 큰
 *          문제라 "안 돌았다" 와 "그래도 끝났다" 를 함께 본다.
 */
SW_TEST_CASE( TaskManagerTest, CancelledTaskDoesNotRunAndStillCompletes )
{
    sw::TaskManager manager;
    SW_ASSERT_TRUE( manager.initialize( kWorkerCount ) );

    sw::atomic<bool> bRan{ false };
    sw::TaskHandle   handle = manager.emplaceTask( "Cancelled", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&bRan]()
      {
        bRan.store( true, std::memory_order_release );
    } ) );

    SW_EXPECT_TRUE( handle.cancel() );
    SW_EXPECT_TRUE( handle.isCancelled() );
    handle.submit();

    SW_EXPECT_TRUE( manager.waitAll( kWaitTimeoutMs ) );
    SW_EXPECT_FALSE( bRan.load( std::memory_order_acquire ) );

    manager.shutdown();
}

/**
 * @brief [TaskManagerTest] whenAll 은 모든 선행이 끝난 뒤 한 번만 돈다
 */
SW_TEST_CASE( TaskManagerTest, WhenAllRunsOnceAfterEveryDependency )
{
    sw::TaskManager manager;
    SW_ASSERT_TRUE( manager.initialize( kWorkerCount ) );

    constexpr int32   kDependencyCount = 8;
    sw::atomic<int32> finishedCount{ 0 };
    sw::atomic<int32> continuationCount{ 0 };
    sw::atomic<int32> seenAtContinuation{ -1 };

    sw::vector<sw::TaskHandle> listTask;
    listTask.reserve( kDependencyCount );
    for ( int32 index = 0; index < kDependencyCount; ++index )
    {
        listTask.push_back( manager.emplaceTask( "Dep", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&finishedCount]()
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
            finishedCount.fetch_add( 1, std::memory_order_release );
        } ) ) );
    }

    sw::TaskHandle joined = manager.whenAll( listTask,
                                             SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&finishedCount, &continuationCount, &seenAtContinuation]()
    {
        seenAtContinuation.store( finishedCount.load( std::memory_order_acquire ), std::memory_order_release );
        continuationCount.fetch_add( 1, std::memory_order_release );
    } ) );

    for ( sw::TaskHandle& task : listTask )
        task.submit();
    joined.submit();

    SW_EXPECT_TRUE( manager.waitAll( kWaitTimeoutMs ) );
    SW_EXPECT_EQUAL( 1, continuationCount.load() );
    SW_EXPECT_EQUAL( kDependencyCount, seenAtContinuation.load() );

    manager.shutdown();
}

/**
 * @brief [TaskManagerTest] 태스크 안에서 다른 스테이지를 기다려도 멈추지 않는다
 * @details 워커가 대기에 들어가면 그 워커가 처리했어야 할 일감이 갈 곳을 잃는다 — 그래서
 *          `waitStage` 는 자면서 기다리지 않고 **다른 일을 도와 실행한다**(work helping).
 *          그 장치가 빠지면 워커 수보다 깊게 중첩되는 순간 전부 서로를 기다린다.
 */
SW_TEST_CASE( TaskManagerTest, NestedWaitInsideATaskDoesNotStall )
{
    sw::TaskManager manager;
    SW_ASSERT_TRUE( manager.initialize( 2 ) ); // 워커보다 깊게 중첩시키려고 일부러 적게 잡는다

    constexpr int32   kOuterCount = 4;
    sw::atomic<int32> innerDoneCount{ 0 };

    sw::TaskStageHandle outerStage = manager.createAnonymousStage( "Outer" );
    for ( int32 outerIndex = 0; outerIndex < kOuterCount; ++outerIndex )
    {
        sw::TaskHandle outer = manager.emplaceTask( "Outer", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&manager, &innerDoneCount]()
        {
            sw::TaskStageHandle innerStage = manager.createAnonymousStage( "Inner" );
            for ( int32 innerIndex = 0; innerIndex < 3; ++innerIndex )
            {
                sw::TaskHandle inner = manager.emplaceTask( "Inner", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&innerDoneCount]()
                {
                    innerDoneCount.fetch_add( 1, std::memory_order_release );
                } ) );
                innerStage.addTask( inner );
                inner.submit();
            }
            manager.waitStage( innerStage );
        } ) );
        outerStage.addTask( outer );
        outer.submit();
    }

    manager.waitStage( outerStage );
    SW_EXPECT_EQUAL( kOuterCount * 3, innerDoneCount.load( std::memory_order_acquire ) );

    manager.waitAll( kWaitTimeoutMs );
    manager.shutdown();
}

/**
 * @brief [TaskManagerTest] 여러 스레드가 동시에 밀어 넣어도 하나도 잃지 않는다
 * @details 워커 로컬 큐가 가득 차면 전역 큐로 흘려보내는 경로가 있다(`WorkStealingDeque::push`
 *          는 늘리지 않고 false 를 돌려준다). 그 흘려보내기가 새면 일감이 조용히 사라진다.
 */
SW_TEST_CASE( TaskManagerTest, ConcurrentSubmitLosesNothing )
{
    sw::TaskManager manager;
    SW_ASSERT_TRUE( manager.initialize( kWorkerCount ) );

    constexpr int32   kProducerCount = 4;
    constexpr int32   kPerProducer   = 500;
    sw::atomic<int32> ranCount{ 0 };

    sw::vector<std::thread> listProducer;
    listProducer.reserve( kProducerCount );
    for ( int32 producerIndex = 0; producerIndex < kProducerCount; ++producerIndex )
    {
        listProducer.emplace_back( [&manager, &ranCount]()
        {
            for ( int32 index = 0; index < kPerProducer; ++index )
            {
                sw::TaskHandle handle = manager.emplaceTask( "Flood", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&ranCount]()
                {
                    ranCount.fetch_add( 1, std::memory_order_relaxed );
                } ) );
                handle.submit();
            }
        } );
    }
    for ( std::thread& producer : listProducer )
        producer.join();

    SW_EXPECT_TRUE( manager.waitAll( kWaitTimeoutMs ) );
    SW_EXPECT_EQUAL( kProducerCount * kPerProducer, ranCount.load() );

    manager.shutdown();
}
