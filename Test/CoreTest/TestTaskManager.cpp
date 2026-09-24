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
#include "Core/Memory/MemoryProfiler.h"
#include "Core/Task/TaskManager.h"

#include "TestFramework/TestFramework.h"

namespace
{
    /** @brief 테스트가 쓰는 최대 대기 시간. 정상 동작은 수 밀리초면 끝난다. */
    constexpr uint32 kWaitTimeoutMs = 5000;

    /** @brief 워커 수를 고정해 둔다 — 코어 수에 따라 답이 갈리면 안 되는 것만 본다. */
    constexpr uint32 kWorkerCount = 4;

    /** @brief 누수 탐침이 한 번에 붙드는 스테이지 수. 풀이 재사용하지 못하게 전부 동시에 쥔다. */
    constexpr uint32 kLeakProbeStageCount = 32;

    /** @brief 모두 깨우기 탐침의 워커 수. 워커가 많을수록 다시 잠든 워커를 쫓던 예전 결함이 잘 드러난다. */
    constexpr uint32 kWakeProbeWorkerCount = 12;

    /** @brief 모두 깨우기 탐침이 부르는 횟수. */
    constexpr uint32 kWakeProbeCallCount = 2000;

    /** @brief 모두 깨우기 탐침의 전체 시간 상한(밀리초). 정상 동작은 WSL 에서도 수백 밀리초 안에 끝난다. */
    constexpr int64 kWakeProbeLimitMilli = 5000;

    /**
     * @brief 매니저를 하나 세워 스테이지를 @p stageCount 개 동시에 쥐었다 놓고 부숩니다.
     * @details 동시에 쥐는 것이 핵심이다 — 하나씩 쥐었다 놓으면 풀이 같은 노드를 돌려써서 한 개만 난다.
     */
    void runStageLifetimeCycle( uint32 stageCount )
    {
        sw::TaskManager manager;
        if ( manager.initialize( kWorkerCount ) == false )
            return;
        {
            sw::vector<sw::TaskStageHandle> listStage;
            listStage.reserve( stageCount );
            for ( uint32 index = 0; index < stageCount; ++index )
            {
                listStage.push_back( manager.createStage() );
            }
        }
        manager.shutdown();
    }
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
 * @brief [TaskManagerTest] 이미 제출한 태스크에 선행을 더 걸어도 그 태스크는 한 번만 돈다
 * @details 의존성 수는 제출로 0 이 되어 큐에 가고, 뒤늦은 `precede` 가 수를 1 로 올린 뒤 선행이 끝나며 **다시** 0 을
 *          지난다. 노드의 "큐에 넣었다" 문(`_bScheduled`)이 그 두 번째를 막는다. 예전의 5단 상태(`TaskState`)가
 *          남아 있던 이유가 이 문 하나였다 — 상태를 걷어낸 뒤에도 문은 지켜야 한다.
 *
 *          선행은 **본문이 도는 동안** 건다. 본문이 끝난 뒤라면 완료 처리가 호출 대상을 이미 비워서, 문이 없어도 두 번째
 *          실행은 빈 본문을 돌 뿐이라 보이지 않는다(문을 빼는 돌연변이로 확인했다). 본문이 도는 동안이면 두 번째 실행이
 *          다른 워커에서 같은 본문을 동시에 돈다.
 */
SW_TEST_CASE( TaskManagerTest, PrecedeAfterSubmitDoesNotRunTheTaskTwice )
{
    sw::TaskManager manager;
    SW_ASSERT_TRUE( manager.initialize( kWorkerCount ) );

    sw::atomic<int32> lateRunCount{ 0 };
    sw::atomic<bool>  bLateStarted{ false };

    sw::TaskHandle late = manager.emplaceTask( "Late", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&lateRunCount, &bLateStarted]()
    {
        lateRunCount.fetch_add( 1, std::memory_order_relaxed );
        bLateStarted.store( true, std::memory_order_release );
        std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
    } ) );
    late.submit();

    const auto startTime = std::chrono::steady_clock::now();
    while ( bLateStarted.load( std::memory_order_acquire ) == false )
    {
        const bool bTimedOut = std::chrono::steady_clock::now() - startTime > std::chrono::milliseconds( kWaitTimeoutMs );
        SW_ASSERT_FALSE( bTimedOut );
        std::this_thread::yield();
    }

    sw::TaskHandle early = manager.emplaceTask( "Early", SW_DELEGATE_LAMBDA( sw::TaskDelegate, []() {} ) );
    early.precede( late );
    early.submit();

    SW_EXPECT_TRUE( manager.waitAll( kWaitTimeoutMs ) );
    SW_EXPECT_EQUAL( 1, lateRunCount.load() );

    manager.shutdown();
}

