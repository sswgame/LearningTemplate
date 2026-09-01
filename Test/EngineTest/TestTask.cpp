#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Task/TaskFuture.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"

#include "TestFramework/TestFramework.h"

namespace sw
{
	namespace
	{
		static atomic<int32> s_taskExecOrder{ 0 };
		static int32		 s_orderA{ 0 };
		static int32		 s_orderB{ 0 };
		static int32		 s_orderC{ 0 };

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

		static int32	  s_recInt{ 0 };
		static float64	  s_recDouble{ 0.0 };
		static sw::string s_recStr = "";
		static void*	  s_recPtr{ nullptr };

		struct CustomPlayerData
		{
			sw::string _name;
			int32	   _level{ 0 };
		};

		static CustomPlayerData	 s_recPlayer;
		static sw::vector<int32> s_recItems;

		/** @brief 임의 타입 인자를 수신 버퍼에 저장합니다. */
		void taskWithArbitraryArgs( const sw::TaskArgs& args )
		{
			s_recInt	= args.get<int32>( 0 );
			s_recDouble = args.get<float64>( 1 );
			s_recStr	= args.get<sw::string>( 2 );
			s_recPtr	= args.get<void*>( 3 );
		}

		/** @brief 커스텀 구조체와 컨테이너 인자를 수신 버퍼에 저장합니다. */
		void taskWithCustomStructAndContainer( const sw::TaskArgs& args )
		{
			s_recPlayer = args.get<CustomPlayerData>( 0 );
			s_recItems	= args.get<sw::vector<int32>>( 1 );
		}

	} // namespace
} // namespace sw

// ------------------------------------------------------------------------------
// 1) Engine_Task — DAG·병렬·체이닝·combinator
// ------------------------------------------------------------------------------
/**
 * @brief [Engine_Task] 일반 태스크 DAG
 */
