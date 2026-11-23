// TaskManager — 실행 · 의존성 · 병렬 분할 · 스테이지 · 메인 스레드 친화도 · 취소 · 동시 제출 · 사이클 거부 · 태그 상속 · 비용 분할(스케줄러가 지킬 약속만 본다).
// 케이스마다 지역 매니저를 세운다(엔진의 매니저는 건드리지 않는다). `waitAll` 은 기한이 없다 — 멈추는 결함은 케이스 시한(`kDefaultCaseTimeoutSeconds`)이 이름과 함께 끊는다.
#include "pch.h"

#include "Core/Concurrency/Futex.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/vector.h"
#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/Task/TaskManager.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Testing/TestWait.h"
#include "Core/Testing/ThreadTestAccess.h"
#include "Core/Time/MonotonicClock.h"

#include <thread>

namespace
{
    /** @brief 테스트가 쓰는 최대 대기 시간. 정상 동작은 수 밀리초면 끝난다. */
    constexpr uint32 kWaitTimeoutMs = 5000;

    /** @brief 워커 수를 고정해 둔다 — 코어 수에 따라 답이 갈리면 안 되는 것만 본다. */
    constexpr uint32 kWorkerCount = 4;

    /** @brief 돌 때 `sequence` 의 다음 번호를 `outOrder` 에 적는 태스크를 만듭니다(순서를 번호로 본다). */
    sw::TaskHandle makeOrderedTask( sw::TaskManager& manager, const utf8* pName, sw::atomic<int32>& sequence, sw::atomic<int32>& outOrder )
    {
        return manager.emplaceTask( pName, SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&sequence, &outOrder]()
        {
            outOrder.store( sequence.fetch_add( 1 ) + 1, std::memory_order_release );
        } ) );
    }

    /** @brief 누수 탐침이 한 번에 붙드는 스테이지 수. 풀이 재사용하지 못하게 전부 동시에 쥔다. */
    constexpr uint32 kLeakProbeStageCount = 32;

    /** @brief 모두 깨우기 탐침의 워커 수. 워커가 많을수록 다시 잠든 워커를 쫓는 결함이 잘 드러난다. */
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

    /**
     * @brief `runParallelByCost` 가 본문을 부른 구간 [start, end) 를 부른 차례대로 적습니다.
     * @details 가벼운 일의 비용을 측정하는 시계(3 us 임계값)를 기록이 늘리지 않게 잠금 없이 고정 칸에 쓴다(`fetch_add` 한 번 + 저장 한 번). 나뉜 부분은 여러 스레드가 부른다.
     */
    struct CostCallRecorder
    {
        static constexpr uint32 kCapacity = 256;

        sw::atomic<uint32>                        _count{ 0 };
        std::array<sw::atomic<uint64>, kCapacity> _arrCall{};

        void record( uint32 start, uint32 end )
        {
            const uint32 slot = _count.fetch_add( 1, std::memory_order_relaxed );
            if ( slot < kCapacity )
                _arrCall[slot].store( ( static_cast<uint64>( start ) << 32 ) | end, std::memory_order_relaxed );
        }

        /** @brief 부른 차례의 구간 목록입니다(호출이 모두 끝난 뒤에 읽는다). */
        sw::vector<std::pair<uint32, uint32>> list() const
        {
            sw::vector<std::pair<uint32, uint32>> listCall;
            const uint32                          count = sw::MathUtil::min( _count.load(), kCapacity );
            for ( uint32 index = 0; index < count; ++index )
            {
                const uint64 packed = _arrCall[index].load();
                listCall.emplace_back( static_cast<uint32>( packed >> 32 ), static_cast<uint32>( packed & 0xFFFFFFFFu ) );
            }
            return listCall;
        }
    };

    /** @brief 부를 때마다 정해진 만큼 가는 가짜 시계입니다. 호출 스레드 하나만 부르므로 잠금이 필요 없다. */
    int64 gFakeClockNanos     = 0;
    int64 gFakeClockStepNanos = 1;

    int64 readFakeClock() noexcept
    {
        gFakeClockNanos += gFakeClockStepNanos;
        return gFakeClockNanos;
    }

    /** @brief 프로파일 훅으로 열린 구간 종류의 표지입니다(비트 = `TaskProfileZoneKind`) — 훅은 함수 포인터라 캡처가 없어 파일 정적 칸으로 받는다. */
    sw::atomic<uint32> s_startedZoneKindMask{ 0 };

    sw::TaskProfileZone markZoneStarted( const utf8*, sw::TaskProfileZoneKind kind )
    {
        s_startedZoneKindMask.fetch_or( 1u << static_cast<uint32>( kind ), std::memory_order_acq_rel );
        return {}; // 구간을 열지 않는다 — 닫는 쪽이 불리지 않는다
    }

    void ignoreZoneEnd( const sw::TaskProfileZone& )
    {
    }

    /** @brief 구간 표지를 남기는 훅입니다. 케이스는 표지를 비운 뒤 `TaskManager::bindProfileHook` 으로 걸고, 시간 대신 이 사건으로 순서를 맞춘다. */
    const sw::TaskProfileHook kZoneStartProbe{ &markZoneStarted, &ignoreZoneEnd };

    /** @brief @p kind 구간이 한 번이라도 열렸으면 true 입니다. */
    bool isZoneStarted( sw::TaskProfileZoneKind kind )
    {
        return ( s_startedZoneKindMask.load( std::memory_order_acquire ) & ( 1u << static_cast<uint32>( kind ) ) ) != 0;
    }

    /** @brief @p kind 구간이 열릴 때까지 이 스레드를 붙듭니다. 시한은 멈춘 실행을 잡는 그물일 뿐이고, 판정은 부르는 쪽이 `isZoneStarted` 로 한다. */
    void holdUntilZoneStarted( sw::TaskProfileZoneKind kind )
    {
        const sw::Deadline deadline = sw::Deadline::afterMilliseconds( kWaitTimeoutMs );
        while ( isZoneStarted( kind ) == false && deadline.isExpired() == false )
        {
            sw::ThreadUtil::yield();
        }
    }
} // namespace

/**
 * @brief 낸 일은 반드시 돌고 waitAll 이 그것을 보장한다
 */
SW_TEST_CASE( TaskManagerTest, SubmittedTaskRunsBeforeWaitAllReturns )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );

    sw::atomic<int32> ranCount{ 0 };
    for ( int32 index = 0; index < 32; ++index )
    {
        sw::TaskHandle handle = manager.emplaceTask( "Unit", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&ranCount]()
        {
            ranCount.fetch_add( 1, std::memory_order_relaxed );
        } ) );
        handle.submit();
    }

    manager.waitAll( "TaskManagerTest" );
    SW_EXPECT_EQUAL( 0u, manager.getActiveTaskCount() );
    SW_EXPECT_EQUAL( 32, ranCount.load() );

    manager.shutdown();
}

/**
 * @brief runBefore 로 건 순서가 지켜진다
 * @details 후속 태스크는 선행이 **끝난 뒤에만** 돌아야 한다. 순서를 직접 보려고 선행이 깃발을
 *          세우고 후속이 그 깃발을 확인한다.
 */
SW_TEST_CASE( TaskManagerTest, PrecedeKeepsTheDependencyOrder )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );

    sw::atomic<bool> bFirstDone{ false };
    sw::atomic<bool> bSecondSawFirst{ false };
    sw::atomic<bool> bSecondRan{ false };

    sw::TaskHandle first = manager.emplaceTask( "First", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&bFirstDone]()
    {
        sw::ThreadUtil::sleepForMilliseconds( 5 );
        bFirstDone.store( true, std::memory_order_release );
    } ) );

    sw::TaskHandle second = manager.emplaceTask( "Second", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&bFirstDone, &bSecondSawFirst, &bSecondRan]()
    {
        bSecondSawFirst.store( bFirstDone.load( std::memory_order_acquire ), std::memory_order_release );
        bSecondRan.store( true, std::memory_order_release );
    } ) );

    SW_REQUIRE_TRUE( first.runBefore( second ) );
    first.submit();
    second.submit();

    manager.waitAll( "TaskManagerTest" );
    SW_EXPECT_EQUAL( 0u, manager.getActiveTaskCount() );
    SW_EXPECT_TRUE( bSecondRan.load() );
    SW_EXPECT_TRUE( bSecondSawFirst.load() );

    manager.shutdown();
}

/**
 * @brief 이미 제출한 태스크에 선행을 더 걸어도 그 태스크는 한 번만 돈다
 * @details 의존성 수는 제출로 0 이 되어 큐에 가고, 뒤늦은 `runBefore` 가 수를 1 로 올린 뒤 선행이 끝나며 **다시** 0 을
 *          지난다. 노드의 "큐에 넣었다" 문(`_bScheduled`)이 그 두 번째를 막는다.
 *
 *          선행은 **본문이 도는 동안** 건다. 본문이 끝난 뒤라면 완료 처리가 호출 대상을 이미 비워서, 문이 없어도 두 번째
 *          실행은 빈 본문을 돌 뿐이라 보이지 않는다. 본문이 도는 동안이면 두 번째 실행이
 *          다른 워커에서 같은 본문을 동시에 돈다.
 */
SW_TEST_CASE( TaskManagerTest, PrecedeAfterSubmitDoesNotRunTheTaskTwice )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );

    sw::atomic<int32> lateRunCount{ 0 };
    sw::atomic<bool>  bLateStarted{ false };
    sw::atomic<bool>  bLateReleased{ false };

    // 본문은 문이 열릴 때까지 붙든다 — 선행을 거는 동안 본문이 돌고 있음을 잠의 길이가 아니라 문으로 보장한다
    // (예전에는 20 ms 를 잤다 — 메인이 그보다 늦게 깨면 본문이 이미 끝나 `runBefore` 가 졌다).
    sw::TaskHandle late = manager.emplaceTask( "Late", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&lateRunCount, &bLateStarted, &bLateReleased]()
    {
        lateRunCount.fetch_add( 1, std::memory_order_relaxed );
        bLateStarted.store( true, std::memory_order_release );
        (void)sw::test::waitUntil( [&bLateReleased]()
        { return bLateReleased.load( std::memory_order_acquire ); } );
    } ) );
    late.submit();

    SW_REQUIRE_TRUE( sw::test::waitUntil( [&bLateStarted]()
    { return bLateStarted.load( std::memory_order_acquire ); } ) );

    sw::TaskHandle early   = manager.emplaceTask( "Early", SW_DELEGATE_LAMBDA( sw::TaskDelegate, []() {} ) );
    const bool     bLinked = early.runBefore( late );
    early.submit();
    bLateReleased.store( true, std::memory_order_release ); // 실패해도 본문을 풀어 waitAll 이 돌아오게 한 뒤 단언한다
    SW_REQUIRE_TRUE( bLinked );

    manager.waitAll( "TaskManagerTest" );
    SW_EXPECT_EQUAL( 0u, manager.getActiveTaskCount() );
    SW_EXPECT_EQUAL( 1, lateRunCount.load() );

    manager.shutdown();
}

/**
 * @brief isCompleted 는 본문과 그 안에서 만든 자식이 모두 끝난 뒤에만 true 다
 * @details 제출 전 · 본문은 끝났지만 자식이 남은 동안 · 모두 끝난 뒤를 차례로 본다. 두 번째가 핵심이다 — 본문만 보고
 *          "끝났다" 고 하면 자식이 쓰는 중인 것을 호출자가 읽는다. 빈 핸들은 기다릴 것이 없으니 true 다.
 */
SW_TEST_CASE( TaskManagerTest, IsCompletedWaitsForTheBodyAndItsChildren )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );

    SW_EXPECT_TRUE( sw::TaskHandle{}.isCompleted() );

    sw::atomic<bool> bParentBodyDone{ false };
    sw::atomic<bool> bReleaseChild{ false };

    sw::TaskHandle parent = manager.emplaceTask( "Parent", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&manager, &bParentBodyDone, &bReleaseChild]()
    {
        sw::TaskHandle child = manager.emplaceTask( "Child", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&bReleaseChild]()
        {
            while ( bReleaseChild.load( std::memory_order_acquire ) == false )
            {
                sw::ThreadUtil::yield();
            }
        } ) );
        child.submit();
        bParentBodyDone.store( true, std::memory_order_release );
    } ) );
    SW_EXPECT_FALSE( parent.isCompleted() );
    parent.submit();

    const sw::Deadline waitDeadline = sw::Deadline::afterMilliseconds( kWaitTimeoutMs );
    while ( bParentBodyDone.load( std::memory_order_acquire ) == false )
    {
        const bool bTimedOut = waitDeadline.isExpired();
        if ( bTimedOut )
            break;
        sw::ThreadUtil::yield();
    }
    SW_EXPECT_TRUE( bParentBodyDone.load() );
    SW_EXPECT_FALSE( parent.isCompleted() );

    bReleaseChild.store( true, std::memory_order_release );
    manager.waitAll( "TaskManagerTest" );
    SW_EXPECT_EQUAL( 0u, manager.getActiveTaskCount() );
    SW_EXPECT_TRUE( parent.isCompleted() );

    manager.shutdown();
}

/**
 * @brief 병렬 분할이 모든 인덱스를 **정확히 한 번씩** 덮는다
 * @details 빠뜨리면 조용히 일부가 계산되지 않고, 겹치면 같은 자리를 두 번 쓴다. 둘 다 결과가
 *          "대체로 맞아" 보이므로 인덱스별 횟수를 세어야 드러난다.
 */