/**
 * @brief [TaskManagerTest] isCompleted 는 본문과 그 안에서 만든 자식이 모두 끝난 뒤에만 true 다
 * @details 제출 전 · 본문은 끝났지만 자식이 남은 동안 · 모두 끝난 뒤를 차례로 본다. 두 번째가 핵심이다 — 본문만 보고
 *          "끝났다" 고 하면 자식이 쓰는 중인 것을 호출자가 읽는다. 빈 핸들은 기다릴 것이 없으니 true 다.
 */
SW_TEST_CASE( TaskManagerTest, IsCompletedWaitsForTheBodyAndItsChildren )
{
    sw::TaskManager manager;
    SW_ASSERT_TRUE( manager.initialize( kWorkerCount ) );

    SW_EXPECT_TRUE( sw::TaskHandle{}.isCompleted() );

    sw::atomic<bool> bParentBodyDone{ false };
    sw::atomic<bool> bReleaseChild{ false };

    sw::TaskHandle parent = manager.emplaceTask( "Parent", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&manager, &bParentBodyDone, &bReleaseChild]()
    {
        sw::TaskHandle child = manager.emplaceTask( "Child", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&bReleaseChild]()
        {
            while ( bReleaseChild.load( std::memory_order_acquire ) == false )
            {
                std::this_thread::yield();
            }
        } ) );
        child.submit();
        bParentBodyDone.store( true, std::memory_order_release );
    } ) );
    SW_EXPECT_FALSE( parent.isCompleted() );
    parent.submit();

    const auto startTime = std::chrono::steady_clock::now();
    while ( bParentBodyDone.load( std::memory_order_acquire ) == false )
    {
        const bool bTimedOut = std::chrono::steady_clock::now() - startTime > std::chrono::milliseconds( kWaitTimeoutMs );
        if ( bTimedOut )
            break;
        std::this_thread::yield();
    }
    SW_EXPECT_TRUE( bParentBodyDone.load() );
    SW_EXPECT_FALSE( parent.isCompleted() );

    bReleaseChild.store( true, std::memory_order_release );
    SW_EXPECT_TRUE( manager.waitAll( kWaitTimeoutMs ) );
    SW_EXPECT_TRUE( parent.isCompleted() );

    manager.shutdown();
}

/**
 * @brief [TaskManagerTest] isInsideParallelTask 는 병렬 본문 안에서만 true 다
 * @details 병렬 본문에서 부르면 안 되는 함수의 단정이 기대는 값이다. 그래서 **나눠 돌든 한 번에 돌든** 같아야 한다 —
 *          문턱 아래의 `runParallel` 도 병렬 본문이다. 보통 태스크 본문과 호출 스레드의 전후는 밖이다.
 */
