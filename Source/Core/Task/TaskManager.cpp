#include "pch.h"

#include "Core/Task/TaskManager.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

namespace sw
{
	SW_LOG_CALLER( "TaskManager" );

	namespace
	{

		thread_local bool	   t_bTaskWorkerThread	 = false;	///< 현재 스레드가 TaskManager 워커 스레드인지 여부
		thread_local bool	   t_bInsideParallelTask = false;	///< 현재 병렬 배치 태스크 내부 실행 중인지 여부
		thread_local int32	   t_currentWorkerIndex	 = -1;		///< 현재 워커 스레드의 인덱스
		thread_local TaskNode* t_pCurrentRunningTask = nullptr; ///< 현재 스레드에서 실행 중인 태스크 노드 포인터

		constexpr uint32 kIdleSpinCount = 64;
#if !defined( SW_SHIPPING )
		constexpr uint32 kTaskNameCapacity = 31;
#endif

		/**
		 * @brief 병렬 태스크 진입/퇴출 시 스레드 로컬 플래그를 관리하는 RAII 스코프 구조체
		 */
		struct ParallelTaskScope
		{
			ParallelTaskScope()
			{
				t_bInsideParallelTask = true;
			}
			~ParallelTaskScope()
			{
				t_bInsideParallelTask = false;
			}
			ParallelTaskScope( const ParallelTaskScope& )			 = delete;
			ParallelTaskScope& operator=( const ParallelTaskScope& ) = delete;
		};

	} // namespace

	/**
	 * @brief 임의의 매개변수 가방(TaskArgs)과 함께 호출되는 델리게이트 페이로드
	 */
	struct TaskArgsPayload
	{
		TaskArgsDelegate _delegate;
		TaskArgs		 _args;
	};

	/**
	 * @brief 타입 소거(Type Erasure)된 실행 가능한 태스크 호출자 variant
	 */
	using TaskCallable = std::variant<
		std::monostate,
		TaskDelegate,
		TaskArgsPayload,
		ParallelTaskDelegate,
		ParallelBlockDelegate>;

	struct SharedTaskCallable
	{
		TaskCallable  _callable;
		atomic<int32> _refCount{ 0 };

		static SharedTaskCallable* create( TaskCallable callable, int32 refCount )
		{
			SharedTaskCallable* pShared = sw_new SharedTaskCallable();
			pShared->_callable			= std::move( callable );
			pShared->_refCount.store( refCount, std::memory_order_relaxed );
			return pShared;
		}

		void release()
		{
			if ( _refCount.fetch_sub( 1, std::memory_order_acq_rel ) == 1 )
				sw_delete( this );
		}
	};

	struct TaskNode;

	/**
	 * @struct InlineSuccessorList
	 * @brief 후속 태스크(Successors) 목록을 스택/인라인 버퍼(최대 4개)에 저장하여 힙 할당을 방지하는 Small-Vector 최적화 구조체
	 */
	struct InlineSuccessorList
	{
		static constexpr uint32 kInlineCapacity = 4;

		uint32						  _count{ 0 };
		TaskNode*					  _arrInlineNodes[kInlineCapacity]{};
		unique_ptr<vector<TaskNode*>> _pOverflow;
		mutable std::atomic_flag	  _lock = ATOMIC_FLAG_INIT;

		void lock() const noexcept
		{
			while ( _lock.test_and_set( std::memory_order_acquire ) )
				sw::cpuPause();
		}

		void unlock() const noexcept
		{
			_lock.clear( std::memory_order_release );
		}

		void push_back( TaskNode* pNode )
		{
			lock();
			if ( _count < kInlineCapacity )
			{
				_arrInlineNodes[_count++] = pNode;
			}
			else
			{
				if ( _pOverflow == nullptr )
					_pOverflow = make_unique<vector<TaskNode*>>();
				_pOverflow->push_back( pNode );
				++_count;
			}
			unlock();
		}

		template <typename Func>
		void forEach( Func&& func ) const
		{
			TaskNode*		  arrInlineCopy[kInlineCapacity]{};
			uint32			  inlineCopyCount{ 0 };
			vector<TaskNode*> overflowCopy;

			lock();
			inlineCopyCount = _count < kInlineCapacity ? _count : kInlineCapacity;
			for ( uint32 index = 0; index < inlineCopyCount; ++index )
			{
				arrInlineCopy[index] = _arrInlineNodes[index];
			}
			if ( _pOverflow != nullptr )
			{
				overflowCopy = *_pOverflow;
			}
			unlock();

			for ( uint32 index = 0; index < inlineCopyCount; ++index )
			{
				func( arrInlineCopy[index] );
			}
			for ( TaskNode* pNode : overflowCopy )
			{
				func( pNode );
			}
		}

		void clearAndRelease();
	};

	struct TaskNode
	{
		void retain()
		{
			_refCount.fetch_add( 1, std::memory_order_relaxed );
		}

		void release();

#if !defined( SW_SHIPPING )
		utf8 _arrName[kTaskNameCapacity + 1]{};
#endif
		InlineSuccessorList _successors;
		TaskCallable		_callable;
		SharedTaskCallable* _pSharedCallable{ nullptr };
		weak_ptr<StageNode> _parentStage;

