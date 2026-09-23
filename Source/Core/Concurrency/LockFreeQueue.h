/**
 * @file LockFreeQueue.h
 * @brief 고정 용량 lock-free 링 버퍼 큐입니다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"

namespace sw
{

    /**
     * @brief 단일 생산자 · 단일 소비자(SPSC)용 고정 용량 lock-free 링 버퍼입니다.
     * @tparam T 요소 타입
     * @tparam Capacity 용량(2의 거듭제곱)
     *
     * @warning **SPSC 전용입니다.** push 는 생산자 스레드 하나, pop 은 소비자 스레드 하나에서만 불러야 합니다.
     *          생산자나 소비자가 여럿이면 데이터 레이스가 생깁니다. MPMC(다중 생산자 · 다중 소비자)가 필요하면
     *          sw::ConcurrentQueue 를 쓰십시오.
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
        /** @brief 버퍼를 그대로 버립니다(잠금 없음). */
        ~LockFreeQueue() = default;

        /** @brief 복사해서 넣습니다. 가득 차 있으면 false 입니다. */
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

        /** @brief 이동해서 넣습니다. 가득 차 있으면 false 입니다. */
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

        /** @brief 맨 앞 요소를 꺼냅니다. 비어 있으면 false 입니다. */
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

        // SPSC 라 push(생산자)와 pop(소비자)이 서로 다른 스레드에서 동시에 이 버퍼를 만진다.
        // 그래서 레이스 탐지기가 붙은 sw::array 를 쓰지 않는다. 이유는 Core/Container/array.h 머리말에 있다.
        std::array<T, Capacity> _buffer{};
        // 초기값은 여기 한 곳에만 둔다. 예전에는 생성자에도 같은 0 이 적혀 있었다(AGENTS.md 의 "초기값은 한 곳에" 규칙).
        // 생성자 쪽 값이 이기므로, 헤더만 고치면 아무 효과 없이 조용히 넘어간다.
        // 생산자와 소비자가 서로의 캐시 라인을 무효화하지 않도록 둘을 따로 떨어뜨려 둔다.
        alignas( 64 ) atomic<uint32> _head{ 0 };
        alignas( 64 ) atomic<uint32> _tail{ 0 };
    };
} // namespace sw
