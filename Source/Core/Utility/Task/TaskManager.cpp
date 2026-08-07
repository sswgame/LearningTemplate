/**
 * @file TaskManager.cpp
 * @brief TaskManager 구현
 */
#include "pch.h"

#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"

#include "Core/Utility/Task/TaskManager.h"
#include "Core/Utility/Log/Logger.h"
#include "Core/Utility/String/StringBuilder.h"
#include "Core/Common/CommonDefines.h"
#include "Core/Utility/Time/EngineTimer.h"

namespace sw
{
	struct TaskNode : public std::enable_shared_from_this<TaskNode>
	{
		TaskNode()
		{
			_successors.reserve( 4 );
			_predecessors.reserve( 4 );
		}

		std::string							   _name;
		std::vector<std::shared_ptr<TaskNode>> _successors;
		std::vector<std::weak_ptr<TaskNode>>   _predecessors;
		TaskDelegate						   _delegate;
		TaskArgsDelegate					   _argsDelegate;
		TaskArgs							   _args;
		ParallelTaskDelegate				   _parallelDelegate;
		ParallelBlockDelegate				   _blockDelegate;
		std::weak_ptr<StageNode>			   _parentStage;

		uint32			   _rangeStart = 0;
		uint32			   _rangeEnd   = 0;
		std::atomic<int32> _unresolvedDependencies{ 0 };

		TaskType			   _type = TaskType::General;
		std::atomic<TaskState> _state{ TaskState::Pending };
	};

	struct StageNode
	{
		StageNode()
		{
			_tasks.reserve( 16 );
		}

		std::string							 _name;
		std::vector<std::weak_ptr<TaskNode>> _tasks;
		std::atomic<uint32>					 _remainingTasks{ 0 };
		std::mutex							 _mutex;
		std::condition_variable				 _cv;
	};

	TaskHandle& TaskHandle::precede( TaskHandle targetTask )
	{
		auto targetNode = targetTask.getNode();
		if ( _node != nullptr && targetNode != nullptr && _node != targetNode )
		{
			_node->_successors.push_back( targetNode );
			targetNode->_predecessors.push_back( _node );
			targetNode->_unresolvedDependencies.fetch_add( 1, std::memory_order_relaxed );
		}
		return *this;
	}

	TaskHandle& TaskHandle::succeed( TaskHandle dependencyTask )
	{
		dependencyTask.precede( *this );
		return *this;
	}

	TaskHandle TaskHandle::then( TaskDelegate nextTaskDelegate )
	{
		TaskHandle nextTask = sw::getTaskManager().emplaceTask( nextTaskDelegate );
		precede( nextTask );
		return nextTask;
	}

	TaskStageHandle& TaskStageHandle::addTask( TaskHandle task )
	{
		auto taskNode = task.getNode();
		if ( _node != nullptr && taskNode != nullptr )
		{
			std::lock_guard<std::mutex> lock{ _node->_mutex };
			_node->_tasks.push_back( taskNode );
			taskNode->_parentStage = _node;
			_node->_remainingTasks.fetch_add( 1, std::memory_order_relaxed );
		}
		return *this;
	}

	bool TaskManager::initialize( uint32 threadCount )
	{
		if ( _bInitialized == true )
			return false;

		if ( threadCount == 0 )
		{
			threadCount = std::thread::hardware_concurrency();
			if ( threadCount == 0 )
				threadCount = 4;
		}

		_bStop = false;
		_workers.reserve( threadCount );
		for ( uint32 threadIndex = 0; threadIndex < threadCount; ++threadIndex )
		{
			_workers.emplace_back( &TaskManager::workerLoop, this, threadIndex );
		}

		_bInitialized = true;
		SW_LOG_INFO( "[TaskManager] Initialized with %# worker threads.", threadCount );
		return true;
	}

