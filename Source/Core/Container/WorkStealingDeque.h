/**
 * @file WorkStealingDeque.h
 * @brief Chase-Lev 방식의 lock-free 작업 훔치기(work-stealing) 데크입니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /**
     * @class WorkStealingDeque
     * @brief 소유 스레드는 뒤쪽에서 LIFO(push/pop)로, 다른 스레드는 앞쪽에서 FIFO(steal)로 접근하는 lock-free 데크입니다.
     */
    template <typename T>
    class WorkStealingDeque
    {
    public:
        /** @brief 요청한 용량이 이보다 작아도 최소 이만큼은 잡습니다. */
        static constexpr size_t kMinCapacity = 8;

        /** @brief 용량을 2의 거듭제곱으로 올려 버퍼를 잡습니다. */
        explicit WorkStealingDeque( size_t capacity = constant::kDefaultDequeCapacity )
            : _top{ 0 }
            , _bottom{ 0 }
            , _pBuffer{ nullptr }
            , _capacityMask{ 0 }
        {
            size_t cap = kMinCapacity;
            while ( cap < capacity )
            {
                cap <<= 1;
            }
            capacity = cap;

            _capacityMask = capacity - 1;
            _pBuffer      = sw_new atomic<T>[capacity];
        }

        /** @brief 버퍼를 해제합니다. 남은 항목은 호출하는 쪽이 이미 비웠다고 가정합니다. */
        ~WorkStealingDeque()
        {
            sw_delete_array( _pBuffer );
        }

        /** @brief 복사를 금지합니다. */
        WorkStealingDeque( const WorkStealingDeque& ) = delete;
        /** @brief 복사 대입을 금지합니다. */
        WorkStealingDeque& operator=( const WorkStealingDeque& ) = delete;

        /**
         * @brief 뒤쪽에 넣습니다(LIFO). **소유 스레드만** 부릅니다.
         * @return 가득 차 있으면 false 이고, 버퍼를 늘리지 않습니다. 넘치는 작업을 어디로 보낼지는 호출하는 쪽이 정합니다
         *         (`TaskManager` 는 전역 큐로 보냅니다). 여기서 버퍼를 늘리면 소유 스레드가 버퍼를 바꾸는 동안 훔치는 쪽이 옛 버퍼를
         *         읽게 되므로, 그 복잡도를 떠안는 대신 상한을 알려 줍니다.
         */
        bool push( T item )
        {
            uint64 bottom = _bottom.load( std::memory_order_relaxed );
            uint64 top    = _top.load( std::memory_order_acquire );

            if ( bottom - top > _capacityMask )
            {
                // 가득 찼다. 일부러 늘리지 않는다. 넘치는 작업을 어디로 보낼지는 호출하는 쪽이 안다.
                return false;
            }

            _pBuffer[bottom & _capacityMask].store( item, std::memory_order_relaxed );

            std::atomic_thread_fence( std::memory_order_release );
            _bottom.store( bottom + 1, std::memory_order_relaxed );
            return true;
        }

        /**
         * @brief 뒤쪽에서 꺼냅니다(LIFO). **소유 스레드만** 부릅니다.
         * @details 마지막 하나는 훔치는 쪽과 경쟁합니다. CAS 로 승자를 정하고, 지면 false 를 반환합니다.
         *          비어 있으면 `_bottom` 을 내리고 펜스를 치기 **전에** 돌아옵니다. `_top` 은 오르기만 하므로 낡게 읽은 값은 실제보다 작고,
         *          그 값으로도 비었다면(`top >= bottom`) 실제로 비었습니다(`_bottom` 은 소유 스레드만 바꾼다). 워커는 큐를 볼 때마다 자기
         *          데크부터 pop 하므로, 빈 데크에서 seq_cst 펜스를 치지 않는 것이 할 일 없는 워커 · 대기자의 바닥 비용을 줄입니다.
         */
        bool pop( T& outItem )
        {
            uint64 bottom = _bottom.load( std::memory_order_relaxed );
            if ( _top.load( std::memory_order_relaxed ) >= bottom )
                return false;

            bottom -= 1;
            _bottom.store( bottom, std::memory_order_relaxed );

            std::atomic_thread_fence( std::memory_order_seq_cst );

            uint64 top = _top.load( std::memory_order_relaxed );

            if ( top <= bottom )
            {
                outItem = _pBuffer[bottom & _capacityMask].load( std::memory_order_relaxed );
                if ( top == bottom )
                {
                    // 마지막 남은 하나
                    if ( _top.compare_exchange_strong( top, top + 1, std::memory_order_seq_cst, std::memory_order_relaxed ) == false )
                    {
                        // 다른 스레드가 먼저 훔쳐 갔다
                        _bottom.store( bottom + 1, std::memory_order_relaxed );
                        return false;
                    }
                    _bottom.store( bottom + 1, std::memory_order_relaxed );
                }
                return true;
            }
            else
            {
                _bottom.store( bottom + 1, std::memory_order_relaxed );
                return false;
            }
        }

        /**
         * @brief 앞쪽에서 꺼냅니다(FIFO). **다른 스레드가** 부릅니다.
         * @details 항목을 먼저 읽고 CAS 로 확정합니다. CAS 에서 지면 그 항목은 다른 쪽이 가져간 것이므로 버립니다.
         *          비어 보이면 seq_cst 펜스 **전에** relaxed 읽기 둘로 돌아옵니다. 할 일 없는 워커는 데크를 한 바퀴(워커 수만큼) 훑으므로
         *          빈 데크마다 펜스를 치면 그것이 훑기 비용의 대부분입니다. 낡은 `_bottom` 때문에 방금 넣은 항목을 못 볼 수 있지만, CAS 에서
         *          진 경우처럼 실패는 "이번에는 못 가져왔다" 일 뿐이고(원래도 펜스 뒤의 `_bottom` 읽기가 같은 순간을 본다), 잠들기 전의
         *          마지막 확인은 부르는 쪽(`TaskManager::workerLoop`)이 유휴 비트를 올린 뒤 친 seq_cst 펜스 뒤에서 합니다.
         */
        bool steal( T& outItem )
        {
            const uint64 observedTop    = _top.load( std::memory_order_relaxed );
            const uint64 observedBottom = _bottom.load( std::memory_order_relaxed );
            if ( observedTop >= observedBottom )
                return false;

            uint64 top = _top.load( std::memory_order_acquire );
            std::atomic_thread_fence( std::memory_order_seq_cst );
            uint64 bottom = _bottom.load( std::memory_order_acquire );

            if ( top < bottom )
            {
                outItem = _pBuffer[top & _capacityMask].load( std::memory_order_relaxed );
                if ( _top.compare_exchange_strong( top, top + 1, std::memory_order_seq_cst, std::memory_order_relaxed ) == false )
                    return false;
                return true;
            }
            return false;
        }

    private:
        // `_top` 은 훔치는 스레드들이, `_bottom` 은 소유 스레드가 계속 쓴다. 둘이 한 캐시 라인에 있으면 서로의 라인을
        // 무효화해서(false sharing) 훔치기가 없어도 느려진다. `SpscQueue` · `ConcurrentQueue` 도 같은 이유로 떼어 놓는다.
        alignas( 64 ) atomic<uint64> _top;
        alignas( 64 ) atomic<uint64> _bottom;

        atomic<T>* _pBuffer;
        uint64     _capacityMask;
    };
} // namespace sw