		uint32		  _rangeStart{ 0 };
		uint32		  _rangeEnd{ 0 };
		atomic<int32> _unresolvedDependencies{ 1 }; // 1 = Builder Dependency

		TaskThreadAffinity _affinity = TaskThreadAffinity::Any;
		TaskPriority	   _priority{ TaskPriority::Normal };
		atomic<TaskState>  _state{ TaskState::Pending };
		atomic<bool>	   _bCancelled{ false };

		TaskManager*  _pOwner{ nullptr };
		TaskNode*	  _pParent{ nullptr };
		atomic<int32> _activeChildren{ 0 };
		atomic<int32> _refCount{ 1 };
	};

	static void setTaskName( TaskNode* pNode, string_view name )
	{
#if !defined( SW_SHIPPING )
		if ( pNode == nullptr )
			return;

		uint32 len = static_cast<uint32>( name.size() );
		if ( len > kTaskNameCapacity )
			len = kTaskNameCapacity;
		if ( len > 0 )
			std::memcpy( pNode->_arrName, name.data(), len );
		pNode->_arrName[len] = 0;
#else
		(void)pNode;
		(void)name;
#endif
	}

	class TaskNodePool
	{
		static constexpr uint32 kSlabSize = 64;

	public:
		TaskNodePool() = default;

		~TaskNodePool()
		{
			std::scoped_lock<mutex> lock{ _slabMutex };
			for ( TaskNode* pSlab : _listSlabs )
			{
				if ( pSlab == nullptr )
					continue;
				for ( uint32 index = 0; index < kSlabSize; ++index )
				{
					pSlab[index].~TaskNode();
				}
				Memory::freeMemory( pSlab );
			}
			_listSlabs.clear();
		}

		TaskNode* allocate()
		{
			TaskNode* pMem{ nullptr };
			if ( _freeQueue.dequeue( pMem ) == false || pMem == nullptr )
			{
				{
					std::scoped_lock<mutex> lock{ _slabMutex };
					if ( _listOverflowFree.empty() == false )
					{
						pMem = _listOverflowFree.back();
						_listOverflowFree.pop_back();
					}
				}
				if ( pMem == nullptr )
				{
					TaskNode* pSlab = static_cast<TaskNode*>( Memory::allocMemory( sizeof( TaskNode ) * kSlabSize ) );
					for ( uint32 index = 0; index < kSlabSize; ++index )
					{
						new ( &pSlab[index] ) TaskNode();
					}
					{
						std::scoped_lock<mutex> lock{ _slabMutex };
						_listSlabs.push_back( pSlab );
					}
					for ( uint32 index = 1; index < kSlabSize; ++index )
					{
						if ( _freeQueue.enqueue( &pSlab[index] ) == false )
						{
							std::scoped_lock<mutex> lock{ _slabMutex };
							_listOverflowFree.push_back( &pSlab[index] );
						}
					}
					pMem = &pSlab[0];
				}
			}

			pMem->_unresolvedDependencies.store( 1, std::memory_order_relaxed );
			pMem->_activeChildren.store( 0, std::memory_order_relaxed );
			pMem->_state.store( TaskState::Pending, std::memory_order_relaxed );
			pMem->_bCancelled.store( false, std::memory_order_relaxed );
			pMem->_refCount.store( 1, std::memory_order_relaxed );
			pMem->_pOwner		   = nullptr;
			pMem->_pParent		   = nullptr;
			pMem->_pSharedCallable = nullptr;
#if !defined( SW_SHIPPING )
			pMem->_arrName[0] = 0;
#endif
			pMem->_rangeStart = 0;
			pMem->_rangeEnd	  = 0;
			pMem->_affinity	  = TaskThreadAffinity::Any;
			pMem->_priority	  = TaskPriority::Normal;
			return pMem;
		}

		void deallocate( TaskNode* pNode )
		{
			if ( pNode == nullptr )
				return;
			pNode->_successors.clearAndRelease();
			pNode->_callable = std::monostate{};
			pNode->_parentStage.reset();
#if !defined( SW_SHIPPING )
			pNode->_arrName[0] = 0;
#endif
			if ( pNode->_pSharedCallable != nullptr )
			{
				pNode->_pSharedCallable->release();
				pNode->_pSharedCallable = nullptr;
			}
			pNode->_pOwner = nullptr;

			if ( _freeQueue.enqueue( pNode ) == false )
			{
				std::scoped_lock<mutex> lock{ _slabMutex };
				_listOverflowFree.push_back( pNode );
			}
		}

	private:
		ConcurrentQueue<TaskNode*, 4096> _freeQueue;
		vector<TaskNode*>				 _listOverflowFree;
		vector<TaskNode*>				 _listSlabs;
		mutex							 _slabMutex;
	};

	void InlineSuccessorList::clearAndRelease()
	{
		lock();
		const uint32 inlineCount = _count < kInlineCapacity ? _count : kInlineCapacity;
		for ( uint32 index = 0; index < inlineCount; ++index )
		{
			if ( _arrInlineNodes[index] != nullptr )
			{
				_arrInlineNodes[index]->release();
				_arrInlineNodes[index] = nullptr;
			}
		}
		if ( _pOverflow != nullptr )
		{
			for ( TaskNode* pNode : *_pOverflow )
			{
				if ( pNode != nullptr )
					pNode->release();
			}
			_pOverflow.reset();
		}
		_count = 0;
		unlock();
	}

