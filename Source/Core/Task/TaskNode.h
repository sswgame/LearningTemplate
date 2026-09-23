/**
 * @file TaskNode.h
 * @brief TaskManager 의 내부 노드 — 태스크 · 스테이지 · 병렬 그룹 · 합류 카운터 · 후속 목록.
 * @details **공개 API 가 아니다.** `Core/Task` 안의 구현 파일만 포함한다. 바깥은 `TaskTypes.h` 의 핸들과
 *          `TaskManager.h` 만 본다. 스케줄러(`TaskManager.cpp`) · 풀(`TaskNodePool.cpp`) · 핸들 구현(`TaskTypes.cpp`)이
 *          같은 노드를 만지므로 그 셋이 공유하는 정의를 여기 둔다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/LockFreeObjectPool.h"
#include "Core/Concurrency/SpinLock.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/String/fixed_string.h"
#include "Core/Task/TaskTypes.h"

namespace sw
{
    struct ParallelGroup;
    struct TaskNode;

    class TaskManager;
    class TaskNodePool;

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
         * @details 돌려준 값이 `isLastFinish` 이면 이 호출이 마지막이고, `waiterSlotPlusOne` 이 깨울 대기자(+1)다.
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

        /** @brief `finishOne` 이 돌려준 워드가 "마지막 하나를 끝냈다" 인가. */
        static bool isLastFinish( uint64 packedBeforeFinish ) { return ( packedBeforeFinish & kCountMask ) == 1; }
        /** @brief `finishOne` 이 돌려준 워드에 적힌 대기자 슬롯 + 1 (0 이면 없음). */
        static uint32 waiterSlotPlusOne( uint64 packedBeforeFinish ) { return static_cast<uint32>( packedBeforeFinish >> kWaiterShift ); }

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

    /** @brief 동시에 살아 있는 병렬 그룹 수의 상한 — 청크 수가 아니다. 넘치면 힙으로 물러난다. */
    constexpr uint32 kParallelGroupPoolCapacity = 512;
    using ParallelGroupPool                     = LockFreeObjectPool<ParallelGroup, kParallelGroupPoolCapacity>;

    /**
     * @struct ParallelGroup
     * @brief 병렬 for 하나 — 본문 · 범위 · 다음 청크 카운터 · 티켓 합류.
     * @details 청크마다 태스크 노드를 만들지 않는다. 큐에는 **티켓** 몇 장(워커 수 이하)만 들어가고, 티켓을 집은
     *          스레드는 `_nextChunkStart` 를 올려 가며 남은 청크를 전부 돈다 — 청크 하나의 비용이 원자 덧셈 하나다.
     *          끝난 스레드가 남은 청크를 이어 받으므로 균형은 저절로 맞는다(Unreal ParallelFor · TBB 의 모양).
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
        TaskNode*                    _pParent{ nullptr }; ///< 풀 그룹: 티켓이 다 끝나면 의존성을 풀 부모 (참조를 하나 쥔다). 스택 그룹은 null
        ParallelGroupPool*           _pPool{ nullptr };   ///< 돌아갈 풀. 스택이거나 힙이면 null
        bool                         _bHeap{ false };     ///< 풀이 비어 힙에서 왔다
        /** @brief 다음에 집을 청크의 시작. 64 비트인 이유: 티켓마다 끝을 지나 한 번 더 더하므로 32 비트는 감길 수 있다. */
        alignas( 64 ) atomic<uint64> _nextChunkStart{ 0 };
        alignas( 64 ) JoinCounter _join; ///< 아직 끝나지 않은 티켓 수 + 기다리는 스레드

        /** @brief 풀에서 왔는가 — 부모를 쥔 그룹만 풀·힙에 산다. 스택 그룹은 마지막 티켓 뒤에 역참조하면 안 된다. */
        bool isPooled() const { return _pParent != nullptr; }
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

        void lock() const noexcept { _lock.lock(); }
        void unlock() const noexcept { _lock.unlock(); }

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
        bool isEmptyRelaxed() const { return _count == 0; }

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

        /** @brief 목록의 모든 참조를 놓고 비웁니다. */
        void clearAndRelease();
    };

    /**
     * @struct TaskNode
     * @brief 사용자에게 보이는 태스크 하나 — 핸들 · 스테이지 · 후속 · 활성 수가 전부 여기 걸린다.
     */
    struct TaskNode
    {
#if !defined( SW_SHIPPING )
        /** @brief 디버깅 이름의 최대 길이 (Shipping 에는 이름이 없다). */
        static constexpr uint32 kNameCapacity = 31;
#endif

        void retain() { _refCount.fetch_add( 1, std::memory_order_relaxed ); }
        /** @brief 마지막 참조가 놓이면 소유 매니저의 풀로 돌아갑니다. */
        void release();

        /** @brief 디버깅·프로파일링용 이름을 설정합니다 (용량 초과는 잘라 담습니다). */
        void setName( [[maybe_unused]] string_view name )
        {
#if !defined( SW_SHIPPING )
            uint32 len = static_cast<uint32>( name.size() );
            if ( len > kNameCapacity )
                len = kNameCapacity;
            if ( len > 0 )
                Memory::copy( _arrName, name.data(), len );
            _arrName[len] = 0;
#endif
        }

#if !defined( SW_SHIPPING )
        utf8 _arrName[kNameCapacity + 1]{};
#endif
        InlineSuccessorList _successors;
        TaskCallable        _callable;
        StageNode*          _parentStage{ nullptr }; ///< 남은 태스크가 있는 동안만 유효 — 스테이지가 그 동안 스스로를 쥔다

        atomic<int32> _unresolvedDependencies{ 1 }; ///< 1 = 빌더 의존성 (`submit` 이 푼다) + 선행 태스크 수 + 병렬 그룹 1

        TaskThreadAffinity _affinity = TaskThreadAffinity::Any;
        TaskPriority       _priority{ TaskPriority::Normal };
        atomic<TaskState>  _state{ TaskState::Pending };
        atomic<bool>       _bCancelled{ false };

        TaskManager* _pOwner{ nullptr };
        TaskNode*    _pParent{ nullptr }; ///< 이 태스크를 본문 안에서 만든 태스크 — 그쪽이 우리 완료를 기다린다
        /**
         * @brief 본문 하나 + 아직 안 끝난 자식 수. 0 으로 내리는 쪽(본문이든 마지막 자식이든)이 완료를 처리한다.
         * @details 예전엔 "자식 수" 와 "상태 = 자식 대기" 를 따로 두고 seq_cst 로 데커 순서를 맞췄는데, 본문이 상태를
         *          적고 자식 수를 읽는 사이에 마지막 자식이 수를 내리고 상태를 읽으면 **둘 다** 완료를 처리했다.
         *          본문 자신을 1 로 세어 두면 카운터 하나로 끝나고 seq_cst 도 필요 없다.
         */
        atomic<int32> _pendingChildren{ 1 };
        atomic<int32> _refCount{ 1 };
    };
} // namespace sw