SW_TEST_CASE( TaskManagerTest, ParallelCoversEveryIndexExactlyOnce )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );

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

    manager.waitAll( "TaskManagerTest" );
    SW_EXPECT_EQUAL( 0u, manager.getActiveTaskCount() );

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
 * @brief 블록 분할도 범위를 빠짐없이 한 번씩 덮는다
 * @details 청크 경계는 `[start, end)` 라 끝을 하나 더 세거나 덜 세기 쉬운 자리다.
 */
SW_TEST_CASE( TaskManagerTest, ParallelBlockCoversTheWholeRangeExactlyOnce )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );

    constexpr uint32              kBeginIndex = 7;
    constexpr uint32              kEndIndex   = 1007;
    sw::vector<sw::atomic<int32>> listHit( kEndIndex );

    // 위 케이스와 같은 이유로 주소를 미리 받는다(`operator[]` 의 레이스 탐지기를 피한다).
    sw::atomic<int32>* pHit = listHit.data();

    sw::TaskHandle handle = manager.emplaceParallelBlock( kBeginIndex, kEndIndex,
                                                          SW_DELEGATE_LAMBDA( sw::ParallelBlockDelegate, [pHit]( uint32 start, uint32 end )
    {
        for ( uint32 index = start; index < end; ++index )
        {
            pHit[index].fetch_add( 1, std::memory_order_relaxed );
        }
    } ) );
    handle.submit();

    manager.waitAll( "TaskManagerTest" );
    SW_EXPECT_EQUAL( 0u, manager.getActiveTaskCount() );

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
 * @brief 스테이지 대기는 그 스테이지의 일이 모두 끝난 뒤에 돌아온다
 */
SW_TEST_CASE( TaskManagerTest, WaitStageReturnsOnlyAfterEveryStageTaskIsDone )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );

    constexpr int32   kTaskCount = 24;
    sw::atomic<int32> doneCount{ 0 };

    sw::TaskStageHandle stage = manager.createStage();
    for ( int32 index = 0; index < kTaskCount; ++index )
    {
        sw::TaskHandle handle = manager.emplaceTask( "StageJob", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&doneCount]()
        {
            sw::ThreadUtil::sleepForMilliseconds( 1 );
            doneCount.fetch_add( 1, std::memory_order_release );
        } ) );
        stage.addTask( handle );
        handle.submit();
    }

    manager.waitStage( stage );

    // 돌아온 시점에 **이미** 다 끝나 있어야 한다 — 여기서 더 기다리지 않는다.
    SW_EXPECT_EQUAL( kTaskCount, doneCount.load( std::memory_order_acquire ) );
    SW_EXPECT_TRUE( manager.isStageComplete( stage ) );

    manager.waitAll( "TaskManagerTest" );
    manager.shutdown();
}

/**
 * @brief 태스크는 스테이지 하나에만 완료를 알린다 — 다른 스테이지에 또 넣으면 거절하고, 두 스테이지가 기다려야 하면 뒤에 도는 빈 태스크를 쓴다
 * @details 예전에는 두 번째 스테이지가 첫 번째를 덮어 첫 스테이지의 남은 수가 영영 줄지 않았다(에디터 백그라운드 잡 — 정지 대기가 끝나지 않았다).
 */
SW_TEST_CASE( TaskManagerTest, TaskReportsToOneStageOnly )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );

    sw::TaskStageHandle firstStage  = manager.createStage();
    sw::TaskStageHandle secondStage = manager.createStage();
    sw::TaskHandle      job         = manager.emplaceTask( "StageJob", sw::TaskDelegate{} );
    firstStage.addTask( job );
    firstStage.addTask( job ); // 같은 스테이지에 두 번 — 남은 수는 하나다
    {
        SW_EXPECT_LOG( "Task.StageAlreadySet" ); // a task already in a stage must not be moved to another
        secondStage.addTask( job );
    }
    sw::TaskHandle marker = manager.emplaceTask( "StageMarker", sw::TaskDelegate{} );
    SW_REQUIRE_TRUE( marker.runAfter( job ) );
    secondStage.addTask( marker );
    marker.submit();
    job.submit();

    manager.waitStage( firstStage );
    manager.waitStage( secondStage );
    SW_EXPECT_TRUE( manager.isStageComplete( firstStage ) );
    SW_EXPECT_TRUE( manager.isStageComplete( secondStage ) );

    manager.waitAll( "TaskManagerTest" );
    manager.shutdown();
}

/**
 * @brief 메인 스레드 친화도 작업은 메인 스레드에서만, dispatch 로만 돈다
 * @details 워커가 집어가면 그 작업이 기대한 스레드 규약(렌더 컨텍스트 · UI 등)이 깨진다.
 *          dispatch 를 부르기 전에는 돌지 않아야 하고, 돈 스레드가 메인이어야 한다.
 */
SW_TEST_CASE( TaskManagerTest, MainThreadTaskRunsOnlyOnTheMainThread )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );

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
    sw::ThreadUtil::sleepForMilliseconds( 20 );
    SW_EXPECT_FALSE( bRan.load( std::memory_order_acquire ) );

    manager.dispatchMainThreadTasks();

    SW_EXPECT_TRUE( bRan.load( std::memory_order_acquire ) );
    SW_EXPECT_TRUE( bRanOnMainThread.load( std::memory_order_acquire ) );

    manager.waitAll( "TaskManagerTest" );
    manager.shutdown();
}

/**
 * @brief 취소한 일은 돌지 않고, 그래도 waitAll 이 돌아온다
 * @details 취소가 카운터를 어긋나게 하면 `waitAll` 이 영영 돌아오지 않는다 — 그쪽이 더 큰
 *          문제라 "안 돌았다" 와 "그래도 끝났다" 를 함께 본다.
 */
SW_TEST_CASE( TaskManagerTest, CancelledTaskDoesNotRunAndStillCompletes )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );

    sw::atomic<bool> bRan{ false };
    sw::TaskHandle   handle = manager.emplaceTask( "Cancelled", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&bRan]()
      {
        bRan.store( true, std::memory_order_release );
    } ) );

    SW_EXPECT_TRUE( handle.cancel() );
    SW_EXPECT_TRUE( handle.isCancelled() );
    handle.submit();

    manager.waitAll( "TaskManagerTest" );
    SW_EXPECT_EQUAL( 0u, manager.getActiveTaskCount() );
    SW_EXPECT_FALSE( bRan.load( std::memory_order_acquire ) );

    manager.shutdown();
}

/**
 * @brief whenAll 은 모든 선행이 끝난 뒤 한 번만 돈다
 */
SW_TEST_CASE( TaskManagerTest, WhenAllRunsOnceAfterEveryDependency )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );

    constexpr int32   kDependencyCount = 8;
    sw::atomic<int32> finishedCount{ 0 };
    sw::atomic<int32> continuationCount{ 0 };
    sw::atomic<int32> observedAtContinuation{ -1 };

    sw::vector<sw::TaskHandle> listTask;
    listTask.reserve( kDependencyCount );
    for ( int32 index = 0; index < kDependencyCount; ++index )
    {
        listTask.push_back( manager.emplaceTask( "Dep", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&finishedCount]()
        {
            sw::ThreadUtil::sleepForMilliseconds( 2 );
            finishedCount.fetch_add( 1, std::memory_order_release );
        } ) ) );
    }

    sw::TaskHandle joined = manager.whenAll( listTask,
                                             SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&finishedCount, &continuationCount, &observedAtContinuation]()
    {
        observedAtContinuation.store( finishedCount.load( std::memory_order_acquire ), std::memory_order_release );
        continuationCount.fetch_add( 1, std::memory_order_release );
    } ) );

    for ( sw::TaskHandle& task : listTask )
    {
        task.submit();
    }
    joined.submit();

    manager.waitAll( "TaskManagerTest" );
    SW_EXPECT_EQUAL( 0u, manager.getActiveTaskCount() );
    SW_EXPECT_EQUAL( 1, continuationCount.load() );
    SW_EXPECT_EQUAL( kDependencyCount, observedAtContinuation.load() );

    manager.shutdown();
}

/**
 * @brief 태스크 안에서 다른 스테이지를 기다려도 멈추지 않는다
 * @details 워커가 대기에 들어가면 그 워커가 처리했어야 할 작업이 갈 곳을 잃는다 — 그래서
 *          `waitStage` 는 자면서 기다리지 않고 **다른 일을 도와 실행한다**(work helping).
 *          그 장치가 빠지면 워커 수보다 깊게 중첩되는 순간 전부 서로를 기다린다.
 */
SW_TEST_CASE( TaskManagerTest, NestedWaitInsideATaskDoesNotStall )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( 2 ) ); // 워커보다 깊게 중첩시키려고 일부러 적게 잡는다

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

    manager.waitAll( "TaskManagerTest" );
    manager.shutdown();
}

/**
 * @brief 여러 스레드가 동시에 밀어 넣어도 하나도 잃지 않는다
 * @details 워커 로컬 큐가 가득 차면 전역 큐로 흘려보내는 경로가 있다(`WorkStealingDeque::push`
 *          는 늘리지 않고 false 를 돌려준다). 그 흘려보내기가 새면 작업이 조용히 사라진다.
 */
SW_TEST_CASE( TaskManagerTest, ConcurrentSubmitLosesNothing )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );

    constexpr int32   kProducerCount = 4;
    constexpr int32   kPerProducer   = 500;
    sw::atomic<int32> ranCount{ 0 };

    sw::vector<sw::thread> listProducer;
    listProducer.reserve( kProducerCount );
    for ( int32 producerIndex = 0; producerIndex < kProducerCount; ++producerIndex )
    {
        listProducer.emplace_back( sw::ThreadTestAccess::getKey(), [&manager, &ranCount]()
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
    for ( sw::thread& producer : listProducer )
    {
        producer.join();
    }

    manager.waitAll( "TaskManagerTest" );
    SW_EXPECT_EQUAL( 0u, manager.getActiveTaskCount() );
    SW_EXPECT_EQUAL( kProducerCount * kPerProducer, ranCount.load() );

    manager.shutdown();
}

/**
 * @brief 부서진 매니저는 자기 스테이지 노드를 돌려놓는다
 * @details 풀 소멸자가 스테이지를 지워야 한다. 스테이지는 재사용되므로 돌고 있는 동안에는 새지 않고, 새는 것은
 *          프로세스가 끝날 때다 — 그래서 빠뜨려도 윈도우에서는 아무 테스트도 지지 않고 리눅스 CI 의 LeakSanitizer 만
 *          보고한다(`StageNode` + 태스크 목록 + 조건변수의 뮤텍스).
 *
 *          여기서는 살아 있는 할당 수로 같은 것을 본다: 매니저를 세워 스테이지를 여럿 **동시에** 쥐었다 부수는
 *          한 바퀴를 돌고, 그 전후의 살아 있는 할당 수가 제자리로 돌아오는지 본다. 첫 바퀴는 워밍업이다 —
 *          로거·스레드 런타임이 한 번만 잡는 것들을 측정하지 않기 위해서다.
 */
SW_TEST_CASE( TaskManagerTest, DestroyedManagerReturnsItsStageNodes )
{
    sw::MemoryProfiler* pMemory = sw::MemoryProfiler::getActive();
    SW_REQUIRE_NOT_NULL( pMemory );

    runStageLifetimeCycle( kLeakProbeStageCount ); // 워밍업

    const bool bWasTracking = pMemory->isTrackingEnabled();
    pMemory->setTrackingEnabled( true );
    const uint64 before = pMemory->getLiveAllocationCount();
    runStageLifetimeCycle( kLeakProbeStageCount );
    const uint64 after = pMemory->getLiveAllocationCount();
    pMemory->setTrackingEnabled( bWasTracking );

    // 스테이지 하나가 새면 그 안의 목록·뮤텍스까지 따라 남으므로 새면 스테이지 수보다 크게 벌어진다.
    // 임계값을 스테이지 수로 둔 것은 로거가 아직 안 비운 메시지 같은 잡음을 결함으로 읽지 않기 위해서다.
    const uint64                                     leaked = after > before ? after - before : 0;
    sw::StringBuilder<sw::constant::buffer::kMax256> textBuilder;
    textBuilder.appendFormat( "%# live allocations remain after the manager is destroyed - %# stages were not returned", leaked, kLeakProbeStageCount );
    SW_EXPECT_TRUE_MSG( leaked < kLeakProbeStageCount,
                        sw::string( textBuilder.view() ) );
}

/**
 * @brief 매니저보다 오래 산 태스크 · 스테이지 핸들은 그 뒤에도 노드를 읽고 놓을 수 있고, 마지막 핸들이 풀을 돌려놓는다
 * @details 핸들의 노드는 매니저의 풀에 있다. 풀이 매니저와 함께 사라지면 뒤늦게 놓이는 핸들이 해제된 메모리의 참조 수를 내린다
 *          (핸들을 든 씬이 매니저보다 늦게 내려가는 순서). 그래서 풀은 핸들이 남아 있는 동안 살고, 마지막 핸들이 놓일 때 사라져야 한다 —
 *          매니저가 사라질 때 풀이 사라지면 "핸들을 놓을 때 줄어드는 할당" 이 0 이다.
 */
SW_TEST_CASE( TaskManagerTest, HandlesOutlivingTheManagerKeepTheirNodes )
{
    sw::MemoryProfiler* pMemory = sw::MemoryProfiler::getActive();
    SW_REQUIRE_NOT_NULL( pMemory );
    const bool bWasTracking = pMemory->isTrackingEnabled();
    pMemory->setTrackingEnabled( true );

    sw::TaskHandle      task;
    sw::TaskStageHandle stage;
    {
        sw::TaskManager manager;
        SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );
        task = manager.emplaceTask( "OutlivesManager", SW_DELEGATE_LAMBDA( sw::TaskDelegate, []() {} ) );
        task.setPriority( sw::TaskPriority::High );
        stage = manager.createStage();
        stage.addTask( task );
        task.submit();
        manager.waitStage( stage );
        manager.waitAll( "TaskManagerTest" );
        SW_EXPECT_EQUAL( 0u, manager.getActiveTaskCount() );
    }

    SW_EXPECT_TRUE_MSG( task.isCompleted(), "매니저가 사라진 뒤 핸들의 노드를 읽을 수 없습니다" );
    SW_EXPECT_TRUE_MSG( task.getPriority() == sw::TaskPriority::High, "매니저가 사라진 뒤 핸들의 노드를 읽을 수 없습니다" );
    const uint64 heldCount     = pMemory->getLiveAllocationCount();
    task                       = sw::TaskHandle{};
    stage                      = sw::TaskStageHandle{};
    const uint64 releasedCount = pMemory->getLiveAllocationCount();
    pMemory->setTrackingEnabled( bWasTracking );

    // 풀 하나 · 태스크 슬랩 하나 · 스테이지 하나가 마지막 핸들과 함께 돌아가야 한다.
    const uint64                                     returnedCount = heldCount > releasedCount ? heldCount - releasedCount : 0;
    sw::StringBuilder<sw::constant::buffer::kMax256> textBuilder;
    textBuilder.appendFormat( "only %# allocations were returned with the last handle - the pool went away with the manager", returnedCount );
    SW_EXPECT_TRUE_MSG( returnedCount >= 3,
                        sw::string( textBuilder.view() ) );
}

