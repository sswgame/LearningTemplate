/**
 * @file StructuralChangeBuffer.h
 * @brief 틱 중 규칙 — 구조 동결 플래그, 틱이 미룬 구조 변경 · 틱 뒤 작업의 큐, 그것을 비우는 순서, 지금 틱하는 오브젝트(스레드별)입니다.
 * @details 유니티 DOTS `EntityCommandBuffer` 의 자리입니다 — 병렬 틱(잡)이 구조 변경을 기록하고, 정해진 동기화 지점(`drain`)에서 게임 스레드가
 *          (디스패치, 쓴 오브젝트, 부른 순서) 순으로 재생합니다(`DeferredDelegateQueue`). `GameObjectManager` 가 소유하고, 게임이 부르는 API(`executeOrDeferPostTick` · `isStructuralMutationFrozen` …)는
 *          매니저가 같은 이름으로 전달합니다.
 *
 *          **플래그는 하나입니다.** 구조 동결과 트랜스폼 읽기 전용은 같은 구간이라 `isFrozen` 하나가 둘 다 답합니다. 얼리고 푸는 것은 틱
 *          디스패치(`SceneTickScheduler`)만 합니다.
 *
 *          **비우는 순서는 `drain` 한 함수가 갖습니다** — 구조 변경(같은 오브젝트는 부른 순서) → 틱 중 트랜스폼 쓰기 → 틱 뒤 큐 → 병합 → 시작. 주의: 큐를 종류마다
 *          나누거나 각 단위가 자기 큐를 자기 시점에 비우게 하면, 틱 안에서 씬 컴포넌트를 붙이고(미뤄짐) 이어 부모에 붙일 때 부착이 먼저 돌아
 *          붙일 씬 컴포넌트가 없고, 쓰기가 미룬 `KeepWorld` 부착보다 먼저 적용되어 부착이 로컬 값을 다시 구해 덮는다.
 *
 *          스레드별 상태(지금 틱하는 오브젝트 · 쓰기의 주인)는 Engine 의 TU 하나에 있습니다. 헤더의 `inline thread_local` 로 옮기면 모듈마다
 *          칸이 생겨, 게임 모듈이 인스턴스화한 세터가 엔진이 채운 칸을 보지 못합니다.
 */
#pragma once
#include "Core/Concurrency/Queue/LockedSwapQueue.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /**
     * @class DeferredDelegateQueue
     * @brief 병렬 틱이 미룬 일을 담았다가 틱 뒤 게임 스레드에서 (디스패치 순번, 쓴 오브젝트, 넣은 순번) 순으로 실행합니다.
     * @details 넣기는 아무 스레드나 합니다 — **스레드 슬롯마다 자기 큐**에 넣어(`engine::getParallelScratchSlot`, 슬롯 잠금은 비우는 쪽과만 겨룬다) 투사체
     *          수천이 한 뮤텍스에 줄 서지 않습니다(유니티 `EntityCommandBuffer.ParallelWriter` 의 스레드별 체인 + sortKey 병합). 비우기는 한 스레드만
     *          하고, 슬롯마다 맞바꿔 모은 뒤 키로 정렬해 잠금 밖에서 실행합니다. 실행 중에 새로 넣은 일은 **다음** 비우기로 갑니다.
     *          순서: 디스패치(파도 · 레인 · 스테이지 — 장벽으로 나뉜 시간 순)가 먼저, 한 디스패치 안은 쓴 오브젝트 순(예전에는 워커 순이라 실행마다 달랐다),
     *          같은 오브젝트는 넣은 순서다. 틱 밖에서 넣은 일(디스패치 0)은 넣은 시간 순으로 맨 앞이다(넣은 순번이 전역 원자값이다).
     */
    class SW_API DeferredDelegateQueue
    {
    public:
        using Callback = Delegate<void()>;

        /** @brief 스레드 슬롯 수(`engine::getParallelScratchSlotCount`)만큼 큐를 잡고, 슬롯을 받지 못한 스레드가 쓸 넘침 큐 하나를 더합니다. */
        DeferredDelegateQueue();

        DeferredDelegateQueue( const DeferredDelegateQueue& )            = delete;
        DeferredDelegateQueue& operator=( const DeferredDelegateQueue& ) = delete;

        /** @brief 일을 넣습니다. 묶이지 않은 델리게이트는 버립니다. 아무 스레드에서나 부를 수 있습니다. */
        void push( Callback callback );

        /** @brief 지금까지 넣은 일을 넣은 순서대로 실행하고 비웁니다. 실행 중에 넣은 일은 다음 비우기로 갑니다. 한 스레드만 부릅니다. */
        void flush();

        /** @brief 넣은 일이 없는지 봅니다(잠금 아래). 비우는 중인 목록은 보지 않습니다. */
        bool isEmpty();

        /** @brief 실행하지 않고 전부 버립니다. */
        void clear();

    private:
        /** @brief 넣은 일 하나와 그 순서 키입니다. */
        struct KeyedCallback
        {
            uint32   _dispatchOrdinal{ 0 }; ///< 넣은 틱의 디스패치 순번(`TickThreadState::_dispatchOrdinal`, 틱 밖이면 0)
            uint64   _writerID{ 0 };        ///< 넣은 틱의 주인 오브젝트 id(틱 밖이면 0)
            uint64   _sequence{ 0 };        ///< 넣은 순번(전역)
            Callback _callback;
        };

        /** @brief 스레드 슬롯 수만큼의 큐를 만듭니다(만들 때 한 번 — 넣는 스레드가 도는 동안 목록이 바뀌지 않는다). */
        static vector<unique_ptr<LockedSwapQueue<KeyedCallback>>> makeSlotList();

        /// 스레드 슬롯마다의 큐(잠금이 옮겨지지 않게 하나씩 잡는다). 끝 칸이 넘침 큐다.
        /// const 목록이라 여러 스레드가 동시에 칸을 찾아도 읽기다 — 쓰기 접근(`operator[]`)은 경합 검출기가 한 칸씩이 아니라 목록 전체의 쓰기로 본다.
        const vector<unique_ptr<LockedSwapQueue<KeyedCallback>>> _listSlot;
        atomic<uint64>                                           _sequence;                     ///< 넣은 순번(전역 — 틱 밖 일의 시간 순)
        ThreadRole                                               _flushThread;                  ///< 비우는 스레드 역할 — 꺼내 온 목록은 drain 을 도는 스레드만 만진다(잠금이 아니라 분석용 능력)
        vector<KeyedCallback> _listTaken                         SW_GUARDED_BY( _flushThread ); ///< 슬롯 하나에서 맞바꿔 온 일(비우는 스레드만)
        vector<KeyedCallback> _listProcessing                    SW_GUARDED_BY( _flushThread ); ///< 비우는 중인 일(비우는 스레드만)
    };
} // namespace sw

