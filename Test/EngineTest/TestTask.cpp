#include "pch.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Memory/MemoryProfiler.h"
#include "Core/Task/TaskFuture.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"

#include "TestFramework/TestFramework.h"

namespace sw
{
    namespace
    {
        static atomic<int32> s_taskExecOrder{ 0 };
        static int32         s_orderA{ 0 };
        static int32         s_orderB{ 0 };
        static int32         s_orderC{ 0 };

        /** @brief 태스크 A 실행 순서를 기록합니다. */
        void taskFuncA()
        {
            s_orderA = ++s_taskExecOrder;
        }

        /** @brief 태스크 B 실행 순서를 기록합니다. */
        void taskFuncB()
        {
            s_orderB = ++s_taskExecOrder;
        }

        /** @brief 태스크 C 실행 순서를 기록합니다. */
        void taskFuncC()
        {
            s_orderC = ++s_taskExecOrder;
        }

        static int32      s_recInt{ 0 };
        static float64    s_recDouble{ 0.0 };
        static sw::string s_recStr = "";
        static void*      s_recPtr{ nullptr };

        struct CustomPlayerData
        {
            sw::string _name;
            int32      _level{ 0 };
        };

        static CustomPlayerData  s_recPlayer;
        static sw::vector<int32> s_recItems;

        /** @brief 임의 타입 인자를 수신 버퍼에 저장합니다. */
        void taskWithArbitraryArgs( const sw::TaskArgs& args )
        {
            s_recInt    = args.get<int32>( 0 );
            s_recDouble = args.get<float64>( 1 );
            s_recStr    = args.get<sw::string>( 2 );
            s_recPtr    = args.get<void*>( 3 );
        }

        /** @brief 커스텀 구조체와 컨테이너 인자를 수신 버퍼에 저장합니다. */
        void taskWithCustomStructAndContainer( const sw::TaskArgs& args )
        {
            s_recPlayer = args.get<CustomPlayerData>( 0 );
            s_recItems  = args.get<sw::vector<int32>>( 1 );
        }

    } // namespace
} // namespace sw

// ------------------------------------------------------------------------------
// 1) Engine_Task — DAG·병렬·체이닝·combinator
// ------------------------------------------------------------------------------
/**
 * @brief [TaskTest] 일반 태스크 DAG
 */
SW_TEST_CASE( TaskTest, GeneralTaskDAG )
{
    sw::TaskManager& taskMgr = sw::engine::getTaskManager();
    taskMgr.initialize();

    sw::s_taskExecOrder = 0;
    sw::s_orderA        = 0;
    sw::s_orderB        = 0;
    sw::s_orderC        = 0;

    sw::TaskDelegate delA = SW_DELEGATE_FUNCTION( sw::TaskDelegate, sw::taskFuncA );
    sw::TaskDelegate delB = SW_DELEGATE_FUNCTION( sw::TaskDelegate, sw::taskFuncB );
    sw::TaskDelegate delC = SW_DELEGATE_FUNCTION( sw::TaskDelegate, sw::taskFuncC );

    sw::TaskHandle handleA = taskMgr.emplaceTask( "TaskA", delA );
    sw::TaskHandle handleB = taskMgr.emplaceTask( "TaskB", delB );
    sw::TaskHandle handleC = taskMgr.emplaceTask( "TaskC", delC );

    handleA.precede( handleB );
    handleB.precede( handleC );

    handleA.submit();
    handleB.submit();
    handleC.submit();

    taskMgr.waitAll();

    SW_EXPECT_TRUE( sw::s_orderA > 0 );
    SW_EXPECT_TRUE( sw::s_orderB > sw::s_orderA );
    SW_EXPECT_TRUE( sw::s_orderC > sw::s_orderB );

    taskMgr.clear();
}

/**
 * @brief [TaskTest] 임의 인자 태스크
 */
SW_TEST_CASE( TaskTest, ArbitraryArgsTask )
{
    sw::TaskManager& taskMgr = sw::engine::getTaskManager();
    taskMgr.initialize();

    int32        dummyVar = 42;
    sw::TaskArgs args =
        sw::MakeTaskArgs<int32, float64, sw::string, void*>( 100, 3.14159, sw::string( "HelloTask" ), &dummyVar );
    sw::TaskArgsDelegate argsDel = SW_DELEGATE_FUNCTION( sw::TaskArgsDelegate, sw::taskWithArbitraryArgs );

    taskMgr.emplaceTask( "ArgsTask", argsDel, args ).submit();
    taskMgr.waitAll();

    SW_EXPECT_EQUAL( 100, sw::s_recInt );
    SW_EXPECT_TRUE( sw::MathUtil::nearEqual( sw::s_recDouble, 3.14159, 0.0001 ) );
    SW_EXPECT_EQUAL( sw::string( "HelloTask" ), sw::s_recStr );
    SW_EXPECT_EQUAL( static_cast<void*>( &dummyVar ), sw::s_recPtr );

    taskMgr.clear();

    sw::CustomPlayerData inputPlayer{ "Antigravity", 99 };
    sw::vector<int32>    inputItems{ 10, 20, 30 };

    sw::TaskArgs         customArgs = sw::MakeTaskArgs<sw::CustomPlayerData, sw::vector<int32>>( inputPlayer, inputItems );
    sw::TaskArgsDelegate customDel  = SW_DELEGATE_FUNCTION( sw::TaskArgsDelegate, sw::taskWithCustomStructAndContainer );

    taskMgr.emplaceTask( "CustomArgsTask", customDel, customArgs ).submit();
    taskMgr.waitAll();

    SW_EXPECT_EQUAL( sw::string( "Antigravity" ), sw::s_recPlayer._name );
    SW_EXPECT_EQUAL( 99, sw::s_recPlayer._level );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( sw::s_recItems.size() ) );
    if ( sw::s_recItems.size() == 3 )
    {
        SW_EXPECT_EQUAL( 10, sw::s_recItems[0] );
        SW_EXPECT_EQUAL( 20, sw::s_recItems[1] );
        SW_EXPECT_EQUAL( 30, sw::s_recItems[2] );
    }

    taskMgr.clear();
}

/**
 * @brief [TaskTest] 병렬 태스크
 */
