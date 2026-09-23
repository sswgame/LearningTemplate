#include "pch.h"

#include "Core/Task/TaskManager.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/Futex.h"
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
        thread_local int32     t_helperScratchSlot   = -1;      ///< 워커가 아닌 스레드가 받은 도우미 스크래치 번호 (0..). 아직이면 -1
        thread_local TaskNode* t_pCurrentRunningTask = nullptr; ///< 현재 스레드에서 실행 중인 태스크 노드 포인터

        /// @brief 대기 함수(waitStage/waitAll/runParallel)가 잠들기 전에 도는 `cpuPause` 횟수(약 2 us). 대기 중엔 남의 일을 돕는다.
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
        /**
         * @brief 병렬 그룹을 참여 스레드(워커 + 호출 스레드) 하나당 몇 청크로 나누는가.
         * @details 청크는 노드가 아니라 원자 카운터 한 번이라 잘게 나눠도 비용이 없다시피 하다. 잘게 나누면
         *          먼저 끝난 스레드가 남은 청크를 이어 집어 꼬리가 짧아진다(청크 하나가 곧 꼬리의 상한이다).
         *          너무 잘게 나누면 청크마다 캐시 라인 하나(`_nextChunkStart`)가 코어 사이를 오간다 — 넷이 균형이다.
         */
        constexpr uint32 kChunksPerThread = 4;
        /** @brief 같은 스테이지를 둘째로 기다리는 스레드가 방송에 잠들 때의 폴링 간격(밀리초). 드문 경로다. */
        constexpr uint32 kSecondWaiterPollMilli = 1;
        /** @brief 큐 항목의 태그 비트 — 켜져 있으면 `ParallelGroup*` 티켓, 아니면 `TaskNode*`. 둘 다 8 정렬이라 하위 비트가 빈다. */
        constexpr uintptr_t kTicketTagBit = 1;
#if !defined( SW_SHIPPING )
        constexpr uint32 kTaskNameCapacity = 31;