namespace sw
{
    class GameObject;
    class GameObjectManager;

    /**
     * @struct TickThreadState
     * @brief 한 스레드가 지금 도는 틱의 상태입니다. 틱 디스패치가 항목을 도는 동안 채우고, 틱 밖에서는 모두 nullptr 입니다.
     */
    struct TickThreadState
    {
        const GameObject* _pTickingObject{ nullptr }; ///< 이 스레드가 지금 틱하는 오브젝트 — 그 오브젝트의 대기 칸에 잠금 없이 쓴다
        const GameObject* _pTickWriter{ nullptr };    ///< 지금 도는 틱의 주인 오브젝트 — 다른 오브젝트에 쓴 건의 순서 키
        /**
         * @brief 지금 도는 디스패치(파도 · 레인 · 스테이지 하나)의 단계 안 순번입니다 — 다른 오브젝트에 쓴 건의 둘째 순서 키(쓴 오브젝트 다음).
         * @details 한 오브젝트의 항목이 파도 · 스테이지마다 다른 스레드에서 돌 수 있어, 스레드 순번만으로는 같은 오브젝트가 앞뒤로 쓴 건의 선후를 가리지 못한다.
         *          디스패치는 장벽으로 나뉘므로 이 순번이 그 선후다(유니티 `EntityCommandBuffer` 의 sortKey 가 청크 순번인 자리).
         */
        uint32 _dispatchOrdinal{ 0 };
        /**
         * @brief 이 스레드의 대기 칸 목록입니다(`SceneTransformHierarchy::findPendingSlotList`). 틱 디스패치가 청크마다 한 번 채운다.
         * @details 세터가 칸 번호를 올릴 때마다 스레드 슬롯을 다시 묻지 않게 한다. nullptr 이면(슬롯을 못 받았거나 틱 청크 밖) 세터는 쓰기 큐로 간다.
         */
        vector<uint32>* _pPendingSlotList{ nullptr };
    };
} // namespace sw

namespace sw
{
    /** @class StructuralChangeBuffer @brief 틱 중 규칙(동결 · 지연 큐 · 비우는 순서)입니다. 파일 머리말 참고. */
    class SW_API StructuralChangeBuffer
    {
    public:
        using Callback = DeferredDelegateQueue::Callback;

        StructuralChangeBuffer();
        ~StructuralChangeBuffer() = default;