SW_TEST_CASE( TaskTest, ParallelTask )
{
    sw::TaskManager& taskMgr = sw::engine::getTaskManager();
    taskMgr.initialize();

    constexpr uint32                      kElementCount = 100;
    static sw::vector<sw::atomic<uint32>> s_results( kElementCount );
    for ( uint32 elementIndex = 0; elementIndex < kElementCount; ++elementIndex )
    {
        s_results[elementIndex] = 0;
    }

    static sw::atomic<uint32>* s_resultsPtr = s_results.data();

    struct ParallelContext
    {
        static void processIndex( uint32 index )
        {
            s_resultsPtr[index].fetch_add( 1, std::memory_order_relaxed );
        }
    };

    sw::ParallelTaskDelegate parallelDel = SW_DELEGATE_FUNCTION( sw::ParallelTaskDelegate, ParallelContext::processIndex );
    taskMgr.emplaceParallel( "ParallelArray", kElementCount, parallelDel ).submit();

    taskMgr.waitAll();

    for ( uint32 elementIndex = 0; elementIndex < kElementCount; ++elementIndex )
    {
        SW_EXPECT_EQUAL( 1u, s_results[elementIndex].load() );
    }

    taskMgr.clear();
}

/**
 * @brief [TaskTest] 스테이지 태스크
 */
SW_TEST_CASE( TaskTest, StagedTask )
{
    sw::TaskManager& taskMgr = sw::engine::getTaskManager();
    taskMgr.initialize();

    static sw::atomic<uint32> s_stageProgress{ 0 };
    s_stageProgress = 0;

    struct StageContext
    {
        static void runStageTask1()
        {
            s_stageProgress.fetch_add( 10, std::memory_order_relaxed );
        }

        static void runStageTask2()
        {
            s_stageProgress.fetch_add( 20, std::memory_order_relaxed );
        }
    };

    sw::TaskStageHandle stage = taskMgr.createStage();

    sw::TaskHandle t1 = taskMgr.emplaceTask( "StageTask1", SW_DELEGATE_FUNCTION( sw::TaskDelegate, StageContext::runStageTask1 ) );
    sw::TaskHandle t2 = taskMgr.emplaceTask( "StageTask2", SW_DELEGATE_FUNCTION( sw::TaskDelegate, StageContext::runStageTask2 ) );

    stage.addTask( t1 );
    stage.addTask( t2 );

    t1.submit();
    t2.submit();

    taskMgr.waitStage( stage );

    SW_EXPECT_TRUE( taskMgr.isStageComplete( stage ) );
    SW_EXPECT_EQUAL( 30u, s_stageProgress.load() );

    taskMgr.clear();
}

/**
 * @brief [TaskTest] then 체이닝
 */
SW_TEST_CASE( TaskTest, TaskChainingThen )
{
    sw::TaskManager& taskMgr = sw::engine::getTaskManager();
    taskMgr.initialize();

    static sw::mutex         s_chainMutex;
    static sw::vector<int32> s_executionOrder;
    {
        std::scoped_lock<sw::mutex> lock{ s_chainMutex };
        s_executionOrder.clear();
    }

    struct ChainContext
    {
        static void step1()
        {
            std::scoped_lock<sw::mutex> lock{ s_chainMutex };
            s_executionOrder.push_back( 1 );
        }
        static void step2()
        {
            std::scoped_lock<sw::mutex> lock{ s_chainMutex };
            s_executionOrder.push_back( 2 );
        }
        static void step3()
        {
            std::scoped_lock<sw::mutex> lock{ s_chainMutex };
            s_executionOrder.push_back( 3 );
        }
    };

    sw::TaskHandle t1 = taskMgr.emplaceTask( SW_DELEGATE_FUNCTION( sw::TaskDelegate, ChainContext::step1 ) );
    sw::TaskHandle t2 = t1.then( SW_DELEGATE_FUNCTION( sw::TaskDelegate, ChainContext::step2 ) );
    sw::TaskHandle t3 = t2.then( SW_DELEGATE_FUNCTION( sw::TaskDelegate, ChainContext::step3 ) );

    t1.submit();
    t2.submit();
    t3.submit();

    taskMgr.waitAll();

    {
        std::scoped_lock<sw::mutex> lock{ s_chainMutex };
        SW_EXPECT_EQUAL( 3, static_cast<int32>( s_executionOrder.size() ) );
        if ( s_executionOrder.size() == 3 )
        {
            SW_EXPECT_EQUAL( 1, s_executionOrder[0] );
            SW_EXPECT_EQUAL( 2, s_executionOrder[1] );
            SW_EXPECT_EQUAL( 3, s_executionOrder[2] );
        }
    }

    taskMgr.clear();
}

/**
 * @brief [TaskTest] 워크 스틸링 병렬 태스크
 */
SW_TEST_CASE( TaskTest, WorkStealingParallelTask )
{
    sw::TaskManager& taskMgr = sw::engine::getTaskManager();
    taskMgr.initialize();

    constexpr uint32   count = 50;
    sw::atomic<uint32> totalSum{ 0 };

    for ( uint32 taskIndex = 0; taskIndex < count; ++taskIndex )
    {
        taskMgr.emplaceTask( SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&totalSum, taskIndex]()
        {
            totalSum.fetch_add( taskIndex + 1, std::memory_order_relaxed );
        } ) )
            .submit();
    }

    taskMgr.waitAll();

    uint32 expectedSum = ( count * ( count + 1 ) ) / 2;
    SW_EXPECT_EQUAL( expectedSum, totalSum.load() );

    taskMgr.clear();
}

/**
 * @brief [TaskTest] whenAll 콤비네이터
 */
