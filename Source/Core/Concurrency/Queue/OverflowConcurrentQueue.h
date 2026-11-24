/**
 * @file OverflowConcurrentQueue.h
 * @brief 막히지 않는 MPMC 큐입니다 — 평소에는 고정 용량 링(`ConcurrentQueue`)이고, 링이 차면 잠금 넘침 목록에 쌓습니다.
 * @details 태스크 전역 큐 · 메인 스레드 큐의 몸통이다. 링만 쓰면 가득 찼을 때 넣는 쪽이 꺼내 줄 쪽을 기다려야 했다 — 워커가 하나(웹 · 저사양)이거나
 *          모두가 넣는 중이면 아무도 꺼내지 않았고, 넣는 워커가 그 자리에서 실행하는 우회도 겹침 깊이 상한(16)에 닿으면 다시 기다렸다.
 *          - **평소 경로는 링 그대로다**: 넣기는 넘침 수(원자 하나) 확인 뒤 링 칸 하나의 비교 교환, 꺼내기는 링이 비었을 때만 넘침 수를 본다.
 *          - **넘침은 찬 뒤의 드문 길이다**: 잠금 + 목록. 넘침이 남아 있는 동안은 새 항목도 넘침에 쌓아 링이 먼저 비고 넘침이 그 뒤를 잇는다(늦게 넣은 것이 앞지르지 않게).
 *          - 잠금 없는 블록 연결 큐(언리얼 Tasks · moodycamel)는 블록 회수(꺼내는 쪽이 읽는 블록을 다른 쪽이 해제)를 새로 들이고 평소 경로를 바꿔 쓰지 않는다.
 *          우선순위 안의 순서는 원래 보장이 아니라(워커 데크 · 훔치기) 넘침 경계의 순서 차이는 계약을 바꾸지 않는다.
 */
#pragma once
#include "Core/Concurrency/Queue/ConcurrentQueue.h"
#include "Core/Concurrency/ScopedLock.h"
#include "Core/Concurrency/ThreadSafety.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @class OverflowConcurrentQueue
     * @tparam T        원소(복사 가능한 작은 값 — 태스크 항목 · 노드 포인터)
     * @tparam Capacity 링 용량(2 의 거듭제곱)
     */
    template <typename T, uint32 Capacity>
    class OverflowConcurrentQueue
    {
    public:
        OverflowConcurrentQueue()  = default;
        ~OverflowConcurrentQueue() = default;

        OverflowConcurrentQueue( const OverflowConcurrentQueue& )            = delete;
        OverflowConcurrentQueue& operator=( const OverflowConcurrentQueue& ) = delete;

        /** @brief 항목 하나를 넣습니다. 실패하지 않고 기다리지도 않습니다(링이 차면 넘침 목록에 쌓는다). */
        void enqueue( const T& item )
        {
            if ( _overflowCount.load( std::memory_order_acquire ) == 0 && _ring.enqueue( item ) )
                return;
            ScopedLock<mutex> lock{ _overflowMutex };
            _listOverflow.push_back( item );
            _overflowCount.store( static_cast<uint32>( _listOverflow.size() - _overflowHead ), std::memory_order_release );
        }

        /** @brief 항목 하나를 꺼냅니다. 링을 먼저 보고, 링이 비었을 때만 넘침 목록을 봅니다. 없으면 false 입니다. */
        bool dequeue( T& outItem )
        {
            if ( _ring.dequeue( outItem ) )
                return true;
            if ( _overflowCount.load( std::memory_order_acquire ) == 0 )
                return false;
            ScopedLock<mutex> lock{ _overflowMutex };
            if ( _overflowHead == _listOverflow.size() )
                return false;
            outItem = _listOverflow[_overflowHead];
            ++_overflowHead;
            if ( _overflowHead == _listOverflow.size() )
            {
                _listOverflow.clear(); // 저장소는 남겨 다음 넘침이 다시 할당하지 않게 한다
                _overflowHead = 0;
            }
            _overflowCount.store( static_cast<uint32>( _listOverflow.size() - _overflowHead ), std::memory_order_release );
            return true;
        }

        /** @brief 링과 넘침 목록이 모두 비었으면 true 입니다(다른 스레드가 넣고 꺼내는 중이면 잠깐의 값이다). */
        [[nodiscard]] bool empty() const { return _ring.empty() && _overflowCount.load( std::memory_order_acquire ) == 0; }

        /** @brief 링과 넘침 목록에 든 항목 수의 어림값입니다(다른 스레드가 넣고 꺼내는 중이면 잠깐의 값). */
        [[nodiscard]] uint32 getQueuedCount() const { return _ring.size() + _overflowCount.load( std::memory_order_acquire ); }

        /** @brief 링 용량입니다. 넘치면 넘침 목록이 받는다. */
        [[nodiscard]] static constexpr uint32 getRingCapacity() { return Capacity; }

    private:
        friend struct OverflowConcurrentQueueTestAccess; ///< 테스트만 안을 본다 — 배포 공개 API 에 진단 조회를 두지 않는다
        ConcurrentQueue<T, Capacity> _ring;
        atomic<uint32>               _overflowCount{ 0 }; ///< 넘침 목록에 남은 수 — 0 이면 넣기 · 꺼내기가 잠금을 보지 않는다
        mutable mutex                _overflowMutex;
        vector<T> _listOverflow      SW_GUARDED_BY( _overflowMutex );
        size_t _overflowHead         SW_GUARDED_BY( _overflowMutex ){ 0 }; ///< 넘침 목록에서 다음에 꺼낼 자리
    };
} // namespace sw