/**
 * @brief 스테이지를 기다리다 잠든 메인 스레드는 메인 전용 작업이 생기면 깨어난다
 * @details 메인은 자기 워드 하나에 잠든다. 그 스테이지의 워커 태스크가 뒤늦게 `MainThread` 친화도 태스크를 같은
 *          스테이지에 넣으면, 그 작업은 메인만 돌릴 수 있으므로 **메인을 깨우지 않으면 둘 다 영원히 기다린다.**
 *          조건 변수 하나에 `notify_one` 을 쓰면 엉뚱한 스레드가 깨어 멈춘다 — 그래서 메인이 잠들기 전에
 *          자기 슬롯을 적어 두고 메인 작업을 넣는 쪽이 그 슬롯을 깨운다. 그 길이 끊기면 이 케이스는 CTest 타임아웃으로 진다.
 */
SW_TEST_CASE( TaskManagerTest, MainThreadTaskWakesParkedMainThread )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );

    sw::atomic<bool>    bMainTaskRan{ false };
    sw::atomic<bool>    bMainTaskRanOnMain{ false };
    sw::TaskStageHandle stage = manager.createStage( "ParkedMainStage" ); // 이름이 있어야 대기 구간 훅이 불린다

    s_startedZoneKindMask.store( 0, std::memory_order_relaxed );
    const sw::Subscription probe = sw::TaskManager::bindProfileHook( &kZoneStartProbe );

    sw::TaskHandle worker = manager.emplaceTask( "LateMainSpawner", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&manager, &stage, &bMainTaskRan, &bMainTaskRanOnMain]()
    {
        // 메인이 스테이지 대기에 들어간 뒤에(사건 — 대기 구간 훅) 메인 작업을 만든다. 예전에는 20 ms 를 자며 메인이 이미 기다린다고 가정해, 부하에서 메인이
        // 늦으면 대기 전에 넣은 쉬운 길만 돌았다. 대기에 든 뒤 스핀(수 us)을 지나 잠들었는지는 사건으로 볼 수 없어 짧은 잠을 더한다 — 옳은 구현은 잠의 길이와
        // 무관하게 통과하고, 잠은 잠든 길을 덮을 확률만 높인다.
        holdUntilZoneStarted( sw::TaskProfileZoneKind::WaitStage );
        sw::ThreadUtil::sleepForMilliseconds( 5 );
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

    SW_EXPECT_TRUE( isZoneStarted( sw::TaskProfileZoneKind::WaitStage ) );
    SW_EXPECT_TRUE( bMainTaskRan.load( std::memory_order_acquire ) );
    SW_EXPECT_TRUE( bMainTaskRanOnMain.load( std::memory_order_acquire ) );

    manager.waitAll( "TaskManagerTest" );
    SW_EXPECT_EQUAL( 0u, manager.getActiveTaskCount() );
    manager.shutdown();
}

/**
 * @brief 워커 태스크 안의 runParallel 도 구간을 빠짐없이 한 번씩 덮고 돌아온다
 * @details 워커 안에서 부르면 티켓이 그 워커의 덱에 들어가고, 조인은 그 워커의 슬롯에 잠든다 — 메인에서 부를 때와
 *          다른 길이다. 워커가 하나뿐이어도(남이 훔쳐 갈 수 없어도) 자기 티켓을 스스로 집어 끝나야 한다.
 */
SW_TEST_CASE( TaskManagerTest, RunParallelInsideWorkerTaskCoversEveryIndex )
{
    for ( uint32 workerCount = 1; workerCount <= kWorkerCount; workerCount += 3 )
    {
        sw::TaskManager manager;
        SW_REQUIRE_TRUE( manager.initialize( workerCount ) );

        constexpr uint32              kCount = 2048;
        sw::vector<sw::atomic<int32>> listHit( kCount );
        sw::atomic<int32>*            pHit = listHit.data();
        sw::atomic<bool>              bReturned{ false };

        sw::TaskHandle outer = manager.emplaceTask( "NestedParallel", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&manager, pHit, &bReturned]()
        {
            manager.runParallel( kCount, 1, SW_DELEGATE_LAMBDA( sw::ParallelBlockDelegate, [pHit]( uint32 start, uint32 end )
            {
                for ( uint32 index = start; index < end; ++index )
                {
                    pHit[index].fetch_add( 1, std::memory_order_relaxed );
                }
            } ) );
            bReturned.store( true, std::memory_order_release );
        } ) );
        outer.submit();

        manager.waitAll( "TaskManagerTest" );
        SW_EXPECT_EQUAL( 0u, manager.getActiveTaskCount() );
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
 * @brief `runParallel` 의 합류를 기다리는 스레드는 Low 줄(백그라운드 I/O)을 돕지 않는다
 * @details 합류가 기다리는 것은 자기 티켓(Normal 줄)과 다른 스레드가 돌고 있는 청크뿐이다. 그 사이 Low 태스크(에셋 파싱 · 파일 완료, 수 ms)를 집으면
 *          그 태스크가 끝날 때까지 합류가 늦어진다 — 로딩 중 게임 스레드의 병렬 틱이 그만큼 는다.
 *          Low 태스크는 워커의 첫 청크 안에서 넣는다. 먼저 넣으면 깨어난 워커가 그것부터 집어 티켓을 가져갈 워커가 없다(호출 스레드가 자기 티켓을 다 가져가
 *          합류가 바로 끝나 재현되지 않는다). Low 를 넣은 청크는 호출 스레드가 합류 대기에 들 때까지(대기 구간 훅) 끝나지 않고 그 뒤 10 ms 를 더 붙들어,
 *          부하와 무관하게 호출 스레드가 기다리는 동안 Low 줄에 일이 있게 한다.
 */
SW_TEST_CASE( TaskManagerTest, RunParallelJoinDoesNotHelpLowPriorityTasks )
{
    constexpr uint32 kRoundCount   = 5;
    constexpr uint32 kLowTaskCount = 8;
    constexpr uint32 kChunkCount   = 12;

    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( 2 ) );

    sw::atomic<uint32>     lowOnJoiningThreadCount{ 0 };
    sw::atomic<bool>       bJoining{ false };
    const sw::Subscription probe = sw::TaskManager::bindProfileHook( &kZoneStartProbe );
    for ( uint32 round = 0; round < kRoundCount; ++round )
    {
        sw::atomic<bool> bLowSubmitted{ false };
        s_startedZoneKindMask.store( 0, std::memory_order_relaxed );
        bJoining.store( true, std::memory_order_release );
        manager.runParallel( kChunkCount, 1, SW_DELEGATE_LAMBDA( sw::ParallelBlockDelegate, [&manager, &bLowSubmitted, &bJoining, &lowOnJoiningThreadCount]( uint32, uint32 )
        {
            if ( manager.isMainThread() )
            {
                sw::ThreadUtil::sleepForMilliseconds( 1 );
                return;
            }
            if ( bLowSubmitted.exchange( true ) == false )
            {
                for ( uint32 lowIndex = 0; lowIndex < kLowTaskCount; ++lowIndex )
                {
                    sw::TaskHandle low = manager.emplaceTask( "BackgroundIOProbe", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&manager, &bJoining, &lowOnJoiningThreadCount]()
                    {
                        if ( manager.isMainThread() && bJoining.load( std::memory_order_acquire ) )
                            lowOnJoiningThreadCount.fetch_add( 1, std::memory_order_relaxed );
                        sw::ThreadUtil::sleepForMilliseconds( 3 );
                    } ) );
                    low.setPriority( sw::TaskPriority::Low );
                    low.submit();
                }
                // 호출 스레드가 합류 대기에 들 때까지(사건 — 대기 구간 훅) 이 청크를 붙든다. 그 뒤 잠은 대기 중인 호출 스레드 앞에 Low 일을 놓아 두는 창이다.
                holdUntilZoneStarted( sw::TaskProfileZoneKind::WaitJoin );
            }
            sw::ThreadUtil::sleepForMilliseconds( 10 );
        } ) );
        bJoining.store( false, std::memory_order_release );
        manager.waitAll( "TaskManagerTest" );
        SW_EXPECT_EQUAL( 0u, manager.getActiveTaskCount() );
    }

    SW_EXPECT_EQUAL( 0u, lowOnJoiningThreadCount.load() );
    manager.shutdown();
}

/**
 * @brief 메인 친화도의 병렬 부모는 청크가 워커에서 다 돈 뒤 메인에서만 완료된다
 * @details 청크는 워커가 돌고(티켓), 부모 노드만 `MainThread` 다. 부모는 그룹이 끝나야 준비되고, 준비돼도 워커가
 *          집어가면 안 된다 — `dispatchMainThreadTasks` 를 부르기 전에는 스테이지가 끝나지 않아야 한다.
 */
SW_TEST_CASE( TaskManagerTest, ParallelParentWithMainAffinityCompletesOnMainOnly )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );

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
        const sw::Deadline deadline = sw::Deadline::afterMilliseconds( kWaitTimeoutMs ); // 기다림의 상한일 뿐 — 짧으면 바쁜 기계에서 흔들린다
        bool               bAllHit  = false;
        while ( bAllHit == false && deadline.isExpired() == false )
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
                sw::ThreadUtil::yield();
        }
        SW_EXPECT_TRUE( bAllHit );
    }
    sw::ThreadUtil::sleepForMilliseconds( 5 );
    SW_EXPECT_FALSE( manager.isStageComplete( stage ) );

    manager.dispatchMainThreadTasks();
    SW_EXPECT_TRUE( manager.isStageComplete( stage ) );

    manager.waitStage( stage );
    manager.waitAll( "TaskManagerTest" );
    SW_EXPECT_EQUAL( 0u, manager.getActiveTaskCount() );
    manager.shutdown();
}

/**
 * @brief 모두 깨우기는 부른 순간 잠들어 있던 워커만 깨우고 돌아온다
 * @details 할 일 없이 깨어난 워커는 스핀(2 us) 뒤 곧바로 다시 잠든다. 루프가 워커 하나를 깨울 때마다 유휴 마스크를
 *          새로 읽으면 그렇게 다시 잠든 워커를 또 깨운다. 깨우기 시스템 호출이 그 스핀보다 느린 곳(WSL)에서는 마스크가 비는
 *          순간이 좀처럼 오지 않아 호출 한 번이 수십 초를 돈다. 작업 없이 여러 번 불러 전체 시간이 상한 안인지 본다.
 *          상한을 넘으면 그 자리에서 멈춘다.
 */
SW_TEST_CASE( TaskManagerTest, WakeAllDoesNotChaseWorkersThatSleepAgain )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWakeProbeWorkerCount ) );

    const sw::Stopwatch stopwatch;
    int64               elapsedMilli = 0;
    uint32              callCount    = 0;
    for ( ; callCount < kWakeProbeCallCount && elapsedMilli < kWakeProbeLimitMilli; ++callCount )
    {
        manager.wakeSleepingWorkers();
        elapsedMilli = stopwatch.getElapsedMilliseconds();
    }
    sw::StringBuilder<sw::constant::buffer::kMax256> textBuilder;
    textBuilder.appendFormat( "%# wake-all calls took %# ms - it chases workers that went back to sleep", callCount, elapsedMilli );
    SW_EXPECT_TRUE_MSG( elapsedMilli < kWakeProbeLimitMilli,
                        sw::string( textBuilder.view() ) );

    manager.shutdown();
}

