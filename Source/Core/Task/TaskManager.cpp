#include "pch.h"

#include "Core/Task/TaskManager.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/LockFreeObjectPool.h"
#include "Core/Concurrency/SpinLock.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/fixed_string.h"

namespace sw
{
    SW_LOG_CALLER( "TaskManager" );

    namespace
    {

        thread_local bool      t_bTaskWorkerThread   = false;   ///< 현재 스레드가 TaskManager 워커 스레드인지 여부
        thread_local bool      t_bInsideParallelTask = false;   ///< 현재 병렬 배치 태스크 내부 실행 중인지 여부
        thread_local int32     t_currentWorkerIndex  = -1;      ///< 현재 워커 스레드의 인덱스
        thread_local TaskNode* t_pCurrentRunningTask = nullptr; ///< 현재 스레드에서 실행 중인 태스크 노드 포인터

        /// @brief 대기 함수(waitStage/waitAll)가 잠들기 전에 도는 `cpuPause` 횟수(약 2 us). 대기 중엔 남의 일을 돕는다.
        constexpr uint32 kIdleSpinCount = 64;
        /**
         * @brief 워커가 잠들기 전에 일감을 기다리며 도는 시간(마이크로초). 짧다 — 재 봤다.
         * @details 스핀은 `_workEpoch` 한 줄만 읽으므로 큐 경합은 없다. 그런데도 길게(50 us) 돌리면 손해다:
         *          워커가 SMT 형제 코어를 점유해 게임·렌더 스레드가 느려진다 — 렌더 그래프 병렬 기록이
         *          150~192 us(2 us 스핀) 대 214~242 us(50 us 스핀). 소수만 길게 돌리는 hot pool(2·4 개)도
         *          이득이 없었다. 이 엔진의 프레임(~0.7 ms)은 잡 사이 빈틈이 워커 수만큼의 코어를 데워 둘
         *          만큼 길지 않다 — 상용 엔진의 8~16 ms 프레임에서는 답이 달라질 수 있으니 상수로 남긴다.
         */
        constexpr int64 kWorkerIdleSpinMicro = 2;
        /**
         * @brief 워커 수를 정할 때 하드웨어 스레드에서 빼 두는 수 — 게임 스레드와 렌더 스레드 몫.
         * @details 예전엔 하드웨어 스레드 수만큼 워커를 만들었다(16 코어에 워커 16 + GT + RT + 메인).
         *          그러면 워커가 일하는 동안 GT·RT 가 코어를 나눠 써야 해서, 게임 스레드가 잡을 돌리면
         *          렌더 스레드의 병렬 기록이 밀렸다(170 -> 261 us). 상용 엔진도 전용 스레드 몫을 뺀다.
         */
        constexpr uint32 kReservedThreadCount = 2;
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
            ParallelTaskScope( const ParallelTaskScope& )            = delete;
            ParallelTaskScope& operator=( const ParallelTaskScope& ) = delete;
        };

    } // namespace

    /**
     * @brief 임의의 매개변수 가방(TaskArgs)과 함께 호출되는 델리게이트 페이로드
     */
    struct TaskArgsPayload
    {
        TaskArgsDelegate _delegate;
        TaskArgs         _args;
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

    struct SharedTaskCallable;
    /** @brief 병렬 그룹 하나가 콜러블 하나를 쓴다 — 동시에 살아 있는 그룹 수의 상한이지 청크 수가 아니다. */
    constexpr uint32 kSharedCallablePoolCapacity = 512;
    using SharedTaskCallablePool                 = LockFreeObjectPool<SharedTaskCallable, kSharedCallablePoolCapacity>;

    /**
     * @brief 병렬 그룹의 서브태스크들이 나눠 쓰는 콜러블. 풀에서 오고, 마지막 서브태스크가 놓으면 풀로 돌아간다.
     * @details 예전에는 그룹마다 `sw_new` 였다 — 렌더 그래프 웨이브·트랜스폼 플러시·씬 수집이 프레임마다 그룹을 만드니
     *          그 수만큼 힙을 두드렸다. 풀이 비면(동시에 512 그룹) 힙으로 물러나고, 그 경우만 `_pPool` 이 null 이다.
     */
    struct SharedTaskCallable
    {
        TaskCallable            _callable;
        atomic<int32>           _refCount{ 0 };
        SharedTaskCallablePool* _pPool{ nullptr }; ///< 돌아갈 풀. 힙에서 왔으면 null.

        static SharedTaskCallable* create( SharedTaskCallablePool* pPool, TaskCallable callable, int32 refCount )
        {
            SharedTaskCallable* pShared = ( pPool != nullptr ) ? pPool->acquire() : nullptr;
            if ( pShared == nullptr )
                pShared = sw_new SharedTaskCallable();
            else
                pShared->_pPool = pPool;
            pShared->_callable = std::move( callable );
            pShared->_refCount.store( refCount, std::memory_order_relaxed );
            return pShared;
        }

        void release()
        {
            if ( _refCount.fetch_sub( 1, std::memory_order_acq_rel ) != 1 )
                return;
            SharedTaskCallablePool* pPool = _pPool;
            if ( pPool != nullptr )
            {
                SharedTaskCallable* pSelf = this;
                pPool->release( pSelf ); // ~SharedTaskCallable 뒤 반납
                return;
            }
            sw_delete( this );
        }
    };

    struct TaskNode;

    class TaskNodePool;

    /**
     * @brief 스테이지 — 태스크 묶음의 완료를 기다리는 단위. 매니저의 풀에서 오고 침입형 참조 계수로 산다.
     * @details 참조는 둘이 쥔다: 핸들 사본과, **남은 태스크가 있는 동안의 스테이지 자신**(0→1 에서 잡고 1→0 에서
     *          놓는다). 태스크 노드는 참조를 쥐지 않는다 — 노드가 스테이지를 쥐고 스테이지가 노드 목록을 쥐면
     *          고리가 되어 어느 쪽도 못 돌아간다. 노드의 `_parentStage` 는 남은 태스크가 있는 동안만 유효하고,
     *          그 동안은 스테이지가 스스로를 쥐고 있으므로 안전하다.
     */
    struct StageNode
    {
        fixed_string<constant::kMaxBuffer64> _name; ///< 이름 있는 스테이지의 조회 키. 힙을 만지지 않는다.
        vector<TaskNode*>                    _listTask;
        atomic<uint32>                       _remainingTasks{ 0 };
        mutex                                _mutex;
        std::condition_variable_any          _cv;
        atomic<int32>                        _refCount{ 0 };
        TaskNodePool*                        _pPool{ nullptr };

        void retain() { _refCount.fetch_add( 1, std::memory_order_relaxed ); }
        /** @brief 마지막 참조가 놓이면 남은 태스크 참조를 놓고 풀로 돌아갑니다. */
        void release();
    };

    /**
     * @struct InlineSuccessorList
     * @brief 후속 태스크(Successors) 목록을 스택/인라인 버퍼(최대 4개)에 저장하여 힙 할당을 방지하는 Small-Vector 최적화 구조체
     */
    struct InlineSuccessorList
    {
        static constexpr uint32 kInlineCapacity = 4;

        uint32                        _count{ 0 };
        TaskNode*                     _arrInlineNode[kInlineCapacity]{};
        unique_ptr<vector<TaskNode*>> _pOverflow;
        mutable SpinLock              _lock;

        void lock() const noexcept
        {
            _lock.lock();
        }

        void unlock() const noexcept
        {
            _lock.unlock();
        }

        void push_back( TaskNode* pNode )
        {
            lock();
            if ( _count < kInlineCapacity )
            {
                _arrInlineNode[_count++] = pNode;
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
            TaskNode*         arrInlineCopy[kInlineCapacity]{};
            uint32            inlineCopyCount{ 0 };
            vector<TaskNode*> listOverflowCopy;

            lock();
            inlineCopyCount = _count < kInlineCapacity ? _count : kInlineCapacity;
            for ( uint32 index = 0; index < inlineCopyCount; ++index )
            {
                arrInlineCopy[index] = _arrInlineNode[index];
            }
            if ( _pOverflow != nullptr )
                listOverflowCopy = *_pOverflow;
            unlock();

            for ( uint32 index = 0; index < inlineCopyCount; ++index )
            {
                func( arrInlineCopy[index] );
            }
            for ( TaskNode* pNode : listOverflowCopy )
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

        /**
         * @brief 디버깅·프로파일링용 이름을 설정합니다 (용량 초과는 잘라 담습니다).
         * @details 예전엔 익명 네임스페이스 안의 자유 헬퍼였다. 하는 일이 전부 이 구조체 자기 필드
         *          조작인데 **완전한 TaskNode 가 필요해서** 파일 위쪽 익명 블록에 못 들어갔고, 그래서
         *          블록이 하나 더 있었다. TaskNode 는 위쪽 블록의 `kTaskNameCapacity` 를 쓰므로 순서를
         *          뒤집을 수도 없었다. 데이터가 있는 자리로 옮기니 그 블록 자체가 필요 없어졌다.
         */
        void setName( [[maybe_unused]] string_view name )
        {
#if !defined( SW_SHIPPING )
            uint32 len = static_cast<uint32>( name.size() );
            if ( len > kTaskNameCapacity )
                len = kTaskNameCapacity;
            if ( len > 0 )
                Memory::copy( _arrName, name.data(), len );
            _arrName[len] = 0;
#endif
        }

#if !defined( SW_SHIPPING )
        utf8 _arrName[kTaskNameCapacity + 1]{};
#endif
        InlineSuccessorList _successors;
        TaskCallable        _callable;
        SharedTaskCallable* _pSharedCallable{ nullptr };
        StageNode*          _parentStage{ nullptr }; ///< 남은 태스크가 있는 동안만 유효 — 스테이지가 그 동안 스스로를 쥔다

        uint32        _rangeStart{ 0 };
        uint32        _rangeEnd{ 0 };
        atomic<int32> _unresolvedDependencies{ 1 }; // 1 = Builder Dependency

        TaskThreadAffinity _affinity = TaskThreadAffinity::Any;
        TaskPriority       _priority{ TaskPriority::Normal };
        atomic<TaskState>  _state{ TaskState::Pending };
        atomic<bool>       _bCancelled{ false };

        TaskManager*  _pOwner{ nullptr };
        TaskNode*     _pParent{ nullptr };
        atomic<int32> _activeChildren{ 0 };
        atomic<int32> _refCount{ 1 };
    };

    class TaskNodePool
    {
        static constexpr uint32 kSlabSize = 64;

    public:
        TaskNodePool() = default;

        /**
         * @brief 태스크 슬랩을 되돌립니다. 스테이지는 `_listStageAll` 이 소유하므로 여기서 할 일이 없다.
         * @details 슬랩만 손으로 푸는 것은 `Memory::allocate` 로 원소 64 개를 한 번에 잡고 배치 new 로
         *          짓기 때문이다 — 그쪽은 짝이 되는 해제도 손으로 해야 한다.
         */
        ~TaskNodePool()
        {
            std::scoped_lock<mutex> lock{ _slabMutex };
            for ( TaskNode* pSlab : _listSlab )
            {
                if ( pSlab == nullptr )
                    continue;
                for ( uint32 index = 0; index < kSlabSize; ++index )
                {
                    pSlab[index].~TaskNode();
                }
                Memory::free( pSlab );
            }
            _listSlab.clear();
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
                    TaskNode* pSlab = static_cast<TaskNode*>( Memory::allocate( sizeof( TaskNode ) * kSlabSize ) );
                    for ( uint32 index = 0; index < kSlabSize; ++index )
                    {
                        new ( &pSlab[index] ) TaskNode();
                    }
                    {
                        std::scoped_lock<mutex> lock{ _slabMutex };
                        _listSlab.push_back( pSlab );
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
            pMem->_pOwner          = nullptr;
            pMem->_pParent         = nullptr;
            pMem->_pSharedCallable = nullptr;
#if !defined( SW_SHIPPING )
            pMem->_arrName[0] = 0;
#endif
            pMem->_rangeStart = 0;
            pMem->_rangeEnd   = 0;
            pMem->_affinity   = TaskThreadAffinity::Any;
            pMem->_priority   = TaskPriority::Normal;
            return pMem;
        }

        void deallocate( TaskNode* pNode )
        {
            if ( pNode == nullptr )
                return;
            pNode->_successors.clearAndRelease();
            pNode->_callable    = std::monostate{};
            pNode->_parentStage = nullptr;
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

        /** @brief 스테이지 노드를 꺼냅니다 — 풀에 남은 것이 없을 때만 새로 만든다(용량은 그 뒤로 남는다). */
        StageNode* allocateStage()
        {
            StageNode* pStage{ nullptr };
            {
                std::scoped_lock<mutex> lock{ _stageMutex };
                if ( _listStageFree.empty() == false )
                {
                    pStage = _listStageFree.back();
                    _listStageFree.pop_back();
                }
            }
            if ( pStage == nullptr )
            {
                unique_ptr<StageNode> uniqueStage = make_unique<StageNode>();
                pStage                            = uniqueStage.get();
                pStage->_listTask.reserve( 16 );
                std::scoped_lock<mutex> lock{ _stageMutex };
                _listStageAll.push_back( std::move( uniqueStage ) );
            }
            pStage->_name.clear();
            pStage->_listTask.clear();
            pStage->_remainingTasks.store( 0, std::memory_order_relaxed );
            pStage->_refCount.store( 1, std::memory_order_relaxed );
            pStage->_pPool = this;
            return pStage;
        }

        /** @brief 마지막 참조가 놓인 스테이지를 되돌립니다. 기다리지 않고 버린 스테이지의 태스크 참조도 여기서 놓는다. */
        void deallocateStage( StageNode* pStage )
        {
            if ( pStage == nullptr )
                return;
            for ( TaskNode* pTask : pStage->_listTask )
            {
                if ( pTask != nullptr )
                    pTask->release();
            }
            pStage->_listTask.clear();
            pStage->_name.clear();
            std::scoped_lock<mutex> lock{ _stageMutex };
            _listStageFree.push_back( pStage );
        }

        /** @brief 강제 정리(`clear`) — 살아 있는 스테이지를 전부 되돌립니다. 아무것도 돌고 있지 않을 때만. */
        void resetAllStages()
        {
            vector<StageNode*> listLive;
            {
                std::scoped_lock<mutex> lock{ _stageMutex };
                for ( const unique_ptr<StageNode>& uniqueStage : _listStageAll )
                {
                    if ( uniqueStage != nullptr && uniqueStage->_refCount.load( std::memory_order_relaxed ) > 0 )
                        listLive.push_back( uniqueStage.get() );
                }
            }
            for ( StageNode* pStage : listLive )
            {
                pStage->_refCount.store( 0, std::memory_order_relaxed );
                pStage->_remainingTasks.store( 0, std::memory_order_relaxed );
                deallocateStage( pStage );
            }
        }

        SharedTaskCallablePool& getSharedCallablePool() { return _sharedCallablePool; }

    private:
        ConcurrentQueue<TaskNode*, 4096> _freeQueue;
        vector<TaskNode*>                _listOverflowFree;
        vector<TaskNode*>                _listSlab;
        mutex                            _slabMutex;
        vector<StageNode*>               _listStageFree; ///< 돌아온 스테이지 — 다음 createAnonymousStage 가 먼저 집는다. 빌려 쓰는 것이라 소유하지 않는다
        /**
         * @brief 만든 스테이지 전부 — **소유한다.**
         * @details 처음에는 raw 포인터 목록이었고 풀 소멸자가 지우는 자리를 빠뜨려 스테이지가 프로세스
         *          끝까지 남았다(리눅스 CI 의 LeakSanitizer 가 잡았다 — 윈도우 테스트는 전부 초록이었다).
         *          소유를 타입으로 적으면 그 자리를 빠뜨릴 수 없다. 꺼내 쓰는 쪽(`_listStageFree`)은
         *          여전히 raw 라 할당 경로에 간접이 늘지 않는다.
         */
        vector<unique_ptr<StageNode>> _listStageAll;
        mutex                         _stageMutex;
        SharedTaskCallablePool        _sharedCallablePool;
    };

    void InlineSuccessorList::clearAndRelease()
    {
        lock();
        const uint32 inlineCount = _count < kInlineCapacity ? _count : kInlineCapacity;
        for ( uint32 index = 0; index < inlineCount; ++index )
        {
            if ( _arrInlineNode[index] != nullptr )
            {
                _arrInlineNode[index]->release();
                _arrInlineNode[index] = nullptr;
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
                _pOwner->deallocateNode( this );
        }
    }

    void StageNode::release()
    {
        if ( _refCount.fetch_sub( 1, std::memory_order_acq_rel ) != 1 )
            return;
        if ( _pPool != nullptr )
            _pPool->deallocateStage( this );
    }

    TaskStageHandle::TaskStageHandle( const TaskStageHandle& other )
        : _pNode{ other._pNode }
    {
        if ( _pNode != nullptr )
            _pNode->retain();
    }

    TaskStageHandle::TaskStageHandle( TaskStageHandle&& other ) noexcept
        : _pNode{ other._pNode }
    {
        other._pNode = nullptr;
    }

    TaskStageHandle& TaskStageHandle::operator=( const TaskStageHandle& other )
    {
        if ( this == &other )
            return *this;
        if ( other._pNode != nullptr )
            other._pNode->retain();
        if ( _pNode != nullptr )
            _pNode->release();
        _pNode = other._pNode;
        return *this;
    }

    TaskStageHandle& TaskStageHandle::operator=( TaskStageHandle&& other ) noexcept
    {
        if ( this == &other )
            return *this;
        if ( _pNode != nullptr )
            _pNode->release();
        _pNode       = other._pNode;
        other._pNode = nullptr;
        return *this;
    }

    TaskStageHandle::~TaskStageHandle()
    {
        if ( _pNode != nullptr )
            _pNode->release();
    }

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
            _pNode       = other._pNode;
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

    TaskHandle& TaskHandle::precede( const TaskHandle& targetTask )
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

    TaskHandle TaskHandle::then( const TaskDelegate& nextTaskDelegate, TaskThreadAffinity affinity )
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

    TaskStageHandle& TaskStageHandle::addTask( const TaskHandle& task )
    {
        TaskNode* pTaskNode = task.getNode();
        if ( _pNode != nullptr && pTaskNode != nullptr )
        {
            std::scoped_lock<mutex> lock{ _pNode->_mutex };
            pTaskNode->retain();
            _pNode->_listTask.push_back( pTaskNode );
            pTaskNode->_parentStage = _pNode;
            // 남은 태스크가 생기는 순간 스테이지가 스스로를 쥔다 — 핸들이 먼저 사라져도 완료 통지가 갈 곳이 남는다.
            if ( _pNode->_remainingTasks.fetch_add( 1, std::memory_order_relaxed ) == 0 )
                _pNode->retain();
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
        , _workEpoch{ 0 }
        , _wakeSignalCount{ 0 }
        , _globalHighQueue{}
        , _globalWorkerQueue{}
        , _globalLowQueue{}
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

    // 소멸자에서 예외가 나가면 종료된다. `shutdown()` 이 잠그는 뮤텍스는 시스템 오류일 때만
    // 던지는데, 그 상황은 소멸자에서 복구할 방법이 없다 — 종료가 맞는 동작이다.
    // NOLINTNEXTLINE(bugprone-exception-escape)
    TaskManager::~TaskManager()
    {
        shutdown();
    }

    TaskNode* TaskManager::allocateNode()
    {
        TaskNode* pNode = _nodePool->allocate();
        pNode->_pOwner  = this;
        return pNode;
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
            else if ( threadCount > kReservedThreadCount + 1 )
                threadCount -= kReservedThreadCount;
            else
                threadCount = 1;
        }

        _mainThreadId = std::this_thread::get_id();
        _bStop        = false;
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
        pNode->_pOwner  = this;
        pNode->setName( name );
        pNode->_affinity = affinity;
        pNode->_callable = delegate;
        pNode->_state    = TaskState::Pending;

        if ( t_pCurrentRunningTask != nullptr )
        {
            pNode->_pParent = t_pCurrentRunningTask;
            t_pCurrentRunningTask->_activeChildren.fetch_add( 1, std::memory_order_relaxed );
            t_pCurrentRunningTask->retain();
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
        pNode->_pOwner  = this;
        pNode->setName( name );
        pNode->_affinity = affinity;
        pNode->_callable = TaskArgsPayload{ delegate, args };
        pNode->_state    = TaskState::Pending;

        if ( t_pCurrentRunningTask != nullptr )
        {
            pNode->_pParent = t_pCurrentRunningTask;
            t_pCurrentRunningTask->_activeChildren.fetch_add( 1, std::memory_order_relaxed );
            t_pCurrentRunningTask->retain();
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

        SharedTaskCallable* pSharedCallable = SharedTaskCallable::create( &_nodePool->getSharedCallablePool(), TaskCallable{ delegate }, static_cast<int32>( numChunks ) );
        for ( uint32 start = 0; start < count; start += chunkSize )
        {
            uint32    end      = MathUtil::min( start + chunkSize, count );
            TaskNode* pSubTask = allocateNode();

            pSubTask->_pOwner          = this;
            pSubTask->_pSharedCallable = pSharedCallable;
            pSubTask->setName( name );
            pSubTask->_rangeStart = start;
            pSubTask->_rangeEnd   = end;
            pSubTask->_state      = TaskState::Pending;

            _activeTaskCount.fetch_add( 1, std::memory_order_relaxed );

            TaskHandle subHandle{ pSubTask };
            subHandle.precede( parentTask );
            submitWithoutWake( subHandle );
        }
        // 서브태스크를 다 넣은 뒤 **한 번만** 깨운다 — 청크마다 깨우면 그 시그널이 디스패치 비용의 전부였다.
        wakeSleepingWorkers( numChunks );

        return parentTask;
    }

    TaskHandle TaskManager::emplaceParallelBlock( uint32 start, uint32 end, const ParallelBlockDelegate& delegate, TaskThreadAffinity affinity )
    {
        if ( end <= start )
            return emplaceTask( "ParallelBlockTask", TaskDelegate{}, affinity );

        uint32 count       = end - start;
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

        SharedTaskCallable* pSharedCallable = SharedTaskCallable::create( &_nodePool->getSharedCallablePool(), TaskCallable{ delegate }, static_cast<int32>( numChunks ) );
        for ( uint32 offset = 0; offset < count; offset += chunkSize )
        {
            uint32    chunkStart = start + offset;
            uint32    chunkEnd   = MathUtil::min( chunkStart + chunkSize, end );
            TaskNode* pSubTask   = allocateNode();

            pSubTask->_pOwner          = this;
            pSubTask->_pSharedCallable = pSharedCallable;
            pSubTask->setName( "ParallelBlockTask" );
            pSubTask->_rangeStart = chunkStart;
            pSubTask->_rangeEnd   = chunkEnd;
            pSubTask->_state      = TaskState::Pending;

            _activeTaskCount.fetch_add( 1, std::memory_order_relaxed );

            TaskHandle subHandle{ pSubTask };
            subHandle.precede( parentTask );
            submitWithoutWake( subHandle );
        }
        // 서브태스크를 다 넣은 뒤 **한 번만** 깨운다 — 청크마다 깨우면 그 시그널이 디스패치 비용의 전부였다.
        wakeSleepingWorkers( numChunks );

        return parentTask;
    }

    void TaskManager::runParallel( uint32 count, uint32 serialThreshold, const ParallelBlockDelegate& body )
    {
        if ( count == 0 || body.isBound() == false )
            return;
        // 문턱 아래·워커 없음은 이 스레드가 한 번에 돈다 — 나누는 비용이 일보다 크다.
        if ( _bInitialized == false || getWorkerCount() == 0 || count < serialThreshold )
        {
            body( 0, count );
            return;
        }

        TaskStageHandle stage  = createAnonymousStage( "RunParallel" );
        TaskHandle      handle = emplaceParallelBlock( 0, count, body );
        if ( handle.isValid() == false )
        {
            body( 0, count );
            return;
        }
        stage.addTask( handle );
        submit( handle );
        waitStage( stage );
    }

    TaskStageHandle TaskManager::createAnonymousStage( string_view stageName )
    {
        // 풀에서 온다 — 프레임마다 웨이브 수만큼 만드는 자리라 힙을 만지면 그 수만큼 churn 이다.
        StageNode* pStage = _nodePool->allocateStage();
        pStage->_name     = stageName;
        return TaskStageHandle{ pStage };
    }

    TaskStageHandle TaskManager::getStage( string_view stageName )
    {
        std::scoped_lock<mutex> lock{ _stageMutex };
        for ( StageNode* pStage : _listAllStage )
        {
            if ( pStage != nullptr && string_view{ pStage->_name.c_str(), pStage->_name.size() } == stageName )
            {
                pStage->retain();
                return TaskStageHandle{ pStage };
            }
        }
        return TaskStageHandle{};
    }

    TaskStageHandle TaskManager::getOrCreateStage( string_view stageName )
    {
        std::scoped_lock<mutex> lock{ _stageMutex };
        for ( StageNode* pStage : _listAllStage )
        {
            if ( pStage != nullptr && string_view{ pStage->_name.c_str(), pStage->_name.size() } == stageName )
            {
                pStage->retain();
                return TaskStageHandle{ pStage };
            }
        }

        StageNode* pStage = _nodePool->allocateStage();
        pStage->_name     = stageName;
        pStage->retain(); // 목록이 하나 쥔다 — clear 가 놓는다
        _listAllStage.push_back( pStage );
        return TaskStageHandle{ pStage };
    }

    void TaskManager::waitStage( const TaskStageHandle& stage )
    {
        if ( stage._pNode == nullptr )
            return;

        uint32 spinCount = 0;
        while ( stage._pNode->_remainingTasks.load( std::memory_order_acquire ) > 0 )
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
            if ( stage._pNode->_remainingTasks.load( std::memory_order_acquire ) > 0 )
                _cvWaitAll.wait( lock );
        }

        {
            std::scoped_lock<mutex> doneLock{ stage._pNode->_mutex };
            for ( TaskNode* pTask : stage._pNode->_listTask )
            {
                if ( pTask == nullptr )
                    continue;
                pTask->release();
            }
            stage._pNode->_listTask.clear();
        }
    }

    bool TaskManager::isStageComplete( const TaskStageHandle& stage )
    {
        if ( stage._pNode == nullptr )
            return true;

        return stage._pNode->_remainingTasks.load( std::memory_order_relaxed ) == 0;
    }

    void TaskManager::submit( const TaskHandle& handle )
    {
        if ( handle.isValid() == false )
            return;

        TaskNode* pNode     = handle.getNode();
        int32     remaining = pNode->_unresolvedDependencies.fetch_sub( 1, std::memory_order_acq_rel );
        if ( remaining == 1 )
            scheduleReadyTask( pNode );
    }

    bool TaskManager::waitAll( uint32 timeoutMs )
    {
        const auto startTime = std::chrono::steady_clock::now();
        uint32     spinCount = 0;
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

                const uint32            remainingMs = static_cast<uint32>( static_cast<int64>( timeoutMs ) - elapsed );
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
            for ( StageNode* pStage : _listAllStage )
            {
                if ( pStage != nullptr )
                    pStage->release();
            }
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
            while ( _globalHighQueue.dequeue( pTemp ) )
            {
                if ( pTemp == nullptr )
                    continue;
                pTemp->release();
            }
            while ( _globalWorkerQueue.dequeue( pTemp ) )
            {
                if ( pTemp == nullptr )
                    continue;
                pTemp->release();
            }
            while ( _globalLowQueue.dequeue( pTemp ) )
            {
                if ( pTemp == nullptr )
                    continue;
                pTemp->release();
            }
        }
        _activeTaskCount.store( 0, std::memory_order_release );
        // 큐를 다 비웠으니 아무 태스크도 돌지 않는다 — 살아 있는 스테이지를 전부 되돌린다.
        _nodePool->resetAllStages();
        {
            std::scoped_lock<mutex> lock{ _waitAllMutex };
            _cvWaitAll.notify_all();
        }
    }

    TaskHandle TaskManager::whenAll( const vector<TaskHandle>& listTask, const TaskDelegate& continuation, TaskThreadAffinity affinity )
    {
        if ( listTask.empty() )
            return emplaceTask( "WhenAllContinuation", continuation, affinity );

        TaskHandle nextTask = emplaceTask( "WhenAllContinuation", continuation, affinity );

        for ( const TaskHandle& task : listTask )
        {
            if ( task.isValid() )
            {
                TaskHandle mutTask = task;
                mutTask.precede( nextTask );
            }
        }

        return nextTask;
    }

    TaskHandle TaskManager::whenAny( const vector<TaskHandle>& listTask, const TaskDelegate& continuation, TaskThreadAffinity affinity )
    {
        if ( listTask.empty() )
            return emplaceTask( "WhenAnyContinuation", continuation, affinity );

        shared_ptr<atomic<bool>> firedFlag = sw::make_shared<atomic<bool>>( false );
        TaskHandle               nextTask  = emplaceTask( "WhenAnyContinuation", continuation, affinity );

        // 1 for user submit(), 1 for trigger completion
        nextTask.getNode()->_unresolvedDependencies.store( 2, std::memory_order_relaxed );

        for ( const TaskHandle& task : listTask )
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
                TaskHandle mutTask     = task;
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
                del( _rangeStart, _rangeEnd );
        }
    };

    void TaskManager::executeTask( TaskNode* pNode )
    {
        pNode->_state = TaskState::Running;

        if ( pNode->_bCancelled.load( std::memory_order_acquire ) == false )
        {
            TaskNode* pPrevRunningTask = t_pCurrentRunningTask;
            t_pCurrentRunningTask      = pNode;

            BLOCK( "Execute Task Delegate" )
            {
                TaskCallable&        callable = pNode->_pSharedCallable != nullptr ? pNode->_pSharedCallable->_callable : pNode->_callable;
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

        // 레인 순서: High -> 내 덱 -> Normal 전역 -> 훔치기 -> Low. 빈 큐의 dequeue 는 시퀀스 한 줄 읽기라
        // High 를 매번 먼저 보는 비용은 없다시피 하다.
        if ( _globalHighQueue.dequeue( pNode ) && pNode != nullptr )
            return true;

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

        if ( _globalLowQueue.dequeue( pNode ) && pNode != nullptr )
            return true;

        pNode = nullptr;
        return false;
    }

    bool TaskManager::tryHelpAndExecute()
    {
        const int32 workerId = getCurrentWorkerIndex();
        if ( workerId >= 0 )
        {
            TaskNode* pNode{ nullptr };
            if ( _globalHighQueue.dequeue( pNode ) && pNode != nullptr )
            {
                executeTask( pNode );
                return true;
            }
            WorkerQueue& localQ = *std::as_const( _listWorkerQueue )[static_cast<uint32>( workerId )];
            if ( localQ._queue.pop( pNode ) && pNode != nullptr )
            {
                executeTask( pNode );
                return true;
            }
        }

        return tryStealAndExecute( invalid_index::kUint32 );
    }

    bool TaskManager::tryStealAndExecute( uint32 excludedWorkerId )
    {
        const uint32 numWorkers = static_cast<uint32>( _listWorkerQueue.size() );
        if ( numWorkers == 0 )
            return false;

        TaskNode* pNode{ nullptr };
        if ( _globalHighQueue.dequeue( pNode ) && pNode != nullptr )
        {
            executeTask( pNode );
            return true;
        }

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

        if ( _globalLowQueue.dequeue( pNode ) && pNode != nullptr )
        {
            executeTask( pNode );
            return true;
        }
        return false;
    }

    void TaskManager::workerLoop( uint32 workerId )
    {
        t_bTaskWorkerThread  = true;
        t_currentWorkerIndex = static_cast<int32>( workerId );

        while ( _bStop.load( std::memory_order_relaxed ) == false )
        {
            TaskNode* pNode{ nullptr };
            if ( tryTakeTask( workerId, pNode ) )
            {
                executeTask( pNode );
                continue;
            }

            // **세대를 먼저 읽고 큐를 본다.** 그래야 그 사이에 들어온 일감이 세대를 올려 스핀이 알아챈다.
            // (읽은 뒤에 들어온 것은 세대가 바뀌고, 읽기 전에 들어온 것은 바로 아래 tryTakeTask 가 본다.)
            bool       bFoundInSpin = false;
            uint32     epochSeen    = _workEpoch.load( std::memory_order_acquire );
            const auto spinStart    = std::chrono::steady_clock::now();
            for ( ;; )
            {
                if ( _workEpoch.load( std::memory_order_acquire ) != epochSeen )
                {
                    epochSeen = _workEpoch.load( std::memory_order_acquire );
                    if ( tryTakeTask( workerId, pNode ) )
                    {
                        bFoundInSpin = true;
                        break;
                    }
                }
                // 시계는 32 번에 한 번만 본다 — 매번 보면 그 자체가 스핀 비용이다.
                for ( uint32 spin = 0; spin < 32; ++spin )
                    sw::cpuPause();
                const int64 spentMicro = std::chrono::duration_cast<std::chrono::microseconds>( std::chrono::steady_clock::now() - spinStart ).count();
                if ( spentMicro >= kWorkerIdleSpinMicro )
                    break;
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

        t_bTaskWorkerThread   = false;
        t_currentWorkerIndex  = -1;
        t_bInsideParallelTask = false;
    }

    void TaskManager::scheduleReadyTask( TaskNode* pNode )
    {
        scheduleReadyTask( pNode, true );
    }

    void TaskManager::submitWithoutWake( const TaskHandle& handle )
    {
        if ( handle.isValid() == false )
            return;

        TaskNode* pNode     = handle.getNode();
        int32     remaining = pNode->_unresolvedDependencies.fetch_sub( 1, std::memory_order_acq_rel );
        if ( remaining == 1 )
            scheduleReadyTask( pNode, false );
    }

    void TaskManager::wakeSleepingWorkers()
    {
        wakeSleepingWorkers( 0xFFFFFFFFu );
    }

    void TaskManager::wakeSleepingWorkers( uint32 wantedCount )
    {
        // 스핀 중인 워커는 세대로 이미 알았다. 잠든 워커만 시그널이 필요하다.
        _workEpoch.fetch_add( 1, std::memory_order_release );
        const int32 sleeping = _sleepingWorkerCount.load( std::memory_order_acquire );
        if ( sleeping <= 0 )
            return;
        _wakeSignalCount.fetch_add( 1, std::memory_order_relaxed );
        // **필요한 수만 깨운다.** `notify_all` 은 워커 열넷이 한꺼번에 깨어 일감 여섯을 다투는 천둥 무리다 —
        // 렌더 그래프 병렬 기록이 150~192 us 에서 329~380 us 로 두 배 느려졌다(재 봤다).
        std::scoped_lock<mutex> workerLock{ _workerMutex };
        if ( wantedCount >= static_cast<uint32>( sleeping ) )
        {
            _cvWorker.notify_all();
            return;
        }
        for ( uint32 index = 0; index < wantedCount; ++index )
            _cvWorker.notify_one();
    }

    void TaskManager::scheduleReadyTask( TaskNode* pNode, bool bWakeWorker )
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
                    if ( isMainThread() )
                        dispatchMainThreadTasks();
                    else
                        std::this_thread::yield();
                }
                // **`notify_all` 이어야 한다.** 이 일감을 실행할 수 있는 것은 메인 스레드뿐인데
                // (`dispatchMainThreadTasks`), `_cvWaitAll` 에는 `waitAll` · `waitStage` 로 들어온
                // 아무 스레드나 잠들어 있다. `notify_one` 이 엉뚱한 스레드를 깨우면 그쪽은 자기
                // 조건이 그대로임을 보고 다시 잠들고, **메인 스레드는 계속 잔다.**
                //
                // 그러면 회복할 길이 없다: 완료 쪽 통지는 `_activeTaskCount` 가 0 이 될 때만
                // 울리는데(아래 `activeLeft == 1`), 지금 넣은 메인 일감이 남아 있으므로 0 이 되지
                // 않는다. 서로를 기다리며 둘 다 멈춘다. 이 파일의 다른 다섯 통지는 전부
                // `notify_all` 이고, 여기만 달랐다.
                std::scoped_lock<mutex> waitLock{ _waitAllMutex };
                _cvWaitAll.notify_all();
            }
            else
            {
                const uint32 numWorkers = static_cast<uint32>( _listWorkerQueue.size() );
                if ( numWorkers > 0 )
                {
                    // 레인은 우선순위가 정한다. High/Low 는 워커에서 넣어도 자기 덱이 아니라 전역 레인이다 —
                    // 덱은 LIFO 라 "먼저" 도 "나중" 도 약속하지 못한다.
                    const int32 workerId = getCurrentWorkerIndex();
                    if ( pNode->_priority == TaskPriority::High )
                    {
                        while ( _globalHighQueue.enqueue( pNode ) == false )
                            std::this_thread::yield();
                    }
                    else if ( pNode->_priority == TaskPriority::Low )
                    {
                        while ( _globalLowQueue.enqueue( pNode ) == false )
                            std::this_thread::yield();
                    }
                    else if ( workerId >= 0 )
                    {
                        WorkerQueue& localQ = *std::as_const( _listWorkerQueue )[static_cast<uint32>( workerId )];
                        while ( localQ._queue.push( pNode ) == false )
                        {
                            if ( _globalWorkerQueue.enqueue( pNode ) )
                                break;
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

                    // 스핀 중인 워커에게 알린다 — 큐를 만진 뒤(release) 세대를 올려야 그쪽이 집을 수 있다.
                    _workEpoch.fetch_add( 1, std::memory_order_release );
                    if ( bWakeWorker && _sleepingWorkerCount.load( std::memory_order_relaxed ) > 0 )
                    {
                        _wakeSignalCount.fetch_add( 1, std::memory_order_relaxed );
                        std::scoped_lock<mutex> workerLock{ _workerMutex };
                        _cvWorker.notify_one();
                    }
                }
            }
        }
    }

    void TaskManager::onTaskFinished( TaskNode* pNode )
    {
        pNode->_state.store( TaskState::WaitingForChildren, std::memory_order_seq_cst );
        if ( pNode->_activeChildren.load( std::memory_order_seq_cst ) > 0 )
            return;

        pNode->_state.store( TaskState::Completed, std::memory_order_seq_cst );

        // **활성 수는 스테이지·부모보다 먼저 내린다.** 예전에는 맨 끝(후속 트리거 뒤)에서 내렸는데, 그러면
        // `waitStage` 가 스테이지 완료 통지를 받고 돌아온 순간에도 이 태스크는 아직 활성으로 세어져 있다.
        // 그 직후 `clear()` 가 수를 0 으로 놓으면 뒤늦은 fetch_sub 가 0xFFFFFFFF 로 감아 버려 이후의
        // `waitAll` 이 영원히 기다린다 — CTest 아래에서만 재현되던 EngineTest_NoGPU 180 초 타임아웃이 이것이다.
        // 후속 태스크는 만들 때 이미 세어져 있으므로 여기서 내려도 `waitAll` 이 일찍 돌아오지 않는다.
        {
            const uint32 activeLeft = _activeTaskCount.fetch_sub( 1, std::memory_order_acq_rel );
            if ( activeLeft == 1 )
            {
                std::scoped_lock<mutex> lock{ _waitAllMutex };
                _cvWaitAll.notify_all();
            }
        }

        BLOCK( "Update Parent Task" )
        {
            TaskNode* pParent = pNode->_pParent;
            if ( pParent != nullptr )
            {
                int32 remaining = pParent->_activeChildren.fetch_sub( 1, std::memory_order_seq_cst ) - 1;
                if ( remaining == 0 && pParent->_state.load( std::memory_order_seq_cst ) == TaskState::WaitingForChildren )
                    onTaskFinished( pParent );
                pParent->release();
            }
        }

        BLOCK( "Update Stage" )
        {
            StageNode* pStage   = pNode->_parentStage;
            pNode->_parentStage = nullptr;
            if ( pStage != nullptr )
            {
                uint32 prev = pStage->_remainingTasks.fetch_sub( 1, std::memory_order_release );
                if ( prev == 1 )
                {
                    {
                        std::scoped_lock<mutex> lock{ pStage->_mutex };
                        pStage->_cv.notify_all();
                    }
                    {
                        std::scoped_lock<mutex> waitLock{ _waitAllMutex };
                        _cvWaitAll.notify_all();
                    }
                    // 마지막 태스크가 끝났다 — addTask 에서 잡은 자기 참조를 놓는다(핸들이 없으면 여기서 풀로).
                    pStage->release();
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
            pNode->_pParent  = nullptr;
        }
    }

} // namespace sw
