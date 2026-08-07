/**
 * @file TestTask.cpp
 * @brief Auto-generated documentation header
 */
#include "TestFramework.h"

#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"

#include "Core/Utility/Task/TaskManager.h"
#include "Core/Utility/Log/Logger.h"

namespace
{
	static std::atomic<int32> s_taskExecOrder{ 0 };
	static int32			  s_orderA = 0;
	static int32			  s_orderB = 0;
	static int32			  s_orderC = 0;

	void taskFuncA()
	{
		s_orderA = ++s_taskExecOrder;
	}

	void taskFuncB()
	{
		s_orderB = ++s_taskExecOrder;
	}

	void taskFuncC()
	{
		s_orderC = ++s_taskExecOrder;
	}

	static int32	   s_recInt	   = 0;
	static double	   s_recDouble = 0.0;
	static std::string s_recStr	   = "";
	static void*	   s_recPtr	   = nullptr;

	struct CustomPlayerData
	{
		std::string name;
		int32		level = 0;
	};

	static CustomPlayerData	  s_recPlayer;
	static std::vector<int32> s_recItems;

	void taskWithArbitraryArgs( const sw::TaskArgs& args )
	{
		s_recInt	= args.get<int32>( 0 );
		s_recDouble = args.get<double>( 1 );
		s_recStr	= args.get<std::string>( 2 );
		s_recPtr	= args.get<void*>( 3 );
	}

	void taskWithCustomStructAndContainer( const sw::TaskArgs& args )
	{
		s_recPlayer = args.get<CustomPlayerData>( 0 );
		s_recItems	= args.get<std::vector<int32>>( 1 );
	}
}

SW_TEST_CASE( Utility_Task, GeneralTaskDAG )
{
	sw::TaskManager& taskMgr = sw::getTaskManager();
	taskMgr.initialize();

	s_taskExecOrder = 0;
	s_orderA		= 0;
	s_orderB		= 0;
	s_orderC		= 0;

	sw::TaskDelegate delA = SW_DELEGATE_FUNCTION( sw::TaskDelegate, taskFuncA );
	sw::TaskDelegate delB = SW_DELEGATE_FUNCTION( sw::TaskDelegate, taskFuncB );
	sw::TaskDelegate delC = SW_DELEGATE_FUNCTION( sw::TaskDelegate, taskFuncC );

	sw::TaskHandle handleA = taskMgr.emplaceTask( "TaskA", delA );
	sw::TaskHandle handleB = taskMgr.emplaceTask( "TaskB", delB );
	sw::TaskHandle handleC = taskMgr.emplaceTask( "TaskC", delC );

	handleA.precede( handleB );
	handleB.precede( handleC );

	taskMgr.waitAll();

	SW_EXPECT_TRUE( s_orderA > 0 );
	SW_EXPECT_TRUE( s_orderB > s_orderA );
	SW_EXPECT_TRUE( s_orderC > s_orderB );

	taskMgr.clear();
}

SW_TEST_CASE( Utility_Task, ArbitraryArgsTask )
{
	sw::TaskManager& taskMgr = sw::getTaskManager();
	taskMgr.initialize();

	int					 dummyVar = 42;
	sw::TaskArgs		 args{ 100, 3.14159, std::string( "HelloTask" ), static_cast<void*>( &dummyVar ) };
	sw::TaskArgsDelegate argsDel = SW_DELEGATE_FUNCTION( sw::TaskArgsDelegate, taskWithArbitraryArgs );

	taskMgr.emplaceTask( "ArgsTask", argsDel, args );
	taskMgr.waitAll();

	SW_EXPECT_EQUAL( 100, s_recInt );
	SW_EXPECT_TRUE( std::abs( s_recDouble - 3.14159 ) < 0.0001 );
	SW_EXPECT_EQUAL( std::string( "HelloTask" ), s_recStr );
	SW_EXPECT_EQUAL( static_cast<void*>( &dummyVar ), s_recPtr );

	taskMgr.clear();

	CustomPlayerData   inputPlayer{ "Antigravity", 99 };
	std::vector<int32> inputItems{ 10, 20, 30 };

	sw::TaskArgs		 customArgs{ inputPlayer, inputItems };
	sw::TaskArgsDelegate customDel = SW_DELEGATE_FUNCTION( sw::TaskArgsDelegate, taskWithCustomStructAndContainer );

	taskMgr.emplaceTask( "CustomArgsTask", customDel, customArgs );
	taskMgr.waitAll();

	SW_EXPECT_EQUAL( std::string( "Antigravity" ), s_recPlayer.name );
	SW_EXPECT_EQUAL( 99, s_recPlayer.level );
	SW_EXPECT_EQUAL( 3u, static_cast<uint32>( s_recItems.size() ) );
	if ( s_recItems.size() == 3 )
	{
		SW_EXPECT_EQUAL( 10, s_recItems[0] );
		SW_EXPECT_EQUAL( 20, s_recItems[1] );
		SW_EXPECT_EQUAL( 30, s_recItems[2] );
	}

	taskMgr.clear();
}