	void TaskManager::shutdown()
	{
		if ( _bInitialized == false )
			return;

		waitAll();

		_bStop = true;
		_cvWorker.notify_all();

		for ( std::thread& worker : _workers )
		{
			if ( worker.joinable() )
			{
				worker.join();
			}
		}
		_workers.clear();

		clear();
		_bInitialized = false;
		SW_LOG_INFO( "[TaskManager] Shutdown cleanly." );
	}

	TaskHandle TaskManager::emplaceTask( const TaskDelegate& delegate )
	{
		return emplaceTask( "GeneralTask", delegate );
	}

	TaskHandle TaskManager::emplaceTask( const std::string_view name, const TaskDelegate& delegate )
	{
		if ( _bInitialized == false || _bStop.load( std::memory_order_relaxed ) == true )
		{
			SW_LOG_WARNING( "[TaskManager] Cannot emplace task '%#' while TaskManager is not initialized or stopping.", std::string{ name }.c_str() );
			return TaskHandle{};
		}

		std::shared_ptr<TaskNode> node = std::make_shared<TaskNode>();
		node->_name					   = name;
		node->_type					   = TaskType::General;
		node->_delegate				   = delegate;
		node->_state				   = TaskState::Pending;
		node->_unresolvedDependencies  = 0;

		{
			std::lock_guard<std::mutex> lock{ _queueMutex };
			_allNodes.push_back( node );
			_activeTaskCount.fetch_add( 1, std::memory_order_relaxed );
		}

		return TaskHandle{ node };
	}

	TaskHandle TaskManager::emplaceTask( const TaskArgsDelegate& delegate, const TaskArgs& args )
	{
		return emplaceTask( "GeneralArgsTask", delegate, args );
	}

	TaskHandle TaskManager::emplaceTask( const std::string_view name, const TaskArgsDelegate& delegate, const TaskArgs& args )
	{
		if ( _bInitialized == false || _bStop.load( std::memory_order_relaxed ) == true )
		{
			SW_LOG_WARNING( "[TaskManager] Cannot emplace task '%#' while TaskManager is not initialized or stopping.", std::string{ name }.c_str() );
			return TaskHandle{};
		}

		std::shared_ptr<TaskNode> node = std::make_shared<TaskNode>();
		node->_name					   = name;
		node->_type					   = TaskType::General;
		node->_argsDelegate			   = delegate;
		node->_args					   = args;
		node->_state				   = TaskState::Pending;
		node->_unresolvedDependencies  = 0;

		{
			std::lock_guard<std::mutex> lock{ _queueMutex };
			_allNodes.push_back( node );
			_activeTaskCount.fetch_add( 1, std::memory_order_relaxed );
		}

		return TaskHandle{ node };
	}

	TaskHandle TaskManager::emplaceParallel( uint32 count, const ParallelTaskDelegate& delegate )
	{
		return emplaceParallel( "ParallelTask", count, delegate );
	}

	TaskHandle TaskManager::emplaceParallel( const std::string_view name, uint32 count, const ParallelTaskDelegate& delegate )
	{
		if ( count == 0 )
		{
			return emplaceTask( name, TaskDelegate{} );
		}

		uint32 workerCount = getWorkerCount();
		if ( workerCount == 0 )
			workerCount = 1;

		uint32 chunkSize = ( count + workerCount - 1 ) / workerCount;

		sw::StringBuilder<constant::kMaxBuffer128> parentNameBuf;
		parentNameBuf.append( name ).append( "_SyncParent" );
		auto parentTask = emplaceTask( parentNameBuf.view(), TaskDelegate{} );

		for ( uint32 start = 0; start < count; start += chunkSize )
		{
			uint32 end	   = std::min( start + chunkSize, count );
			auto   subTask = std::make_shared<TaskNode>();

			sw::StringBuilder<constant::kMaxBuffer128> subNameBuf;
			subNameBuf.append( name ).append( "_Chunk" );
			subTask->_name					 = subNameBuf.view();
			subTask->_type					 = TaskType::Parallel;
			subTask->_parallelDelegate		 = delegate;
			subTask->_rangeStart			 = start;
			subTask->_rangeEnd				 = end;
			subTask->_state					 = TaskState::Pending;
			subTask->_unresolvedDependencies = 0;

			{
				std::lock_guard<std::mutex> lock{ _queueMutex };
				_allNodes.push_back( subTask );
				_activeTaskCount.fetch_add( 1, std::memory_order_relaxed );
			}

			TaskHandle subHandle{ subTask };
			subHandle.precede( parentTask );
		}

		return parentTask;
	}