SW_TEST_CASE( TaskTest, TaskCombinatorWhenAll )
{
    sw::TaskManager& taskMgr = sw::engine::getTaskManager();
    taskMgr.initialize();

    sw::atomic<int32> completedCount{ 0 };
    bool              whenAllExecuted{ false };

    sw::TaskHandle t1 = taskMgr.emplaceTask( SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&completedCount]()
    { completedCount.fetch_add( 1 ); } ) );
    sw::TaskHandle t2 = taskMgr.emplaceTask( SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&completedCount]()
    { completedCount.fetch_add( 1 ); } ) );
    sw::TaskHandle t3 = taskMgr.emplaceTask( SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&completedCount]()
    { completedCount.fetch_add( 1 ); } ) );

    sw::TaskHandle whenAllTask = taskMgr.whenAll( { t1, t2, t3 }, SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&whenAllExecuted, &completedCount]()
    {
        if ( completedCount.load() == 3 )
            whenAllExecuted = true;
    } ) );

    t1.submit();
    t2.submit();
    t3.submit();
    whenAllTask.submit();

    taskMgr.waitAll();

    SW_EXPECT_EQUAL( 3, completedCount.load() );
    SW_EXPECT_TRUE( whenAllExecuted );

    taskMgr.clear();
}

/**
 * @brief [TaskTest] whenAny 콤비네이터
 */
SW_TEST_CASE( TaskTest, TaskCombinatorWhenAny )
{
    sw::TaskManager& taskMgr = sw::engine::getTaskManager();
    taskMgr.initialize();

    bool whenAnyExecuted{ false };

    sw::TaskHandle t1 = taskMgr.emplaceTask( SW_DELEGATE_LAMBDA( sw::TaskDelegate, []() {} ) );
    sw::TaskHandle t2 = taskMgr.emplaceTask( SW_DELEGATE_LAMBDA( sw::TaskDelegate, []() {} ) );

    sw::TaskHandle whenAnyTask = taskMgr.whenAny( { t1, t2 }, SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&whenAnyExecuted]()
    {
        whenAnyExecuted = true;
    } ) );

    t1.submit();
    t2.submit();
    whenAnyTask.submit();

    taskMgr.waitAll();

    SW_EXPECT_TRUE( whenAnyExecuted );

    taskMgr.clear();
}

/**
 * @brief [TaskTest] 태스크 취소(cancel) 및 CancellationToken 검증
 */
SW_TEST_CASE( TaskTest, CancellationTokenAndCancellation )
{
    sw::TaskManager& taskMgr = sw::engine::getTaskManager();
    taskMgr.initialize();

    bool bTaskExecuted{ false };
    bool bChainedTaskExecuted{ false };

    sw::TaskHandle handle = taskMgr.emplaceTask( SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&bTaskExecuted]()
    {
        bTaskExecuted = true;
    } ) );

    sw::TaskHandle chained = handle.then( SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&bChainedTaskExecuted]()
    {
        bChainedTaskExecuted = true;
    } ) );

    // Cancel the root task before submit
    SW_EXPECT_TRUE( handle.cancel() );
    SW_EXPECT_TRUE( handle.isCancelled() );

    handle.submit();
    chained.submit();

    taskMgr.waitAll();

    // Cancelled task should NOT have executed its body, but should have triggered its successors!
    SW_EXPECT_FALSE( bTaskExecuted );
    SW_EXPECT_TRUE( bChainedTaskExecuted );

    // CancellationToken test
    sw::CancellationToken token;
    SW_EXPECT_FALSE( token.isCancelled() );
    token.cancel();
    SW_EXPECT_TRUE( token.isCancelled() );

    taskMgr.clear();
}

/**
 * @brief [TaskTest] TaskManager::waitAll(timeoutMs) 정상 완료 및 타임아웃 지원 검증
 */
SW_TEST_CASE( TaskTest, WaitAllWithTimeout )
{
    sw::TaskManager& taskMgr = sw::engine::getTaskManager();
    taskMgr.initialize();

    sw::atomic<bool> bCompleted{ false };

    sw::TaskHandle handle = taskMgr.emplaceTask( SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&bCompleted]()
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        bCompleted.store( true, std::memory_order_release );
    } ) );

    handle.submit();

    // Wait with 5000ms timeout — should finish well within timeout and return true
    const bool bFinished = taskMgr.waitAll( 5000 );
    SW_EXPECT_TRUE( bFinished );
    SW_EXPECT_TRUE( bCompleted.load( std::memory_order_acquire ) );

    taskMgr.clear();
}

/**
 * @brief [TaskTest] C++17 호환 TaskFuture / TaskPromise Fluent 체이닝 및 비동기 파이프라인 검증
 */
SW_TEST_CASE( TaskTest, TaskFutureMonadicPipeline )
{
    sw::TaskPromise<int32> promise;
    sw::TaskFuture<int32>  future = promise.getFuture();

    SW_EXPECT_TRUE( future.isValid() );
    SW_EXPECT_FALSE( future.isReady() );

    int32 finalResult = 0;

    // Fluent monadic continuation chain
    auto chained = future
                       .then( []( int32 val )
    {
        return val * 2;
    } ).then( []( int32 val )
    {
        return val + 10;
    } ).then( [&finalResult]( int32 val )
    {
        finalResult = val;
    } );

    promise.setValue( 50 ); // (50 * 2) + 10 = 110

    chained.wait();

    SW_EXPECT_TRUE( chained.isReady() );
    SW_EXPECT_EQUAL( 110, finalResult );
}

/**
 * @brief [TaskTest] TaskFuture fallback(기본값 복구) 기능 검증
 */
SW_TEST_CASE( TaskTest, TaskFutureFallback )
{
    // 1) 유효한 Future + 값 설정 시 원본 값 유지
    sw::TaskPromise<int32> promiseValid;
    sw::TaskFuture<int32>  futureValid     = promiseValid.getFuture();
    sw::TaskFuture<int32>  fallbackFuture1 = futureValid.fallback( -1 );

    promiseValid.setValue( 42 );
    SW_EXPECT_TRUE( fallbackFuture1.isReady() );
    SW_EXPECT_EQUAL( 42, fallbackFuture1.get() );

    // 2) 유효하지 않은 Future(기본 생성)에 대해 fallback 값 즉시 반환
    sw::TaskFuture<int32> invalidFuture;
    sw::TaskFuture<int32> fallbackFuture2 = invalidFuture.fallback( 999 );

    SW_EXPECT_TRUE( fallbackFuture2.isReady() );
    SW_EXPECT_EQUAL( 999, fallbackFuture2.get() );
}