SW_TEST_CASE( TaskManagerTest, InsideParallelTaskIsTrueOnlyInParallelBodies )
{
    sw::TaskManager manager;
    SW_ASSERT_TRUE( manager.initialize( kWorkerCount ) );

    SW_EXPECT_FALSE( manager.isInsideParallelTask() );

    sw::atomic<uint32> insideCount{ 0 };
    sw::atomic<uint32> outsideCount{ 0 };

    const sw::ParallelBlockDelegate blockBody = SW_DELEGATE_LAMBDA( sw::ParallelBlockDelegate, [&manager, &insideCount, &outsideCount]( uint32 start, uint32 end )
    {
        if ( manager.isInsideParallelTask() )
            insideCount.fetch_add( end - start, std::memory_order_relaxed );
        else
            outsideCount.fetch_add( end - start, std::memory_order_relaxed );
    } );

    manager.runParallel( 4096, 16, blockBody ); // 나눠 돈다(호출 스레드도 청크를 집는다)
    manager.runParallel( 8, 16, blockBody );    // 문턱 아래 — 호출 스레드가 한 번에 돈다
    SW_EXPECT_FALSE( manager.isInsideParallelTask() );

    sw::TaskHandle indexed = manager.emplaceParallel( "InsideIndex", 256, SW_DELEGATE_LAMBDA( sw::ParallelTaskDelegate, [&manager, &insideCount, &outsideCount]( uint32 )
    {
        if ( manager.isInsideParallelTask() )
            insideCount.fetch_add( 1, std::memory_order_relaxed );
        else
            outsideCount.fetch_add( 1, std::memory_order_relaxed );
    } ) );
    indexed.submit();

    sw::atomic<int32> plainInside{ -1 };

    sw::TaskHandle plain = manager.emplaceTask( "Plain", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&manager, &plainInside]()
    {
        plainInside.store( manager.isInsideParallelTask() ? 1 : 0, std::memory_order_relaxed );
    } ) );
    plain.submit();

    SW_EXPECT_TRUE( manager.waitAll( kWaitTimeoutMs ) );
    SW_EXPECT_EQUAL( 4096u + 8u + 256u, insideCount.load() );
    SW_EXPECT_EQUAL( 0u, outsideCount.load() );
    SW_EXPECT_EQUAL( 0, plainInside.load() );

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

    sw::TaskStageHandle stage = manager.createStage();
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

    sw::TaskStageHandle outerStage = manager.createStage();
    for ( int32 outerIndex = 0; outerIndex < kOuterCount; ++outerIndex )
    {
        sw::TaskHandle outer = manager.emplaceTask( "Outer", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&manager, &innerDoneCount]()
        {
            sw::TaskStageHandle innerStage = manager.createStage();
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

/**
 * @brief [TaskManagerTest] 부서진 매니저는 자기 스테이지 노드를 돌려놓는다
 * @details 스테이지를 풀로 옮기면서 **풀 소멸자가 스테이지를 지우는 자리를 빠뜨렸다.** 스테이지는 재사용되므로
 *          돌고 있는 동안에는 새지 않고, 새는 것은 프로세스가 끝날 때다 — 그래서 윈도우에서는 아무 테스트도 지지
 *          않았고 리눅스 CI 의 LeakSanitizer 만 보고했다(`StageNode` + 태스크 목록 + 조건변수의 뮤텍스).
 *
 *          여기서는 살아 있는 할당 수로 같은 것을 본다: 매니저를 세워 스테이지를 여럿 **동시에** 쥐었다 부수는
 *          한 바퀴를 돌고, 그 전후의 살아 있는 할당 수가 제자리로 돌아오는지 본다. 첫 바퀴는 워밍업이다 —
 *          로거·스레드 런타임이 한 번만 잡는 것들을 재지 않기 위해서다.
 */
SW_TEST_CASE( TaskManagerTest, DestroyedManagerReturnsItsStageNodes )
{
    sw::MemoryProfiler* pMemory = sw::MemoryProfiler::getActive();
    SW_ASSERT_NOT_NULL( pMemory );

    runStageLifetimeCycle( kLeakProbeStageCount ); // 워밍업

    const bool bWasTracking = pMemory->isTrackingEnabled();
    pMemory->setTrackingEnabled( true );
    const uint64 before = pMemory->getLiveAllocationCount();
    runStageLifetimeCycle( kLeakProbeStageCount );
    const uint64 after = pMemory->getLiveAllocationCount();
    pMemory->setTrackingEnabled( bWasTracking );

    // 스테이지 하나가 새면 그 안의 목록·뮤텍스까지 따라 남으므로 새면 스테이지 수보다 크게 벌어진다.
    // 문턱을 스테이지 수로 둔 것은 로거가 아직 안 비운 메시지 같은 잡음을 결함으로 읽지 않기 위해서다.
    const uint64 leaked = after > before ? after - before : 0;
    SW_EXPECT_TRUE_MSG( leaked < kLeakProbeStageCount,
                        ( sw::string( "매니저를 부순 뒤에도 살아 있는 할당이 " ) + sw::to_string( leaked ) +
                          " 개 남았다 — 스테이지 " + sw::to_string( kLeakProbeStageCount ) + " 개를 돌려놓지 않았다" )
                            .c_str() );
}

/**
 * @brief [TaskManagerTest] 스테이지를 기다리다 잠든 메인 스레드는 메인 전용 일감이 생기면 깨어난다
 * @details 메인은 자기 워드 하나에 잠든다. 그 스테이지의 워커 태스크가 뒤늦게 `MainThread` 친화도 태스크를 같은
 *          스테이지에 넣으면, 그 일감은 메인만 돌릴 수 있으므로 **메인을 깨우지 않으면 둘 다 영원히 기다린다.**
 *          예전에 조건 변수의 `notify_one` 이 엉뚱한 스레드를 깨워 실제로 멈췄던 자리다 — 지금은 메인이 잠들기 전에
 *          자기 슬롯을 적어 두고 메인 일감을 넣는 쪽이 그 슬롯을 깨운다. 그 길이 끊기면 이 케이스는 CTest 타임아웃으로 진다.
 */
SW_TEST_CASE( TaskManagerTest, MainThreadTaskWakesParkedMainThread )
{
    sw::TaskManager manager;
    SW_ASSERT_TRUE( manager.initialize( kWorkerCount ) );

    sw::atomic<bool>    bMainTaskRan{ false };
    sw::atomic<bool>    bMainTaskRanOnMain{ false };
    sw::TaskStageHandle stage = manager.createStage();

    sw::TaskHandle worker = manager.emplaceTask( "LateMainSpawner", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&manager, &stage, &bMainTaskRan, &bMainTaskRanOnMain]()
    {
        // 메인이 스핀(수 us)을 지나 잠들 때까지 기다린 뒤에 메인 일감을 만든다.
        std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
        sw::TaskHandle mainTask = manager.emplaceTask( "LateMain",
                                                       SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&manager, &bMainTaskRan, &bMainTaskRanOnMain]()
        {
            bMainTaskRanOnMain.store( manager.isMainThread(), std::memory_order_release );
            bMainTaskRan.store( true, std::memory_order_release );
        } ),
                                                       sw::TaskThreadAffinity::MainThread );
        stage.addTask( mainTask );
        mainTask.submit();
    } ) );
    stage.addTask( worker );
    worker.submit();

    manager.waitStage( stage );

    SW_EXPECT_TRUE( bMainTaskRan.load( std::memory_order_acquire ) );
    SW_EXPECT_TRUE( bMainTaskRanOnMain.load( std::memory_order_acquire ) );

    SW_EXPECT_TRUE( manager.waitAll( kWaitTimeoutMs ) );
    manager.shutdown();
}