        StructuralChangeBuffer( const StructuralChangeBuffer& )            = delete;
        StructuralChangeBuffer& operator=( const StructuralChangeBuffer& ) = delete;

        /** @brief 컴포넌트 틱 중이면 true 입니다. 이때 구조 변경은 미뤄지고 트랜스폼은 읽기 전용입니다. 워커가 acquire 로 읽습니다. */
        bool isFrozen() const { return _bFrozen.load( std::memory_order_acquire ); }
        /** @brief 컴포넌트 틱을 시작합니다 — 계층 변경 미룸 표시를 지우고 얼립니다. 틱 디스패치만 부릅니다. */
        void freeze();
        /** @brief 컴포넌트 틱을 끝냅니다(동결 해제). 틱 디스패치만 부릅니다. 쌓인 일은 `drain` 이 비웁니다. */
        void thaw();

        /** @brief 틱 중의 구조 변경을 넣습니다(같은 오브젝트의 것은 부른 순서대로 돈다). `drain` 이 가장 먼저 돌립니다. 아무 스레드에서나 부를 수 있습니다. */
        void deferStructuralChange( Callback func );
        /**
         * @brief 틱 중의 계층 변경(attach · detach)을 구조 변경 큐에 넣고, 이번 단계에 계층 변경이 미뤄졌다고 적습니다.
         * @details 그 뒤로는 스테이지 경계의 트랜스폼 적용을 하지 않습니다(`hasDeferredHierarchyChange`) — 미룬 계층 변경보다 뒤에 부른 쓰기가
         *          그 변경보다 먼저 적용되면 `KeepWorld` 부착이 쓴 로컬 값을 다시 구해 덮는다.
         */
        void deferHierarchyChange( Callback func );
        /** @brief 이번 틱 단계(마지막 `freeze` 뒤)에 계층 변경이 미뤄졌으면 true 입니다. */
        bool hasDeferredHierarchyChange() const { return _bDeferredHierarchyChange.load( std::memory_order_relaxed ) != SW_FALSE; }
        /** @brief 병렬 틱이 끝난 뒤 게임 스레드에서 실행할 작업을 넣습니다. 구조 변경 · 트랜스폼 쓰기 적용 뒤에 돕니다. */
        void deferPostTick( Callback func );
        /** @brief 얼어 있으면 `deferPostTick` 으로 미루고, 아니면 바로 실행합니다. 묶이지 않은 델리게이트는 버립니다. */
        void executeOrDeferPostTick( Callback func );

        /**
         * @brief 틱이 남긴 것을 정해진 순서로 적용합니다(게임 스레드, 틱 밖) — 구조 변경 → 틱 중 트랜스폼 쓰기 → 틱 뒤 큐 → 병합 → 시작.
         * @details 순서는 이 함수 하나가 갖습니다(파일 머리말의 주의). 물리 앞 · 프레임 끝에 한 번씩 불립니다.
         */
        void flush( GameObjectManager& manager );

        /**
         * @brief 두 큐가 빌 때까지 `drain` 을 되풀이합니다(모듈 언로드 직전, 틱 밖).
         * @details `drain` 은 큐를 한 번 맞바꿔 돌므로 그 안의 콜백 · BeginPlay 가 다시 미룬 일은 남습니다. 모듈이 내려간 뒤 남은 람다가
         *          돌면 없는 코드로 뛰므로 여기서 끝까지 비웁니다. 상한에 닿으면(서로 다시 미루는 고리) 오류를 남기고 호출 없이 버립니다.
         */
        void flushUntilEmpty( GameObjectManager& manager );

        /** @brief 쌓인 일을 실행하지 않고 버립니다(매니저 clear). */
        void clear();

        /** @brief 이 스레드의 틱 상태입니다. 틱 디스패치가 항목 묶음마다 한 번 받아 채웁니다. */
        static TickThreadState& getThreadState();
        /** @brief 이 스레드가 지금 틱하는 오브젝트입니다. 틱 밖에서는 nullptr 입니다. */
        static const GameObject* getTickingObject();

    private:
        atomic<bool>          _bFrozen;                  ///< 컴포넌트 틱 중(`isFrozen`)
        atomic<uint8>         _bDeferredHierarchyChange; ///< 이번 단계에 계층 변경이 미뤄졌는지 — 그 뒤로는 스테이지 경계 적용을 하지 않는다
        DeferredDelegateQueue _structuralQueue;          ///< 틱이 미룬 구조 변경(컴포넌트 추가 · attach · detach · 태그 · 활성), 같은 오브젝트는 부른 순서
        DeferredDelegateQueue _postTickQueue;            ///< 틱이 미룬 스폰 · 데미지 · 태그(`deferPostTick`)
    };
} // namespace sw