/**
 * @brief 이미 끝난 태스크에 `runBefore` · `whenAll` · `whenAny` 로 후속을 붙여도 후속은 돈다(유실 없음).
 * @details 완료 쪽이 후속 목록을 훑고 난 뒤 붙은 후속은 영영 풀리지 않아 `waitAll` 과 종료가 멈춘다. 그래서 끝난 태스크의 목록은 닫혀
 *          있고, 붙이려는 쪽은 "이미 끝남" 을 듣고 의존을 되돌린다(언리얼 FGraphEvent 와 같다).
 */
SW_TEST_CASE( TaskManagerTest, SuccessorAddedAfterCompletionStillRuns )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );

    sw::TaskHandle first = manager.emplaceTask( "First", SW_DELEGATE_LAMBDA( sw::TaskDelegate, []() {} ) );
    first.submit();
    manager.waitAll( "TaskManagerTest" );
    SW_EXPECT_EQUAL( 0u, manager.getActiveTaskCount() );
    SW_REQUIRE_TRUE( first.isCompleted() );

    sw::atomic<int32> ranCount{ 0 };
    sw::TaskHandle    late = manager.emplaceTask( "Late", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&ranCount]()
       {
        ranCount.fetch_add( 1 );
    } ) );
    SW_REQUIRE_TRUE( first.runBefore( late ) );
    late.submit();

    sw::vector<sw::TaskHandle> listDone{ first };
    sw::TaskHandle             all = manager.whenAll( listDone, SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&ranCount]()
                {
        ranCount.fetch_add( 1 );
    } ) );
    all.submit();
    sw::TaskHandle any = manager.whenAny( listDone, SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&ranCount]()
    {
        ranCount.fetch_add( 1 );
    } ) );
    any.submit();

    manager.waitAll( "TaskManagerTest" );
    SW_EXPECT_TRUE_MSG( manager.getActiveTaskCount() == 0, "끝난 태스크에 붙인 후속이 풀리지 않았습니다" );
    SW_EXPECT_EQUAL( 3, ranCount.load() );
    manager.shutdown();
}

/**
 * @brief 유효한 핸들이 하나도 없는 `whenAny`, 만들기만 하고 제출하지 않고 버린 태스크가 `waitAll` 을 막지 않는다.
 */
SW_TEST_CASE( TaskManagerTest, AbandonedTasksDoNotBlockWaitAll )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );

    sw::atomic<int32> ranCount{ 0 };
    {
        sw::vector<sw::TaskHandle> listInvalid{ sw::TaskHandle{}, sw::TaskHandle{} };
        sw::TaskHandle             any = manager.whenAny( listInvalid, SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&ranCount]()
                    {
            ranCount.fetch_add( 1 );
        } ) );
        any.submit();
    }
    {
        // 만들고 선행까지 걸었지만 제출하지 않고 핸들을 버린다. 후속(제출됨)은 버려진 선행을 "취소로 끝남" 으로 보고 돈다.
        sw::TaskHandle abandoned = manager.emplaceTask( "Abandoned", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&ranCount]()
        {
            ranCount.fetch_add( 100 );
        } ) );
        sw::TaskHandle follower  = manager.emplaceTask( "Follower", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&ranCount]()
         {
            ranCount.fetch_add( 1 );
        } ) );
        SW_REQUIRE_TRUE( abandoned.runBefore( follower ) );
        follower.submit();
    }

    manager.waitAll( "TaskManagerTest" );
    SW_EXPECT_TRUE_MSG( manager.getActiveTaskCount() == 0, "버려진 태스크가 waitAll 을 막았습니다" );
    SW_EXPECT_EQUAL( 2, ranCount.load() );
    manager.shutdown();
}

/**
 * @brief 병렬 그룹의 본문(청크)은 부모에 건 `runAfter` 를 기다리고, 취소하면 돌지 않는다.
 * @details `emplaceParallel` 이 만드는 순간 티켓을 올리면 본문이 선행보다 먼저 돌고 취소도 듣지 않는다.
 */
SW_TEST_CASE( TaskManagerTest, ParallelBodyWaitsForPrerequisiteAndHonoursCancel )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );

    sw::atomic<bool>  bPrerequisiteDone{ false };
    sw::atomic<int32> earlyChunkCount{ 0 };
    sw::atomic<int32> chunkCount{ 0 };

    sw::TaskHandle prerequisite = manager.emplaceTask( "Prerequisite", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&bPrerequisiteDone]()
    {
        sw::ThreadUtil::sleepForMilliseconds( 30 );
        bPrerequisiteDone.store( true, std::memory_order_release );
    } ) );
    sw::TaskHandle group        = manager.emplaceParallel( "LatchedGroup", 256, SW_DELEGATE_LAMBDA( sw::ParallelTaskDelegate, [&bPrerequisiteDone, &earlyChunkCount, &chunkCount]( uint32 )
           {
        if ( bPrerequisiteDone.load( std::memory_order_acquire ) == false )
            earlyChunkCount.fetch_add( 1 );
        chunkCount.fetch_add( 1 );
    } ) );
    SW_REQUIRE_TRUE( group.runAfter( prerequisite ) );
    group.submit();
    prerequisite.submit();
    manager.waitAll( "TaskManagerTest" );
    SW_EXPECT_EQUAL( 0u, manager.getActiveTaskCount() );
    SW_EXPECT_EQUAL( 0, earlyChunkCount.load() );
    SW_EXPECT_EQUAL( 256, chunkCount.load() );

    sw::atomic<int32> cancelledChunkCount{ 0 };
    sw::TaskHandle    cancelled = manager.emplaceParallel( "CancelledGroup", 64, SW_DELEGATE_LAMBDA( sw::ParallelTaskDelegate, [&cancelledChunkCount]( uint32 )
       {
        cancelledChunkCount.fetch_add( 1 );
    } ) );
    cancelled.cancel();
    cancelled.submit();
    manager.waitAll( "TaskManagerTest" );
    SW_EXPECT_EQUAL( 0u, manager.getActiveTaskCount() );
    SW_EXPECT_EQUAL( 0, cancelledChunkCount.load() );

    manager.shutdown();
}

/**
 * @brief 사이클을 만드는 의존(직접 · 간접 · 자기 자신)은 걸리지 않고 오류로 알리며, `waitAll` 은 멈추지 않는다.
 * @details 사이클이 걸리면 후속의 의존 수가 영영 0 이 되지 않아 `waitAll` 이 멈춘다. 거부된 뒤에도 이미 걸린 의존(A→B→C)은 순서대로 돈다.
 */
SW_TEST_CASE( TaskManagerTest, DependencyCycleIsRejectedAndDoesNotHangWaitAll )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );

    sw::atomic<int32> sequence{ 0 };
    sw::atomic<int32> orderA{ 0 };
    sw::atomic<int32> orderB{ 0 };
    sw::atomic<int32> orderC{ 0 };
    sw::TaskHandle    taskA = makeOrderedTask( manager, "A", sequence, orderA );
    sw::TaskHandle    taskB = makeOrderedTask( manager, "B", sequence, orderB );
    sw::TaskHandle    taskC = makeOrderedTask( manager, "C", sequence, orderC );
    SW_REQUIRE_TRUE( taskA.runBefore( taskB ) );
    SW_REQUIRE_TRUE( taskB.runBefore( taskC ) );
    {
        SW_EXPECT_LOG( "Task.DependencyCycle" );     // dependency cycles must be rejected
        SW_EXPECT_FALSE( taskB.runBefore( taskA ) ); // 직접: A→B→A
        SW_EXPECT_FALSE( taskC.runBefore( taskA ) ); // 간접: A→B→C→A
        SW_EXPECT_FALSE( taskA.runAfter( taskC ) );  // runAfter 도 같다
        SW_EXPECT_FALSE( taskA.runBefore( taskA ) ); // 자기 자신
    }

    taskC.submit();
    taskB.submit();
    taskA.submit();
    manager.waitAll( "TaskManagerTest" );
    SW_EXPECT_TRUE_MSG( manager.getActiveTaskCount() == 0, "거부된 사이클 간선이 waitAll 을 막았습니다" );
    SW_EXPECT_TRUE( 0 < orderA.load() );
    SW_EXPECT_TRUE( orderA.load() < orderB.load() );
    SW_EXPECT_TRUE( orderB.load() < orderC.load() );

    manager.shutdown();
}

/**
 * @brief `whenAny` 가 돌려준 후속을 그 입력의 선행으로 거는 간선(입력 → 트리거 → 후속 → 입력)은 사이클로 거부되고 `waitAll` 이 멈추지 않는다.
 * @details 트리거 → 후속은 후속 목록의 간선이 아니라 숨은 후속 칸(`TaskNode::_pAnyOfSuccessor`)이다 — 검사가 그 칸을 따라가지 않으면 이 간선이 걸리고 영영 돌지 않는다.
 */
SW_TEST_CASE( TaskManagerTest, WhenAnyContinuationCannotRunBeforeItsInput )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );

    sw::atomic<int32>                sequence{ 0 };
    sw::atomic<int32>                orderA{ 0 };
    sw::atomic<int32>                orderB{ 0 };
    sw::atomic<int32>                ranCount{ 0 };
    sw::TaskHandle                   taskA = makeOrderedTask( manager, "A", sequence, orderA );
    sw::TaskHandle                   taskB = makeOrderedTask( manager, "B", sequence, orderB );
    const sw::vector<sw::TaskHandle> listInput{ taskA, taskB };
    sw::TaskHandle                   any = manager.whenAny( listInput, SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&ranCount]()
                      { ranCount.fetch_add( 1 ); } ) );
    {
        SW_EXPECT_LOG( "Task.DependencyCycle" ); // a whenAny continuation must not run before its input
        SW_EXPECT_FALSE( any.runBefore( taskA ) );
        SW_EXPECT_FALSE( taskB.runAfter( any ) );
    }

    any.submit();
    taskA.submit();
    taskB.submit();
    manager.waitAll( "TaskManagerTest" );
    SW_EXPECT_TRUE_MSG( manager.getActiveTaskCount() == 0, "거부된 whenAny 사이클 간선이 waitAll 을 막았습니다" );
    SW_EXPECT_EQUAL( 1, ranCount.load() );

    manager.shutdown();
}

/**
 * @brief 다이아몬드(A→B,C→D)는 사이클이 아니고, 사이클을 거부한 뒤에도 정상 그래프는 순서대로 돈다.
 */
SW_TEST_CASE( TaskManagerTest, DiamondIsNotACycleAndSurvivesARejectedEdge )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );

    sw::atomic<int32> sequence{ 0 };
    sw::atomic<int32> orderA{ 0 };
    sw::atomic<int32> orderB{ 0 };
    sw::atomic<int32> orderC{ 0 };
    sw::atomic<int32> orderD{ 0 };
    sw::TaskHandle    taskA = makeOrderedTask( manager, "A", sequence, orderA );
    sw::TaskHandle    taskB = makeOrderedTask( manager, "B", sequence, orderB );
    sw::TaskHandle    taskC = makeOrderedTask( manager, "C", sequence, orderC );
    sw::TaskHandle    taskD = makeOrderedTask( manager, "D", sequence, orderD );
    SW_REQUIRE_TRUE( taskA.runBefore( taskB ) );
    SW_REQUIRE_TRUE( taskA.runBefore( taskC ) );
    SW_REQUIRE_TRUE( taskB.runBefore( taskD ) );
    SW_REQUIRE_TRUE( taskC.runBefore( taskD ) );
    {
        SW_EXPECT_LOG( "Task.DependencyCycle" ); // a back edge of the diamond must be rejected
        SW_EXPECT_FALSE( taskD.runBefore( taskA ) );
    }

    taskD.submit();
    taskC.submit();
    taskB.submit();
    taskA.submit();
    manager.waitAll( "TaskManagerTest" );
    SW_EXPECT_EQUAL( 0u, manager.getActiveTaskCount() );
    SW_EXPECT_TRUE( orderA.load() < orderB.load() );
    SW_EXPECT_TRUE( orderA.load() < orderC.load() );
    SW_EXPECT_TRUE( orderB.load() < orderD.load() );
    SW_EXPECT_TRUE( orderC.load() < orderD.load() );

    manager.shutdown();
}

/**
 * @brief 겹치는 다이아몬드가 깊어도 사이클 검사는 노드마다 한 번만 본다(방문 집합).
 * @details 방문 집합이 없으면 40 단 사다리의 경로 수가 2^40 이라 검사가 끝나지 않는다. 사이클이 없는 간선(새 태스크 → 사다리 맨 앞)을
 *          걸어 사다리를 끝까지 훑게 한다.
 */