/**
 * @brief [TaskManagerTest] 워커 태스크 안의 runParallel 도 구간을 빠짐없이 한 번씩 덮고 돌아온다
 * @details 워커 안에서 부르면 티켓이 그 워커의 덱에 들어가고, 조인은 그 워커의 슬롯에 잠든다 — 메인에서 부를 때와
 *          다른 길이다. 워커가 하나뿐이어도(남이 훔쳐 갈 수 없어도) 자기 티켓을 스스로 집어 끝나야 한다.
 */
SW_TEST_CASE( TaskManagerTest, RunParallelInsideWorkerTaskCoversEveryIndex )
{
    for ( uint32 workerCount = 1; workerCount <= kWorkerCount; workerCount += 3 )
    {
        sw::TaskManager manager;
        SW_ASSERT_TRUE( manager.initialize( workerCount ) );

        constexpr uint32              kCount = 2048;
        sw::vector<sw::atomic<int32>> listHit( kCount );
        sw::atomic<int32>*            pHit = listHit.data();
        sw::atomic<bool>              bReturned{ false };

        sw::TaskHandle outer = manager.emplaceTask( "NestedParallel", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&manager, pHit, &bReturned]()
        {
            manager.runParallel( kCount, 1, SW_DELEGATE_LAMBDA( sw::ParallelBlockDelegate, [pHit]( uint32 start, uint32 end )
            {
                for ( uint32 index = start; index < end; ++index )
                    pHit[index].fetch_add( 1, std::memory_order_relaxed );
            } ) );
            bReturned.store( true, std::memory_order_release );
        } ) );
        outer.submit();

        SW_EXPECT_TRUE( manager.waitAll( kWaitTimeoutMs ) );
        SW_EXPECT_TRUE( bReturned.load( std::memory_order_acquire ) );

        uint32 wrongCount = 0;
        for ( uint32 index = 0; index < kCount; ++index )
        {
            if ( pHit[index].load() != 1 )
                ++wrongCount;
        }
        SW_EXPECT_EQUAL( 0u, wrongCount );

        manager.shutdown();
    }
}