#endif

        /**
         * @brief 병렬 태스크 진입/퇴출 시 스레드 로컬 플래그를 관리하는 RAII 스코프 구조체. 중첩되면 바깥 값을 되돌린다.
         */
        struct ParallelTaskScope
        {
            ParallelTaskScope()
                : _bPrevious{ t_bInsideParallelTask }
            {
                t_bInsideParallelTask = true;
            }
            ~ParallelTaskScope()
            {
                t_bInsideParallelTask = _bPrevious;
            }
            ParallelTaskScope( const ParallelTaskScope& )            = delete;
            ParallelTaskScope& operator=( const ParallelTaskScope& ) = delete;

            bool _bPrevious;
        };

        /** @brief 큐 항목 하나 — 태스크 노드 포인터이거나 태그 비트가 켜진 병렬 그룹 포인터. */
        struct TaskQueueItem
        {
            static uintptr_t      fromNode( TaskNode* pNode ) { return reinterpret_cast<uintptr_t>( pNode ); }
            static uintptr_t      fromTicket( ParallelGroup* pGroup ) { return reinterpret_cast<uintptr_t>( pGroup ) | kTicketTagBit; }
            static bool           isTicket( uintptr_t item ) { return ( item & kTicketTagBit ) != 0; }
            static TaskNode*      toNode( uintptr_t item ) { return reinterpret_cast<TaskNode*>( item ); }
            static ParallelGroup* toGroup( uintptr_t item ) { return reinterpret_cast<ParallelGroup*>( item & ~kTicketTagBit ); }
        };

        /** @brief 0 이 아닌 값의 가장 낮은 켜진 비트 번호. */
        uint32 countTrailingZeros64( uint64 value )
        {
#if defined( __clang__ ) || defined( __GNUC__ )
            return static_cast<uint32>( __builtin_ctzll( value ) );
#else
            // 이 저장소의 컴파일러는 전부 clang 이다 — 다른 컴파일러를 위한 자리라 느려도 된다.
            uint32 index = 0;
            while ( ( ( value >> index ) & 1u ) == 0 )
                ++index;
            return index;
#endif
        }

        /** @brief 병렬 그룹을 어떻게 나눌지 — 청크 크기와 큐에 넣을 티켓 수. */
        struct ParallelSplit
        {
            uint32 _chunkSize{ 1 };
            uint32 _ticketCount{ 0 };

            /**
             * @brief @p count 개를 워커 @p workerCount 명 (+ 호출 스레드) 이 나눠 갖도록 자릅니다.
             * @param bCallerRuns 호출 스레드가 곧바로 같이 도는가 (`runParallel`). 그러면 티켓은 나머지 참여자 몫만 넣는다.
             */
            static ParallelSplit compute( uint32 count, uint32 workerCount, bool bCallerRuns )
            {
                ParallelSplit split{};
                if ( count == 0 )
                    return split;
                const uint32 participantCount = workerCount + 1;
                uint32       chunkCount       = MathUtil::min( count, participantCount * kChunksPerThread );
                split._chunkSize              = ( count + chunkCount - 1 ) / chunkCount;
                if ( split._chunkSize == 0 )
                    split._chunkSize = 1;
                chunkCount = ( count + split._chunkSize - 1 ) / split._chunkSize;
                // 티켓 하나는 스레드 하나의 몫이다 — 청크가 티켓보다 적으면 남는 티켓은 집자마자 끝난다(빈 깨움).
                const uint32 wantedTicketCount = bCallerRuns ? ( chunkCount > 0 ? chunkCount - 1 : 0 ) : chunkCount;
                split._ticketCount             = MathUtil::min( wantedTicketCount, workerCount );
                return split;
            }
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
     * @brief 타입 소거(Type Erasure)된 실행 가능한 태스크 호출자 variant. 병렬 본문은 여기 없다 — 그룹이 든다.
     */
    using TaskCallable = std::variant<
        std::monostate,
        TaskDelegate,
        TaskArgsPayload>;

    /**
     * @struct JoinCounter
     * @brief 남은 수와 대기자 하나를 **한 워드**에 담은 합류 카운터.
     * @details 하위 32 비트가 남은 수, 상위 32 비트가 대기자 슬롯 + 1(0 이면 없음). 마지막으로 끝내는 쪽의 CAS 하나가
     *          "0 이 됐다" 와 "누구를 깨울까" 를 같이 답하고 대기자 칸을 비운다 — 그 뒤로는 이 워드를 **다시 만지지
     *          않으므로** 스택에 놓인 그룹(`runParallel`)이 안전하다: 대기자가 0 을 본 순간 스택이 사라져도 된다.
     *          대기자 등록도 같은 워드의 CAS 라 "등록했는데 이미 끝났다" 가 새지 않는다.
     */
    struct JoinCounter
    {
        static constexpr uint64 kCountMask   = 0xFFFFFFFFull;
        static constexpr uint32 kWaiterShift = 32;

        enum class RegisterResult : uint8
        {
            Registered,  ///< 이 스레드가 대기자로 올랐다(또는 이미 올라 있었다)
            AlreadyDone, ///< 남은 것이 없다 — 잠들 필요 없다
            Busy         ///< 다른 스레드가 대기자 칸을 쥐고 있다 — 방송으로 간다
        };

        atomic<uint64> _packed{ 0 };

        void   reset() { _packed.store( 0, std::memory_order_relaxed ); }
        uint32 getPending() const { return static_cast<uint32>( _packed.load( std::memory_order_acquire ) & kCountMask ); }

        /** @brief 남은 수를 @p count 만큼 올리고 이전 남은 수를 돌려줍니다. */
        uint32 addPending( uint32 count )
        {
            return static_cast<uint32>( _packed.fetch_add( count, std::memory_order_acq_rel ) & kCountMask );
        }

        /**
         * @brief 하나를 끝냅니다. 마지막이면 대기자 칸까지 비우고, **이전** 워드를 돌려줍니다.
         * @details 돌려준 값의 남은 수가 1 이면 이 호출이 마지막이고, 상위 32 비트가 깨울 대기자(+1)다.
         */
        uint64 finishOne()
        {
            uint64 current = _packed.load( std::memory_order_relaxed );
            for ( ;; )
            {
                const uint64 pending = current & kCountMask;
                SW_ASSERT( pending > 0 );
                const uint64 next = ( pending == 1 ) ? 0 : ( current - 1 );
                if ( _packed.compare_exchange_weak( current, next, std::memory_order_acq_rel, std::memory_order_relaxed ) )
                    return current;
            }
        }

        /** @brief 이 스레드(슬롯 + 1)를 대기자로 올립니다. 남은 것이 없거나 다른 대기자가 있으면 그렇다고 답한다. */
        RegisterResult tryRegisterWaiter( uint32 slotPlusOne )
        {
            uint64 current = _packed.load( std::memory_order_acquire );
            for ( ;; )
            {
                if ( ( current & kCountMask ) == 0 )
                    return RegisterResult::AlreadyDone;
                const uint32 waiter = static_cast<uint32>( current >> kWaiterShift );
                if ( waiter == slotPlusOne )
                    return RegisterResult::Registered;
                if ( waiter != 0 )
                    return RegisterResult::Busy;
                const uint64 next = current | ( static_cast<uint64>( slotPlusOne ) << kWaiterShift );
                if ( _packed.compare_exchange_weak( current, next, std::memory_order_acq_rel, std::memory_order_acquire ) )
                    return RegisterResult::Registered;
            }
        }
    };

    struct ParallelGroup;
    struct TaskNode;

    class TaskNodePool;
    /** @brief 동시에 살아 있는 병렬 그룹 수의 상한 — 청크 수가 아니다. 넘치면 힙으로 물러난다. */
    constexpr uint32 kParallelGroupPoolCapacity = 512;
    using ParallelGroupPool                     = LockFreeObjectPool<ParallelGroup, kParallelGroupPoolCapacity>;

    /**
     * @struct ParallelGroup
     * @brief 병렬 for 하나 — 본문 · 범위 · 다음 청크 카운터 · 티켓 합류.
     * @details 예전에는 청크마다 태스크 노드를 만들어 큐에 넣었다(워커 수 × 2 개): 청크마다 노드 할당 · 상태 CAS ·
     *          큐 넣기/빼기 · 활성 수 · 후속 목록 잠금 · 노드 반납이 전부 공유 캐시 라인의 원자 연산이었고, 청크 수가
     *          곧 그 수였다. 지금은 큐에 **티켓** 몇 장(워커 수 이하)만 넣고, 티켓을 집은 스레드는 `_nextChunkStart`
     *          를 올려 가며 남은 청크를 전부 돈다 — 청크 하나의 비용이 원자 덧셈 하나다. 끝난 스레드가 남은 청크를
     *          이어 받으므로 균형은 저절로 맞는다(Unreal ParallelFor · TBB 의 모양).
     *
     *          `runParallel` 은 이것을 **호출 스레드의 스택에** 둔다. 마지막 티켓이 `_join` 을 0 으로 만든 뒤에는
     *          아무도 이 메모리를 만지지 않는다 — 그 약속이 `JoinCounter::finishOne` 의 CAS 하나에 있다.
     *          `emplaceParallel*` 은 풀에서 꺼내고 부모 노드를 쥐며, 마지막 티켓이 부모의 의존성을 풀고 반납한다.
     */
    struct ParallelGroup
    {
        ParallelBlockDelegate        _blockBody;             ///< `emplaceParallelBlock` — 사본
        ParallelTaskDelegate         _indexBody;             ///< `emplaceParallel` — 사본
        const ParallelBlockDelegate* _pBlockBody{ nullptr }; ///< `runParallel` — 호출자의 델리게이트 (호출이 끝날 때까지 산다)
        uint32                       _rangeStart{ 0 };
        uint32                       _rangeEnd{ 0 };
        uint32                       _chunkSize{ 1 };
        TaskNode*                    _pParent{ nullptr }; ///< 풀 그룹: 티켓이 다 끝나면 의존성을 풀 부모 (참조를 하나 쥔다)
        ParallelGroupPool*           _pPool{ nullptr };   ///< 돌아갈 풀. 스택이거나 힙이면 null
        bool                         _bHeap{ false };     ///< 풀이 비어 힙에서 왔다
        /** @brief 다음에 집을 청크의 시작. 64 비트인 이유: 티켓마다 끝을 지나 한 번 더 더하므로 32 비트는 감길 수 있다. */
        alignas( 64 ) atomic<uint64> _nextChunkStart{ 0 };
        alignas( 64 ) JoinCounter _join; ///< 아직 끝나지 않은 티켓 수 + 기다리는 스레드
    };

    /**
     * @brief 스테이지 — 태스크 묶음의 완료를 기다리는 단위. 매니저의 풀에서 오고 침입형 참조 계수로 산다.
     * @details 참조는 둘이 쥔다: 핸들 사본과, **남은 태스크가 있는 동안의 스테이지 자신**(0→1 에서 잡고 1→0 에서
     *          놓는다). 태스크 노드는 참조를 쥐지 않는다 — 노드가 스테이지를 쥐고 스테이지가 노드 목록을 쥐면
     *          고리가 되어 어느 쪽도 못 돌아간다. 노드의 `_parentStage` 는 남은 태스크가 있는 동안만 유효하고,
     *          그 동안은 스테이지가 스스로를 쥐고 있으므로 안전하다.
     *          대기는 `_join` 에 적힌 대기자 하나를 마지막 태스크가 직접 깨운다 — 뮤텍스도 조건 변수도 없다.
     */
    struct StageNode
    {
        fixed_string<constant::kMaxBuffer64> _name; ///< 이름 있는 스테이지의 조회 키. 힙을 만지지 않는다.
        vector<TaskNode*>                    _listTask;
        mutex                                _listMutex; ///< `_listTask` 만 지킨다
        JoinCounter                          _join;
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

        /** @brief 비어 있으면 잠금 없이 답합니다 — 완료 경로의 흔한 경우(후속 없음)가 스핀락을 안 잡게. */
        bool isEmptyRelaxed() const
        {
            return _count == 0;
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
        StageNode*          _parentStage{ nullptr }; ///< 남은 태스크가 있는 동안만 유효 — 스테이지가 그 동안 스스로를 쥔다

        atomic<int32> _unresolvedDependencies{ 1 }; // 1 = Builder Dependency

        TaskThreadAffinity _affinity = TaskThreadAffinity::Any;
        TaskPriority       _priority{ TaskPriority::Normal };
        atomic<TaskState>  _state{ TaskState::Pending };
        atomic<bool>       _bCancelled{ false };

        TaskManager* _pOwner{ nullptr };
        TaskNode*    _pParent{ nullptr };
        /**
         * @brief 본문 하나 + 아직 안 끝난 자식 수. 0 으로 내리는 쪽(본문이든 마지막 자식이든)이 완료를 처리한다.
         * @details 예전엔 "자식 수" 와 "상태 = 자식 대기" 를 따로 두고 seq_cst 로 데커 순서를 맞췄는데, 본문이 상태를
         *          적고 자식 수를 읽는 사이에 마지막 자식이 수를 내리고 상태를 읽으면 **둘 다** 완료를 처리했다.
         *          본문 자신을 1 로 세어 두면 카운터 하나로 끝나고 seq_cst 도 필요 없다.
         */
        atomic<int32> _pendingChildren{ 1 };
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
            pMem->_pendingChildren.store( 1, std::memory_order_relaxed );
            pMem->_state.store( TaskState::Pending, std::memory_order_relaxed );
            pMem->_bCancelled.store( false, std::memory_order_relaxed );
            pMem->_refCount.store( 1, std::memory_order_relaxed );
            pMem->_pOwner  = nullptr;
            pMem->_pParent = nullptr;
#if !defined( SW_SHIPPING )
            pMem->_arrName[0] = 0;
#endif
            pMem->_affinity = TaskThreadAffinity::Any;
            pMem->_priority = TaskPriority::Normal;
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
            pStage->_join.reset();
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
                pStage->_join.reset();
                deallocateStage( pStage );
            }
        }

        /** @brief 병렬 그룹을 꺼냅니다. 풀이 비면 힙에서 — 그 경우 `_bHeap` 이 켜진다. */
        ParallelGroup* allocateGroup()
        {
            ParallelGroup* pGroup = _groupPool.acquire();
            if ( pGroup != nullptr )
            {
                pGroup->_pPool = &_groupPool;
                return pGroup;
            }
            pGroup         = sw_new ParallelGroup();
            pGroup->_bHeap = true;
            return pGroup;
        }

        /** @brief 병렬 그룹을 되돌립니다 — 풀로, 힙에서 왔으면 힙으로. */
        void deallocateGroup( ParallelGroup* pGroup )
        {
            if ( pGroup == nullptr )
                return;
            if ( pGroup->_bHeap )
            {
                sw_delete( pGroup );
                return;
            }
            ParallelGroupPool* pPool = pGroup->_pPool;
            if ( pPool != nullptr )
                pPool->release( pGroup );
        }

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
        ParallelGroupPool             _groupPool;
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
            std::scoped_lock<mutex> lock{ _pNode->_listMutex };
            pTaskNode->retain();
            _pNode->_listTask.push_back( pTaskNode );
            pTaskNode->_parentStage = _pNode;
            // 남은 태스크가 생기는 순간 스테이지가 스스로를 쥔다 — 핸들이 먼저 사라져도 완료 통지가 갈 곳이 남는다.
            if ( _pNode->_join.addPending( 1 ) == 0 )
                _pNode->retain();
        }
        return *this;
    }

    TaskManager::TaskManager()
        : _bInitialized{ false }
        , _mainThreadId{}
        , _bStop{ false }
        , _listWorker{}
        , _listWorkerSlot{}
        , _helperSlotCount{ 0 }
        , _idleWorkerMask{ 0 }
        , _workEpoch{ 0 }
        , _wakeSignalCount{ 0 }
        , _completionEpoch{ 0 }
        , _broadcastWaiterCount{ 0 }
        , _mainThreadParkedSlot{ -1 }
        , _globalHighQueue{}
        , _globalWorkerQueue{}
        , _globalLowQueue{}
        , _queueMainThread{}
        , _arrHelperWaiter{}
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
        if ( threadCount > kMaxWorkerCount )
            threadCount = kMaxWorkerCount;

        _mainThreadId = std::this_thread::get_id();
        _bStop        = false;
        _idleWorkerMask.store( 0, std::memory_order_relaxed );
        _mainThreadParkedSlot.store( -1, std::memory_order_relaxed );
        _listWorker.reserve( threadCount );
        _listWorkerSlot.reserve( threadCount );
        for ( uint32 workerIndex = 0; workerIndex < threadCount; ++workerIndex )
        {
            _listWorkerSlot.push_back( make_unique<WorkerSlot>() );
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
            _bStop.store( true, std::memory_order_seq_cst );
            // 잠든 워커는 자기 워드에서, 스핀 중인 워커는 세대로 알아챈다 — 전부 깨운다.
            wakeSleepingWorkers();

            for ( std::thread& worker : _listWorker )
            {
                if ( worker.joinable() )
                    worker.join();
            }
            _listWorker.clear();
            _listWorkerSlot.clear();
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

    uint32 TaskManager::getCurrentThreadScratchSlot()
    {
        if ( t_currentWorkerIndex >= 0 )
            return static_cast<uint32>( t_currentWorkerIndex );
        if ( t_helperScratchSlot < 0 )
        {
            const uint32 assigned = _helperSlotCount.fetch_add( 1, std::memory_order_relaxed );
            if ( assigned >= kMaxHelperThreadCount )
            {
                // 넘치면 마지막 칸을 나눠 쓴다 — 스크래치가 겹칠 수 있으니 알린다. 엔진에서는 닿지 않는 수다.
                // (대기 워드는 나눠 써도 안전하다 — 허위로 깨어날 뿐이다.)
                SW_LOG_ERROR( "More than %# non-worker threads execute tasks — scratch slots collide (raise kMaxHelperThreadCount)", kMaxHelperThreadCount );
                t_helperScratchSlot = static_cast<int32>( kMaxHelperThreadCount - 1 );
            }
            else
                t_helperScratchSlot = static_cast<int32>( assigned );
        }
        return getWorkerCount() + static_cast<uint32>( t_helperScratchSlot );
    }

    uint32 TaskManager::getCurrentWaiterSlotIndex()
    {
        return getCurrentThreadScratchSlot();
    }

    atomic<uint32>& TaskManager::getWaiterWord( uint32 slotIndex )
    {
        const uint32 workerCount = getWorkerCount();
        if ( slotIndex < workerCount )
            return std::as_const( _listWorkerSlot )[slotIndex]->_park._word;
        uint32 helperIndex = slotIndex - workerCount;
        if ( helperIndex >= kMaxHelperThreadCount )
            helperIndex = kMaxHelperThreadCount - 1;
        return _arrHelperWaiter[helperIndex]._word;
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
        pNode->_state.store( TaskState::Pending, std::memory_order_relaxed );

        if ( t_pCurrentRunningTask != nullptr )
        {
            pNode->_pParent = t_pCurrentRunningTask;
            t_pCurrentRunningTask->_pendingChildren.fetch_add( 1, std::memory_order_relaxed );
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
        pNode->_state.store( TaskState::Pending, std::memory_order_relaxed );

        if ( t_pCurrentRunningTask != nullptr )
        {
            pNode->_pParent = t_pCurrentRunningTask;
            t_pCurrentRunningTask->_pendingChildren.fetch_add( 1, std::memory_order_relaxed );
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
        return emplaceParallelGroup( name, 0, count, nullptr, &delegate, affinity );
    }

    TaskHandle TaskManager::emplaceParallelBlock( uint32 start, uint32 end, const ParallelBlockDelegate& delegate, TaskThreadAffinity affinity )
    {
        return emplaceParallelGroup( "ParallelBlockTask", start, end, &delegate, nullptr, affinity );
    }

    TaskHandle TaskManager::emplaceParallelGroup( string_view name, uint32 start, uint32 end, const ParallelBlockDelegate* pBlockBody, const ParallelTaskDelegate* pIndexBody, TaskThreadAffinity affinity )
    {
        if ( end <= start )
            return emplaceTask( name, TaskDelegate{}, affinity );

        // 부모 노드가 사용자에게 보이는 태스크다 — 핸들 · 스테이지 · 후속 · 활성 수 전부 이 하나에 걸린다.
        TaskHandle parentTask = emplaceTask( name, TaskDelegate{}, affinity );
        if ( parentTask.isValid() == false )
            return parentTask;

        const uint32        count = end - start;
        const ParallelSplit split = ParallelSplit::compute( count, MathUtil::max( getWorkerCount(), 1u ), false );

        ParallelGroup* pGroup = _nodePool->allocateGroup();
        if ( pBlockBody != nullptr )
            pGroup->_blockBody = *pBlockBody;
        else
            pGroup->_blockBody = ParallelBlockDelegate{};
        if ( pIndexBody != nullptr )
            pGroup->_indexBody = *pIndexBody;
        else
            pGroup->_indexBody = ParallelTaskDelegate{};
        pGroup->_pBlockBody = nullptr;
        pGroup->_rangeStart = start;
        pGroup->_rangeEnd   = end;
        pGroup->_chunkSize  = split._chunkSize;
        pGroup->_nextChunkStart.store( start, std::memory_order_relaxed );
        pGroup->_join.reset();

        // 그룹이 부모를 쥐고, 부모는 그룹이 끝나야 준비된다(빌더 의존성 1 + 그룹 의존성 1).
        TaskNode* pParent = parentTask.getNode();
        pParent->retain();
        pParent->_unresolvedDependencies.fetch_add( 1, std::memory_order_relaxed );
        pGroup->_pParent = pParent;

        const uint32 ticketCount = MathUtil::max( split._ticketCount, 1u );
        pGroup->_join.addPending( ticketCount );
        pushGroupTickets( pGroup, ticketCount );

        return parentTask;
    }

    void TaskManager::runParallel( uint32 count, uint32 serialThreshold, const ParallelBlockDelegate& body )
    {
        if ( count == 0 || body.isBound() == false )
            return;
        // 문턱 아래·워커 없음은 이 스레드가 한 번에 돈다 — 나누는 비용이 일보다 크다.
        const uint32 workerCount = getWorkerCount();
        if ( _bInitialized == false || workerCount == 0 || count < serialThreshold )
        {
            body( 0, count );
            return;
        }

        const ParallelSplit split = ParallelSplit::compute( count, workerCount, true );
        if ( split._ticketCount == 0 )
        {
            body( 0, count );
            return;
        }

        // 그룹은 이 스택에 산다. 마지막 티켓이 합류를 0 으로 만든 뒤에는 아무도 이 메모리를 만지지 않는다.
        ParallelGroup group{};
        group._pBlockBody = &body;
        group._rangeStart = 0;
        group._rangeEnd   = count;
        group._chunkSize  = split._chunkSize;
        group._nextChunkStart.store( 0, std::memory_order_relaxed );
        group._join.addPending( split._ticketCount );

        // `waitAll` 에는 그룹 하나가 태스크 하나다 — 티켓마다 활성 수를 오르내리지 않는다.
        _activeTaskCount.fetch_add( 1, std::memory_order_relaxed );
        pushGroupTickets( &group, split._ticketCount );

        // 워커가 깨기를 기다리지 않는다 — 이 스레드가 첫 청크부터 집는다.
        runGroupChunks( &group );
        waitForGroup( group );

        if ( _activeTaskCount.fetch_sub( 1, std::memory_order_acq_rel ) == 1 )
            notifyBroadcast();
    }

    void TaskManager::pushGroupTickets( ParallelGroup* pGroup, uint32 ticketCount )
    {
        const uintptr_t item     = TaskQueueItem::fromTicket( pGroup );
        const int32     workerId = getCurrentWorkerIndex();
        for ( uint32 index = 0; index < ticketCount; ++index )
        {
            if ( workerId >= 0 )
            {
                WorkerSlot& localSlot = *std::as_const( _listWorkerSlot )[static_cast<uint32>( workerId )];
                while ( localSlot._queue.push( item ) == false )
                {
                    if ( _globalWorkerQueue.enqueue( item ) )
                        break;
                    std::this_thread::yield();
                }
            }
            else
            {
                while ( _globalWorkerQueue.enqueue( item ) == false )
                    std::this_thread::yield();
            }
        }
        // 티켓을 다 넣은 뒤 **한 번** — 세대를 올리고 티켓 수만큼만 깨운다.
        wakeSleepingWorkers( ticketCount );
    }

    void TaskManager::runHighLaneBetweenChunks()
    {
        // 빈 큐의 size 는 위치 두 개 읽기다 — 청크마다 봐도 비용이 없다시피 하다.
        if ( _globalHighQueue.empty() )
            return;
        const bool bWasInsideParallel = t_bInsideParallelTask;
        t_bInsideParallelTask         = false;
        uintptr_t item{ 0 };
        while ( _globalHighQueue.dequeue( item ) )
        {
            executeItem( item );
        }
        t_bInsideParallelTask = bWasInsideParallel;
    }

    void TaskManager::runGroupChunks( ParallelGroup* pGroup )
    {
        const ParallelTaskScope parallelScope{};
        const uint32            end       = pGroup->_rangeEnd;
        const uint32            chunkSize = pGroup->_chunkSize;
        for ( ;; )
        {
            // **High 레인은 청크 사이에 본다.** 티켓을 쥔 워커가 그룹을 다 돌 때까지 렌더 스레드의 패스 기록을
            // 세워 두면 그 줄이 그대로 프레임 지연이다 — 예전에는 청크가 노드라 노드 사이에 저절로 봤다.
            // 지금 청크 하나(참여자당 넷)는 그때의 노드 하나보다 작으므로 기다림의 상한도 더 짧다.
            runHighLaneBetweenChunks();
            const uint64 claimed = pGroup->_nextChunkStart.fetch_add( chunkSize, std::memory_order_relaxed );
            if ( claimed >= end )
                break;
            const uint32 chunkStart = static_cast<uint32>( claimed );
            const uint32 chunkEnd   = MathUtil::min( chunkStart + chunkSize, end );
            if ( pGroup->_pBlockBody != nullptr )
            {
                ( *pGroup->_pBlockBody )( chunkStart, chunkEnd );
            }
            else if ( pGroup->_blockBody.isBound() )
            {
                pGroup->_blockBody( chunkStart, chunkEnd );
            }
            else if ( pGroup->_indexBody.isBound() )
            {
                for ( uint32 elementIndex = chunkStart; elementIndex < chunkEnd; ++elementIndex )
                    pGroup->_indexBody( elementIndex );
            }
        }
    }

    void TaskManager::runGroupTicket( ParallelGroup* pGroup )
    {
        runGroupChunks( pGroup );
        // 스택 그룹은 합류가 0 이 되는 순간 사라질 수 있다 — 그 뒤에 필요한 것은 전부 **앞서** 읽는다.
        const bool   bPooledGroup = pGroup->_pParent != nullptr;
        const uint64 joinBefore   = pGroup->_join.finishOne();
        if ( ( joinBefore & JoinCounter::kCountMask ) == 1 )
            onGroupFinished( pGroup, joinBefore, bPooledGroup );
    }

    void TaskManager::onGroupFinished( ParallelGroup* pGroup, uint64 joinBeforeFinish, bool bPooledGroup )
    {
        const uint32 waiterPlusOne = static_cast<uint32>( joinBeforeFinish >> JoinCounter::kWaiterShift );
        if ( bPooledGroup )
        {
            // 풀 그룹은 우리가 마지막 손이다 — 부모의 그룹 의존성을 풀고(준비되면 큐로) 그룹을 되돌린다.
            TaskNode* pParent = pGroup->_pParent;
            pGroup->_pParent  = nullptr;
            _nodePool->deallocateGroup( pGroup );
            if ( pParent != nullptr )
            {
                if ( pParent->_unresolvedDependencies.fetch_sub( 1, std::memory_order_acq_rel ) == 1 )
                    scheduleReadyTask( pParent );
                pParent->release();
            }
        }
        if ( waiterPlusOne != 0 )
            unparkWaiter( waiterPlusOne - 1 );
    }

    void TaskManager::waitForGroup( ParallelGroup& group )
    {
        uint32 spinCount = 0;
        while ( group._join.getPending() > 0 )
        {
            if ( isMainThread() )
                dispatchMainThreadTasks();

            // 남은 티켓은 이 스레드가 닿을 수 있는 큐(자기 덱 · 전역)에만 있다 — 돕다 보면 스스로 집는다.
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

            parkOnJoin( group._join );
            spinCount = 0;
        }
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
        while ( stage._pNode->_join.getPending() > 0 )
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

            parkOnJoin( stage._pNode->_join );
            spinCount = 0;
        }

        {
            std::scoped_lock<mutex> doneLock{ stage._pNode->_listMutex };
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

        return stage._pNode->_join.getPending() == 0;
    }

    void TaskManager::parkOnJoin( JoinCounter& join )
    {
        const uint32    slotIndex = getCurrentWaiterSlotIndex();
        atomic<uint32>& word      = getWaiterWord( slotIndex );
        // **등록보다 먼저** 읽는다. 등록 뒤에 온 깨움은 워드를 바꾸므로 아래 wait 가 바로 돌아온다.
        const uint32 seen = word.load( std::memory_order_acquire );

        switch ( join.tryRegisterWaiter( slotIndex + 1 ) )
        {
            case JoinCounter::RegisterResult::AlreadyDone:
                return;
            case JoinCounter::RegisterResult::Busy:
            {
                // 같은 것을 둘째로 기다린다 — 드물다. 방송에 잠들되 짧게 끊어 폴링한다(완료는 방송도 울린다).
                const uint32 epoch = prepareBroadcastWait();
                if ( join.getPending() > 0 )
                    commitBroadcastWait( epoch, kSecondWaiterPollMilli );
                else
                    cancelBroadcastWait();
                return;
            }
            case JoinCounter::RegisterResult::Registered:
                break;
            default:
                break;
        }

        const bool bMainThread = isMainThread();
        if ( bMainThread )
        {
            // 메인 일감을 넣는 쪽이 이 칸을 보고 깨운다. 적은 뒤 큐를 다시 봐야 그 사이 들어온 것을 놓치지 않는다.
            _mainThreadParkedSlot.store( static_cast<int32>( slotIndex ), std::memory_order_seq_cst );
            std::atomic_thread_fence( std::memory_order_seq_cst );
            if ( _queueMainThread.empty() == false )
            {
                _mainThreadParkedSlot.store( -1, std::memory_order_seq_cst );
                return;
            }
        }

        Futex::wait( word, seen );

        if ( bMainThread )
            _mainThreadParkedSlot.store( -1, std::memory_order_seq_cst );
    }

    uint32 TaskManager::prepareBroadcastWait()
    {
        const uint32 epoch = _completionEpoch.load( std::memory_order_acquire );
        _broadcastWaiterCount.fetch_add( 1, std::memory_order_seq_cst );
        std::atomic_thread_fence( std::memory_order_seq_cst );
        return epoch;
    }

    void TaskManager::commitBroadcastWait( uint32 seenEpoch, uint32 timeoutMilli )
    {
        if ( timeoutMilli == 0 )
            Futex::wait( _completionEpoch, seenEpoch );
        else
            Futex::waitFor( _completionEpoch, seenEpoch, timeoutMilli );
        _broadcastWaiterCount.fetch_sub( 1, std::memory_order_seq_cst );
    }

    void TaskManager::cancelBroadcastWait()
    {
        _broadcastWaiterCount.fetch_sub( 1, std::memory_order_seq_cst );
    }

    void TaskManager::notifyBroadcast()
    {
        // 완료를 만든 원자 연산 뒤에 울타리를 치고 대기자 수를 본다 — 대기자 쪽의 "수 올리고 울타리 치고 조건 보기" 와 짝이다.
        std::atomic_thread_fence( std::memory_order_seq_cst );
        if ( _broadcastWaiterCount.load( std::memory_order_seq_cst ) == 0 )
            return;
        _completionEpoch.fetch_add( 1, std::memory_order_release );
        Futex::wakeAll( _completionEpoch );
    }

    void TaskManager::wakeParkedMainThread()
    {
        std::atomic_thread_fence( std::memory_order_seq_cst );
        const int32 parkedSlot = _mainThreadParkedSlot.load( std::memory_order_seq_cst );
        if ( parkedSlot >= 0 )
            unparkWaiter( static_cast<uint32>( parkedSlot ) );
        // 메인이 방송(`waitAll`)에 잠들어 있을 수도 있다.
        notifyBroadcast();
    }

    void TaskManager::unparkWaiter( uint32 slotIndex )
    {
        atomic<uint32>& word = getWaiterWord( slotIndex );
        word.fetch_add( 1, std::memory_order_release );
        Futex::wakeOne( word );
    }

    void TaskManager::unparkWorker( uint32 workerId )
    {
        WorkerSlot& slot = *std::as_const( _listWorkerSlot )[workerId];
        slot._park._word.fetch_add( 1, std::memory_order_release );
        Futex::wakeOne( slot._park._word );
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

            if ( spinCount < kIdleSpinCount )
            {
                sw::cpuPause();
                ++spinCount;
                continue;
            }

            uint32 waitMilli = 0;
            if ( timeoutMs > 0 )
            {
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() - startTime ).count();
                if ( elapsed >= static_cast<int64>( timeoutMs ) )
                    return _activeTaskCount.load( std::memory_order_acquire ) == 0;
                waitMilli = static_cast<uint32>( static_cast<int64>( timeoutMs ) - elapsed );
            }

            const uint32 epoch = prepareBroadcastWait();
            if ( _activeTaskCount.load( std::memory_order_seq_cst ) > 0 )
                commitBroadcastWait( epoch, waitMilli );
            else
                cancelBroadcastWait();
            spinCount = 0;
        }
        return true;
    }

    void TaskManager::drainQueueItem( uintptr_t item )
    {
        if ( item == 0 )
            return;
        if ( TaskQueueItem::isTicket( item ) == false )
        {
            TaskQueueItem::toNode( item )->release();
            return;
        }
        // 티켓은 돌리지 않고 닫는다. 마지막이면 그룹만 되돌린다 — 부모는 준비되지 않은 채 핸들이 놓일 때 돌아간다.
        ParallelGroup* pGroup       = TaskQueueItem::toGroup( item );
        const bool     bPooledGroup = pGroup->_pParent != nullptr;
        const uint64   joinBefore   = pGroup->_join.finishOne();
        if ( ( joinBefore & JoinCounter::kCountMask ) != 1 )
            return;
        const uint32 waiterPlusOne = static_cast<uint32>( joinBefore >> JoinCounter::kWaiterShift );
        if ( bPooledGroup )
        {
            TaskNode* pParent = pGroup->_pParent;
            pGroup->_pParent  = nullptr;
            _nodePool->deallocateGroup( pGroup );
            if ( pParent != nullptr )
                pParent->release();
        }
        if ( waiterPlusOne != 0 )
            unparkWaiter( waiterPlusOne - 1 );
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
        for ( const unique_ptr<WorkerSlot>& slot : _listWorkerSlot )
        {
            uintptr_t item{ 0 };
            while ( slot->_queue.steal( item ) )
            {
                drainQueueItem( item );
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
            uintptr_t item{ 0 };
            while ( _globalHighQueue.dequeue( item ) )
            {
                drainQueueItem( item );
            }
            while ( _globalWorkerQueue.dequeue( item ) )
            {
                drainQueueItem( item );
            }
            while ( _globalLowQueue.dequeue( item ) )
            {
                drainQueueItem( item );
            }
        }
        _activeTaskCount.store( 0, std::memory_order_release );
        // 큐를 다 비웠으니 아무 태스크도 돌지 않는다 — 살아 있는 스테이지를 전부 되돌린다.
        _nodePool->resetAllStages();
        notifyBroadcast();
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
    };

    void TaskManager::executeItem( uintptr_t item )
    {
        if ( item == 0 )
            return;
        if ( TaskQueueItem::isTicket( item ) )
            runGroupTicket( TaskQueueItem::toGroup( item ) );
        else
            executeTask( TaskQueueItem::toNode( item ) );
    }

    void TaskManager::executeTask( TaskNode* pNode )
    {
        pNode->_state.store( TaskState::Running, std::memory_order_relaxed );

        if ( pNode->_bCancelled.load( std::memory_order_acquire ) == false )
        {
            TaskNode* pPrevRunningTask = t_pCurrentRunningTask;
            t_pCurrentRunningTask      = pNode;

            BLOCK( "Execute Task Delegate" )
            {
                TaskExecutionVisitor visitor{};
                std::visit( visitor, pNode->_callable );
            }

            t_pCurrentRunningTask = pPrevRunningTask;
        }

        // 본문 몫(1)을 내린다. 0 이 되면 자식도 다 끝난 것이고, 아니면 마지막 자식이 완료를 처리한다.
        if ( pNode->_pendingChildren.fetch_sub( 1, std::memory_order_acq_rel ) == 1 )
            completeTask( pNode );
        else
            pNode->_state.store( TaskState::WaitingForChildren, std::memory_order_relaxed );

        pNode->release(); // Release queue reference
    }

    void TaskManager::completeTask( TaskNode* pNode )
    {
        pNode->_state.store( TaskState::Completed, std::memory_order_release );

        // **활성 수는 스테이지·부모보다 먼저 내린다.** 예전에는 맨 끝(후속 트리거 뒤)에서 내렸는데, 그러면
        // `waitStage` 가 스테이지 완료 통지를 받고 돌아온 순간에도 이 태스크는 아직 활성으로 세어져 있다.
        // 그 직후 `clear()` 가 수를 0 으로 놓으면 뒤늦은 fetch_sub 가 0xFFFFFFFF 로 감아 버려 이후의
        // `waitAll` 이 영원히 기다린다 — CTest 아래에서만 재현되던 EngineTest_NoGPU 180 초 타임아웃이 이것이다.
        // 후속 태스크는 만들 때 이미 세어져 있으므로 여기서 내려도 `waitAll` 이 일찍 돌아오지 않는다.
        if ( _activeTaskCount.fetch_sub( 1, std::memory_order_acq_rel ) == 1 )
            notifyBroadcast();

        BLOCK( "Update Parent Task" )
        {
            TaskNode* pParent = pNode->_pParent;
            pNode->_pParent   = nullptr;
            if ( pParent != nullptr )
            {
                if ( pParent->_pendingChildren.fetch_sub( 1, std::memory_order_acq_rel ) == 1 )
                    completeTask( pParent );
                pParent->release();
            }
        }

        BLOCK( "Update Stage" )
        {
            StageNode* pStage   = pNode->_parentStage;
            pNode->_parentStage = nullptr;
            if ( pStage != nullptr )
            {
                const uint64 joinBefore = pStage->_join.finishOne();
                if ( ( joinBefore & JoinCounter::kCountMask ) == 1 )
                {
                    // 마지막 태스크가 끝났다 — 적혀 있던 대기자만 깨우고(둘째 대기자는 방송), addTask 에서 잡은 자기 참조를 놓는다.
                    const uint32 waiterPlusOne = static_cast<uint32>( joinBefore >> JoinCounter::kWaiterShift );
                    if ( waiterPlusOne != 0 )
                        unparkWaiter( waiterPlusOne - 1 );
                    notifyBroadcast();
                    pStage->release();
                }
            }
        }

        BLOCK( "Trigger Successors and Cleanup" )
        {
            if ( pNode->_successors.isEmptyRelaxed() == false )
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
            }

            pNode->_callable = std::monostate{};
        }
    }

    bool TaskManager::tryTakeTask( uint32 workerId, uintptr_t& outItem )
    {
        outItem = 0;

        // 레인 순서: High -> 내 덱 -> Normal 전역 -> 훔치기 -> Low. 빈 큐의 dequeue 는 시퀀스 한 줄 읽기라
        // High 를 매번 먼저 보는 비용은 없다시피 하다.
        if ( _globalHighQueue.dequeue( outItem ) && outItem != 0 )
            return true;

        WorkerSlot& localSlot = *std::as_const( _listWorkerSlot )[workerId];
        if ( localSlot._queue.pop( outItem ) && outItem != 0 )
            return true;

        if ( _globalWorkerQueue.dequeue( outItem ) && outItem != 0 )
            return true;

        const uint32 numWorkers = static_cast<uint32>( _listWorkerSlot.size() );
        for ( uint32 workerIndex = 1; workerIndex < numWorkers; ++workerIndex )
        {
            const uint32 targetId   = ( workerId + workerIndex ) % numWorkers;
            WorkerSlot&  targetSlot = *std::as_const( _listWorkerSlot )[targetId];
            if ( targetSlot._queue.steal( outItem ) && outItem != 0 )
                return true;
        }

        if ( _globalLowQueue.dequeue( outItem ) && outItem != 0 )
            return true;

        outItem = 0;
        return false;
    }

    bool TaskManager::tryHelpAndExecute()
    {
        const int32 workerId = getCurrentWorkerIndex();
        if ( workerId >= 0 )
        {
            uintptr_t item{ 0 };
            if ( _globalHighQueue.dequeue( item ) && item != 0 )
            {
                executeItem( item );
                return true;
            }
            WorkerSlot& localSlot = *std::as_const( _listWorkerSlot )[static_cast<uint32>( workerId )];
            if ( localSlot._queue.pop( item ) && item != 0 )
            {
                executeItem( item );
                return true;
            }
        }

        return tryStealAndExecute( invalid_index::kUint32 );
    }

    bool TaskManager::tryStealAndExecute( uint32 excludedWorkerId )
    {
        const uint32 numWorkers = static_cast<uint32>( _listWorkerSlot.size() );
        if ( numWorkers == 0 )
            return false;

        uintptr_t item{ 0 };
        if ( _globalHighQueue.dequeue( item ) && item != 0 )
        {
            executeItem( item );
            return true;
        }

        if ( _globalWorkerQueue.dequeue( item ) && item != 0 )
        {
            executeItem( item );
            return true;
        }

        for ( uint32 workerIndex = 0; workerIndex < numWorkers; ++workerIndex )
        {
            if ( workerIndex == excludedWorkerId )
                continue;

            WorkerSlot& targetSlot = *std::as_const( _listWorkerSlot )[workerIndex];
            if ( targetSlot._queue.steal( item ) && item != 0 )
            {
                executeItem( item );
                return true;
            }
        }

        if ( _globalLowQueue.dequeue( item ) && item != 0 )
        {
            executeItem( item );
            return true;
        }
        return false;
    }

    void TaskManager::workerLoop( uint32 workerId )
    {
        t_bTaskWorkerThread  = true;
        t_currentWorkerIndex = static_cast<int32>( workerId );

        WorkerSlot&  slot    = *std::as_const( _listWorkerSlot )[workerId];
        const uint64 idleBit = static_cast<uint64>( 1 ) << workerId;

        while ( _bStop.load( std::memory_order_relaxed ) == false )
        {
            uintptr_t item{ 0 };
            if ( tryTakeTask( workerId, item ) )
            {
                executeItem( item );
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
                    if ( tryTakeTask( workerId, item ) )
                    {
                        bFoundInSpin = true;
                        break;
                    }
                }
                if ( _bStop.load( std::memory_order_relaxed ) )
                    break;
                // 시계는 32 번에 한 번만 본다 — 매번 보면 그 자체가 스핀 비용이다.
                for ( uint32 spin = 0; spin < 32; ++spin )
                    sw::cpuPause();
                const int64 spentMicro = std::chrono::duration_cast<std::chrono::microseconds>( std::chrono::steady_clock::now() - spinStart ).count();
                if ( spentMicro >= kWorkerIdleSpinMicro )
                    break;
            }

            if ( bFoundInSpin && item != 0 )
            {
                executeItem( item );
                continue;
            }

            // 잠든다: 워드를 읽고 → 유휴 비트를 올리고 → 큐를 한 번 더 본다. 제출 쪽은 큐에 넣고 → 세대를 올리고 →
            // 유휴 비트를 본다. 양쪽 다 seq_cst 라 둘 중 하나는 상대를 본다 — 넣은 일감이 잠든 워커 뒤에 남지 않는다.
            const uint32 parkSeen = slot._park._word.load( std::memory_order_acquire );
            _idleWorkerMask.fetch_or( idleBit, std::memory_order_seq_cst );
            std::atomic_thread_fence( std::memory_order_seq_cst );
            if ( tryTakeTask( workerId, item ) )
            {
                _idleWorkerMask.fetch_and( ~idleBit, std::memory_order_seq_cst );
                executeItem( item );
                continue;
            }
            if ( _bStop.load( std::memory_order_seq_cst ) )
            {
                _idleWorkerMask.fetch_and( ~idleBit, std::memory_order_seq_cst );
                break;
            }
            Futex::wait( slot._park._word, parkSeen );
            // 깨운 쪽이 비트를 내렸다. 허위로 깨었으면 아직 켜져 있으니 여기서 내린다 — 다음 회차가 다시 잠들지를 정한다.
            _idleWorkerMask.fetch_and( ~idleBit, std::memory_order_seq_cst );
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
        // 스핀 중인 워커는 세대로 안다. seq_cst 인 이유는 워커의 "유휴 비트 올리기 → 큐 보기" 와 데커 짝이기 때문이다.
        _workEpoch.fetch_add( 1, std::memory_order_seq_cst );
        if ( wantedCount == 0 )
            return;

        uint64 mask = _idleWorkerMask.load( std::memory_order_seq_cst );
        if ( mask == 0 )
            return;

        // **필요한 수만, 비트를 내린 쪽이** 깨운다. 두 제출자가 같은 워커를 두 번 깨우지 않고, 깨움 하나는 주소 하나다 —
        // 예전의 `notify_one` × n 은 뮤텍스 아래에서 차례로 나갔고 깨어난 n 명이 그 뮤텍스를 다시 잡느라 줄을 섰다.
        uint32 wokenCount = 0;
        while ( mask != 0 && wokenCount < wantedCount )
        {
            const uint32 workerId = countTrailingZeros64( mask );
            const uint64 bit      = static_cast<uint64>( 1 ) << workerId;
            const uint64 previous = _idleWorkerMask.fetch_and( ~bit, std::memory_order_seq_cst );
            if ( ( previous & bit ) != 0 )
            {
                unparkWorker( workerId );
                ++wokenCount;
            }
            mask = previous & ~bit;
        }
        if ( wokenCount > 0 )
            _wakeSignalCount.fetch_add( 1, std::memory_order_relaxed );
    }

    void TaskManager::scheduleReadyTask( TaskNode* pNode, bool bWakeWorker )
    {
        if ( pNode == nullptr )
            return;

        TaskState expected = TaskState::Pending;
        if ( pNode->_state.compare_exchange_strong( expected, TaskState::Ready, std::memory_order_acq_rel ) == false )
            return;

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
            // 이 일감을 실행할 수 있는 것은 메인 스레드뿐이다(`dispatchMainThreadTasks`). 메인이 어느 대기에서든
            // 잠들어 있으면 깨워야 한다 — 예전에 `notify_one` 이 엉뚱한 스레드를 깨워 메인과 태스크가 서로를 기다렸다.
            wakeParkedMainThread();
            return;
        }

        const uint32 numWorkers = static_cast<uint32>( _listWorkerSlot.size() );
        if ( numWorkers == 0 )
            return;

        // 레인은 우선순위가 정한다. High/Low 는 워커에서 넣어도 자기 덱이 아니라 전역 레인이다 —
        // 덱은 LIFO 라 "먼저" 도 "나중" 도 약속하지 못한다.
        const uintptr_t item     = TaskQueueItem::fromNode( pNode );
        const int32     workerId = getCurrentWorkerIndex();
        if ( pNode->_priority == TaskPriority::High )
        {
            while ( _globalHighQueue.enqueue( item ) == false )
                std::this_thread::yield();
        }
        else if ( pNode->_priority == TaskPriority::Low )
        {
            while ( _globalLowQueue.enqueue( item ) == false )
                std::this_thread::yield();
        }
        else if ( workerId >= 0 )
        {
            WorkerSlot& localSlot = *std::as_const( _listWorkerSlot )[static_cast<uint32>( workerId )];
            while ( localSlot._queue.push( item ) == false )
            {
                if ( _globalWorkerQueue.enqueue( item ) )
                    break;
                std::this_thread::yield();
            }
        }
        else
        {
            while ( _globalWorkerQueue.enqueue( item ) == false )
            {
                std::this_thread::yield();
            }
        }

        if ( bWakeWorker )
            wakeSleepingWorkers( 1 );
    }

} // namespace sw