	void TaskNode::release()
	{
		if ( _refCount.fetch_sub( 1, std::memory_order_acq_rel ) == 1 )
		{
			if ( _pOwner != nullptr )
			{
				_pOwner->deallocateNode( this );
			}
		}
	}

	struct StageNode
	{
		StageNode()
		{
			_listTasks.reserve( 16 );
		}

		string						_name;
		vector<TaskNode*>			_listTasks;
		atomic<uint32>				_remainingTasks{ 0 };
		mutex						_mutex;
		std::condition_variable_any _cv;
	};

	TaskHandle::TaskHandle( TaskNode* pNode )
		: _pNode{ pNode }
	{
	}

	TaskHandle::TaskHandle( const TaskHandle& other )
		: _pNode{ other._pNode }
	{
		if ( _pNode != nullptr )
			_pNode->retain();
	}

	TaskHandle::TaskHandle( TaskHandle&& other ) noexcept
		: _pNode{ other._pNode }
	{
		other._pNode = nullptr;
	}

	TaskHandle::~TaskHandle()
	{
		if ( _pNode != nullptr )
		{
			_pNode->release();
			_pNode = nullptr;
		}
	}

	TaskHandle& TaskHandle::operator=( const TaskHandle& other )
	{
		if ( this != &other )
		{
			if ( other._pNode != nullptr )
				other._pNode->retain();
			if ( _pNode != nullptr )
				_pNode->release();
			_pNode = other._pNode;
		}
		return *this;
	}

	TaskHandle& TaskHandle::operator=( TaskHandle&& other ) noexcept
	{
		if ( this != &other )
		{
			if ( _pNode != nullptr )
				_pNode->release();
			_pNode		 = other._pNode;
			other._pNode = nullptr;
		}
		return *this;
	}

	TaskHandle& TaskHandle::setPriority( TaskPriority priority )
	{
		if ( _pNode != nullptr )
			_pNode->_priority = priority;
		return *this;
	}

	TaskPriority TaskHandle::getPriority() const
	{
		return _pNode != nullptr ? _pNode->_priority : TaskPriority::Normal;
	}

	TaskHandle& TaskHandle::precede( TaskHandle targetTask )
	{
		TaskNode* pTargetNode = targetTask.getNode();
		if ( _pNode != nullptr && pTargetNode != nullptr && _pNode != pTargetNode )
		{
			pTargetNode->retain();
			_pNode->_successors.push_back( pTargetNode );
			pTargetNode->_unresolvedDependencies.fetch_add( 1, std::memory_order_relaxed );
		}
		return *this;
	}

	TaskHandle& TaskHandle::succeed( TaskHandle dependencyTask )
	{
		dependencyTask.precede( *this );
		return *this;
	}

	TaskHandle TaskHandle::then( TaskDelegate nextTaskDelegate, TaskThreadAffinity affinity )
	{
		if ( _pNode == nullptr || _pNode->_pOwner == nullptr )
			return TaskHandle{};

		TaskHandle nextTask = _pNode->_pOwner->emplaceTask( "ChainedTask", nextTaskDelegate, affinity );
		precede( nextTask );
		return nextTask;
	}

	bool TaskHandle::cancel()
	{
		if ( _pNode != nullptr )
		{
			_pNode->_bCancelled.store( true, std::memory_order_release );
			return true;
		}
		return false;
	}

	bool TaskHandle::isCancelled() const
	{
		return _pNode != nullptr && _pNode->_bCancelled.load( std::memory_order_acquire );
	}

	void TaskHandle::submit()
	{
		if ( _pNode == nullptr || _pNode->_pOwner == nullptr )
			return;
		_pNode->_pOwner->submit( *this );
	}

	TaskStageHandle& TaskStageHandle::addTask( TaskHandle task )
	{
		TaskNode* pTaskNode = task.getNode();
		if ( _node != nullptr && pTaskNode != nullptr )
		{
			std::scoped_lock<mutex> lock{ _node->_mutex };
			pTaskNode->retain();
			_node->_listTasks.push_back( pTaskNode );
			pTaskNode->_parentStage = _node;
			_node->_remainingTasks.fetch_add( 1, std::memory_order_relaxed );
		}
		return *this;
	}

	TaskManager::TaskManager()
		: _bInitialized{ false }
		, _mainThreadId{}
		, _bStop{ false }
		, _listWorker{}
		, _listWorkerQueue{}
		, _nextWorkerQueueIndex{ 0 }
		, _sleepingWorkerCount{ 0 }
		, _globalWorkerQueue{}
		, _queueMainThread{}
		, _workerMutex{}
		, _cvWorker{}
		, _waitAllMutex{}
		, _cvWaitAll{}
		, _listAllStage{}
		, _stageMutex{}
		, _activeTaskCount{ 0 }
		, _nodePool{ sw::make_unique<TaskNodePool>() }
	{
	}

	TaskManager::~TaskManager()
	{
		shutdown();
	}

	TaskNode* TaskManager::allocateNode()
	{
		return _nodePool->allocate();
	}