SW_TEST_CASE( TaskManagerTest, CycleCheckVisitsEachNodeOnceOnOverlappingDiamonds )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );

    constexpr int32 kLadderSteps = 40;

    sw::atomic<int32>          ranCount{ 0 };
    sw::vector<sw::TaskHandle> listTask;
    const sw::TaskDelegate     countRun = SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&ranCount]()
        {
        ranCount.fetch_add( 1 );
    } );

    sw::TaskHandle head = manager.emplaceTask( "Head", countRun );
    listTask.push_back( head );
    sw::TaskHandle joint = head;
    for ( int32 step = 0; step < kLadderSteps; ++step )
    {
        sw::TaskHandle left      = manager.emplaceTask( "Left", countRun );
        sw::TaskHandle right     = manager.emplaceTask( "Right", countRun );
        sw::TaskHandle nextJoint = manager.emplaceTask( "Joint", countRun );
        SW_REQUIRE_TRUE( joint.runBefore( left ) );
        SW_REQUIRE_TRUE( joint.runBefore( right ) );
        SW_REQUIRE_TRUE( left.runBefore( nextJoint ) );
        SW_REQUIRE_TRUE( right.runBefore( nextJoint ) );
        listTask.push_back( left );
        listTask.push_back( right );
        listTask.push_back( nextJoint );
        joint = nextJoint;
    }

    // 새 태스크 → 사다리 맨 앞. 사다리 전체(경로 2^40)를 훑어도 새 태스크에는 닿지 않으므로 사이클이 아니다.
    sw::TaskHandle before = manager.emplaceTask( "BeforeHead", countRun );
    listTask.push_back( before );
    const sw::Deadline checkDeadline = sw::Deadline::afterMilliseconds( kWaitTimeoutMs );
    SW_REQUIRE_TRUE( before.runBefore( head ) );
    SW_EXPECT_FALSE_MSG( checkDeadline.isExpired(), "겹치는 다이아몬드에서 사이클 검사가 노드를 되풀이해 훑었습니다" );

    for ( sw::TaskHandle& task : listTask )
    {
        task.submit();
    }
    manager.waitAll( "TaskManagerTest" );
    SW_EXPECT_EQUAL( 0u, manager.getActiveTaskCount() );
    SW_EXPECT_EQUAL( static_cast<int32>( listTask.size() ), ranCount.load() );

    manager.shutdown();
}

/**
 * @brief 이미 끝난 선행에 걸기, 끝난 쪽이 낀 간선, 빈 핸들은 사이클이 아니다.
 */
SW_TEST_CASE( TaskManagerTest, DependencyOnCompletedTaskIsNotACycle )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );

    sw::atomic<int32> sequence{ 0 };
    sw::atomic<int32> orderA{ 0 };
    sw::atomic<int32> orderB{ 0 };
    sw::TaskHandle    taskA = makeOrderedTask( manager, "A", sequence, orderA );
    sw::TaskHandle    taskB = makeOrderedTask( manager, "B", sequence, orderB );
    SW_REQUIRE_TRUE( taskA.runBefore( taskB ) );
    taskB.submit();
    taskA.submit();
    manager.waitAll( "TaskManagerTest" );
    SW_EXPECT_EQUAL( 0u, manager.getActiveTaskCount() );
    SW_REQUIRE_TRUE( taskA.isCompleted() );
    SW_REQUIRE_TRUE( taskB.isCompleted() );

    // 끝난 선행에 새 후속을 건다 — 의존은 이미 채워져 후속은 바로 돈다.
    sw::atomic<int32> orderLate{ 0 };
    sw::TaskHandle    taskLate = makeOrderedTask( manager, "Late", sequence, orderLate );
    SW_EXPECT_TRUE( taskA.runBefore( taskLate ) );
    taskLate.submit();

    // 둘 다 끝난 쪽끼리는 반대 방향 간선을 걸어도 아무도 기다리지 않는다 — 사이클로 막지 않는다.
    SW_EXPECT_TRUE( taskB.runBefore( taskA ) );

    // 빈 핸들은 걸 의존이 없다. 사이클 오류가 아니다.
    SW_EXPECT_FALSE( taskA.runBefore( sw::TaskHandle{} ) );
    SW_EXPECT_FALSE( sw::TaskHandle{}.runBefore( taskA ) );

    manager.waitAll( "TaskManagerTest" );
    SW_EXPECT_TRUE_MSG( manager.getActiveTaskCount() == 0, "끝난 선행에 건 후속이 풀리지 않았습니다" );
    SW_EXPECT_TRUE( 0 < orderLate.load() );

    manager.shutdown();
}

/**
 * @brief 서로 반대 방향의 간선을 동시에 걸어도 하나만 걸리고 하나는 사이클로 거부된다.
 * @details 검사와 걸기가 한 잠금 안에 있지 않으면 두 스레드가 서로의 간선을 못 보고 둘 다 통과해 `waitAll` 이 멈춘다.
 */
SW_TEST_CASE( TaskManagerTest, OppositeEdgesAddedConcurrentlyNeverBothSucceed )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );

    constexpr int32 kRoundCount = 64;
    {
        SW_EXPECT_LOG( "Task.DependencyCycle" ); // opposite edges race; exactly one is rejected
        for ( int32 round = 0; round < kRoundCount; ++round )
        {
            sw::TaskHandle taskA = manager.emplaceTask( "A", SW_DELEGATE_LAMBDA( sw::TaskDelegate, []() {} ) );
            sw::TaskHandle taskB = manager.emplaceTask( "B", SW_DELEGATE_LAMBDA( sw::TaskDelegate, []() {} ) );

            sw::atomic<bool>  bGo{ false };
            sw::atomic<int32> linkedCount{ 0 };
            sw::thread        otherSide( sw::ThreadTestAccess::getKey(), [&]()
                   {
                while ( bGo.load( std::memory_order_acquire ) == false )
                {
                    sw::ThreadUtil::yield();
                }
                if ( taskB.runBefore( taskA ) )
                    linkedCount.fetch_add( 1 );
            } );
            bGo.store( true, std::memory_order_release );
            if ( taskA.runBefore( taskB ) )
                linkedCount.fetch_add( 1 );
            otherSide.join();

            SW_EXPECT_EQUAL( 1, linkedCount.load() );
            taskA.submit();
            taskB.submit();
        }
    }
    manager.waitAll( "TaskManagerTest" );
    SW_EXPECT_TRUE_MSG( manager.getActiveTaskCount() == 0, "동시에 건 반대 방향 간선이 둘 다 통과해 waitAll 이 멈췄습니다" );

    manager.shutdown();
}

/**
 * @brief 본문 안에서 만든 자식을 부모의 후속으로 거는 일(`부모.runBefore( 자식 )`)은 사이클이라 거부된다. 부모는 자식의 완료를 기다리므로 걸면 `waitAll` 이 멈춘다.
 * @details 자식 → 부모 완료 기다림은 후속 목록의 간선이 아니어서 `_pParent` 사슬로 따로 검사한다. 손자도 같다. 형제끼리의 간선은 사이클이 아니다.
 */
SW_TEST_CASE( TaskManagerTest, ParentCannotRunBeforeItsOwnDescendant )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );

    sw::atomic<int32> childRejectedCount{ 0 };
    sw::atomic<int32> grandchildRejectedCount{ 0 };
    sw::atomic<int32> siblingLinkedCount{ 0 };
    sw::atomic<int32> ranCount{ 0 };
    sw::TaskHandle    parent;
    {
        SW_EXPECT_LOG( "Task.DependencyCycle" ); // a parent cannot run before its own child
        parent = manager.emplaceTask( "Parent", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&]()
        {
            sw::TaskHandle child   = manager.emplaceTask( "Child", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&]()
              {
                ranCount.fetch_add( 1 );
            } ) );
            sw::TaskHandle sibling = manager.emplaceTask( "Sibling", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&]()
            {
                ranCount.fetch_add( 1 );
            } ) );
            // 손자는 자식(`host`)의 본문 안에서 만든다. 부모 본문이 끝나도 도는 본문이라 부모의 지역 변수는 잡지 않는다.
            sw::TaskHandle host = manager.emplaceTask( "Host", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&]()
            {
                sw::TaskHandle grandchild = manager.emplaceTask( "Grandchild", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&]()
                {
                    ranCount.fetch_add( 1 );
                } ) );
                if ( parent.runBefore( grandchild ) == false )
                    grandchildRejectedCount.fetch_add( 1 );
                grandchild.submit();
            } ) );
            if ( parent.runBefore( child ) == false )
                childRejectedCount.fetch_add( 1 );
            if ( child.runBefore( sibling ) )
                siblingLinkedCount.fetch_add( 1 );
            sibling.submit();
            child.submit();
            host.submit();
        } ) );
        parent.submit();
        manager.waitAll( "TaskManagerTest" );
        SW_EXPECT_TRUE_MSG( manager.getActiveTaskCount() == 0, "부모 → 자식 간선이 걸려 waitAll 이 멈췄습니다" );
    }
    SW_EXPECT_EQUAL( 1, childRejectedCount.load() );
    SW_EXPECT_EQUAL( 1, grandchildRejectedCount.load() );
    SW_EXPECT_EQUAL( 1, siblingLinkedCount.load() );
    SW_EXPECT_EQUAL( 3, ranCount.load() );

    manager.shutdown();
}

/**
 * @brief 서로 다른 `TaskManager`(= 다른 풀)의 태스크끼리는 간선을 걸지 않고 오류로 알린다.
 * @details 검사와 걸기를 지키는 잠금이 풀마다 하나라서, 다른 풀 사이의 반대 방향 간선을 동시에 걸면 서로를 못 보고 둘 다 통과한다 — 그 동시 정확성을 보증하지 못해 거부한다.
 */
SW_TEST_CASE( TaskManagerTest, DependencyAcrossManagersIsRejected )
{
    // 매니저는 전역 큐를 안에 품어 하나가 수백 KB 다 — 둘을 스택에 두면 Debug 메인 스레드 스택(1 MB)이 넘친다.
    const auto       pFirstManager  = sw::make_unique<sw::TaskManager>();
    const auto       pSecondManager = sw::make_unique<sw::TaskManager>();
    sw::TaskManager& firstManager   = *pFirstManager;
    sw::TaskManager& secondManager  = *pSecondManager;
    SW_REQUIRE_TRUE( firstManager.initialize( 1 ) );
    SW_REQUIRE_TRUE( secondManager.initialize( 1 ) );

    sw::atomic<int32> ranCount{ 0 };
    sw::TaskHandle    firstTask  = firstManager.emplaceTask( "First", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&]()
        { ranCount.fetch_add( 1 ); } ) );
    sw::TaskHandle    secondTask = secondManager.emplaceTask( "Second", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&]()
       { ranCount.fetch_add( 1 ); } ) );
    sw::TaskHandle    sameTask   = firstManager.emplaceTask( "Same", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&]()
         { ranCount.fetch_add( 1 ); } ) );
    {
        SW_EXPECT_LOG( "Task.DependencyCrossPool" ); // dependencies across task managers must be rejected
        SW_EXPECT_FALSE( firstTask.runBefore( secondTask ) );
        SW_EXPECT_FALSE( secondTask.runBefore( firstTask ) );
    }
    SW_EXPECT_TRUE( firstTask.runBefore( sameTask ) ); // 같은 매니저끼리는 그대로 걸린다

    firstTask.submit();
    sameTask.submit();
    secondTask.submit();
    firstManager.waitAll( "TaskManagerTest" );
    SW_EXPECT_EQUAL( 0u, firstManager.getActiveTaskCount() );
    secondManager.waitAll( "TaskManagerTest" );
    SW_EXPECT_EQUAL( 0u, secondManager.getActiveTaskCount() );
    SW_EXPECT_EQUAL( 3, ranCount.load() );

    firstManager.shutdown();
    secondManager.shutdown();
}

/**
 * @brief 취소는 후속에 전파되지 않는다 — 취소된 선행은 본문만 건너뛰고 후속은 돌며, 후속은 `isCancelled` 로 선행의 취소를 안다.
 */
SW_TEST_CASE( TaskManagerTest, CancelledPrerequisiteStillReleasesItsSuccessor )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );

    sw::atomic<bool> bPrerequisiteBodyRan{ false };
    sw::atomic<bool> bSuccessorRan{ false };
    sw::atomic<bool> bSuccessorSawCancel{ false };
    sw::atomic<bool> bChainedRan{ false };

    sw::TaskHandle prerequisite = manager.emplaceTask( "Prerequisite", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&bPrerequisiteBodyRan]()
    {
        bPrerequisiteBodyRan.store( true );
    } ) );
    sw::TaskHandle successor    = manager.emplaceTask( "Successor", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&bSuccessorRan, &bSuccessorSawCancel, prerequisite]()
       {
        bSuccessorSawCancel.store( prerequisite.isCancelled() );
        bSuccessorRan.store( true );
    } ) );
    sw::TaskHandle chained      = prerequisite.then( SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&bChainedRan]()
         {
        bChainedRan.store( true );
    } ) );
    SW_REQUIRE_TRUE( prerequisite.runBefore( successor ) );

    prerequisite.cancel();
    successor.submit();
    chained.submit();
    prerequisite.submit();

    manager.waitAll( "TaskManagerTest" );
    SW_EXPECT_TRUE_MSG( manager.getActiveTaskCount() == 0, "취소된 선행이 후속을 붙들었습니다" );
    SW_EXPECT_FALSE( bPrerequisiteBodyRan.load() );
    SW_EXPECT_TRUE( bSuccessorRan.load() );
    SW_EXPECT_TRUE( bSuccessorSawCancel.load() );
    SW_EXPECT_TRUE( bChainedRan.load() );

    manager.shutdown();
}

/**
 * @brief 끝나는 비워커 스레드가 돌려준 도우미 슬롯을 다음 스레드가 다시 쓴다.
 * @details 돌려받지 않으면 렌더 스레드를 다시 만들 때마다 새 칸을 받고, 상한(8)을 넘으면 칸을 나눠 써 스크래치가 겹친다.
 */
