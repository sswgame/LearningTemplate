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
                cap <<= 1;
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
         * @return 가득 차 있으면 false 이고, 버퍼를 늘리지 않습니다. 넘치는 일감을 어디로 보낼지는 호출하는 쪽이 정합니다
         *         (`TaskManager` 는 전역 큐로 보냅니다). 여기서 버퍼를 늘리면 소유 스레드가 버퍼를 바꾸는 동안 훔치는 쪽이 옛 버퍼를
         *         읽게 되므로, 그 복잡도를 떠안는 대신 상한을 알려 줍니다.
         */
        bool push( T item )
        {
            uint64 b = _bottom.load( std::memory_order_relaxed );
            uint64 t = _top.load( std::memory_order_acquire );

            if ( b - t > _capacityMask )
            {
                // 가득 찼다. 일부러 늘리지 않는다. 넘치는 일감을 어디로 보낼지는 호출하는 쪽이 안다.
                return false;
            }

            _pBuffer[b & _capacityMask].store( item, std::memory_order_relaxed );

            std::atomic_thread_fence( std::memory_order_release );
            _bottom.store( b + 1, std::memory_order_relaxed );
            return true;
        }

        /**
         * @brief 뒤쪽에서 꺼냅니다(LIFO). **소유 스레드만** 부릅니다.
         * @details 마지막 하나는 훔치는 쪽과 경쟁합니다. CAS 로 승자를 정하고, 지면 false 를 반환합니다.
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
                        // 다른 스레드가 먼저 훔쳐 갔다
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
         * @brief 앞쪽에서 꺼냅니다(FIFO). **다른 스레드가** 부릅니다.
         * @details 항목을 먼저 읽고 CAS 로 확정합니다. CAS 에서 지면 그 항목은 다른 쪽이 가져간 것이므로 버립니다.
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
        // `_top` 은 훔치는 스레드들이, `_bottom` 은 소유 스레드가 계속 쓴다. 둘이 한 캐시 라인에 있으면 서로의 라인을
        // 무효화해서(false sharing) 훔치기가 없어도 느려진다. `LockFreeQueue` · `ConcurrentQueue` 는 같은 이유로 이미 떼어
        // 놓았는데 여기만 붙어 있었다.
        alignas( 64 ) atomic<uint64> _top;
        alignas( 64 ) atomic<uint64> _bottom;

        atomic<T>* _pBuffer;
        uint64     _capacityMask;
    };
} // namespace sw