/**
 * @brief [TaskTest] 유효하지 않거나 빈 입력에도 콤비네이터가 멈추지 않는다
 * @details `then` 은 상태가 없는 future 에 콜백을 걸어 주지 않고 그냥 돌아간다. 그런 자리를
 *          카운트다운에 넣으면 `whenAllFutures` 의 결과가 **영원히 끝나지 않았다** — 기본 생성된
 *          future 하나가 섞이는 것으로 충분했다. `whenAnyFuture` 는 빈 목록에서 **유효한** future 를
 *          돌려줬는데 아무도 값을 넣어 주지 않아 `wait()` 가 영원히 멈췄다. 형제인 `whenAllFutures`
 *          는 빈 목록을 제대로 끝냈다 — 같은 질문에 둘이 다르게 답하고 있었다.
 */
SW_TEST_CASE( TaskTest, CombinatorsDoNotHangOnInvalidOrEmptyInput )
{
    // 1) whenAll — 기다릴 수 있는 것 하나 + 유효하지 않은 것 하나.
    {
        sw::TaskPromise<int32> promise;
        promise.setValue( 7 );

        sw::vector<sw::TaskFuture<int32>> listFuture;
        listFuture.push_back( promise.getFuture() );
        listFuture.push_back( sw::TaskFuture<int32>{} );

        sw::TaskFuture<sw::vector<int32>> allFuture = sw::whenAllFutures( listFuture );
        SW_ASSERT_TRUE( allFuture.waitFor( 2000 ) );

        const sw::vector<int32> results = allFuture.get();
        SW_ASSERT_EQUAL( size_t( 2 ), results.size() );
        SW_EXPECT_EQUAL( 7, results[0] );
        SW_EXPECT_EQUAL( 0, results[1] ); // 기다릴 수 없는 자리는 기본값으로 남는다
    }

    // 2) whenAll — 빈 목록은 곧바로 끝난 빈 결과다.
    {
        const sw::vector<sw::TaskFuture<int32>> listEmpty;
        sw::TaskFuture<sw::vector<int32>>       allFuture = sw::whenAllFutures( listEmpty );
        SW_ASSERT_TRUE( allFuture.waitFor( 2000 ) );
        SW_EXPECT_TRUE( allFuture.get().empty() );
    }

    // 3) whenAny — 후보가 없으면 **유효하지 않은** future 여야 한다. 기다릴 수 있으면 안 된다.
    {
        const sw::vector<sw::TaskFuture<int32>> listEmpty;
        SW_EXPECT_FALSE( sw::whenAnyFuture( listEmpty ).isValid() );

        sw::vector<sw::TaskFuture<int32>> listAllInvalid;
        listAllInvalid.push_back( sw::TaskFuture<int32>{} );
        SW_EXPECT_FALSE( sw::whenAnyFuture( listAllInvalid ).isValid() );
    }

    // 4) whenAny — 유효하지 않은 것이 섞여도 유효한 쪽이 이긴다.
    {
        sw::TaskPromise<int32>            promise;
        sw::vector<sw::TaskFuture<int32>> listFuture;
        listFuture.push_back( sw::TaskFuture<int32>{} );
        listFuture.push_back( promise.getFuture() );

        sw::TaskFuture<int32> anyFuture = sw::whenAnyFuture( listFuture );
        SW_ASSERT_TRUE( anyFuture.isValid() );

        promise.setValue( 42 );
        SW_ASSERT_TRUE( anyFuture.waitFor( 2000 ) );
        SW_EXPECT_EQUAL( 42, anyFuture.get() );
    }
}

/**
 * @brief [TaskTest] TaskFuture whenAllFutures 콤비네이터 검증
 */
SW_TEST_CASE( TaskTest, TaskFutureWhenAllCombinator )
{
    sw::TaskPromise<int32> promise1;
    sw::TaskPromise<int32> promise2;
    sw::TaskPromise<int32> promise3;

    sw::vector<sw::TaskFuture<int32>> listFutures;
    listFutures.push_back( promise1.getFuture() );
    listFutures.push_back( promise2.getFuture() );
    listFutures.push_back( promise3.getFuture() );

    sw::TaskFuture<sw::vector<int32>> allFuture = sw::whenAllFutures( listFutures );
    SW_EXPECT_TRUE( allFuture.isValid() );
    SW_EXPECT_FALSE( allFuture.isReady() );

    promise1.setValue( 10 );
    SW_EXPECT_FALSE( allFuture.isReady() );

    promise2.setValue( 20 );
    SW_EXPECT_FALSE( allFuture.isReady() );

    promise3.setValue( 30 );
    SW_EXPECT_TRUE( allFuture.isReady() );

    sw::vector<int32> results = allFuture.get();
    SW_ASSERT_EQUAL( size_t( 3 ), results.size() );
    SW_EXPECT_EQUAL( 10, results[0] );
    SW_EXPECT_EQUAL( 20, results[1] );
    SW_EXPECT_EQUAL( 30, results[2] );
}

/**
 * @brief [TaskTest] TaskFuture whenAnyFuture 콤비네이터 검증
 */
SW_TEST_CASE( TaskTest, TaskFutureWhenAnyCombinator )
{
    sw::TaskPromise<sw::string> promise1;
    sw::TaskPromise<sw::string> promise2;

    sw::vector<sw::TaskFuture<sw::string>> listFutures;
    listFutures.push_back( promise1.getFuture() );
    listFutures.push_back( promise2.getFuture() );

    sw::TaskFuture<sw::string> anyFuture = sw::whenAnyFuture( listFutures );
    SW_EXPECT_TRUE( anyFuture.isValid() );
    SW_EXPECT_FALSE( anyFuture.isReady() );

    // 두 번째 태스크가 먼저 완료
    promise2.setValue( "SecondWinner" );
    SW_EXPECT_TRUE( anyFuture.isReady() );
    SW_EXPECT_EQUAL( sw::string( "SecondWinner" ), anyFuture.get() );

    // 첫 번째 태스크 완료되어도 Any 결과는 불변
    promise1.setValue( "FirstLate" );
    SW_EXPECT_EQUAL( sw::string( "SecondWinner" ), anyFuture.get() );
}

/**
 * @brief [TaskTest] TaskFuture 30단계 Deep Continuation 체이닝 스트레스 테스트
 */