/**
 * @brief [TaskManagerTest] 메인 친화도의 병렬 부모는 청크가 워커에서 다 돈 뒤 메인에서만 완료된다
 * @details 청크는 워커가 돌고(티켓), 부모 노드만 `MainThread` 다. 부모는 그룹이 끝나야 준비되고, 준비돼도 워커가
 *          집어가면 안 된다 — `dispatchMainThreadTasks` 를 부르기 전에는 스테이지가 끝나지 않아야 한다.
 */
SW_TEST_CASE( TaskManagerTest, ParallelParentWithMainAffinityCompletesOnMainOnly )
{
    sw::TaskManager manager;
    SW_ASSERT_TRUE( manager.initialize( kWorkerCount ) );

    constexpr uint32              kCount = 512;
    sw::vector<sw::atomic<int32>> listHit( kCount );
    sw::atomic<int32>*            pHit = listHit.data();

    sw::TaskStageHandle stage  = manager.createStage();
    sw::TaskHandle      handle = manager.emplaceParallel( "MainParentGroup", kCount,
                                                          SW_DELEGATE_LAMBDA( sw::ParallelTaskDelegate, [pHit]( uint32 index )
         {
        pHit[index].fetch_add( 1, std::memory_order_relaxed );
    } ),
                                                          sw::TaskThreadAffinity::MainThread );
    stage.addTask( handle );
    handle.submit();

    // 청크는 워커가 끝내지만 부모는 메인 큐에서 기다린다 — 여기서 스테이지가 끝나 있으면 워커가 부모를 집어간 것이다.
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds( 200 );
        bool       bAllHit  = false;
        while ( bAllHit == false && std::chrono::steady_clock::now() < deadline )
        {
            bAllHit = true;
            for ( uint32 index = 0; index < kCount; ++index )
            {
                if ( pHit[index].load( std::memory_order_acquire ) != 1 )
                {
                    bAllHit = false;
                    break;
                }
            }
            if ( bAllHit == false )
                std::this_thread::yield();
        }
        SW_EXPECT_TRUE( bAllHit );
    }
    std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    SW_EXPECT_FALSE( manager.isStageComplete( stage ) );

    manager.dispatchMainThreadTasks();
    SW_EXPECT_TRUE( manager.isStageComplete( stage ) );

    manager.waitStage( stage );
    SW_EXPECT_TRUE( manager.waitAll( kWaitTimeoutMs ) );
    manager.shutdown();
}

/**
 * @brief [TaskManagerTest] 모두 깨우기는 부른 순간 잠들어 있던 워커만 깨우고 돌아온다
 * @details 할 일 없이 깨어난 워커는 스핀(2 us) 뒤 곧바로 다시 잠든다. 예전 루프는 워커 하나를 깨울 때마다 유휴 마스크를
 *          새로 읽어, 그렇게 다시 잠든 워커를 또 깨웠다. 깨우기 시스템 호출이 그 스핀보다 느린 곳(WSL)에서는 마스크가 비는
 *          순간이 좀처럼 오지 않아 호출 한 번이 1~90 초를 돌았다(`TaskManagerBenchTest.SmallTaskThroughput` 이 CTest
 *          타임아웃에 걸린 이유다). 일감 없이 여러 번 불러 전체 시간이 상한 안인지 본다. 상한을 넘으면 그 자리에서 멈춘다.
 */
SW_TEST_CASE( TaskManagerTest, WakeAllDoesNotChaseWorkersThatSleepAgain )
{
    sw::TaskManager manager;
    SW_ASSERT_TRUE( manager.initialize( kWakeProbeWorkerCount ) );

    const auto start        = std::chrono::steady_clock::now();
    int64      elapsedMilli = 0;
    uint32     callCount    = 0;
    for ( ; callCount < kWakeProbeCallCount && elapsedMilli < kWakeProbeLimitMilli; ++callCount )
    {
        manager.wakeSleepingWorkers();
        elapsedMilli = std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() - start ).count();
    }
    SW_EXPECT_TRUE_MSG( elapsedMilli < kWakeProbeLimitMilli,
                        ( sw::string( "모두 깨우기 " ) + sw::to_string( callCount ) + " 번에 " + sw::to_string( elapsedMilli ) +
                          " ms 가 걸렸다 — 부른 뒤에 다시 잠든 워커를 쫓고 있다" )
                            .c_str() );

    manager.shutdown();
}