	void TaskManager::deallocateNode( TaskNode* pNode )
	{
		_nodePool->deallocate( pNode );
	}

	bool TaskManager::initialize( uint32 threadCount )
	{
		if ( _bInitialized )
			return false;

		constexpr uint32 kDefaultThreadCount = 4;
		if ( threadCount == 0 )
		{
			threadCount = std::thread::hardware_concurrency();
			if ( threadCount == 0 )
				threadCount = kDefaultThreadCount;
		}

		_mainThreadId = std::this_thread::get_id();
		_bStop		  = false;
		_sleepingWorkerCount.store( 0, std::memory_order_relaxed );
		_listWorker.reserve( threadCount );
		_listWorkerQueue.reserve( threadCount );
		for ( uint32 workerIndex = 0; workerIndex < threadCount; ++workerIndex )
		{
			_listWorkerQueue.push_back( make_unique<WorkerQueue>() );
		}

		for ( uint32 threadIndex = 0; threadIndex < threadCount; ++threadIndex )
		{
			_listWorker.emplace_back( &TaskManager::workerLoop, this, threadIndex );
		}

		_bInitialized = true;
		SW_LOG_INFO( "Initialized with %# worker threads.", threadCount );
		return true;
	}

	void TaskManager::shutdown()
	{
		if ( _bInitialized == false )
			return;

		BLOCK( "Wait For Active Tasks" )
		{
			waitAll();
		}

		BLOCK( "Stop Worker Threads" )
		{
			{
				std::scoped_lock<mutex> lock{ _workerMutex };
				_bStop = true;
				_cvWorker.notify_all();
			}

			for ( std::thread& worker : _listWorker )
			{
				if ( worker.joinable() )
					worker.join();
			}
			_listWorker.clear();
			_listWorkerQueue.clear();
		}

		BLOCK( "Cleanup Resources" )
		{
			clear();
			_mainThreadId = {};
			_bInitialized = false;
		}
		SW_LOG_INFO( "Shutdown cleanly." );
	}

	bool TaskManager::isMainThread() const
	{
		return _bInitialized && std::this_thread::get_id() == _mainThreadId;
	}

	void TaskManager::ensureMainThread() const
	{
		SW_ASSERT( isMainThread() );
	}

	int32 TaskManager::getCurrentWorkerIndex() const
	{
		return t_currentWorkerIndex;
	}

	bool TaskManager::isWorkerThread() const
	{
		return t_bTaskWorkerThread;
	}

	void TaskManager::ensureWorkerThread() const
	{
		SW_ASSERT( isWorkerThread() );
	}

	bool TaskManager::isInsideParallelTask() const
	{
		return t_bInsideParallelTask;
	}

	void TaskManager::ensureInsideParallelTask() const
	{
		SW_ASSERT( isInsideParallelTask() );
	}

	TaskHandle TaskManager::emplaceTask( const TaskDelegate& delegate, TaskThreadAffinity affinity )
	{
		return emplaceTask( "GeneralTask", delegate, affinity );
	}

	TaskHandle TaskManager::emplaceTask( string_view name, const TaskDelegate& delegate, TaskThreadAffinity affinity )
	{
		if ( _bInitialized == false || _bStop.load( std::memory_order_relaxed ) )
		{
			SW_LOG_WARNING( "Cannot emplace task '%#' while TaskManager is not initialized or stopping.", string{ name }.c_str() );
			return TaskHandle{};
		}

		TaskNode* pNode = allocateNode();
		pNode->_pOwner	= this;
		setTaskName( pNode, name );
		pNode->_affinity = affinity;
		pNode->_callable = delegate;
		pNode->_state	 = TaskState::Pending;

		if ( t_pCurrentRunningTask != nullptr )
		{
			pNode->_pParent = t_pCurrentRunningTask;
			t_pCurrentRunningTask->_activeChildren.fetch_add( 1, std::memory_order_relaxed );
		}

		_activeTaskCount.fetch_add( 1, std::memory_order_relaxed );
		return TaskHandle{ pNode };
	}

	TaskHandle TaskManager::emplaceTask( const TaskArgsDelegate& delegate, const TaskArgs& args, TaskThreadAffinity affinity )
	{
		return emplaceTask( "GeneralArgsTask", delegate, args, affinity );
	}

	TaskHandle TaskManager::emplaceTask( string_view name, const TaskArgsDelegate& delegate, const TaskArgs& args, TaskThreadAffinity affinity )
	{
		if ( _bInitialized == false || _bStop.load( std::memory_order_relaxed ) )
		{
			SW_LOG_WARNING( "Cannot emplace task '%#' while TaskManager is not initialized or stopping.", string{ name }.c_str() );
			return TaskHandle{};
		}

		TaskNode* pNode = allocateNode();
		pNode->_pOwner	= this;
		setTaskName( pNode, name );
		pNode->_affinity = affinity;
		pNode->_callable = TaskArgsPayload{ delegate, args };
		pNode->_state	 = TaskState::Pending;

		if ( t_pCurrentRunningTask != nullptr )
		{
			pNode->_pParent = t_pCurrentRunningTask;
			t_pCurrentRunningTask->_activeChildren.fetch_add( 1, std::memory_order_relaxed );
		}

		_activeTaskCount.fetch_add( 1, std::memory_order_relaxed );
		return TaskHandle{ pNode };
	}