	TaskHandle TaskManager::emplaceParallelBlock( uint32 start, uint32 end, const ParallelBlockDelegate& delegate )
	{
		auto node					  = std::make_shared<TaskNode>();
		node->_name					  = "ParallelBlockTask";
		node->_type					  = TaskType::Parallel;
		node->_blockDelegate		  = delegate;
		node->_rangeStart			  = start;
		node->_rangeEnd				  = end;
		node->_state				  = TaskState::Pending;
		node->_unresolvedDependencies = 0;

		{
			std::lock_guard<std::mutex> lock{ _queueMutex };
			_allNodes.push_back( node );
			_activeTaskCount.fetch_add( 1, std::memory_order_relaxed );
		}

		return TaskHandle{ node };
	}

	TaskStageHandle TaskManager::createStage( const std::string_view stageName )
	{
		auto stage	 = std::make_shared<StageNode>();
		stage->_name = stageName;

		std::lock_guard<std::mutex> lock{ _queueMutex };
		_allStages.push_back( stage );

		return TaskStageHandle{ stage };
	}

	void TaskManager::waitStage( TaskStageHandle stage )
	{
		if ( stage._node == nullptr )
			return;

		dispatch();

		std::unique_lock<std::mutex> lock{ stage._node->_mutex };
		stage._node->_cv.wait( lock, [&]()
		{
			return stage._node->_remainingTasks.load( std::memory_order_relaxed ) == 0;
		} );
	}

	bool TaskManager::isStageComplete( TaskStageHandle stage )
	{
		if ( stage._node == nullptr )
			return true;

		return stage._node->_remainingTasks.load( std::memory_order_relaxed ) == 0;
	}

	void TaskManager::dispatch()
	{
		std::lock_guard<std::mutex> lock{ _queueMutex };
		for ( const std::shared_ptr<TaskNode>& node : _allNodes )
		{
			if ( node->_state == TaskState::Pending && node->_unresolvedDependencies.load( std::memory_order_relaxed ) == 0 )
			{
				scheduleReadyTask( node );
			}
		}
	}

	void TaskManager::waitAll()
	{
		dispatch();

		std::unique_lock<std::mutex> lock{ _queueMutex };
		_cvWaitAll.wait( lock, [&]()
		{
			return _activeTaskCount.load( std::memory_order_relaxed ) == 0;
		} );
	}

	void TaskManager::clear()
	{
		std::lock_guard<std::mutex> lock{ _queueMutex };
		_allNodes.clear();
		_allStages.clear();
		std::queue<std::shared_ptr<TaskNode>> emptyQueue;
		std::swap( _readyQueue, emptyQueue );
		_activeTaskCount = 0;
	}

	void TaskManager::scheduleReadyTask( const std::shared_ptr<TaskNode>& node )
	{
		if ( node != nullptr && node->_state == TaskState::Pending )
		{
			node->_state = TaskState::Ready;
			_readyQueue.push( node );
			_cvWorker.notify_one();
		}
	}

	std::shared_ptr<TaskNode> TaskManager::tryStealWorkNode()
	{
		for ( const std::shared_ptr<TaskNode>& node : _allNodes )
		{
			if ( node->_unresolvedDependencies.load( std::memory_order_relaxed ) == 0 )
			{
				TaskState expected = TaskState::Pending;
				if ( node->_state.compare_exchange_strong( expected, TaskState::Running, std::memory_order_acq_rel ) )
				{
					return node;
				}
			}
		}
		return nullptr;
	}