SW_TEST_CASE( TaskTest, TaskFutureDeepContinuationChainStress )
{
    sw::TaskPromise<int32> initialPromise;
    sw::TaskFuture<int32>  currentFuture = initialPromise.getFuture();

    // 30단계 체인 생성: 매 단계마다 index를 더함 (초기 0 -> 0 + 1 + 2 + ... + 30 = 465)
    constexpr int32 kStages = 30;
    for ( int32 stageIndex = 1; stageIndex <= kStages; ++stageIndex )
    {
        currentFuture = currentFuture.then( [stageIndex]( int32 val )
        {
            return val + stageIndex;
        } );
    }

    SW_EXPECT_FALSE( currentFuture.isReady() );
    initialPromise.setValue( 0 );

    SW_EXPECT_TRUE( currentFuture.isReady() );
    constexpr int32 expectedSum = ( kStages * ( kStages + 1 ) ) / 2; // 465
    SW_EXPECT_EQUAL( expectedSum, currentFuture.get() );
}

/**
 * @brief [TaskTest] TaskFuture whenAll 대규모 동시성(64개 워커) 스트레스 테스트
 */
SW_TEST_CASE( TaskTest, TaskFutureWhenAllMassiveConcurrencyStress )
{
    sw::TaskManager& taskMgr = sw::engine::getTaskManager();
    taskMgr.initialize();

    constexpr int32 kTaskCount = 64;

    struct SharedTaskContext
    {
        sw::TaskPromise<int32> _promise{};
    };

    sw::vector<sw::shared_ptr<SharedTaskContext>> listContext;
    sw::vector<sw::TaskFuture<int32>>             listFuture;
    listContext.reserve( kTaskCount );
    listFuture.reserve( kTaskCount );

    for ( int32 index = 0; index < kTaskCount; ++index )
    {
        auto pCtx = sw::make_shared<SharedTaskContext>();
        listFuture.push_back( pCtx->_promise.getFuture() );
        listContext.push_back( pCtx );
    }

    sw::TaskFuture<sw::vector<int32>> allFuture = sw::whenAllFutures( listFuture );
    SW_EXPECT_FALSE( allFuture.isReady() );

    int64 expectedSum = 0;
    for ( size_t index = 0; index < static_cast<size_t>( kTaskCount ); ++index )
    {
        const int32 val = static_cast<int32>( ( index + 1 ) * 10 );
        expectedSum += val;

        auto pCtx = listContext[index];
        taskMgr.emplaceTask(
                   "WhenAllWorker",
                   SW_DELEGATE_LAMBDA(
                       sw::TaskDelegate,
                       [pCtx, val]()
        {
            pCtx->_promise.setValue( val );
        } ),
                   sw::TaskThreadAffinity::Any )
            .submit();
    }

    taskMgr.waitAll();

    SW_EXPECT_TRUE( allFuture.isReady() );
    sw::vector<int32> listResult = allFuture.get();
    SW_ASSERT_EQUAL( static_cast<size_t>( kTaskCount ), listResult.size() );

    int64 actualSum = 0;
    for ( size_t index = 0; index < static_cast<size_t>( kTaskCount ); ++index )
    {
        actualSum += listResult[index];
        SW_EXPECT_EQUAL( static_cast<int32>( ( index + 1 ) * 10 ), listResult[index] );
    }
    SW_EXPECT_EQUAL( expectedSum, actualSum );
}

/**
 * @brief [TaskTest] TaskFuture whenAny 다중 스레드 레이스 스트레스 테스트
 */
SW_TEST_CASE( TaskTest, TaskFutureWhenAnyRaceStress )
{
    sw::TaskManager& taskMgr = sw::engine::getTaskManager();
    taskMgr.initialize();

    constexpr int32 kRacers = 32;

    struct SharedRacerContext
    {
        sw::TaskPromise<int32> _promise{};
    };

    sw::vector<sw::shared_ptr<SharedRacerContext>> listContext;
    sw::vector<sw::TaskFuture<int32>>              listFuture;
    listContext.reserve( kRacers );
    listFuture.reserve( kRacers );

    for ( int32 index = 0; index < kRacers; ++index )
    {
        auto pCtx = sw::make_shared<SharedRacerContext>();
        listFuture.push_back( pCtx->_promise.getFuture() );
        listContext.push_back( pCtx );
    }

    sw::TaskFuture<int32> anyFuture = sw::whenAnyFuture( listFuture );
    SW_EXPECT_FALSE( anyFuture.isReady() );

    for ( size_t index = 0; index < static_cast<size_t>( kRacers ); ++index )
    {
        auto        pCtx       = listContext[index];
        const int32 racerIndex = static_cast<int32>( index );
        taskMgr.emplaceTask(
                   "RacerTask",
                   SW_DELEGATE_LAMBDA(
                       sw::TaskDelegate,
                       [pCtx, racerIndex]()
        {
            pCtx->_promise.setValue( racerIndex );
        } ),
                   sw::TaskThreadAffinity::Any )
            .submit();
    }

    taskMgr.waitAll();

    SW_EXPECT_TRUE( anyFuture.isReady() );
    const int32 winnerIndex = anyFuture.get();
    SW_EXPECT_TRUE( 0 <= winnerIndex && winnerIndex < kRacers );
}

/**
 * @brief [TaskTest] 무효한 future 의 `then` 은 **무효한 future** 를 돌려준다.
 * @details 예전에는 유효한(그러나 아무도 값을 넣어 주지 않는) future 를 돌려줬다. 그것을 `wait()`
 *          하면 영원히 멈춘다 — 조건 변수는 절대 깨어나지 않는다. 증상은 "느리다" 가 아니라
 *          **완전한 정지**이고, 스택만 보면 기다리는 것이 정상인지 아닌지 알 수 없다.
 *
 *          그 함정을 `whenAllFutures` · `whenAnyFuture` 가 각자 우회하고 있었다(유효한 것만 세고,
 *          후보가 없으면 무효를 돌려준다). 우회가 두 벌이면 세 번째 호출부가 같은 함정에 빠진다 —
 *          그래서 뿌리인 `then` 을 고쳤다. 네 조합(T→T · T→void · void→T · void→void)을 모두 본다:
 *          `TaskFuture<T>` 와 `TaskFuture<void>` 는 **서로 다른 특수화**라 한쪽만 고쳐질 수 있다.
 *
 * @note 여기서 `wait()` 를 부르지 않는 것은 일부러다 — 회귀가 나면 그 호출이 테스트를 **멈춰
 *       세운다**(실패가 아니라 정지다). `isValid()` 와 `waitFor` 로만 묻는다.
 */
