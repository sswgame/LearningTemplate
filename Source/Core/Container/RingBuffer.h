/**
 * @file RingBuffer.h
 * @brief 고정 용량 링 버퍼입니다. 가득 차면 가장 오래된 값을 덮어쓰고, 오래된 것부터 새것 순서로 순회합니다.
 * @details 최근 N 개의 기록(프레임 시간, 지연 표본, 입력 기록)을 담는 자리입니다. 용량은 2 의 거듭제곱이라 위치를 나머지 대신 마스크로 셉니다.
 *          틱 번호로 슬롯을 찾는 기록은 `TickRingBuffer`(키를 슬롯마다 적는다), 여러 스레드가 넣고 빼는 큐는 `ConcurrentQueue` 입니다.
 *          스레드 안전하지 않습니다(쓰는 쪽 하나).
 */
#pragma once
#include "Core/Container/array.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @class RingBuffer
     * @tparam T        요소 타입(기본 생성 가능)
     * @tparam Capacity 용량(2 의 거듭제곱)
     */
    template <typename T, uint32 Capacity>
    class RingBuffer
    {
        static_assert( Capacity > 0 && ( Capacity & ( Capacity - 1 ) ) == 0, "RingBuffer capacity must be a power of 2" );
        static constexpr uint32 kMask = Capacity - 1;

    public:
        /** @brief 오래된 것부터 새것 순서로 도는 반복자입니다. */
        template <typename TBuffer, typename TValue>
        class Iterator
        {
        public:
            Iterator( TBuffer* pBuffer, uint32 index )
                : _pBuffer{ pBuffer }
                , _index{ index }
            {
            }

            TValue&   operator*() const { return ( *_pBuffer )[_index]; }
            TValue*   operator->() const { return &( *_pBuffer )[_index]; }
            Iterator& operator++()
            {
                ++_index;
                return *this;
            }
            bool operator==( const Iterator& other ) const { return _index == other._index; }
            bool operator!=( const Iterator& other ) const { return _index != other._index; }

        private:
            TBuffer* _pBuffer;
            uint32   _index;
        };

        RingBuffer()
            : _arrItem{}
            , _head{ 0 }
            , _count{ 0 }
        {
        }

        /** @brief 새것으로 넣습니다. 가득 차 있으면 가장 오래된 값을 덮어씁니다. */
        void push( const T& item ) { _arrItem[acquireSlot()] = item; }
        void push( T&& item ) { _arrItem[acquireSlot()] = std::move( item ); }

        /** @brief 가장 오래된 값을 뺍니다. 비어 있으면 부르지 않습니다. */
        void popFront()
        {
            SW_ASSERT( _count > 0 );
            _head = ( _head + 1 ) & kMask;
            --_count;
        }

        /** @brief 넣은 값을 모두 잊습니다. 슬롯의 값은 다음 `push` 가 덮어씁니다. */
        void clear()
        {
            _head  = 0;
            _count = 0;
        }

        /** @brief @p index 번째 값입니다. 0 이 가장 오래된 값, `size() - 1` 이 가장 새 값입니다. */
        T& operator[]( uint32 index )
        {
            SW_ASSERT( index < _count );
            return _arrItem[( _head + index ) & kMask];
        }
        const T& operator[]( uint32 index ) const
        {
            SW_ASSERT( index < _count );
            return _arrItem[( _head + index ) & kMask];
        }
        T&       front() { return ( *this )[0]; }
        const T& front() const { return ( *this )[0]; }
        T&       back() { return ( *this )[_count - 1]; }
        const T& back() const { return ( *this )[_count - 1]; }

        uint32                  size() const { return _count; }
        bool                    empty() const { return _count == 0; }
        bool                    full() const { return _count == Capacity; }
        static constexpr uint32 capacity() { return Capacity; }

        Iterator<RingBuffer, T>             begin() { return { this, 0 }; }
        Iterator<RingBuffer, T>             end() { return { this, _count }; }
        Iterator<const RingBuffer, const T> begin() const { return { this, 0 }; }
        Iterator<const RingBuffer, const T> end() const { return { this, _count }; }

    private:
        /** @brief 새 값이 들어갈 슬롯을 돌려줍니다. 가득 차 있으면 가장 오래된 슬롯이며 시작을 한 칸 민다. */
        uint32 acquireSlot()
        {
            const uint32 slot = ( _head + _count ) & kMask;
            if ( _count == Capacity )
                _head = ( _head + 1 ) & kMask;
            else
                ++_count;
            return slot;
        }

        array<T, Capacity> _arrItem;
        uint32             _head;  ///< 가장 오래된 값의 슬롯
        uint32             _count; ///< 들어 있는 값의 수
    };
} // namespace sw