SW_TEST_CASE( Engine_Task, GeneralTaskDAG )
{
	sw::TaskManager& taskMgr = sw::engine::getTaskManager();
	taskMgr.initialize();

	sw::s_taskExecOrder = 0;
	sw::s_orderA		= 0;
	sw::s_orderB		= 0;
	sw::s_orderC		= 0;

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
 * @brief [Engine_Task] 임의 인자 태스크
 */
SW_TEST_CASE( Engine_Task, ArbitraryArgsTask )
{
	sw::TaskManager& taskMgr = sw::engine::getTaskManager();
	taskMgr.initialize();

	int32		 dummyVar = 42;
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
	sw::vector<int32>	 inputItems{ 10, 20, 30 };

	sw::TaskArgs		 customArgs = sw::MakeTaskArgs<sw::CustomPlayerData, sw::vector<int32>>( inputPlayer, inputItems );
	sw::TaskArgsDelegate customDel	= SW_DELEGATE_FUNCTION( sw::TaskArgsDelegate, sw::taskWithCustomStructAndContainer );

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
 * @brief [Engine_Task] 병렬 태스크
 */
SW_TEST_CASE( Engine_Task, ParallelTask )
{
	sw::TaskManager& taskMgr = sw::engine::getTaskManager();
	taskMgr.initialize();

	constexpr uint32					  kElementCount = 100;
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
 * @brief [Engine_Task] 스테이지 태스크
 */
SW_TEST_CASE( Engine_Task, StagedTask )
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

	sw::TaskStageHandle stage = taskMgr.getOrCreateStage( "GameUpdateStage" );

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
 * @brief [Engine_Task] then 체이닝
 */
SW_TEST_CASE( Engine_Task, TaskChainingThen )
{
	sw::TaskManager& taskMgr = sw::engine::getTaskManager();
	taskMgr.initialize();

	static sw::mutex		 s_chainMutex;
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
 * @brief [Engine_Task] 워크 스틸링 병렬 태스크
 */
SW_TEST_CASE( Engine_Task, WorkStealingParallelTask )
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
 * @brief [Engine_Task] whenAll 콤비네이터
 */
SW_TEST_CASE( Engine_Task, TaskCombinatorWhenAll )
{
	sw::TaskManager& taskMgr = sw::engine::getTaskManager();
	taskMgr.initialize();

	sw::atomic<int32> completedCount{ 0 };
	bool			  whenAllExecuted{ false };

	sw::TaskHandle t1 = taskMgr.emplaceTask( SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&completedCount]()
	{ completedCount.fetch_add( 1 ); } ) );
	sw::TaskHandle t2 = taskMgr.emplaceTask( SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&completedCount]()
	{ completedCount.fetch_add( 1 ); } ) );
	sw::TaskHandle t3 = taskMgr.emplaceTask( SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&completedCount]()
	{ completedCount.fetch_add( 1 ); } ) );

	sw::TaskHandle whenAllTask = taskMgr.whenAll( { t1, t2, t3 }, SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&whenAllExecuted, &completedCount]()
	{
		if ( completedCount.load() == 3 )
		{
			whenAllExecuted = true;
		}
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
 * @brief [Engine_Task] whenAny 콤비네이터
 */
SW_TEST_CASE( Engine_Task, TaskCombinatorWhenAny )
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
 * @brief [Engine_Task] 태스크 취소(cancel) 및 CancellationToken 검증
 */
SW_TEST_CASE( Engine_Task, CancellationTokenAndCancellation )
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
 * @brief [Engine_Task] TaskManager::waitAll(timeoutMs) 정상 완료 및 타임아웃 지원 검증
 */
SW_TEST_CASE( Engine_Task, WaitAllWithTimeout )
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
 * @brief [Engine_Task] C++17 호환 TaskFuture / TaskPromise Fluent 체이닝 및 비동기 파이프라인 검증
 */
SW_TEST_CASE( Engine_Task, TaskFutureMonadicPipeline )
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
 * @brief [Engine_Task] TaskFuture fallback(기본값 복구) 기능 검증
 */
SW_TEST_CASE( Engine_Task, TaskFutureFallback )
{
	// 1) 유효한 Future + 값 설정 시 원본 값 유지
	sw::TaskPromise<int32> promiseValid;
	sw::TaskFuture<int32>  futureValid	   = promiseValid.getFuture();
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
 * @brief [Engine_Task] TaskFuture whenAllFutures 콤비네이터 검증
 */
SW_TEST_CASE( Engine_Task, TaskFutureWhenAllCombinator )
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
 * @brief [Engine_Task] TaskFuture whenAnyFuture 콤비네이터 검증
 */
SW_TEST_CASE( Engine_Task, TaskFutureWhenAnyCombinator )
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
 * @brief [Engine_Task] TaskFuture 30단계 Deep Continuation 체이닝 스트레스 테스트
 */
SW_TEST_CASE( Engine_Task, TaskFutureDeepContinuationChainStress )
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
 * @brief [Engine_Task] TaskFuture whenAll 대규모 동시성(64개 워커) 스트레스 테스트
 */
SW_TEST_CASE( Engine_Task, TaskFutureWhenAllMassiveConcurrencyStress )
{
	sw::TaskManager& taskMgr = sw::engine::getTaskManager();
	taskMgr.initialize();

	constexpr int32 kTaskCount = 64;

	struct SharedTaskContext
	{
		sw::TaskPromise<int32> _promise{};
	};

	sw::vector<sw::shared_ptr<SharedTaskContext>> listContext;
	sw::vector<sw::TaskFuture<int32>>			  listFuture;
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
 * @brief [Engine_Task] TaskFuture whenAny 다중 스레드 레이스 스트레스 테스트
 */
SW_TEST_CASE( Engine_Task, TaskFutureWhenAnyRaceStress )
{
	sw::TaskManager& taskMgr = sw::engine::getTaskManager();
	taskMgr.initialize();

	constexpr int32 kRacers = 32;

	struct SharedRacerContext
	{
		sw::TaskPromise<int32> _promise{};
	};

	sw::vector<sw::shared_ptr<SharedRacerContext>> listContext;
	sw::vector<sw::TaskFuture<int32>>			   listFuture;
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
		auto		pCtx	   = listContext[index];
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