SW_TEST_CASE( TaskTest, InvalidFutureThenStaysInvalid )
{
    constexpr uint32 kShortTimeoutMs = 20;

    BLOCK( "T -> T" )
    {
        sw::TaskFuture<int32> invalidFuture;
        SW_ASSERT_FALSE( invalidFuture.isValid() );

        sw::TaskFuture<int32> chained = invalidFuture.then( []( const int32& value )
        {
            return value + 1;
        } );
        SW_EXPECT_FALSE_MSG( chained.isValid(),
                             "무효한 future 의 then 이 유효한 future 를 돌려주면 wait() 가 영원히 멈춥니다" );
        SW_EXPECT_FALSE( chained.waitFor( kShortTimeoutMs ) );
    }

    BLOCK( "T -> void" )
    {
        sw::TaskFuture<int32> invalidFuture;
        sw::TaskFuture<void>  chained = invalidFuture.then( []( const int32& ) {} );
        SW_EXPECT_FALSE_MSG( chained.isValid(), "T -> void 사슬에서도 무효가 전해져야 합니다" );
        SW_EXPECT_FALSE( chained.waitFor( kShortTimeoutMs ) );
    }

    BLOCK( "void -> T" )
    {
        sw::TaskFuture<void> invalidFuture;
        SW_ASSERT_FALSE( invalidFuture.isValid() );

        sw::TaskFuture<int32> chained = invalidFuture.then( []()
        {
            return 7;
        } );
        SW_EXPECT_FALSE_MSG( chained.isValid(), "TaskFuture<void> 특수화도 같은 규칙을 따라야 합니다" );
        SW_EXPECT_FALSE( chained.waitFor( kShortTimeoutMs ) );
    }

    BLOCK( "void -> void" )
    {
        sw::TaskFuture<void> invalidFuture;
        sw::TaskFuture<void> chained = invalidFuture.then( []() {} );
        SW_EXPECT_FALSE_MSG( chained.isValid(), "void -> void 사슬에서도 무효가 전해져야 합니다" );
        SW_EXPECT_FALSE( chained.waitFor( kShortTimeoutMs ) );
    }

    BLOCK( "유효한 future 는 그대로 이어진다" )
    {
        sw::TaskPromise<int32> promise;
        sw::TaskFuture<int32>  chained = promise.getFuture().then( []( const int32& value )
        {
            return value * 2;
        } );
        SW_ASSERT_TRUE( chained.isValid() );

        promise.setValue( 21 );
        SW_EXPECT_TRUE( chained.waitFor( 1000 ) );
        SW_EXPECT_EQUAL( 42, chained.get() );
    }
}

/**
 * @brief [TaskTest] 병렬 그룹은 잠든 워커를 한 번만 깨운다
 * @details 서브태스크마다 깨우면 깨우기(뮤텍스 + 조건 변수 시그널)가 청크 수만큼 반복된다 — 청크 32 개에
 *          디스패치가 125 us 였고 그 안의 일은 몇 us 였다. 그룹 하나는 시그널 하나여야 한다.
 *          부모 태스크가 준비될 때(마지막 자식이 끝날 때) 워커가 한 번 더 깨울 수 있으므로 상한은 2 다.
 */
SW_TEST_CASE( TaskTest, ParallelGroupWakesWorkersOnce )
{
    sw::TaskManager& taskMgr = sw::engine::getTaskManager();
    taskMgr.initialize();

    // 워커를 재운다 — 스핀 예산(수십 us)이 지나야 잠들므로 넉넉히 기다린다.
    std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    const uint32 wakeBefore = taskMgr.getWakeSignalCount();

    static sw::atomic<uint32> s_processedCount{ 0 };
    s_processedCount = 0;
    struct WakeOnceContext
    {
        static void processRange( uint32 start, uint32 end ) { s_processedCount.fetch_add( end - start, std::memory_order_relaxed ); }
    };

    constexpr uint32    kItemCount = 64;
    sw::TaskStageHandle stage      = taskMgr.createStage();
    sw::TaskHandle      handle     = taskMgr.emplaceParallelBlock( 0, kItemCount, SW_DELEGATE_FUNCTION( sw::ParallelBlockDelegate, WakeOnceContext::processRange ) );
    stage.addTask( handle );
    handle.submit();
    taskMgr.waitStage( stage );

    SW_EXPECT_EQUAL( kItemCount, s_processedCount.load() );
    const uint32 wakeCount = taskMgr.getWakeSignalCount() - wakeBefore;
    SW_EXPECT_TRUE_MSG( wakeCount <= 2, ( sw::string( "병렬 그룹 하나가 워커를 " ) + sw::to_string( wakeCount ) + " 번 깨웠다 — 서브태스크마다 깨우고 있다" ).c_str() );

    taskMgr.clear();
}

/**
 * @brief [TaskTest] High 우선순위 태스크는 줄 앞으로 간다
 * @details 워커를 전부 문 앞에 세우고(문이 열릴 때까지 도는 Normal 태스크), 그 뒤에 Normal 을 잔뜩 줄 세운 다음
 *          High 하나를 넣고 문을 연다. 처음 비는 워커가 High 를 집어야 한다 — 시작 순번이 워커 수의 두 배를
 *          넘지 않는다. 우선순위를 저장만 하고 보지 않으면 High 는 FIFO 꼬리라 순번이 (전체 - 워커 수) 뒤다.
 */