	TaskHandle TaskManager::emplaceParallel( uint32 count, const ParallelTaskDelegate& delegate, TaskThreadAffinity affinity )
	{
		return emplaceParallel( "ParallelTask", count, delegate, affinity );
	}

	TaskHandle TaskManager::emplaceParallel( string_view name, uint32 count, const ParallelTaskDelegate& delegate, TaskThreadAffinity affinity )
	{
		if ( count == 0 )
			return emplaceTask( name, TaskDelegate{}, affinity );

		uint32 workerCount = getWorkerCount();
		if ( workerCount == 0 )
			workerCount = 1;

		uint32 numChunks = MathUtil::min( count, workerCount * 2 );
		uint32 chunkSize = ( count + numChunks - 1 ) / numChunks;
		if ( chunkSize == 0 )
			chunkSize = 1;
		numChunks = ( count + chunkSize - 1 ) / chunkSize;

		TaskHandle parentTask = emplaceTask( name, TaskDelegate{}, affinity );
		if ( parentTask.isValid() == false )
			return parentTask;

		SharedTaskCallable* pSharedCallable = SharedTaskCallable::create( TaskCallable{ delegate }, static_cast<int32>( numChunks ) );
		for ( uint32 start = 0; start < count; start += chunkSize )
		{
			uint32	  end	   = MathUtil::min( start + chunkSize, count );
			TaskNode* pSubTask = allocateNode();

			pSubTask->_pOwner		   = this;
			pSubTask->_pSharedCallable = pSharedCallable;
			setTaskName( pSubTask, name );
			pSubTask->_rangeStart = start;
			pSubTask->_rangeEnd	  = end;
			pSubTask->_state	  = TaskState::Pending;

			_activeTaskCount.fetch_add( 1, std::memory_order_relaxed );

			TaskHandle subHandle{ pSubTask };
			subHandle.precede( parentTask );
			subHandle.submit();
		}

		return parentTask;
	}

	TaskHandle TaskManager::emplaceParallelBlock( uint32 start, uint32 end, const ParallelBlockDelegate& delegate, TaskThreadAffinity affinity )
	{
		if ( end <= start )
			return emplaceTask( "ParallelBlockTask", TaskDelegate{}, affinity );

		uint32 count	   = end - start;
		uint32 workerCount = getWorkerCount();
		if ( workerCount == 0 )
			workerCount = 1;

		uint32 numChunks = MathUtil::min( count, workerCount * 2 );
		uint32 chunkSize = ( count + numChunks - 1 ) / numChunks;
		if ( chunkSize == 0 )
			chunkSize = 1;
		numChunks = ( count + chunkSize - 1 ) / chunkSize;

		TaskHandle parentTask = emplaceTask( "ParallelBlockTask", TaskDelegate{}, affinity );
		if ( parentTask.isValid() == false )
			return parentTask;

		SharedTaskCallable* pSharedCallable = SharedTaskCallable::create( TaskCallable{ delegate }, static_cast<int32>( numChunks ) );
		for ( uint32 offset = 0; offset < count; offset += chunkSize )
		{
			uint32	  chunkStart = start + offset;
			uint32	  chunkEnd	 = MathUtil::min( chunkStart + chunkSize, end );
			TaskNode* pSubTask	 = allocateNode();

			pSubTask->_pOwner		   = this;
			pSubTask->_pSharedCallable = pSharedCallable;
			setTaskName( pSubTask, "ParallelBlockTask" );
			pSubTask->_rangeStart = chunkStart;
			pSubTask->_rangeEnd	  = chunkEnd;
			pSubTask->_state	  = TaskState::Pending;

			_activeTaskCount.fetch_add( 1, std::memory_order_relaxed );

			TaskHandle subHandle{ pSubTask };
			subHandle.precede( parentTask );
			subHandle.submit();
		}

		return parentTask;
	}

	TaskStageHandle TaskManager::createAnonymousStage( string_view stageName )
	{
		shared_ptr<StageNode> stage = sw::make_shared<StageNode>();
		stage->_name				= stageName;
		return TaskStageHandle{ stage };
	}

	TaskStageHandle TaskManager::getStage( string_view stageName )
	{
		std::scoped_lock<mutex> lock{ _stageMutex };
		for ( auto it = _listAllStage.begin(); it != _listAllStage.end(); )
		{
			shared_ptr<StageNode> pStage = it->lock();
			if ( pStage != nullptr )
			{
				if ( pStage->_name == stageName )
					return TaskStageHandle{ pStage };
				++it;
			}
			else
				it = _listAllStage.erase( it );
		}
		return TaskStageHandle{};
	}

	TaskStageHandle TaskManager::getOrCreateStage( string_view stageName )
	{
		std::scoped_lock<mutex> lock{ _stageMutex };
		for ( auto it = _listAllStage.begin(); it != _listAllStage.end(); )
		{
			shared_ptr<StageNode> pStage = it->lock();
			if ( pStage != nullptr )
			{
				if ( pStage->_name == stageName )
					return TaskStageHandle{ pStage };
				++it;
			}
			else
				it = _listAllStage.erase( it );
		}

		shared_ptr<StageNode> stage = sw::make_shared<StageNode>();
		stage->_name				= stageName;
		_listAllStage.push_back( stage );

		return TaskStageHandle{ stage };
	}