SW_TEST_CASE( Utility_Task, ParallelTask )
{
	sw::TaskManager& taskMgr = sw::getTaskManager();
	taskMgr.initialize();

	constexpr uint32						kElementCount = 100;
	static std::vector<std::atomic<uint32>> s_results( kElementCount );
	for ( uint32 i = 0; i < kElementCount; ++i )
	{
		s_results[i] = 0;
	}

	struct ParallelContext
	{
		static void processIndex( uint32 index )
		{
			s_results[index].fetch_add( 1, std::memory_order_relaxed );
		}
	};

	sw::ParallelTaskDelegate parallelDel = SW_DELEGATE_FUNCTION( sw::ParallelTaskDelegate, ParallelContext::processIndex );
	taskMgr.emplaceParallel( "ParallelArray", kElementCount, parallelDel );

	taskMgr.waitAll();

	for ( uint32 i = 0; i < kElementCount; ++i )
	{
		SW_EXPECT_EQUAL( 1u, s_results[i].load() );
	}

	taskMgr.clear();
}

SW_TEST_CASE( Utility_Task, StagedTask )
{
	sw::TaskManager& taskMgr = sw::getTaskManager();
	taskMgr.initialize();

	static std::atomic<uint32> s_stageProgress{ 0 };
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

	sw::TaskStageHandle stage = taskMgr.createStage( "GameUpdateStage" );

	sw::TaskHandle t1 = taskMgr.emplaceTask( "StageTask1", SW_DELEGATE_FUNCTION( sw::TaskDelegate, StageContext::runStageTask1 ) );
	sw::TaskHandle t2 = taskMgr.emplaceTask( "StageTask2", SW_DELEGATE_FUNCTION( sw::TaskDelegate, StageContext::runStageTask2 ) );

	stage.addTask( t1 );
	stage.addTask( t2 );

	taskMgr.waitStage( stage );

	SW_EXPECT_TRUE( taskMgr.isStageComplete( stage ) );
	SW_EXPECT_EQUAL( 30u, s_stageProgress.load() );

	taskMgr.clear();
}

SW_TEST_CASE( Utility_Task, TaskChainingThen )
{
	sw::TaskManager& taskMgr = sw::getTaskManager();
	taskMgr.initialize();

	static std::vector<int32> s_executionOrder;
	s_executionOrder.clear();

	struct ChainContext
	{
		static void step1() { s_executionOrder.push_back( 1 ); }
		static void step2() { s_executionOrder.push_back( 2 ); }
		static void step3() { s_executionOrder.push_back( 3 ); }
	};

	sw::TaskHandle t1 = taskMgr.emplaceTask( SW_DELEGATE_FUNCTION( sw::TaskDelegate, ChainContext::step1 ) );
	sw::TaskHandle t2 = t1.then( SW_DELEGATE_FUNCTION( sw::TaskDelegate, ChainContext::step2 ) );
	sw::TaskHandle t3 = t2.then( SW_DELEGATE_FUNCTION( sw::TaskDelegate, ChainContext::step3 ) );

	taskMgr.dispatch();
	taskMgr.waitAll();

	SW_EXPECT_EQUAL( 3, static_cast<int32>( s_executionOrder.size() ) );
	if ( s_executionOrder.size() == 3 )
	{
		SW_EXPECT_EQUAL( 1, s_executionOrder[0] );
		SW_EXPECT_EQUAL( 2, s_executionOrder[1] );
		SW_EXPECT_EQUAL( 3, s_executionOrder[2] );
	}

	taskMgr.clear();
}

