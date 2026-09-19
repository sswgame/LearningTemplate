/**
 * @file LockFreeQueue.h
 * @brief 고정 용량 lock-free 링 버퍼 큐
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"

namespace sw
{

    /**
     * @brief 단일 생산자/단일 소비자(SPSC)에 최적화된 고정 용량 lock-free 링 버퍼
     * @tparam T 요소 타입
     * @tparam Capacity 용량 (2의 거듭제곱)
     *
     * @warning **SPSC 전용** — 생산자(push) 와 소비자(pop) 각각 한 스레드에서만 호출해야 합니다.
     *          다중 생산자 또는 다중 소비자 시나리오에서는 **데이터 경쟁(Data Race)**이 발생합니다.
     *          MPMC(다중 생산자-다중 소비자)가 필요하다면 sw::ConcurrentQueue 를 사용하세요.
     */
    // ------------------------------------------------------------------------------
    // 1) LockFreeQueue — SPSC 링 버퍼. push 한 스레드, pop 한 스레드
    //    MPMC 는 ConcurrentQueue
    // ------------------------------------------------------------------------------
    /** @brief 고정 용량 SPSC lock-free 링 버퍼입니다. */
    template <typename T, uint32 Capacity = 1024>
    class LockFreeQueue
    {
        static_assert( ( Capacity & ( Capacity - 1 ) ) == 0, "Capacity must be a power of 2!" );

    public:
        /** @brief 버퍼만 버리며 락은 없습니다. */
        ~LockFreeQueue() = default;

        /** @brief 복사로 요소를 넣습니다. 가득 차면 false. */
        bool push( const T& item )
        {
            const uint32 currentTail = _tail.load( std::memory_order_relaxed );
            const uint32 currentHead = _head.load( std::memory_order_acquire );

            if ( currentTail - currentHead >= Capacity )
                return false;

            _buffer[currentTail & kMask] = item;
            _tail.store( currentTail + 1, std::memory_order_release );
            return true;
        }

        /** @brief 이동으로 요소를 넣습니다. 가득 차면 false. */
        bool push( T&& item )
        {
            const uint32 currentTail = _tail.load( std::memory_order_relaxed );
            const uint32 currentHead = _head.load( std::memory_order_acquire );

            if ( currentTail - currentHead >= Capacity )
                return false;

            _buffer[currentTail & kMask] = std::move( item );
            _tail.store( currentTail + 1, std::memory_order_release );
            return true;
        }

        /** @brief 앞에서 요소를 꺼냅니다. 비어 있으면 false. */
        bool pop( T& outItem )
        {
            const uint32 currentHead = _head.load( std::memory_order_relaxed );
            const uint32 currentTail = _tail.load( std::memory_order_acquire );

            if ( currentHead == currentTail )
                return false;

            outItem = std::move( _buffer[currentHead & kMask] );
            _head.store( currentHead + 1, std::memory_order_release );
            return true;
        }

        /** @brief 비어 있는지 반환합니다. */
        bool empty() const { return _head.load( std::memory_order_relaxed ) == _tail.load( std::memory_order_relaxed ); }

        /** @brief 현재 요소 수를 반환합니다. */
        uint32 size() const
        {
            const uint32 head = _head.load( std::memory_order_relaxed );
            const uint32 tail = _tail.load( std::memory_order_relaxed );
            return ( tail - head );
        }

        /** @brief 고정 용량을 반환합니다. */
        constexpr uint32 capacity() const { return Capacity; }

    private:
        static constexpr uint32 kMask = Capacity - 1;

        // SPSC 라 push(생산자)와 pop(소비자)이 **서로 다른 스레드에서 동시에** 이 버퍼를 만진다.
        // 그래서 레이스 탐지기가 붙은 sw::array 를 쓰지 않는다 — 이유는 Core/Container/array.h 머리말.
        std::array<T, Capacity> _buffer{};
        // 값은 여기 한 곳에만 둔다 — 예전에는 생성자에도 같은 0 이 적혀 있었다(AGENTS 의
        // "초기값의 집은 하나" 규칙). 생성자가 이기므로 헤더만 고치면 조용히 안 먹는다.
        // 소유 스레드와 소비 스레드가 서로의 캐시라인을 무효화하지 않도록 따로 앉힌다.
        alignas( 64 ) atomic<uint32> _head{ 0 };
        alignas( 64 ) atomic<uint32> _tail{ 0 };
    };
} // namespace sw