SW_TEST_CASE( TaskTest, HighPriorityTaskJumpsTheQueue )
{
    sw::TaskManager& taskMgr = sw::engine::getTaskManager();
    taskMgr.initialize();
    const uint32 workerCount = taskMgr.getWorkerCount();
    SW_ASSERT_TRUE( workerCount >= 1 );

    // 지역 구조체는 정적 멤버를 못 가진다 — 함수 지역 static 으로 둔다(위 WakeOnce 와 같은 꼴).
    static sw::atomic<bool>   s_bGateOpen{ false };
    static sw::atomic<uint32> s_runningCount{ 0 };
    static sw::atomic<uint32> s_startOrder{ 0 };
    static sw::atomic<uint32> s_highOrder{ 0 };
    s_bGateOpen    = false;
    s_runningCount = 0;
    s_startOrder   = 0;
    s_highOrder    = 0;

    struct PriorityContext
    {
        /** @brief 문이 열릴 때까지 워커를 붙든다. 끝은 횟수가 아니라 시간으로 잡는다(고장 나도 CI 가 매달리지 않게). */
        static void gatedNormal()
        {
            s_startOrder.fetch_add( 1, std::memory_order_relaxed );
            s_runningCount.fetch_add( 1, std::memory_order_acq_rel );
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 2 );
            while ( s_bGateOpen.load( std::memory_order_acquire ) == false && std::chrono::steady_clock::now() < deadline )
                std::this_thread::yield();
        }

        static void high() { s_highOrder.store( s_startOrder.fetch_add( 1, std::memory_order_relaxed ) + 1, std::memory_order_relaxed ); }
    };

    const uint32 normalCount = workerCount * 4 < 16 ? 16 : workerCount * 4;
    for ( uint32 index = 0; index < normalCount; ++index )
    {
        sw::TaskHandle handle = taskMgr.emplaceTask( "GatedNormal", SW_DELEGATE_FUNCTION( sw::TaskDelegate, PriorityContext::gatedNormal ) );
        SW_ASSERT_TRUE( handle.isValid() );
        handle.submit();
    }

    // 워커가 전부 문에 닿을 때까지 — 나머지 Normal 은 그 뒤에 줄 서 있다.
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 2 );
        while ( s_runningCount.load( std::memory_order_acquire ) < workerCount && std::chrono::steady_clock::now() < deadline )
            std::this_thread::yield();
    }
    SW_ASSERT_EQUAL( workerCount, s_runningCount.load() );

    sw::TaskHandle highHandle = taskMgr.emplaceTask( "HighLane", SW_DELEGATE_FUNCTION( sw::TaskDelegate, PriorityContext::high ) );
    SW_ASSERT_TRUE( highHandle.isValid() );
    highHandle.setPriority( sw::TaskPriority::High );
    highHandle.submit();

    s_bGateOpen.store( true, std::memory_order_release );
    const bool bAllDone = taskMgr.waitAll( 5000 );
    if ( bAllDone == false )
    {
        // 실패해도 매니저는 비우고 나간다 — 남은 태스크를 다음 테스트가 영원히 기다리면 안 된다.
        taskMgr.clear();
        SW_ASSERT_TRUE( bAllDone );
    }

    // 처음 비는 워커가 집는다. 동시에 끝난 워커 몇이 Normal 을 먼저 세어도 워커 수 두 배 안이다.
    const uint32 highOrder = s_highOrder.load();
    SW_EXPECT_TRUE_MSG( highOrder >= 1 && highOrder <= workerCount * 2,
                        ( sw::string( "High 태스크의 시작 순번이 " ) + sw::to_string( highOrder ) + " — 워커 " + sw::to_string( workerCount ) +
                          " 개, Normal " + sw::to_string( normalCount ) + " 개 뒤에 줄을 섰다(우선순위를 보지 않는다)" )
                            .c_str() );

    taskMgr.clear();
}

/**
 * @brief [TaskTest] waitStage 가 돌아온 순간 활성 태스크 수는 0 이다
 * @details 완료 처리가 스테이지에 먼저 알리고 활성 수를 나중에 내리면, `waitStage` 뒤에 부르는 `clear()` 가
 *          0 으로 놓은 수를 뒤늦은 감소가 0xFFFFFFFF 로 감아 버린다 — 이후의 `waitAll` 은 영원히 기다린다.
 *          창이 좁아 한 번엔 안 걸리므로 여러 번 돈다(CTest 아래에서는 매번 걸렸다).
 */
SW_TEST_CASE( TaskTest, WaitStageLeavesNoActiveTaskBehind )
{
    sw::TaskManager& taskMgr = sw::engine::getTaskManager();
    taskMgr.initialize();

    static sw::atomic<uint32> s_touchCount{ 0 };
    s_touchCount = 0;
    struct TouchContext
    {
        static void touchRange( uint32 start, uint32 end ) { s_touchCount.fetch_add( end - start, std::memory_order_relaxed ); }
    };

    constexpr uint32 kRound      = 500;
    constexpr uint32 kItemCount  = 64;
    uint32           staleRounds = 0;
    for ( uint32 round = 0; round < kRound; ++round )
    {
        sw::TaskStageHandle stage  = taskMgr.createStage();
        sw::TaskHandle      handle = taskMgr.emplaceParallelBlock( 0, kItemCount, SW_DELEGATE_FUNCTION( sw::ParallelBlockDelegate, TouchContext::touchRange ) );
        stage.addTask( handle );
        handle.submit();
        taskMgr.waitStage( stage );
        if ( taskMgr.getActiveTaskCount() != 0 )
            ++staleRounds;
    }
    SW_EXPECT_EQUAL( kRound * kItemCount, s_touchCount.load() );
    SW_EXPECT_TRUE_MSG( staleRounds == 0, ( sw::string( "waitStage 직후 활성 태스크가 남아 있던 회차: " ) + sw::to_string( staleRounds ) + " / " + sw::to_string( kRound ) ).c_str() );

    taskMgr.clear();
}

/**
 * @brief [TaskTest] 스테이지 디스패치는 정상 상태에서 힙을 만지지 않는다
 * @details 상용 잡 시스템의 기준이다 — 프레임마다 도는 병렬 그룹이 힙을 두드리면 그 수가 곧 프레임당 할당 수다.
 *          스테이지 노드는 풀·침입형 참조, 그룹 콜러블은 락프리 풀, 이름은 fixed_string 이라 워밍업 뒤에는 0 이어야 한다.
 *          `MemoryProfiler` 의 할당 횟수 누계로 잰다(sw 할당자만 센다 — 이 경로의 할당은 전부 그것이다).
 */
