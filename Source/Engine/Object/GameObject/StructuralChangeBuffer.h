/**
 * @file StructuralChangeBuffer.h
 * @brief 틱 중 규칙 — 구조 동결 플래그, 틱이 미룬 구조 변경 · 틱 뒤 작업의 큐, 그것을 비우는 순서, 지금 틱하는 오브젝트(스레드별)입니다.
 * @details 유니티 DOTS `EntityCommandBuffer` 의 자리입니다 — 병렬 틱(잡)이 구조 변경을 기록하고, 정해진 동기화 지점(`drain`)에서 게임 스레드가
 *          부른 순서대로 재생합니다. `GameObjectManager` 가 소유하고, 게임이 부르는 API(`executeOrDeferPostTick` · `isStructuralMutationFrozen` …)는
 *          매니저가 같은 이름으로 전달합니다.
 *
 *          **플래그는 하나입니다.** 구조 동결과 트랜스폼 읽기 전용은 같은 구간이라 `isFrozen` 하나가 둘 다 답합니다. 얼리고 푸는 것은 틱
 *          디스패치(`SceneTickScheduler`)만 합니다.
 *
 *          **비우는 순서는 `drain` 한 함수가 갖습니다** — 구조 변경(부른 순서) → 틱 중 트랜스폼 쓰기 → 틱 뒤 큐 → 병합 → 시작. 주의: 큐를 종류마다
 *          나누거나 각 단위가 자기 큐를 자기 시점에 비우게 하면, 틱 안에서 씬 컴포넌트를 붙이고(미뤄짐) 이어 부모에 붙일 때 부착이 먼저 돌아
 *          붙일 씬 컴포넌트가 없고, 쓰기가 미룬 `KeepWorld` 부착보다 먼저 적용되어 부착이 로컬 값을 다시 구해 덮는다.
 *
 *          스레드별 상태(지금 틱하는 오브젝트 · 쓰기의 주인)는 Engine 의 TU 하나에 있습니다. 헤더의 `inline thread_local` 로 옮기면 모듈마다
 *          칸이 생겨, 게임 모듈이 인스턴스화한 세터가 엔진이 채운 칸을 보지 못합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"

#include "Engine/Object/GameObject/DeferredDelegateQueue.h"

namespace sw
{
    class GameObject;
    class GameObjectManager;

    /**
     * @struct TickThreadState
     * @brief 한 스레드가 지금 도는 틱의 상태입니다. 틱 디스패치가 항목을 도는 동안 채우고, 틱 밖에서는 둘 다 nullptr 입니다.
     */
    struct TickThreadState
    {
        const GameObject* _pTickingObject{ nullptr }; ///< 이 스레드가 지금 틱하는 오브젝트 — 그 오브젝트의 대기 칸에 잠금 없이 쓴다
        const GameObject* _pTickWriter{ nullptr };    ///< 지금 도는 틱의 주인 오브젝트 — 다른 오브젝트에 쓴 건의 순서 키
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

        /** @brief 틱 중의 구조 변경을 부른 순서대로 넣습니다. `drain` 이 가장 먼저 돌립니다. 아무 스레드에서나 부를 수 있습니다. */
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
        void drain( GameObjectManager& manager );

        /** @brief 쌓인 일을 실행하지 않고 버립니다(매니저 clear). */
        void clear();

        /** @brief 이 스레드의 틱 상태입니다. 틱 디스패치가 항목 묶음마다 한 번 받아 채웁니다. */
        static TickThreadState& getThreadState();
        /** @brief 이 스레드가 지금 틱하는 오브젝트입니다. 틱 밖에서는 nullptr 입니다. */
        static const GameObject* getTickingObject();
        /** @brief 이 스레드에서 지금 도는 틱의 주인 오브젝트입니다. 틱 밖에서는 nullptr 입니다. */
        static const GameObject* getTickWriter();

    private:
        atomic<bool>          _bFrozen;                  ///< 컴포넌트 틱 중(`isFrozen`)
        atomic<uint8>         _bDeferredHierarchyChange; ///< 이번 단계에 계층 변경이 미뤄졌는지 — 그 뒤로는 스테이지 경계 적용을 하지 않는다
        DeferredDelegateQueue _structuralQueue;          ///< 틱이 미룬 구조 변경(컴포넌트 추가 · attach · detach · 태그 · 활성), 부른 순서
        DeferredDelegateQueue _postTickQueue;            ///< 틱이 미룬 스폰 · 데미지 · 태그(`deferPostTick`)
    };
} // namespace sw
