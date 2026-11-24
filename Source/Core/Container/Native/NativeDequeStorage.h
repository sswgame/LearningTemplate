/**
 * @file NativeDequeStorage.h
 * @brief sw::deque 의 자체 저장입니다(기본 구성) — 2 의 거듭제곱 연속 링. 쓰는 쪽은 `Core/Container/deque.h` 를 include 합니다.
 * @details MSVC STL 의 std::deque 블록은 16 B 라 8 B 넘는 원소마다 힙 할당이 하나씩 붙는다(감사 C-9). 링은 용량이 찬 뒤로는 할당하지 않는다.
 *          번호 검사 · 레이스 검사는 부르는 쪽(`sw::deque`)이 한다.
 */
#pragma once
#include "Core/Container/BitwiseRelocatable.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

namespace sw::internal
{
    /** @brief 앞에서 센 번호로 원소를 주는 링입니다. 검사하지 않습니다. */
    template <typename T, typename Allocator>
    class DequeStorage
    {
    public:
        using AllocTraits = std::allocator_traits<Allocator>;

        explicit DequeStorage( const Allocator& alloc ) noexcept
            : _allocator{ alloc }
        {
        }
        DequeStorage( const DequeStorage& other )
            : _allocator{ AllocTraits::select_on_container_copy_construction( other._allocator ) }
        {
            copyFrom( other );
        }
        DequeStorage( const DequeStorage& other, const Allocator& alloc )
            : _allocator{ alloc }
        {
            copyFrom( other );
        }
        DequeStorage( DequeStorage&& other ) noexcept
            : _allocator{ std::move( other._allocator ) }
        {
            stealFrom( other );
        }
        ~DequeStorage() { releaseStorage(); }

        DequeStorage& operator=( const DequeStorage& other )
        {
            if ( this != &other )
            {
                clear();
                copyFrom( other );
            }
            return *this;
        }
        DequeStorage& operator=( DequeStorage&& other ) noexcept
        {
            if ( this != &other )
            {
                releaseStorage();
                _allocator = std::move( other._allocator );
                stealFrom( other );
            }
            return *this;
        }

        const Allocator& get_allocator() const noexcept { return _allocator; }
        SW_INLINE size_t size() const noexcept { return _size; }
        SW_INLINE size_t capacity() const noexcept { return _capacity; }

        /** @brief 앞에서 @p index 번째 슬롯입니다(끝 너머 빈 슬롯도 준다). 용량은 2 의 거듭제곱이라 나머지 대신 마스크다. */
        SW_INLINE T*       slotAt( size_t index ) { return _pData + ( ( _head + index ) & ( _capacity - 1 ) ); }
        SW_INLINE const T* slotAt( size_t index ) const { return _pData + ( ( _head + index ) & ( _capacity - 1 ) ); }

        void reserve( size_t count )
        {
            if ( count > _capacity )
                growTo( count );
        }

        void shrink_to_fit()
        {
            if ( _size == 0 )
            {
                releaseStorage();
                return;
            }
            if ( roundUpCapacity( _size ) < _capacity )
                moveToNewBuffer( roundUpCapacity( _size ) );
        }

        void clear() noexcept
        {
            for ( size_t index = 0; index < _size; ++index )
                AllocTraits::destroy( _allocator, slotAt( index ) );
            _size = 0;
            _head = 0;
        }

        template <class... Args>
        T& emplaceBack( Args&&... args )
        {
            if ( _size == _capacity )
            {
                T value( std::forward<Args>( args )... ); // 인자가 이 줄의 원소일 수 있다 — 키우기 전에 뜬다
                growTo( _size + 1 );
                T* pSlot = slotAt( _size );
                AllocTraits::construct( _allocator, pSlot, std::move( value ) );
                ++_size;
                return *pSlot;
            }
            T* pSlot = slotAt( _size );
            AllocTraits::construct( _allocator, pSlot, std::forward<Args>( args )... );
            ++_size;
            return *pSlot;
        }

        template <class... Args>
        T& emplaceFront( Args&&... args )
        {
            if ( _size == _capacity )
            {
                T value( std::forward<Args>( args )... );
                growTo( _size + 1 );
                _head    = ( _head - 1 ) & ( _capacity - 1 );
                T* pSlot = _pData + _head;
                AllocTraits::construct( _allocator, pSlot, std::move( value ) );
                ++_size;
                return *pSlot;
            }
            _head    = ( _head - 1 ) & ( _capacity - 1 );
            T* pSlot = _pData + _head;
            AllocTraits::construct( _allocator, pSlot, std::forward<Args>( args )... );
            ++_size;
            return *pSlot;
        }

        void popBack()
        {
            --_size;
            AllocTraits::destroy( _allocator, slotAt( _size ) );
        }

        void popFront()
        {
            AllocTraits::destroy( _allocator, _pData + _head );
            _head = ( _head + 1 ) & ( _capacity - 1 );
            --_size;
        }

        /** @brief [@p index, @p index + @p count) 를 지웁니다. 짧은 쪽 원소를 당긴다. */
        void erase( size_t index, size_t count )
        {
            const size_t tailCount = _size - index - count;
            if ( index < tailCount )
            {
                // 앞쪽이 짧다 — 앞 원소를 뒤로 당기고 앞에서 지운다(앞에서 빼는 FIFO 는 옮김이 0).
                for ( size_t moved = index; moved > 0; --moved )
                    *slotAt( moved - 1 + count ) = std::move( *slotAt( moved - 1 ) );
                for ( size_t removed = 0; removed < count; ++removed )
                    AllocTraits::destroy( _allocator, slotAt( removed ) );
                _head = ( _head + count ) & ( _capacity - 1 );
            }
            else
            {
                for ( size_t moved = index; moved < index + tailCount; ++moved )
                    *slotAt( moved ) = std::move( *slotAt( moved + count ) );
                for ( size_t removed = _size - count; removed < _size; ++removed )
                    AllocTraits::destroy( _allocator, slotAt( removed ) );
            }
            _size -= count;
        }