SW_TEST_CASE( TaskTest, StageDispatchDoesNotAllocate )
{
    sw::MemoryProfiler* pMemory = sw::MemoryProfiler::getActive();
    SW_ASSERT_NOT_NULL( pMemory );
    sw::TaskManager& taskMgr = sw::engine::getTaskManager();
    taskMgr.initialize();

    static sw::atomic<uint32> s_dispatchTouch{ 0 };
    s_dispatchTouch = 0;
    struct DispatchContext
    {
        static void touchRange( uint32 start, uint32 end ) { s_dispatchTouch.fetch_add( end - start, std::memory_order_relaxed ); }
    };
    auto dispatchOnce = [&]()
    {
        sw::TaskStageHandle stage  = taskMgr.createStage();
        sw::TaskHandle      handle = taskMgr.emplaceParallelBlock( 0, 64, SW_DELEGATE_FUNCTION( sw::ParallelBlockDelegate, DispatchContext::touchRange ) );
        stage.addTask( handle );
        handle.submit();
        taskMgr.waitStage( stage );
    };

    // 워밍업 — 풀 슬랩·스테이지 노드·목록 용량은 처음 한 번만 잡는다.
    for ( uint32 round = 0; round < 4; ++round )
        dispatchOnce();

    // 스테이지 노드를 **여러 개** 미리 만들어 둔다. 하나만으로는 부하가 걸린 기계에서 진다:
    // `waitStage` 는 남은 태스크가 0 이 되면 돌아오지만, 그 0 을 만든 워커가 스테이지의 자기 참조를
    // 놓는 것은 그 다음이다. 그 틈에 다음 디스패치가 오면 풀에 남은 것이 없어 하나를 더 만든다
    // (노드 하나 + 소유 목록이 늘며 두 번). 이 테스트가 재는 것은 그 경합이 아니라 정상 상태의
    // churn 이므로, 여유분을 미리 만들어 틈을 메운다 — CI 처럼 붐비는 기계에서만 지던 이유다.
    {
        constexpr uint32                kSpareStageCount = 8;
        sw::vector<sw::TaskStageHandle> listSpareStage;
        listSpareStage.reserve( kSpareStageCount );
        for ( uint32 index = 0; index < kSpareStageCount; ++index )
        {
            listSpareStage.push_back( taskMgr.createStage() );
        }
    }

    const bool bWasTracking = pMemory->isTrackingEnabled();
    pMemory->setTrackingEnabled( true );
    const uint64     before = pMemory->getTotalAllocationCount();
    constexpr uint32 kRound = 50;
    for ( uint32 round = 0; round < kRound; ++round )
        dispatchOnce();
    const uint64 allocations = pMemory->getTotalAllocationCount() - before;
    pMemory->setTrackingEnabled( bWasTracking );

    SW_EXPECT_EQUAL( uint32( 64 * ( kRound + 4 ) ), s_dispatchTouch.load() );
    SW_EXPECT_TRUE_MSG( allocations == 0, ( sw::string( "스테이지 디스패치 " ) + sw::to_string( kRound ) + " 회에 힙 할당 " + sw::to_string( allocations ) + " 회 — 프레임마다 그만큼 churn 이다" ).c_str() );

    taskMgr.clear();
}

/**
 * @brief [TaskTest] runParallel 은 문턱 아래에서는 나누지 않고, 위에서는 구간을 빠짐없이 한 번씩 덮는다
 * @details 디스패치 바닥(~50 us)보다 작은 일을 나누면 느려지므로 문턱 아래는 본문이 **한 번**, (0, count) 로 불려야 한다.
 *          문턱 위에서는 어떤 인덱스도 두 번 오거나 빠지면 안 된다 — 청크 경계 오류를 잡는다.
 */
SW_TEST_CASE( TaskTest, RunParallelSplitsOnlyAboveThreshold )
{
    sw::TaskManager& taskMgr = sw::engine::getTaskManager();
    taskMgr.initialize();

    static sw::atomic<uint32> s_callCount{ 0 };
    static sw::atomic<uint32> s_arrHit[4096];
    struct RangeContext
    {
        static void countRange( uint32 start, uint32 end )
        {
            s_callCount.fetch_add( 1, std::memory_order_relaxed );
            for ( uint32 index = start; index < end; ++index )
                s_arrHit[index].fetch_add( 1, std::memory_order_relaxed );
        }
    };
    auto resetHits = [&]( uint32 count )
    {
        s_callCount = 0;
        for ( uint32 index = 0; index < count; ++index )
            s_arrHit[index] = 0;
    };

    // 문턱 아래: 한 번에, 나누지 않는다.
    resetHits( 10 );
    taskMgr.runParallel( 10, 16, SW_DELEGATE_FUNCTION( sw::ParallelBlockDelegate, RangeContext::countRange ) );
    SW_EXPECT_EQUAL( uint32( 1 ), s_callCount.load() );
    for ( uint32 index = 0; index < 10; ++index )
        SW_EXPECT_EQUAL( uint32( 1 ), s_arrHit[index].load() );

    // 문턱 위: 여러 청크로 나뉘되 인덱스는 한 번씩.
    resetHits( 4096 );
    taskMgr.runParallel( 4096, 16, SW_DELEGATE_FUNCTION( sw::ParallelBlockDelegate, RangeContext::countRange ) );
    SW_EXPECT_TRUE( s_callCount.load() > 1 );
    uint32 wrongCount = 0;
    for ( uint32 index = 0; index < 4096; ++index )
    {
        if ( s_arrHit[index].load() != 1 )
            ++wrongCount;
    }
    SW_EXPECT_TRUE_MSG( wrongCount == 0, ( sw::string( "한 번이 아닌 인덱스 " ) + sw::to_string( wrongCount ) + " 개" ).c_str() );

    // 0 개는 아무것도 부르지 않는다.
    resetHits( 1 );
    taskMgr.runParallel( 0, 16, SW_DELEGATE_FUNCTION( sw::ParallelBlockDelegate, RangeContext::countRange ) );
    SW_EXPECT_EQUAL( uint32( 0 ), s_callCount.load() );

    taskMgr.clear();
}
