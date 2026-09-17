/**
 * @file WorkStealingDeque.h
 * @brief Chase-Lev Lock-Free Work-Stealing Deque
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
     * @brief 소유자 스레드는 LIFO(push/pop), 다른 스레드는 FIFO(steal)로 접근하는 Lock-Free Deque
     */
    template <typename T>
    class WorkStealingDeque
    {
    public:
        /** @brief 요청 용량이 이보다 작아도 이만큼은 잡습니다. */
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
                cap <<= 1;
            capacity = cap;

            _capacityMask = capacity - 1;
            _pBuffer      = sw_new atomic<T>[capacity];
        }

        /** @brief 버퍼를 돌려줍니다. 남은 항목은 호출자가 이미 비웠다고 봅니다. */
        ~WorkStealingDeque()
        {
            sw_delete_array( _pBuffer );
        }

        /** @brief 복사를 금지합니다. */
        WorkStealingDeque( const WorkStealingDeque& ) = delete;
        /** @brief 복사 대입을 금지합니다. */
        WorkStealingDeque& operator=( const WorkStealingDeque& ) = delete;

        /**
         * @brief **소유 스레드만** 부릅니다. 뒤쪽에 넣습니다 (LIFO).
         * @return 가득 차면 false — 늘리지 않습니다. 넘치는 일감을 어디로 보낼지는 호출부가 정한다
         *         (`TaskManager` 는 전역 큐로 흘려보낸다). 여기서 늘리면 소유 스레드가 버퍼를 바꾸는
         *         동안 훔치는 쪽이 옛 버퍼를 읽으므로, 그 복잡도를 지지 않는 대신 상한을 알린다.
         */
        bool push( T item )
        {
            uint64 b = _bottom.load( std::memory_order_relaxed );
            uint64 t = _top.load( std::memory_order_acquire );

            if ( b - t > _capacityMask )
            {
                // 가득 찼다. **일부러 늘리지 않는다** — 호출부가 넘치는 일감의 갈 곳을 안다.
                return false;
            }

            _pBuffer[b & _capacityMask].store( item, std::memory_order_relaxed );

            std::atomic_thread_fence( std::memory_order_release );
            _bottom.store( b + 1, std::memory_order_relaxed );
            return true;
        }

        /**
         * @brief **소유 스레드만** 부릅니다. 뒤쪽에서 꺼냅니다 (LIFO).
         * @details 마지막 하나는 훔치는 쪽과 다툰다 — CAS 로 정하고, 지면 false 를 돌려준다.
         */
        bool pop( T& outItem )
        {
            uint64 b = _bottom.load( std::memory_order_relaxed );
            if ( b == 0 )
                return false;

            b -= 1;
            _bottom.store( b, std::memory_order_relaxed );

            std::atomic_thread_fence( std::memory_order_seq_cst );

            uint64 t = _top.load( std::memory_order_relaxed );

            if ( t <= b )
            {
                outItem = _pBuffer[b & _capacityMask].load( std::memory_order_relaxed );
                if ( t == b )
                {
                    // 마지막 남은 하나
                    if ( _top.compare_exchange_strong( t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed ) == false )
                    {
                        // steal 당함
                        _bottom.store( b + 1, std::memory_order_relaxed );
                        return false;
                    }
                    _bottom.store( b + 1, std::memory_order_relaxed );
                }
                return true;
            }
            else
            {
                _bottom.store( b + 1, std::memory_order_relaxed );
                return false;
            }
        }

        /**
         * @brief **다른 스레드가** 부릅니다. 앞쪽에서 꺼냅니다 (FIFO).
         * @details 항목을 먼저 읽고 CAS 로 확정한다 — CAS 가 지면 그 항목은 남이 가져간 것이므로 버린다.
         */
        bool steal( T& outItem )
        {
            uint64 t = _top.load( std::memory_order_acquire );
            std::atomic_thread_fence( std::memory_order_seq_cst );
            uint64 b = _bottom.load( std::memory_order_acquire );

            if ( t < b )
            {
                outItem = _pBuffer[t & _capacityMask].load( std::memory_order_relaxed );
                if ( _top.compare_exchange_strong( t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed ) == false )
                    return false;
                return true;
            }
            return false;
        }

    private:
        // `_top` 은 훔치는 스레드들이, `_bottom` 은 소유 스레드가 계속 쓴다. 한 캐시라인에 같이 앉으면
        // 서로의 라인을 무효화해(false sharing) 훔치기가 없어도 느려진다 — `LockFreeQueue` ·
        // `ConcurrentQueue` 가 같은 이유로 이미 떼어 놓았는데 여기만 붙어 있었다.
        alignas( 64 ) atomic<uint64> _top;
        alignas( 64 ) atomic<uint64> _bottom;

        atomic<T>* _pBuffer;
        uint64     _capacityMask;
    };
} // namespace sw