SW_TEST_CASE( TaskManagerTest, HelperSlotIsRecycledWhenThreadEnds )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );

    const uint32 helperLimit = manager.getScratchSlotCount();
    uint32       firstSlot   = ~0u;
    for ( uint32 threadIndex = 0; threadIndex < 20; ++threadIndex )
    {
        uint32     slot = ~0u;
        sw::thread helper( sw::ThreadTestAccess::getKey(), [&manager, &slot]()
        {
            slot = manager.getCurrentThreadScratchSlot();
            manager.releaseCurrentThreadHelperSlot();
        } );
        helper.join();
        SW_EXPECT_TRUE( slot < helperLimit );
        if ( threadIndex == 0 )
            firstSlot = slot;
        SW_EXPECT_EQUAL( firstSlot, slot );
    }
    manager.shutdown();
}

/**
 * @brief 태스크 · 병렬 청크는 **만든 쪽의** 할당 태그로 실행된다
 * @details 태그는 스레드 로컬이라 그대로 두면 워커에서 하는 할당(비동기 씬 로드 · 에셋 스트리밍)이 모두 Unknown 으로 샌다. 태스크 노드와 병렬
 *          그룹이 만든 스레드의 태그를 담아 실행하는 동안 그 태그를 쓴다. 기다리는 스레드가 대신 실행하는 경우도 본다 — 그 스레드의 태그(Mesh)가
 *          아니라 만든 쪽의 태그(Texture)여야 한다. `runParallel` 은 호출 스레드의 첫 청크가 워커의 청크를 기다려, 워커 경로를 반드시 지난다.
 */
SW_TEST_CASE( TaskManagerTest, TaskInheritsCreatorMemoryTag )
{

    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );

    constexpr uint32  kCount = 256;
    sw::atomic<int32> taskRunCount{ 0 };
    sw::atomic<int32> taskWrongTagCount{ 0 };
    sw::atomic<int32> groupRunCount{ 0 };
    sw::atomic<int32> groupWrongTagCount{ 0 };

    sw::TaskHandle task;
    sw::TaskHandle group;
    {
        const sw::ScopedMemoryTag textureScope{ sw::MemoryTag::Texture };
        task  = manager.emplaceTask( "TagTask", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&taskRunCount, &taskWrongTagCount]()
         {
            if ( sw::Memory::getCurrentMemoryTag() != sw::MemoryTag::Texture )
                taskWrongTagCount.fetch_add( 1, std::memory_order_relaxed );
            taskRunCount.fetch_add( 1, std::memory_order_relaxed );
        } ) );
        group = manager.emplaceParallel( "TagGroup", kCount, SW_DELEGATE_LAMBDA( sw::ParallelTaskDelegate, [&groupRunCount, &groupWrongTagCount]( uint32 )
        {
            if ( sw::Memory::getCurrentMemoryTag() != sw::MemoryTag::Texture )
                groupWrongTagCount.fetch_add( 1, std::memory_order_relaxed );
            groupRunCount.fetch_add( 1, std::memory_order_relaxed );
        } ) );
    }
    {
        // 이 스레드가 기다리며 대신 실행해도 Mesh 가 아니라 Texture 여야 한다.
        const sw::ScopedMemoryTag meshScope{ sw::MemoryTag::Mesh };
        task.submit();
        group.submit();
        manager.waitAll( "TaskManagerTest" );
        SW_EXPECT_EQUAL( 0u, manager.getActiveTaskCount() );
    }
    SW_EXPECT_EQUAL( 1, taskRunCount.load() );
    SW_EXPECT_EQUAL( 0, taskWrongTagCount.load() );
    SW_EXPECT_EQUAL( static_cast<int32>( kCount ), groupRunCount.load() );
    SW_EXPECT_EQUAL( 0, groupWrongTagCount.load() );

    sw::atomic<int32>     stackRunCount{ 0 };
    sw::atomic<int32>     stackWrongTagCount{ 0 };
    sw::atomic<int32>     workerChunkCount{ 0 };
    const std::thread::id callerID = std::this_thread::get_id();
    {
        const sw::ScopedMemoryTag materialScope{ sw::MemoryTag::Material };
        manager.runParallel( kCount, 1, SW_DELEGATE_LAMBDA( sw::ParallelBlockDelegate, [&stackRunCount, &stackWrongTagCount, &workerChunkCount, callerID]( uint32 start, uint32 end )
        {
            if ( std::this_thread::get_id() == callerID )
            {
                // 워커가 청크 하나를 실행할 때까지 호출 스레드를 붙든다 — 청크가 모두 호출 스레드에서 돌면 상속을 보지 못한다.
                const sw::Deadline deadline = sw::Deadline::afterMilliseconds( kWaitTimeoutMs );
                while ( workerChunkCount.load( std::memory_order_acquire ) == 0 && deadline.isExpired() == false )
                {
                    sw::ThreadUtil::yield();
                }
            }
            else
            {
                workerChunkCount.fetch_add( 1, std::memory_order_acq_rel );
            }
            if ( sw::Memory::getCurrentMemoryTag() != sw::MemoryTag::Material )
                stackWrongTagCount.fetch_add( 1, std::memory_order_relaxed );
            stackRunCount.fetch_add( static_cast<int32>( end - start ), std::memory_order_relaxed );
        } ) );
    }
    SW_EXPECT_EQUAL( static_cast<int32>( kCount ), stackRunCount.load() );
    SW_EXPECT_TRUE( workerChunkCount.load() > 0 );
    SW_EXPECT_EQUAL( 0, stackWrongTagCount.load() );

    manager.shutdown();
}

/**
 * @brief `runParallelByCost` 는 가벼운 일도 무거운 일도 구간을 빠짐없이 한 번씩 덮는다
 * @details 앞 항목을 호출 스레드가 1, 2, 4, … 개씩 직접 돌고 나머지만 나누므로, 직렬로 끝나는 길과 나뉘는 길 모두 덮개가 정확해야 한다.
 */
SW_TEST_CASE( TaskManagerTest, RunParallelByCostCoversEveryIndexOnLightAndHeavyBodies )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );

    for ( const uint32 count : { 1u, 2u, 7u, 100u, 4096u, 100000u } )
    {
        for ( const bool bHeavy : { false, true } )
        {
            const uint32                  heavyCount = bHeavy ? sw::MathUtil::min( count, 256u ) : count;
            sw::vector<sw::atomic<int32>> listHit( heavyCount );
            sw::atomic<int32>*            pHit = listHit.data();
            manager.runParallelByCost( heavyCount, SW_DELEGATE_LAMBDA( sw::ParallelBlockDelegate, [pHit, bHeavy]( uint32 start, uint32 end )
            {
                for ( uint32 index = start; index < end; ++index )
                {
                    pHit[index].fetch_add( 1, std::memory_order_relaxed );
                    if ( bHeavy )
                    {
                        const sw::Stopwatch spin;
                        while ( spin.getElapsedMicroseconds() < 20 )
                        {
                        }
                    }
                }
            } ) );
            uint32 wrongCount = 0;
            for ( uint32 index = 0; index < heavyCount; ++index )
            {
                if ( pHit[index].load() != 1 )
                    ++wrongCount;
            }
            SW_EXPECT_EQUAL( 0u, wrongCount );
        }
    }
    manager.shutdown();
}

/**
 * @brief `runParallelByCost`: 무거운 항목은 첫 항목에서 바로 나뉜다
 * @details 항목 하나가 100 us 의 바쁜 대기라 첫 호출만으로 경과가 비용 임계값(3 us)을 넘고, 남은 63 개의 예상 시간(6 ms 이상)이 직렬 예산을 넘는다. 이 판단은 시계가 단조라는 것에만 기대므로
 *          스레드가 실제로 깨어나는지(스케줄러)와 무관하게 결정적이다. 나뉘었다는 것은 호출 기록으로 본다: 첫 호출은 (0, 1) 이고, 남은 구간이 한 덩이 (1, 64) 로 직렬로 끝나지 않고
 *          여러 청크로 쪼개져 불린다. 고정 개수만큼 먼저 직렬로 돌았다면(앞 16 개) 첫 호출이 (0, 1) 이 아니거나 나머지가 한 덩이가 된다.
 */
SW_TEST_CASE( TaskManagerTest, RunParallelByCostSplitsHeavyItemsAtTheFirstItem )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );

    CostCallRecorder  recorder;
    CostCallRecorder* pRecorder = &recorder;
    manager.runParallelByCost( 64, SW_DELEGATE_LAMBDA( sw::ParallelBlockDelegate, [pRecorder]( uint32 start, uint32 end )
    {
        pRecorder->record( start, end );
        for ( uint32 index = start; index < end; ++index )
        {
            const sw::Stopwatch spin;
            while ( spin.getElapsedMicroseconds() < 100 )
            {
            }
        }
    } ) );

    const sw::vector<std::pair<uint32, uint32>> listCall = recorder.list();
    SW_REQUIRE_TRUE( listCall.empty() == false );
    SW_EXPECT_EQUAL( 0u, listCall.front().first );
    SW_EXPECT_EQUAL( 1u, listCall.front().second );
    SW_EXPECT_TRUE( listCall.size() >= 3u );
    bool bRestInOneBlock = false;
    for ( const auto& call : listCall )
    {
        if ( call.first == 1u && call.second == 64u )
            bRestInOneBlock = true;
    }
    SW_EXPECT_FALSE( bRestInOneBlock );
    manager.shutdown();
}

/**
 * @brief `runParallelByCost`: 아주 가벼운 일은 깨우지 않고 호출 스레드가 직렬로 끝낸다
 * @details 가짜 시계가 부를 때마다 1 ns 만 가므로 경과는 비용 임계값(3 us)에 닿지 않는다. 직렬 길은 구간을 1, 2, 4, … 개씩 차례로 부르므로 호출 기록이 정확히 그 열이면 나뉘지 않은 것이다.
 *          실제 시계를 쓰지 않아 호출 스레드 선점 잡음에 흔들리지 않는다.
 */
SW_TEST_CASE( TaskManagerTest, RunParallelByCostKeepsVeryLightJobsSerial )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );

    constexpr uint32                      kCount = 4000;
    sw::vector<std::pair<uint32, uint32>> listExpected;
    for ( uint32 done = 0, chunk = 1; done < kCount; chunk *= 2 )
    {
        const uint32 end = sw::MathUtil::min( kCount, done + chunk );
        listExpected.emplace_back( done, end );
        done = end;
    }

    CostCallRecorder  recorder;
    CostCallRecorder* pRecorder = &recorder;
    gFakeClockNanos             = 0;
    gFakeClockStepNanos         = 1;
    manager.runParallelByCostWithClock( kCount, SW_DELEGATE_LAMBDA( sw::ParallelBlockDelegate, [pRecorder]( uint32 start, uint32 end )
    {
        pRecorder->record( start, end );
    } ),
                                        &readFakeClock );
    SW_EXPECT_TRUE( recorder.list() == listExpected );
    manager.shutdown();
}

/**
 * @brief `runParallelByCost`: 시계가 첫 항목에서 임계값을 넘겼다고 하면 나머지를 나눈다
 * @details 가짜 시계가 부를 때마다 5 us 가므로 첫 호출 (0, 1) 뒤 경과 5 us, 남은 예상이 직렬 예산을 넘어 나머지가 한 덩이 (1, 4000) 으로 끝나지 않는다.
 */
SW_TEST_CASE( TaskManagerTest, RunParallelByCostSplitsWhenClockSaysCostly )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );

    CostCallRecorder  recorder;
    CostCallRecorder* pRecorder = &recorder;
    gFakeClockNanos             = 0;
    gFakeClockStepNanos         = 5000;
    manager.runParallelByCostWithClock( 4000, SW_DELEGATE_LAMBDA( sw::ParallelBlockDelegate, [pRecorder]( uint32 start, uint32 end )
    {
        pRecorder->record( start, end );
    } ),
                                        &readFakeClock );

    const sw::vector<std::pair<uint32, uint32>> listCall = recorder.list();
    SW_REQUIRE_TRUE( listCall.empty() == false );
    SW_EXPECT_EQUAL( 0u, listCall.front().first );
    SW_EXPECT_EQUAL( 1u, listCall.front().second );
    bool bRestInOneBlock = false;
    for ( const auto& call : listCall )
    {
        if ( call.first == 1u && call.second == 4000u )
            bRestInOneBlock = true;
    }
    SW_EXPECT_FALSE( bRestInOneBlock );
    SW_EXPECT_TRUE( listCall.size() >= 3u );
    manager.shutdown();
}

#if !defined( SW_SHIPPING )
/**
 * @brief describeUnfinishedTask 는 도는 태스크의 이름을 대고, 끝나면 빈 문자열이다
 */