        void resize( size_t count )
        {
            reserve( count );
            while ( _size > count )
                AllocTraits::destroy( _allocator, slotAt( --_size ) );
            for ( ; _size < count; ++_size )
                AllocTraits::construct( _allocator, slotAt( _size ) );
        }

        void resize( size_t count, const T& value )
        {
            reserve( count );
            while ( _size > count )
                AllocTraits::destroy( _allocator, slotAt( --_size ) );
            for ( ; _size < count; ++_size )
                AllocTraits::construct( _allocator, slotAt( _size ), value );
        }

        /**
         * @brief [@p index, @p index + @p count) 자리를 비우고 @p source( i ) 로 채웁니다(`index <= size`, `count > 0`). 뒤쪽 원소를 한 번만 민다.
         * @details 살아 있는 슬롯은 대입, 끝 너머 빈 슬롯은 생성한다(`vector::insert( pos, count, value )` 와 같은 규칙).
         */
        template <typename TSource>
        void insertGap( size_t index, size_t count, TSource&& source )
        {
            if ( _size + count > _capacity )
                growTo( _size + count );

            const size_t tailCount = _size - index;
            for ( size_t movedCount = 0; movedCount < tailCount; ++movedCount )
            {
                const size_t fromIndex = _size - 1 - movedCount;
                const size_t toIndex   = fromIndex + count;
                if ( toIndex >= _size )
                    AllocTraits::construct( _allocator, slotAt( toIndex ), std::move( *slotAt( fromIndex ) ) );
                else
                    *slotAt( toIndex ) = std::move( *slotAt( fromIndex ) );
            }
            for ( size_t filled = 0; filled < count; ++filled )
            {
                const size_t targetIndex = index + filled;
                if ( targetIndex < _size )
                    *slotAt( targetIndex ) = source( filled );
                else
                    AllocTraits::construct( _allocator, slotAt( targetIndex ), source( filled ) );
            }
            _size += count;
        }

        void swap( DequeStorage& other ) noexcept
        {
            std::swap( _allocator, other._allocator );
            std::swap( _pData, other._pData );
            std::swap( _capacity, other._capacity );
            std::swap( _head, other._head );
            std::swap( _size, other._size );
        }

    private:
        static size_t roundUpCapacity( size_t count )
        {
            size_t capacity = 8; // 빈 줄이 처음 한 번 넣을 때 작은 재할당을 여러 번 하지 않게
            while ( capacity < count )
                capacity <<= 1;
            return capacity;
        }

        void growTo( size_t count ) { moveToNewBuffer( roundUpCapacity( count > _capacity * 2 ? count : _capacity * 2 ) ); }

        /** @brief 새 버퍼로 옮기며 앞을 0 번 칸에 맞춘다. */
        void moveToNewBuffer( size_t newCapacity )
        {
            static_assert( std::is_move_constructible_v<T>, "sw::deque moves elements when it grows - use ChunkedArray for address-stable elements" );
            T* pNewData = AllocTraits::allocate( _allocator, newCapacity );
            if constexpr ( is_bitwise_relocatable_v<T> )
            {
                // 바이트로 옮겨도 되는 형식은 링의 두 토막(앞 → 버퍼 끝, 버퍼 앞 → 감긴 끝)을 바이트로 옮긴다 — `vector::reserveInternal` 과 같은 규칙.
                // 원소 루프는 마스크 때문에 컴파일러가 memcpy 로 바꾸지 못한다.
                const size_t firstCount = MathUtil::min( _size, _capacity - _head );
                if ( firstCount > 0 )
                    Memory::copy( static_cast<void*>( pNewData ), static_cast<const void*>( _pData + _head ), firstCount * sizeof( T ) );
                if ( _size > firstCount )
                    Memory::copy( static_cast<void*>( pNewData + firstCount ), static_cast<const void*>( _pData ), ( _size - firstCount ) * sizeof( T ) );
            }
            else
            {
                for ( size_t index = 0; index < _size; ++index )
                {
                    T* pOld = slotAt( index );
                    AllocTraits::construct( _allocator, pNewData + index, std::move( *pOld ) );
                    AllocTraits::destroy( _allocator, pOld );
                }
            }
            if ( _pData != nullptr )
                AllocTraits::deallocate( _allocator, _pData, _capacity );
            _pData    = pNewData;
            _capacity = newCapacity;
            _head     = 0;
        }

        void copyFrom( const DequeStorage& other )
        {
            if ( other._size > _capacity )
                growTo( other._size );
            for ( size_t index = 0; index < other._size; ++index )
            {
                AllocTraits::construct( _allocator, slotAt( index ), *other.slotAt( index ) );
                ++_size;
            }
        }

        void stealFrom( DequeStorage& other ) noexcept
        {
            _pData          = other._pData;
            _capacity       = other._capacity;
            _head           = other._head;
            _size           = other._size;
            other._pData    = nullptr;
            other._capacity = 0;
            other._head     = 0;
            other._size     = 0;
        }

        void releaseStorage() noexcept
        {
            clear();
            if ( _pData != nullptr )
                AllocTraits::deallocate( _allocator, _pData, _capacity );
            _pData    = nullptr;
            _capacity = 0;
        }

    private:
        Allocator _allocator;
        T*        _pData    = nullptr;
        size_t    _capacity = 0; ///< 0 또는 2 의 거듭제곱
        size_t    _head     = 0; ///< 앞 원소의 물리 칸
        size_t    _size     = 0;
    };
} // namespace sw::internal