	void TaskManager::waitStage( TaskStageHandle stage )
	{
		if ( stage._node == nullptr )
			return;

		uint32 spinCount = 0;
		while ( stage._node->_remainingTasks.load( std::memory_order_acquire ) > 0 )
		{
			if ( isMainThread() )
				dispatchMainThreadTasks();

			if ( tryHelpAndExecute() )
			{
				spinCount = 0;
				continue;
			}

			if ( spinCount < kIdleSpinCount )
			{
				sw::cpuPause();
				++spinCount;
				continue;
			}

			std::unique_lock<mutex> lock{ _waitAllMutex };
			if ( stage._node->_remainingTasks.load( std::memory_order_acquire ) > 0 )
				_cvWaitAll.wait( lock );
		}

		{
			std::scoped_lock<mutex> doneLock{ stage._node->_mutex };
			for ( TaskNode* pTask : stage._node->_listTasks )
			{
				if ( pTask == nullptr )
					continue;
				pTask->release();
			}
			stage._node->_listTasks.clear();
		}
	}

	bool TaskManager::isStageComplete( TaskStageHandle stage )
	{
		if ( stage._node == nullptr )
			return true;

		return stage._node->_remainingTasks.load( std::memory_order_relaxed ) == 0;
	}

	void TaskManager::submit( TaskHandle handle )
	{
		if ( handle.isValid() == false )
			return;

		TaskNode* pNode		= handle.getNode();
		int32	  remaining = pNode->_unresolvedDependencies.fetch_sub( 1, std::memory_order_acq_rel );
		if ( remaining == 1 )
			scheduleReadyTask( pNode );
	}

