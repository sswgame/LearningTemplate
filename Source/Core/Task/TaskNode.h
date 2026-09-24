/**
 * @file TaskNode.h
 * @brief TaskManager 의 내부 노드입니다(태스크 · 스테이지 · 병렬 그룹 · 조인 카운터 · 후속 목록).
 * @details **공개 API 가 아닙니다.** `Core/Task` 안의 구현 파일만 include 합니다. 바깥에서는 `TaskTypes.h` 의 핸들과
 *          `TaskManager.h` 만 봅니다. 스케줄러(`TaskManager.cpp`) · 풀(`TaskNodePool.cpp`) · 핸들 구현(`TaskTypes.cpp`)이
 *          같은 노드를 다루므로, 셋이 공유하는 정의를 여기 둡니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/LockFreeObjectPool.h"
#include "Core/Concurrency/SpinLock.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Task/TaskTypes.h"

namespace sw
{
    struct ParallelGroup;
    struct TaskNode;

    class TaskManager;
    class TaskNodePool;

    /**
     * @brief 임의의 매개변수 묶음(TaskArgs)과 함께 호출되는 델리게이트 페이로드입니다.
     */
    struct TaskArgsPayload
    {
        TaskArgsDelegate _delegate;
        TaskArgs         _args;
    };

    /**
     * @brief 타입을 지운 태스크 호출 대상 variant 입니다. 병렬 본문은 여기 없고 그룹이 가집니다.
     */
    using TaskCallable = std::variant<
        std::monostate,
        TaskDelegate,
        TaskArgsPayload>;

    /**
     * @struct JoinCounter
     * @brief 남은 수와 대기자 하나를 **한 워드**에 담은 조인 카운터입니다.
     * @details 하위 32비트는 남은 수, 상위 32비트는 대기자 슬롯 + 1(0 이면 없음)입니다. 마지막으로 끝내는 쪽의 CAS 하나가
     *          "0 이 됐는가" 와 "누구를 깨울까" 에 함께 답하고 대기자 칸을 비웁니다. 그 뒤로는 이 워드를 **다시 건드리지
     *          않으므로** 스택에 놓인 그룹(`runParallel`)도 안전합니다. 대기자가 0 을 본 순간 스택이 사라져도 됩니다. 대기자
     *          등록도 같은 워드의 CAS 라서 "등록했는데 이미 끝났다" 는 경우를 놓치지 않습니다.
     */
    struct JoinCounter
    {
        static constexpr uint64 kCountMask   = 0xFFFFFFFFull;
        static constexpr uint32 kWaiterShift = 32;

        enum class RegisterResult : uint8
        {
            Registered,  ///< 이 스레드가 대기자로 등록됐다(또는 이미 등록돼 있었다)
            AlreadyDone, ///< 남은 것이 없다. 잠들 필요가 없다
            Busy         ///< 다른 스레드가 대기자 칸을 잡고 있다. 브로드캐스트로 깨우는 방식으로 넘어간다
        };

        atomic<uint64> _packed{ 0 };

        void   reset() { _packed.store( 0, std::memory_order_relaxed ); }
        uint32 getPending() const { return static_cast<uint32>( _packed.load( std::memory_order_acquire ) & kCountMask ); }

        /** @brief 남은 수를 @p count 만큼 올리고 이전의 남은 수를 반환합니다. */
        uint32 addPending( uint32 count )
        {
            return static_cast<uint32>( _packed.fetch_add( count, std::memory_order_acq_rel ) & kCountMask );
        }

        /**
         * @brief 하나를 끝냅니다. 마지막이면 대기자 칸까지 비우고, **이전** 워드를 반환합니다.
         * @details 반환값이 `isLastFinish` 를 만족하면 이 호출이 마지막이고, `waiterSlotPlusOne` 이 깨울 대기자(+1)입니다.
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

        /** @brief `finishOne` 이 반환한 워드가 "마지막 하나를 끝냈다" 를 뜻하는지 확인합니다. */
        static bool isLastFinish( uint64 packedBeforeFinish ) { return ( packedBeforeFinish & kCountMask ) == 1; }
        /** @brief `finishOne` 이 반환한 워드에 적힌 대기자 슬롯 + 1 입니다(0 이면 없음). */
        static uint32 waiterSlotPlusOne( uint64 packedBeforeFinish ) { return static_cast<uint32>( packedBeforeFinish >> kWaiterShift ); }

        /** @brief 이 스레드(슬롯 + 1)를 대기자로 등록합니다. 남은 것이 없거나 다른 대기자가 있으면 그렇다고 알려 줍니다. */
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

    /** @brief 동시에 살아 있는 병렬 그룹 수의 상한입니다(청크 수가 아닙니다). 넘치면 힙을 씁니다. */
    constexpr uint32 kParallelGroupPoolCapacity = 512;
    using ParallelGroupPool                     = LockFreeObjectPool<ParallelGroup, kParallelGroupPoolCapacity>;

    /**
     * @struct ParallelGroup
     * @brief 병렬 for 하나입니다(본문 · 범위 · 다음 청크 카운터 · 티켓 조인).
     * @details 청크마다 태스크 노드를 만들지 않습니다. 큐에는 **티켓** 몇 장(워커 수 이하)만 들어가고, 티켓을 가져간 스레드는
     *          `_nextChunkStart` 를 올려 가며 남은 청크를 모두 실행합니다. 청크 하나의 비용이 원자 덧셈 하나입니다. 먼저 끝난
     *          스레드가 남은 청크를 이어서 가져가므로 부하 균형이 저절로 맞습니다(Unreal ParallelFor · TBB 와 같은 방식).
     *
     *          `runParallel` 은 이것을 **호출 스레드의 스택에** 둡니다. 마지막 티켓이 `_join` 을 0 으로 만든 뒤에는 아무도 이
     *          메모리를 건드리지 않습니다. 그 약속이 `JoinCounter::finishOne` 의 CAS 하나에 담겨 있습니다. `emplaceParallel*` 은
     *          풀(바닥나면 힙)에서 꺼내고 부모 노드를 잡으며, 마지막 티켓이 부모의 의존성을 풀고 그룹을 반납합니다. 어디로
     *          돌려줄지는 풀이 주소로 가립니다(`LockFreeObjectPool::owns`).
     */
    struct ParallelGroup
    {
        ParallelBlockDelegate _blockBody; ///< `emplaceParallelBlock` 용 사본
        ParallelTaskDelegate  _indexBody; ///< `emplaceParallel` 용 사본
        /**
         * @brief 청크마다 부를 블록 본문입니다. null 이면 `_indexBody` 를 인덱스마다 부릅니다.
         * @details `runParallel` 은 호출하는 쪽의 델리게이트(호출이 끝날 때까지 살아 있다)를, `emplaceParallelBlock` 은 자기
         *          사본(`_blockBody`)을 가리킵니다. 그래서 청크 실행의 갈래가 블록 · 인덱스 둘뿐입니다.
         */
        const ParallelBlockDelegate* _pBlockBody{ nullptr };
        uint32                       _rangeEnd{ 0 };
        uint32                       _chunkSize{ 1 };
        TaskNode*                    _pParent{ nullptr }; ///< 풀 그룹: 티켓이 모두 끝나면 의존성을 풀 부모(참조를 하나 잡는다). 스택 그룹은 null
        /** @brief 다음에 가져갈 청크의 시작입니다. 64비트인 이유: 티켓마다 끝을 지나서 한 번 더 더하므로 32비트는 넘칠 수 있습니다. */
        alignas( 64 ) atomic<uint64> _nextChunkStart{ 0 };
        alignas( 64 ) JoinCounter _join; ///< 아직 끝나지 않은 티켓 수 + 기다리는 스레드

        /** @brief 풀에서 왔는지 확인합니다. 부모를 잡은 그룹만 풀 · 힙에 있습니다. 스택 그룹은 마지막 티켓 뒤에 역참조하면 안 됩니다. */
        bool isPooled() const { return _pParent != nullptr; }
    };

    /**
     * @brief 스테이지입니다. 태스크 묶음의 완료를 기다리는 단위로, 매니저의 풀에서 오고 침입형 참조 계수로 수명을 관리합니다.
     * @details 스테이지는 **남은 수만 셉니다.** 태스크를 붙들지 않습니다. 태스크의 수명은 핸들과 큐가 잡은 참조가 정하고,
     *          스테이지는 완료 통지가 올 곳일 뿐입니다. (예전에는 태스크 목록을 뮤텍스 아래에 들고 태스크마다 참조를 하나씩
     *          잡았다가 `waitStage` 에서 놓았습니다. 그 목록을 읽는 곳이 없었고, `addTask` 마다 잠금 한 번과 참조 계수 왕복이
     *          들었습니다.)
     *
     *          참조는 두 쪽이 잡습니다. 핸들 사본과, **남은 태스크가 있는 동안의 스테이지 자신**입니다(0→1 에서 잡고 1→0 에서
     *          놓습니다). 태스크 노드는 참조를 잡지 않습니다. 노드의 `_parentStage` 는 남은 태스크가 있는 동안만 유효하고,
     *          그동안은 스테이지가 스스로를 잡고 있으므로 안전합니다. 대기는 `_join` 에 적힌 대기자 하나를 마지막 태스크가 직접
     *          깨웁니다. 뮤텍스도 조건 변수도 없습니다.
     */
    struct StageNode
    {
        JoinCounter   _join;
        atomic<int32> _refCount{ 0 };
        TaskNodePool* _pPool{ nullptr };

        void retain() { _refCount.fetch_add( 1, std::memory_order_relaxed ); }
        /** @brief 마지막 참조가 놓이면 풀로 돌아갑니다. */
        void release();
    };

    /**
     * @struct InlineSuccessorList
     * @brief 후속 태스크 목록을 인라인 버퍼(최대 4개)에 담아 힙 할당을 피하는 small-vector 구조체입니다.
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

        /** @brief 비어 있으면 잠금 없이 답합니다. 완료 경로에서 흔한 경우(후속 없음)가 스핀락을 잡지 않게 하려는 것입니다. */
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
     * @brief 사용자에게 보이는 태스크 하나입니다. 핸들 · 스테이지 · 후속 태스크 · 활성 수가 모두 여기에 연결됩니다.
     */
    struct TaskNode
    {
#if !defined( SW_SHIPPING )
        /** @brief 디버깅용 이름의 최대 길이입니다(Shipping 에는 이름이 없습니다). */
        static constexpr uint32 kNameCapacity = 31;
#endif

        void retain() { _refCount.fetch_add( 1, std::memory_order_relaxed ); }
        /** @brief 마지막 참조가 놓이면 소유 매니저의 풀로 돌아갑니다. */
        void release();

        /** @brief 디버깅 · 프로파일링용 이름을 정합니다(용량을 넘으면 잘라 담습니다). */
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
        StageNode*          _parentStage{ nullptr }; ///< 남은 태스크가 있는 동안만 유효하다. 그동안 스테이지가 스스로를 잡고 있다

        atomic<int32> _unresolvedDependencies{ 1 }; ///< 1 = 빌더 의존성(`submit` 이 푼다) + 선행 태스크 수 + 병렬 그룹 1

        TaskThreadAffinity _affinity = TaskThreadAffinity::Any;
        TaskPriority       _priority{ TaskPriority::Normal };
        /**
         * @brief 큐에 한 번 넣었는지입니다. 두 번 넣지 않게 막는 문 하나입니다.
         * @details 의존성 수는 0 을 한 번만 지나지만, 이미 제출한 태스크에 `precede` 로 선행을 더 걸면 수가 다시 올랐다가
         *          내려와 한 번 더 0 을 봅니다. 예전의 5단 상태(`TaskState`)는 이 문 말고는 읽는 곳이 없었습니다(밖에서 물을 수
         *          있는 "끝났나" 는 `TaskHandle::isCompleted` 가 `_pendingChildren` 으로 답합니다).
         */
        atomic<bool> _bScheduled{ false };
        atomic<bool> _bCancelled{ false };

        TaskManager* _pOwner{ nullptr };
        TaskNode*    _pParent{ nullptr }; ///< 이 태스크를 본문 안에서 만든 태스크. 그쪽이 이 태스크의 완료를 기다린다
        /**
         * @brief 본문 하나 + 아직 끝나지 않은 자식 수입니다. 0 으로 내리는 쪽(본문이든 마지막 자식이든)이 완료를 처리합니다.
         * @details 예전에는 "자식 수" 와 "상태 = 자식 대기" 를 따로 두고 seq_cst 로 Dekker 식 순서를 맞췄는데, 본문이 상태를 적고
         *          자식 수를 읽는 사이에 마지막 자식이 수를 내리고 상태를 읽으면 **둘 다** 완료를 처리했습니다. 본문 자신을 1 로 세어
         *          두면 카운터 하나로 끝나고 seq_cst 도 필요 없습니다. `TaskHandle::isCompleted` 도 이 값이 0 인지를 봅니다.
         */
        atomic<int32> _pendingChildren{ 1 };
        atomic<int32> _refCount{ 1 };
    };
} // namespace sw