SW_TEST_CASE( TaskManagerTest, DescribeUnfinishedTaskNamesTheRunningTask )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );

    sw::atomic<bool> bStarted{ false };
    sw::atomic<bool> bRelease{ false };
    sw::TaskHandle   handle = manager.emplaceTask( "StuckProbe", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&bStarted, &bRelease]()
      {
        bStarted.store( true, std::memory_order_release );
        const sw::Stopwatch stopwatch;
        while ( bRelease.load( std::memory_order_acquire ) == false && stopwatch.getElapsedMilliseconds() < kWaitTimeoutMs )
        {
            sw::ThreadUtil::yield();
        }
    } ) );
    handle.submit();

    const sw::Stopwatch startWatch;
    while ( bStarted.load( std::memory_order_acquire ) == false && startWatch.getElapsedMilliseconds() < kWaitTimeoutMs )
    {
        sw::ThreadUtil::yield();
    }
    SW_REQUIRE_TRUE( bStarted.load( std::memory_order_acquire ) );
    SW_EXPECT_TRUE( manager.describeUnfinishedTask() == "StuckProbe" );

    bRelease.store( true, std::memory_order_release );
    manager.waitAll( "TaskManagerTest" );
    SW_EXPECT_EQUAL( 0u, manager.getActiveTaskCount() );
    handle = sw::TaskHandle{};
    SW_EXPECT_TRUE( manager.describeUnfinishedTask().empty() );

    manager.shutdown();
}

/**
 * @brief describeUnfinishedTask 는 만들기만 하고 제출하지 않은 태스크를 잡지 않는다
 */
SW_TEST_CASE( TaskManagerTest, DescribeUnfinishedTaskIgnoresUnsubmittedTask )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );

    sw::TaskHandle handle = manager.emplaceTask( "NeverSubmitted", SW_DELEGATE_LAMBDA( sw::TaskDelegate, []() {} ) );
    SW_EXPECT_TRUE( manager.describeUnfinishedTask().empty() );

    handle = sw::TaskHandle{}; // 제출 없이 놓으면 취소로 완료 처리된다
    manager.waitAll( "TaskManagerTest" );
    SW_EXPECT_EQUAL( 0u, manager.getActiveTaskCount() );
    manager.shutdown();
}
#endif

/**
 * @brief 잠든 풀에 깨우지 않고(`SubmitBatch`) High 큐 용량(1024)보다 많이 넣어도 끝난다(감사 C-4) — 범위가 끝날 때 한 번 깨운다
 * @details 큐가 가득 차면 넣는 스레드가 yield 만 하며 기다렸다 — 워커가 모두 잠들어 아무도 비우지 않아 영원히 돌았다. 지금은 넘침 목록이 받는다.
 */
SW_TEST_CASE( TaskManagerTest, SubmitBatchPastQueueCapacityFinishes )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );
    manager.waitAll( "TaskManagerTest" ); // 워커가 잠들게 둔다
    constexpr int32   kTaskCount = 2000;
    sw::atomic<int32> ranCount{ 0 };
    {
        sw::TaskManager::SubmitBatch batch{ manager };
        for ( int32 index = 0; index < kTaskCount; ++index )
        {
            sw::TaskHandle handle = manager.emplaceTask( "Unit", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&ranCount]()
            {
                ranCount.fetch_add( 1, std::memory_order_relaxed );
            } ) );
            handle.setPriority( sw::TaskPriority::High );
            batch.submit( handle );
        }
    }
    manager.waitAll( "TaskManagerTest" );
    SW_EXPECT_EQUAL( kTaskCount, ranCount.load() );
    manager.shutdown();
}

/**
 * @brief `runParallel` 의 합류는 아무도 집지 않은 티켓을 기다리지 않는다 — 워커가 모두 다른 일에 붙들려 있어도 호출 스레드가 혼자 끝내고, 줄 앞에 선 남의 태스크를 대신 돌지 않는다
 * @details 예전에는 합류가 티켓을 세어, 호출 스레드가 자기 티켓이 나올 때까지 앞에 선 태스크(여기서는 표식을 켜는 태스크)를 돌았다. 워커는 합류가 돌아온 뒤에야 놓아준다.
 */
SW_TEST_CASE( TaskManagerTest, RunParallelDoesNotWaitForUnclaimedTickets )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( 1 ) );

    sw::atomic<bool> bWorkerHeld{ false };
    sw::atomic<bool> bReleaseWorker{ false };
    sw::TaskHandle   holder = manager.emplaceTask( "HoldWorker", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&bWorkerHeld, &bReleaseWorker]()
      {
        bWorkerHeld.store( true, std::memory_order_release );
        while ( bReleaseWorker.load( std::memory_order_acquire ) == false )
        {
            sw::ThreadUtil::yield();
        }
    } ) );
    holder.submit();
    while ( bWorkerHeld.load( std::memory_order_acquire ) == false )
    {
        sw::ThreadUtil::yield();
    }

    sw::atomic<bool> bQueuedTaskRan{ false };
    sw::TaskHandle   queued = manager.emplaceTask( "QueuedAhead", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&bQueuedTaskRan]()
      {
        bQueuedTaskRan.store( true, std::memory_order_release );
    } ) );
    queued.submit(); // 티켓보다 앞에 선다

    sw::atomic<uint32> coveredCount{ 0 };
    manager.runParallel( 64, 1, SW_DELEGATE_LAMBDA( sw::ParallelBlockDelegate, [&coveredCount]( uint32 start, uint32 end )
    {
        coveredCount.fetch_add( end - start, std::memory_order_relaxed );
    } ) );
    SW_EXPECT_EQUAL( 64u, coveredCount.load() );
    SW_EXPECT_FALSE( bQueuedTaskRan.load( std::memory_order_acquire ) );

    bReleaseWorker.store( true, std::memory_order_release );
    manager.waitAll( "TaskManagerTest" );
    SW_EXPECT_TRUE( bQueuedTaskRan.load( std::memory_order_acquire ) );
    SW_EXPECT_EQUAL( 0u, manager.getActiveTaskCount() );
    manager.shutdown(); // 남은 티켓은 그룹 참조만 놓는다 — 풀 단언(나간 그룹 0)이 지킨다
}

/**
 * @brief High 태스크 안의 `runParallel` 은 자기 티켓을 같은 스레드에서 겹쳐 집지 않는다 — 워커가 많고 모두 붙들려 있어도 병렬 본문의 겹침이 한 겹이다
 * @details 예전에는 High 그룹의 청크 사이에서 High 줄을 비워, 아무도 집지 않은 자기 티켓을 본문 안에서 하나씩 겹쳐 돌았다(워커 수만큼 — 겹침 상한 단언 16). 워커 하나만 남기고
 *          모두 붙든 뒤 남은 하나가 High 태스크를 돈다. 겹침은 본문이 자기 스레드에서 다시 불린 수로 잰다.
 */
SW_TEST_CASE( TaskManagerTest, RunParallelInHighTaskDoesNotNestItsOwnTickets )
{
    constexpr uint32 kManyWorkerCount = 24; // 겹침 상한(16)보다 많다
    sw::TaskManager  manager;
    SW_REQUIRE_TRUE( manager.initialize( kManyWorkerCount ) );

    sw::atomic<uint32> heldCount{ 0 };
    sw::atomic<bool>   bReleaseWorkers{ false };
    for ( uint32 index = 0; index + 1 < kManyWorkerCount; ++index )
    {
        sw::TaskHandle holder = manager.emplaceTask( "HoldWorker", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&heldCount, &bReleaseWorkers]()
        {
            heldCount.fetch_add( 1, std::memory_order_acq_rel );
            while ( bReleaseWorkers.load( std::memory_order_acquire ) == false )
            {
                sw::ThreadUtil::sleepForMicroseconds( 100 ); // 코어보다 많은 워커가 서로를 굶기지 않게
            }
        } ) );
        holder.submit();
    }
    while ( heldCount.load( std::memory_order_acquire ) + 1 < kManyWorkerCount )
    {
        sw::ThreadUtil::yield();
    }

    sw::atomic<uint32> coveredCount{ 0 };
    sw::atomic<int32>  depth{ 0 };
    sw::atomic<int32>  maxDepth{ 0 };
    sw::atomic<bool>   bHighDone{ false };
    sw::TaskHandle     high = manager.emplaceTask( "HighParallel", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&manager, &coveredCount, &depth, &maxDepth, &bHighDone]()
        {
        manager.runParallel( 1024, 1, SW_DELEGATE_LAMBDA( sw::ParallelBlockDelegate, [&coveredCount, &depth, &maxDepth]( uint32 start, uint32 end )
            {
            // 남은 워커는 하나라 본문은 이 스레드에서만 돈다 — 동시에 둘이면 겹친 것이다.
            const int32 now = depth.fetch_add( 1, std::memory_order_acq_rel ) + 1;
            if ( now > maxDepth.load( std::memory_order_relaxed ) )
                maxDepth.store( now, std::memory_order_relaxed );
            coveredCount.fetch_add( end - start, std::memory_order_relaxed );
            depth.fetch_sub( 1, std::memory_order_acq_rel );
        } ) );
        bHighDone.store( true, std::memory_order_release );
    } ) );
    high.setPriority( sw::TaskPriority::High );
    high.submit();
    while ( bHighDone.load( std::memory_order_acquire ) == false )
    {
        sw::ThreadUtil::yield();
    }
    SW_EXPECT_EQUAL( 1024u, coveredCount.load() );
    SW_EXPECT_EQUAL( 1, maxDepth.load() );

    bReleaseWorkers.store( true, std::memory_order_release );
    manager.waitAll( "TaskManagerTest" );
    SW_EXPECT_EQUAL( 0u, manager.getActiveTaskCount() );
    manager.shutdown();
}

/**
 * @brief 어느 우선순위의 태스크 안에서 부른 `runParallel` 이든 구간을 빠짐없이 한 번씩 덮고 돌아온다(티켓은 그 태스크의 줄에 선다 — 세 줄 모두 다른 길이다)
 */
SW_TEST_CASE( TaskManagerTest, RunParallelInsideEachPriorityCoversEveryIndex )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( 2 ) );

    constexpr uint32              kCount = 256;
    sw::vector<sw::atomic<int32>> listHit( kCount );
    sw::atomic<int32>*            pHit = listHit.data();
    const sw::TaskPriority        arrPriority[]{ sw::TaskPriority::High, sw::TaskPriority::Normal, sw::TaskPriority::Low };
    for ( const sw::TaskPriority priority : arrPriority )
    {
        sw::TaskHandle outer = manager.emplaceTask( "PriorityParallel", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&manager, pHit]()
        {
            manager.runParallel( kCount, 1, SW_DELEGATE_LAMBDA( sw::ParallelBlockDelegate, [pHit]( uint32 start, uint32 end )
            {
                for ( uint32 index = start; index < end; ++index )
                {
                    pHit[index].fetch_add( 1, std::memory_order_relaxed );
                }
            } ) );
        } ) );
        outer.setPriority( priority );
        outer.submit();
        manager.waitAll( "TaskManagerTest" );
    }
    uint32 wrongCount = 0;
    for ( uint32 index = 0; index < kCount; ++index )
    {
        wrongCount += pHit[index].load() != 3 ? 1u : 0u;
    }
    SW_EXPECT_EQUAL( 0u, wrongCount );
    manager.shutdown();
}

/**
 * @brief `runParallelInto` 는 항목마다 제 칸 하나를 건넨다 — 모든 칸이 한 번씩, 자기 번호로 채워진다(고정 임계 · 비용 측정 둘 다)
 */
SW_TEST_CASE( TaskManagerTest, RunParallelIntoFillsEverySlotWithItsOwnIndex )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );
    constexpr uint32   kCount = 4096;
    sw::vector<uint32> listFixed( kCount, 0u );
    sw::vector<uint32> listByCost( kCount, 0u );
    auto               fill = []( uint32 index, uint32& outSlot )
    { outSlot += index * 3u + 1u; };
    manager.runParallelInto<uint32>( listFixed, 1, fill );
    manager.runParallelIntoByCost<uint32>( listByCost, fill );
    uint32 wrongCount = 0;
    for ( uint32 index = 0; index < kCount; ++index )
    {
        if ( listFixed[index] != index * 3u + 1u || listByCost[index] != index * 3u + 1u )
            ++wrongCount;
    }
    SW_EXPECT_EQUAL( 0u, wrongCount );
    manager.shutdown();
}

/**
 * @brief `runParallelReduce` 는 워커 수와 무관하게 같은 비트를 돌려준다 — 블록을 번호 순서로 접고 블록 순서로 합친 직렬 계산과 부동소수 합이 정확히 같다
 */
SW_TEST_CASE( TaskManagerTest, RunParallelReduceMatchesTheSerialBlockOrderBitForBit )
{
    constexpr uint32 kCount     = 20000;
    constexpr uint32 kBlockSize = 64;
    auto             addItem    = []( uint32 index, float32& inoutPartial )
    { inoutPartial += 1.0f / static_cast<float32>( index + 1u ); };
    auto combine = []( float32 left, float32 right )
    { return left + right; };

    float32 expected = 0.0f;
    for ( uint32 start = 0; start < kCount; start += kBlockSize )
    {
        float32 partial = 0.0f;
        for ( uint32 index = start; index < sw::MathUtil::min( kCount, start + kBlockSize ); ++index )
        {
            addItem( index, partial );
        }
        expected = ( start == 0 ) ? partial : combine( expected, partial );
    }

    for ( uint32 workerCount = 1; workerCount <= kWorkerCount; workerCount += 3 )
    {
        sw::TaskManager manager;
        SW_REQUIRE_TRUE( manager.initialize( workerCount ) );
        for ( uint32 round = 0; round < 4; ++round )
        {
            const float32 actual = manager.runParallelReduce( kCount, kBlockSize, 0.0f, addItem, combine );
            SW_EXPECT_TRUE( actual == expected );
        }
        SW_EXPECT_TRUE( manager.runParallelReduce( 0u, kBlockSize, 7.0f, addItem, combine ) == 7.0f ); // 빈 범위는 항등원
        manager.shutdown();
    }
}