	bool TaskManager::waitAll( uint32 timeoutMs )
	{
		const auto startTime = std::chrono::steady_clock::now();
		uint32	   spinCount = 0;
		while ( _activeTaskCount.load( std::memory_order_acquire ) > 0 )
		{
			if ( isMainThread() )
				dispatchMainThreadTasks();

			if ( tryHelpAndExecute() )
			{
				spinCount = 0;
				continue;
			}

			if ( timeoutMs > 0 )
			{
				const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() - startTime ).count();
				if ( elapsed >= static_cast<int64>( timeoutMs ) )
					return _activeTaskCount.load( std::memory_order_acquire ) == 0;

				const uint32			remainingMs = static_cast<uint32>( static_cast<int64>( timeoutMs ) - elapsed );
				std::unique_lock<mutex> lock{ _waitAllMutex };
				if ( _activeTaskCount.load( std::memory_order_acquire ) > 0 )
					_cvWaitAll.wait_for( lock, std::chrono::milliseconds( remainingMs ) );
				continue;
			}

			if ( spinCount < kIdleSpinCount )
			{
				sw::cpuPause();
				++spinCount;
				continue;
			}

			std::unique_lock<mutex> lock{ _waitAllMutex };
			if ( _activeTaskCount.load( std::memory_order_acquire ) > 0 )
				_cvWaitAll.wait( lock );
		}
		return true;
	}

	void TaskManager::clear()
	{
		{
			std::scoped_lock<mutex> lock{ _stageMutex };
			_listAllStage.clear();
		}
		for ( auto& wq : _listWorkerQueue )
		{
			TaskNode* pTemp{ nullptr };
			while ( wq->_queue.steal( pTemp ) )
			{
				if ( pTemp == nullptr )
					continue;
				pTemp->release();
			}
		}
		{
			TaskNode* pTemp{ nullptr };
			while ( _queueMainThread.dequeue( pTemp ) )
			{
				if ( pTemp == nullptr )
					continue;
				pTemp->release();
			}
		}
		{
			TaskNode* pTemp{ nullptr };
			while ( _globalWorkerQueue.dequeue( pTemp ) )
			{
				if ( pTemp == nullptr )
					continue;
				pTemp->release();
			}
		}
		_activeTaskCount.store( 0, std::memory_order_release );
		{
			std::scoped_lock<mutex> lock{ _waitAllMutex };
			_cvWaitAll.notify_all();
		}
	}

	TaskHandle TaskManager::whenAll( const vector<TaskHandle>& tasks, const TaskDelegate& continuation, TaskThreadAffinity affinity )
	{
		if ( tasks.empty() )
			return emplaceTask( "WhenAllContinuation", continuation, affinity );

		TaskHandle nextTask = emplaceTask( "WhenAllContinuation", continuation, affinity );

		for ( const TaskHandle& task : tasks )
		{
			if ( task.isValid() )
			{
				TaskHandle mutTask = task;
				mutTask.precede( nextTask );
			}
		}

		return nextTask;
	}

	TaskHandle TaskManager::whenAny( const vector<TaskHandle>& tasks, const TaskDelegate& continuation, TaskThreadAffinity affinity )
	{
		if ( tasks.empty() )
			return emplaceTask( "WhenAnyContinuation", continuation, affinity );

		shared_ptr<atomic<bool>> firedFlag = sw::make_shared<atomic<bool>>( false );
		TaskHandle				 nextTask  = emplaceTask( "WhenAnyContinuation", continuation, affinity );

		// 1 for user submit(), 1 for trigger completion
		nextTask.getNode()->_unresolvedDependencies.store( 2, std::memory_order_relaxed );

		for ( const TaskHandle& task : tasks )
		{
			if ( task.isValid() )
			{
				TaskDelegate triggerDelegate = SW_DELEGATE_LAMBDA( TaskDelegate,
																   [nextTask, firedFlag, this]()
				{
					if ( firedFlag->exchange( true, std::memory_order_acq_rel ) == false )
					{
						int32 remaining = nextTask.getNode()->_unresolvedDependencies.fetch_sub( 1, std::memory_order_acq_rel );
						if ( remaining == 1 )
							scheduleReadyTask( nextTask.getNode() );
					}
				} );

				TaskHandle triggerTask = emplaceTask( "whenAny_Trigger", triggerDelegate );
				TaskHandle mutTask	   = task;
				mutTask.precede( triggerTask );
				triggerTask.submit();
			}
		}

		return nextTask;
	}

	void TaskManager::dispatchMainThreadTasks()
	{
		ensureMainThread();

		TaskNode* pNode{ nullptr };
		while ( _queueMainThread.dequeue( pNode ) )
		{
			if ( pNode == nullptr )
				continue;
			executeTask( pNode );
		}
	}

	struct TaskExecutionVisitor
	{
		uint32 _rangeStart{ 0 };
		uint32 _rangeEnd{ 0 };

		void operator()( std::monostate& ) const {}

		void operator()( TaskDelegate& del ) const
		{
			if ( del.isBound() )
				del();
		}

		void operator()( TaskArgsPayload& payload ) const
		{
			if ( payload._delegate.isBound() )
				payload._delegate( payload._args );
		}

		void operator()( ParallelTaskDelegate& del ) const
		{
			const ParallelTaskScope parallelScope{};
			if ( del.isBound() )
			{
				for ( uint32 elementIndex = _rangeStart; elementIndex < _rangeEnd; ++elementIndex )
				{
					del( elementIndex );
				}
			}
		}

		void operator()( ParallelBlockDelegate& del ) const
		{
			const ParallelTaskScope parallelScope{};
			if ( del.isBound() )
			{
				del( _rangeStart, _rangeEnd );
			}
		}
	};

	void TaskManager::executeTask( TaskNode* pNode )
	{
		pNode->_state = TaskState::Running;

		if ( pNode->_bCancelled.load( std::memory_order_acquire ) == false )
		{
			TaskNode* pPrevRunningTask = t_pCurrentRunningTask;
			t_pCurrentRunningTask	   = pNode;

			BLOCK( "Execute Task Delegate" )
			{
				TaskCallable&		 callable = pNode->_pSharedCallable != nullptr ? pNode->_pSharedCallable->_callable : pNode->_callable;
				TaskExecutionVisitor visitor{ pNode->_rangeStart, pNode->_rangeEnd };
				std::visit( visitor, callable );
			}

			t_pCurrentRunningTask = pPrevRunningTask;
		}

		BLOCK( "Finalize Task" )
		{
			onTaskFinished( pNode );
		}

		pNode->release(); // Release queue reference
	}

	bool TaskManager::tryTakeTask( uint32 workerId, TaskNode*& pNode )
	{
		pNode = nullptr;

		WorkerQueue& localQ = *std::as_const( _listWorkerQueue )[workerId];
		if ( localQ._queue.pop( pNode ) && pNode != nullptr )
			return true;

		if ( _globalWorkerQueue.dequeue( pNode ) && pNode != nullptr )
			return true;

		const uint32 numWorkers = static_cast<uint32>( _listWorkerQueue.size() );
		for ( uint32 workerIndex = 1; workerIndex < numWorkers; ++workerIndex )
		{
			const uint32 targetId = ( workerId + workerIndex ) % numWorkers;
			WorkerQueue& targetQ  = *std::as_const( _listWorkerQueue )[targetId];
			if ( targetQ._queue.steal( pNode ) && pNode != nullptr )
				return true;
		}

		pNode = nullptr;
		return false;
	}

	bool TaskManager::tryHelpAndExecute()
	{
		const int32 workerId = getCurrentWorkerIndex();
		if ( workerId >= 0 )
		{
			TaskNode*	 pNode{ nullptr };
			WorkerQueue& localQ = *std::as_const( _listWorkerQueue )[static_cast<uint32>( workerId )];
			if ( localQ._queue.pop( pNode ) && pNode != nullptr )
			{
				executeTask( pNode );
				return true;
			}
		}

		return tryStealAndExecute( ~0u );
	}

	bool TaskManager::tryStealAndExecute( uint32 excludedWorkerId )
	{
		const uint32 numWorkers = static_cast<uint32>( _listWorkerQueue.size() );
		if ( numWorkers == 0 )
			return false;

		TaskNode* pNode{ nullptr };
		if ( _globalWorkerQueue.dequeue( pNode ) && pNode != nullptr )
		{
			executeTask( pNode );
			return true;
		}

		for ( uint32 workerIndex = 0; workerIndex < numWorkers; ++workerIndex )
		{
			if ( workerIndex == excludedWorkerId )
				continue;

			WorkerQueue& targetQ = *std::as_const( _listWorkerQueue )[workerIndex];
			if ( targetQ._queue.steal( pNode ) && pNode != nullptr )
			{
				executeTask( pNode );
				return true;
			}
		}
		return false;
	}

	void TaskManager::workerLoop( uint32 workerId )
	{
		t_bTaskWorkerThread	 = true;
		t_currentWorkerIndex = static_cast<int32>( workerId );

		while ( _bStop.load( std::memory_order_relaxed ) == false )
		{
			TaskNode* pNode{ nullptr };
			if ( tryTakeTask( workerId, pNode ) )
			{
				executeTask( pNode );
				continue;
			}

			bool bFoundInSpin = false;
			for ( uint32 spin = 0; spin < kIdleSpinCount; ++spin )
			{
				sw::cpuPause();
				if ( tryTakeTask( workerId, pNode ) )
				{
					bFoundInSpin = true;
					break;
				}
			}

			if ( bFoundInSpin && pNode != nullptr )
			{
				executeTask( pNode );
				continue;
			}

			_sleepingWorkerCount.fetch_add( 1, std::memory_order_relaxed );
			{
				std::unique_lock<mutex> lock{ _workerMutex };
				if ( _bStop.load( std::memory_order_relaxed ) == false && tryTakeTask( workerId, pNode ) == false )
					_cvWorker.wait( lock );
			}
			_sleepingWorkerCount.fetch_sub( 1, std::memory_order_relaxed );

			if ( pNode != nullptr )
			{
				executeTask( pNode );
				continue;
			}
		}

		t_bTaskWorkerThread	  = false;
		t_currentWorkerIndex  = -1;
		t_bInsideParallelTask = false;
	}

	void TaskManager::scheduleReadyTask( TaskNode* pNode )
	{
		if ( pNode == nullptr )
			return;

		TaskState expected = TaskState::Pending;
		if ( pNode->_state.compare_exchange_strong( expected, TaskState::Ready, std::memory_order_acq_rel ) )
		{
			pNode->retain(); // Queue holds ownership

			if ( pNode->_affinity == TaskThreadAffinity::MainThread )
			{
				while ( _queueMainThread.enqueue( pNode ) == false )
				{
					std::this_thread::yield();
				}
				std::scoped_lock<mutex> waitLock{ _waitAllMutex };
				_cvWaitAll.notify_one();
			}
			else
			{
				const uint32 numWorkers = static_cast<uint32>( _listWorkerQueue.size() );
				if ( numWorkers > 0 )
				{
					int32 workerId = getCurrentWorkerIndex();
					if ( workerId >= 0 )
					{
						WorkerQueue& localQ = *std::as_const( _listWorkerQueue )[static_cast<uint32>( workerId )];
						while ( localQ._queue.push( pNode ) == false )
						{
							std::this_thread::yield();
						}
					}
					else
					{
						while ( _globalWorkerQueue.enqueue( pNode ) == false )
						{
							std::this_thread::yield();
						}
					}

					std::scoped_lock<mutex> workerLock{ _workerMutex };
					_cvWorker.notify_one();
				}
			}
		}
	}

	void TaskManager::onTaskFinished( TaskNode* pNode )
	{
		pNode->_state.store( TaskState::WaitingForChildren, std::memory_order_release );
		if ( pNode->_activeChildren.load( std::memory_order_acquire ) > 0 )
			return;

		pNode->_state.store( TaskState::Completed, std::memory_order_release );

		BLOCK( "Update Parent Task" )
		{
			TaskNode* pParent = pNode->_pParent;
			if ( pParent != nullptr )
			{
				int32 remaining = pParent->_activeChildren.fetch_sub( 1, std::memory_order_acq_rel ) - 1;
				if ( remaining == 0 && pParent->_state.load( std::memory_order_acquire ) == TaskState::WaitingForChildren )
					onTaskFinished( pParent );
			}
		}

		BLOCK( "Update Stage" )
		{
			shared_ptr<StageNode> pStage = pNode->_parentStage.lock();
			if ( pStage != nullptr )
			{
				uint32 prev = pStage->_remainingTasks.fetch_sub( 1, std::memory_order_release );
				if ( prev == 1 )
				{
					{
						std::scoped_lock<mutex> lock{ pStage->_mutex };
						pStage->_cv.notify_all();
					}
					std::scoped_lock<mutex> waitLock{ _waitAllMutex };
					_cvWaitAll.notify_all();
				}
			}
		}

		BLOCK( "Trigger Successors and Cleanup" )
		{
			pNode->_successors.forEach( [this]( TaskNode* pSucc )
			{
				if ( pSucc != nullptr )
				{
					int32 remaining = pSucc->_unresolvedDependencies.fetch_sub( 1, std::memory_order_acq_rel );
					if ( remaining == 1 )
						scheduleReadyTask( pSucc );
				}
			} );

			pNode->_callable = std::monostate{};
			pNode->_pParent	 = nullptr;

			uint32 activeLeft = _activeTaskCount.fetch_sub( 1, std::memory_order_acq_rel );
			if ( activeLeft == 1 )
			{
				std::scoped_lock<mutex> lock{ _waitAllMutex };
				_cvWaitAll.notify_all();
			}
		}
	}

} // namespace sw