	void TaskManager::workerLoop( uint32 workerId )
	{
		(void)workerId;
		while ( true )
		{
			std::shared_ptr<TaskNode> node;
			{
				std::unique_lock<std::mutex> lock{ _queueMutex };
				_cvWorker.wait( lock, [&]()
				{
					return _bStop.load() || _readyQueue.empty() == false;
				} );

				if ( _bStop.load() && _readyQueue.empty() )
					break;

				if ( _readyQueue.empty() == false )
				{
					node = _readyQueue.front();
					_readyQueue.pop();
				}
				else
				{
					node = tryStealWorkNode();
				}
			}

			if ( node != nullptr )
			{
				node->_state = TaskState::Running;

				if ( node->_type == TaskType::General )
				{
					if ( node->_argsDelegate.isBound() == true )
					{
						node->_argsDelegate( node->_args );
					}
					else if ( node->_delegate.isBound() == true )
					{
						node->_delegate();
					}
				}
				else if ( node->_type == TaskType::Parallel )
				{
					if ( node->_parallelDelegate.isBound() == true )
					{
						for ( uint32 elementIndex = node->_rangeStart; elementIndex < node->_rangeEnd; ++elementIndex )
						{
							node->_parallelDelegate( elementIndex );
						}
					}
					else if ( node->_blockDelegate.isBound() == true )
					{
						node->_blockDelegate( node->_rangeStart, node->_rangeEnd );
					}
				}

				onTaskFinished( node );
			}
		}
	}

	void TaskManager::onTaskFinished( const std::shared_ptr<TaskNode>& node )
	{
		node->_state = TaskState::Completed;

		if ( std::shared_ptr<StageNode> stage = node->_parentStage.lock() )
		{
			uint32 prev = stage->_remainingTasks.fetch_sub( 1, std::memory_order_relaxed );
			if ( prev == 1 )
			{
				std::lock_guard<std::mutex> lock{ stage->_mutex };
				stage->_cv.notify_all();
			}
		}

		{
			std::lock_guard<std::mutex> lock{ _queueMutex };
			for ( const std::shared_ptr<TaskNode>& succ : node->_successors )
			{
				int32 remaining = succ->_unresolvedDependencies.fetch_sub( 1, std::memory_order_relaxed );
				if ( remaining == 1 )
				{
					scheduleReadyTask( succ );
				}
			}

			uint32 activeLeft = _activeTaskCount.fetch_sub( 1, std::memory_order_relaxed );
			if ( activeLeft == 1 )
			{
				_cvWaitAll.notify_all();
			}
		}
	}

	TaskHandle TaskManager::whenAll( const std::vector<TaskHandle>& tasks, const TaskDelegate& continuation )
	{
		TaskHandle continuationTask = emplaceTask( "whenAll_Continuation", continuation );

		for ( const TaskHandle& task : tasks )
		{
			if ( task.isValid() )
			{
				TaskHandle mutTask = task;
				mutTask.precede( continuationTask );
			}
		}

		return continuationTask;
	}

	TaskHandle TaskManager::whenAny( const std::vector<TaskHandle>& tasks, const TaskDelegate& continuation )
	{
		TaskHandle						   continuationTask = emplaceTask( "whenAny_Continuation", continuation );
		std::shared_ptr<std::atomic<bool>> firedFlag		= std::make_shared<std::atomic<bool>>( false );

		for ( const TaskHandle& task : tasks )
		{
			if ( task.isValid() )
			{
				TaskDelegate triggerDelegate = SW_DELEGATE_LAMBDA( TaskDelegate,
																   [continuation, firedFlag]()
				{
					if ( firedFlag->exchange( true, std::memory_order_relaxed ) == false )
					{
						continuation();
					}
				} );

				TaskHandle triggerTask = emplaceTask( "whenAny_Trigger", triggerDelegate );
				TaskHandle mutTask	   = task;
				mutTask.precede( triggerTask );
			}
		}

		return continuationTask;
	}
} // namespace sw