SW_TEST_CASE( Utility_Task, WorkStealingParallelTask )
{
	sw::TaskManager& taskMgr = sw::getTaskManager();
	taskMgr.initialize();

	constexpr uint32	count = 50;
	std::atomic<uint32> totalSum{ 0 };

	for ( uint32 i = 0; i < count; ++i )
	{
		auto cb = [&totalSum, i]()
		{
			totalSum.fetch_add( i + 1, std::memory_order_relaxed );
		};
		taskMgr.emplaceTask( SW_DELEGATE_LAMBDA( sw::TaskDelegate, cb ) );
	}

	taskMgr.waitAll();

	uint32 expectedSum = ( count * ( count + 1 ) ) / 2;
	SW_EXPECT_EQUAL( expectedSum, totalSum.load() );

	taskMgr.clear();
}

SW_TEST_CASE( Utility_Task, TaskCombinatorWhenAll )
{
	sw::TaskManager& taskMgr = sw::getTaskManager();
	taskMgr.initialize();

	std::atomic<int32> completedCount{ 0 };
	bool			   whenAllExecuted = false;

	auto cb1 = [&completedCount]()
	{ completedCount.fetch_add( 1 ); };
	auto cb2 = [&completedCount]()
	{ completedCount.fetch_add( 1 ); };
	auto cb3 = [&completedCount]()
	{ completedCount.fetch_add( 1 ); };
	sw::TaskHandle t1 = taskMgr.emplaceTask( SW_DELEGATE_LAMBDA( sw::TaskDelegate, cb1 ) );
	sw::TaskHandle t2 = taskMgr.emplaceTask( SW_DELEGATE_LAMBDA( sw::TaskDelegate, cb2 ) );
	sw::TaskHandle t3 = taskMgr.emplaceTask( SW_DELEGATE_LAMBDA( sw::TaskDelegate, cb3 ) );

	auto whenAllCb = [&whenAllExecuted, &completedCount]()
	{
		if ( completedCount.load() == 3 )
		{
			whenAllExecuted = true;
		}
	};
	sw::TaskHandle whenAllTask = taskMgr.whenAll( { t1, t2, t3 }, SW_DELEGATE_LAMBDA( sw::TaskDelegate, whenAllCb ) );

	taskMgr.dispatch();
	taskMgr.waitAll();

	SW_EXPECT_EQUAL( 3, completedCount.load() );
	SW_EXPECT_TRUE( whenAllExecuted );

	taskMgr.clear();
}

SW_TEST_CASE( Utility_Task, TaskCombinatorWhenAny )
{
	sw::TaskManager& taskMgr = sw::getTaskManager();
	taskMgr.initialize();

	bool whenAnyExecuted = false;

	auto		   cbEmpty = []() {};
	sw::TaskHandle t1	   = taskMgr.emplaceTask( SW_DELEGATE_LAMBDA( sw::TaskDelegate, cbEmpty ) );
	sw::TaskHandle t2	   = taskMgr.emplaceTask( SW_DELEGATE_LAMBDA( sw::TaskDelegate, cbEmpty ) );

	auto whenAnyCb = [&whenAnyExecuted]()
	{
		whenAnyExecuted = true;
	};
	sw::TaskHandle whenAnyTask = taskMgr.whenAny( { t1, t2 }, SW_DELEGATE_LAMBDA( sw::TaskDelegate, whenAnyCb ) );

	taskMgr.dispatch();
	taskMgr.waitAll();

	SW_EXPECT_TRUE( whenAnyExecuted );

	taskMgr.clear();
}