/**
 * @brief 호출 스레드가 제 청크를 다 돌고 남의 청크를 기다리면 합류 대기가 한 번 세지고, 읽으면 횟수 · 시간이 0 으로 돌아간다
 * @details 순서는 시간이 아니라 사건으로 맞춘다: 첫 워커 청크는 합류 대기 구간이 열릴 때(프로파일 훅 `WaitJoin`)까지 끝나지 않고, 호출 스레드의 청크는
 *          워커가 청크 하나를 잡을 때까지 끝나지 않는다. 그래서 부하 · 스케줄과 무관하게 호출 스레드는 반드시 남의 청크를 기다린다(예전에는 워커가 5 ms 자는 동안
 *          호출 스레드가 먼저 합류 자리에 닿는다고 가정해 묶음 검증의 부하에서 졌다).
 */
SW_TEST_CASE( TaskManagerTest, JoinWaitIsCountedAndTakeResetsTheStats )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );
    (void)manager.takeWaitStats();

    s_startedZoneKindMask.store( 0, std::memory_order_relaxed );
    const sw::Subscription probe = sw::TaskManager::bindProfileHook( &kZoneStartProbe );

    sw::atomic<int32>     workerChunkCount{ 0 };
    const std::thread::id callerID = std::this_thread::get_id();
    manager.runParallel( 64, 1, SW_DELEGATE_LAMBDA( sw::ParallelBlockDelegate, [&workerChunkCount, callerID]( uint32, uint32 )
    {
        if ( std::this_thread::get_id() != callerID )
        {
            // 첫 워커 청크는 호출 스레드가 합류 대기에 들어갈 때까지 붙든다.
            if ( workerChunkCount.fetch_add( 1, std::memory_order_acq_rel ) == 0 )
                holdUntilZoneStarted( sw::TaskProfileZoneKind::WaitJoin );
            return;
        }
        // 호출 스레드 청크는 워커가 청크 하나를 잡을 때까지 붙든다 — 호출 스레드가 모든 청크를 혼자 돌면 기다릴 남의 청크가 없다.
        const sw::Deadline deadline = sw::Deadline::afterMilliseconds( kWaitTimeoutMs );
        while ( workerChunkCount.load( std::memory_order_acquire ) == 0 && deadline.isExpired() == false )
        {
            sw::ThreadUtil::yield();
        }
    } ) );

    const sw::TaskManager::WaitStats stats = manager.takeWaitStats();
    SW_EXPECT_TRUE( workerChunkCount.load() > 0 );
    SW_EXPECT_TRUE( isZoneStarted( sw::TaskProfileZoneKind::WaitJoin ) );
    SW_EXPECT_EQUAL( uint64{ 1 }, stats._joinWaitCount );
    // 대기 시간의 크기는 단언하지 않는다 — 표지가 켜진 뒤 워커가 곧 끝나면 대기가 시계 한 눈금보다 짧을 수 있다(사건으로 보장할 수 없는 값). 읽으면 0 으로 돌아가는지만 본다.
    const sw::TaskManager::WaitStats statsAfterTake = manager.takeWaitStats();
    SW_EXPECT_EQUAL( uint64{ 0 }, statsAfterTake._joinWaitCount );
    SW_EXPECT_EQUAL( uint64{ 0 }, statsAfterTake._joinWaitNanos );
    manager.shutdown();
}

/**
 * @brief `runParallelRangeInto` 는 구간마다 출력 여럿의 같은 구간 조각을 건넨다 — 모든 칸이 한 번씩 자기 번호로 채워지고, 빈 출력은 빈 조각으로 온다(고정 임계 · 비용 측정 둘 다)
 */
SW_TEST_CASE( TaskManagerTest, RunParallelRangeIntoHandsEachRangeItsOwnSlices )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( kWorkerCount ) );
    constexpr uint32  kCount = 5000;
    sw::atomic<int32> nonEmptyAbsentCount{ 0 };
    sw::atomic<int32> wrongSliceCount{ 0 };
    auto              fill = [&nonEmptyAbsentCount, &wrongSliceCount]( uint32 start, uint32 end, sw::span<uint32> outIndex, sw::span<float32> outHalf, sw::span<uint8> outAbsent )
    {
        if ( outAbsent.empty() == false )
            nonEmptyAbsentCount.fetch_add( 1, std::memory_order_relaxed );
        if ( outIndex.size() != end - start || outHalf.size() != end - start )
        {
            wrongSliceCount.fetch_add( 1, std::memory_order_relaxed );
            return;
        }
        for ( uint32 index = start; index < end; ++index )
        {
            outIndex[index - start] += index + 1u;
            outHalf[index - start] += static_cast<float32>( index ) * 0.5f;
        }
    };
    for ( uint32 round = 0; round < 2; ++round )
    {
        sw::vector<uint32>  listIndex( kCount, 0u );
        sw::vector<float32> listHalf( kCount, 0.0f );
        if ( round == 0 )
            manager.runParallelRangeInto( kCount, 1, fill, sw::span<uint32>( listIndex ), sw::span<float32>( listHalf ), sw::span<uint8>{} );
        else
            manager.runParallelRangeIntoByCost( kCount, fill, sw::span<uint32>( listIndex ), sw::span<float32>( listHalf ), sw::span<uint8>{} );
        uint32 wrongCount = 0;
        for ( uint32 index = 0; index < kCount; ++index )
        {
            if ( listIndex[index] != index + 1u || listHalf[index] != static_cast<float32>( index ) * 0.5f )
                ++wrongCount;
        }
        SW_EXPECT_EQUAL( 0u, wrongCount );
    }
    SW_EXPECT_EQUAL( 0, nonEmptyAbsentCount.load() );
    SW_EXPECT_EQUAL( 0, wrongSliceCount.load() );
    manager.shutdown();
}

namespace
{
    /** @brief 큐 용량을 넘겨 넣는 탐침의 상태입니다. 자식은 넣은 차례 번호를 들고 돌며, 우선순위 큐(High · Low)는 FIFO 라 그 차례대로 돌아야 한다. */
    struct QueueOverflowProbe
    {
        // 용량(High 1024 · Low 1024 · 워커 데크 4096 + Normal 전역 4096, `TaskManager.h`)을 넉넉히 넘긴다.
        static constexpr int32 kHighCount   = 1024 * 2 + 16;
        static constexpr int32 kLowCount    = 1024 * 2 + 16;
        static constexpr int32 kNormalCount = 4096 * 2 + 64;
        static constexpr int32 kTotalCount  = kHighCount + kLowCount + kNormalCount;

        sw::atomic<int32>  _highNext{ 0 };
        sw::atomic<int32>  _lowNext{ 0 };
        sw::atomic<int32>  _highOutOfOrderCount{ 0 };
        sw::atomic<int32>  _lowOutOfOrderCount{ 0 };
        sw::atomic<int32>  _normalRanCount{ 0 };
        sw::atomic<int32>  _doneCount{ 0 };
        sw::atomic<uint32> _allDoneWord{ 0 }; ///< 마지막 자식이 1 로 세우고 깨운다(메인은 태스크를 돕지 않는 Futex 로 기다린다)

        void finishOne()
        {
            if ( _doneCount.fetch_add( 1, std::memory_order_acq_rel ) + 1 != kTotalCount )
                return;
            _allDoneWord.store( 1, std::memory_order_release );
            sw::Futex::wakeAll( _allDoneWord );
        }
    };
} // namespace

/**
 * @brief 워커가 하나뿐일 때 워커 안에서 큐 용량을 넘겨 넣어도 끝나고, 모두 한 번씩 · 우선순위 큐는 넣은 차례대로 돈다
 * @details 전역 큐는 링이 차면 넘침 목록이 받는다(`OverflowConcurrentQueue`) — 넣는 쪽이 유일한 워커여도 기다리지 않고, 링 → 넘침 차례로 꺼내 FIFO 가 남는다.
 *          예전에는 가득 찬 큐에서 넣는 워커가 그 자리에서 실행했고 겹침 상한(16)을 넘으면 다시 깨우고 양보만 했다. 메인은 태스크를 돕지 않는 Futex 로 기다린다 —
 *          `waitAll` 로 기다리면 메인이 큐를 비워 결함이 가려진다. 끝나지 않으면 케이스 시한이 이름과 함께 끊는다.
 */
SW_TEST_CASE( TaskManagerTest, WorkerSubmittingPastQueueCapacityFinishesInOrder )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( 1 ) );

    QueueOverflowProbe  probe;
    QueueOverflowProbe* pProbe   = &probe;
    sw::TaskHandle      producer = manager.emplaceTask( "QueueOverflowProducer", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&manager, pProbe]()
         {
        for ( int32 index = 0; index < QueueOverflowProbe::kHighCount; ++index )
        {
            sw::TaskHandle high = manager.emplaceTask( "OverflowHigh", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [pProbe, index]()
                 {
                if ( pProbe->_highNext.fetch_add( 1, std::memory_order_relaxed ) != index )
                    pProbe->_highOutOfOrderCount.fetch_add( 1, std::memory_order_relaxed );
                pProbe->finishOne();
            } ) );
            high.setPriority( sw::TaskPriority::High );
            high.submit();
        }
        for ( int32 index = 0; index < QueueOverflowProbe::kLowCount; ++index )
        {
            sw::TaskHandle low = manager.emplaceTask( "OverflowLow", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [pProbe, index]()
                 {
                if ( pProbe->_lowNext.fetch_add( 1, std::memory_order_relaxed ) != index )
                    pProbe->_lowOutOfOrderCount.fetch_add( 1, std::memory_order_relaxed );
                pProbe->finishOne();
            } ) );
            low.setPriority( sw::TaskPriority::Low );
            low.submit();
        }
        for ( int32 index = 0; index < QueueOverflowProbe::kNormalCount; ++index )
        {
            sw::TaskHandle normal = manager.emplaceTask( "OverflowNormal", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [pProbe]()
                 {
                pProbe->_normalRanCount.fetch_add( 1, std::memory_order_relaxed );
                pProbe->finishOne();
            } ) );
            normal.submit();
        }
    } ) );
    producer.submit();

    while ( probe._allDoneWord.load( std::memory_order_acquire ) == 0 )
    {
        sw::Futex::wait( probe._allDoneWord, 0 );
    }
    manager.waitAll( "TaskManagerTest" );
    SW_EXPECT_EQUAL( 0u, manager.getActiveTaskCount() );
    SW_EXPECT_EQUAL( QueueOverflowProbe::kHighCount, probe._highNext.load() );
    SW_EXPECT_EQUAL( QueueOverflowProbe::kLowCount, probe._lowNext.load() );
    SW_EXPECT_EQUAL( QueueOverflowProbe::kNormalCount, probe._normalRanCount.load() );
    SW_EXPECT_EQUAL( 0, probe._highOutOfOrderCount.load() );
    SW_EXPECT_EQUAL( 0, probe._lowOutOfOrderCount.load() );
    manager.shutdown();
}

/**
 * @brief 워커가 아닌 스레드가 메인 스레드 큐 용량(1024)보다 많은 메인 전용 일을 넣어도 메인을 기다리지 않고 끝나고, 메인이 비우면 모두 한 번씩 돈다
 * @details 예전에는 메인 큐가 차면 넣는 스레드가 메인을 깨우고 양보하며 돌았다 — 메인이 그 스레드를 기다리고 있으면 둘 다 멈췄다. 여기서 메인은 넣는 스레드가
 *          끝나기를 먼저 기다린 뒤에야 비운다(그 순서가 예전에는 끝나지 않았다).
 */
SW_TEST_CASE( TaskManagerTest, MainThreadQueuePastCapacityDoesNotWaitForTheMainThread )
{
    sw::TaskManager manager;
    SW_REQUIRE_TRUE( manager.initialize( 1 ) );
    constexpr int32   kTaskCount = 1024 * 2 + 16;
    sw::atomic<int32> ranCount{ 0 };
    sw::atomic<int32> wrongThreadCount{ 0 };

    std::thread producer{ [&manager, &ranCount, &wrongThreadCount]()
    {
        for ( int32 index = 0; index < kTaskCount; ++index )
        {
            sw::TaskHandle handle = manager.emplaceTask( "MainOverflow", SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&manager, &ranCount, &wrongThreadCount]()
            {
                if ( manager.isMainThread() == false )
                    wrongThreadCount.fetch_add( 1, std::memory_order_relaxed );
                ranCount.fetch_add( 1, std::memory_order_relaxed );
            } ),
                                                         sw::TaskThreadAffinity::MainThread );
            handle.submit();
        }
    } };
    producer.join(); // 메인은 아직 비우지 않는다 — 넣는 쪽이 메인을 기다리면 여기서 끝나지 않는다
    SW_EXPECT_EQUAL( 0, ranCount.load() );

    manager.dispatchMainThreadTasks();
    manager.waitAll( "TaskManagerTest" );
    SW_EXPECT_EQUAL( kTaskCount, ranCount.load() );
    SW_EXPECT_EQUAL( 0, wrongThreadCount.load() );
    manager.shutdown();
}
