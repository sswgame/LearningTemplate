/**
 * @file StdDequeStorage.h
 * @brief sw::deque 의 std 저장입니다(`SW_ENABLE_STL_CONTAINER`). std::deque 를 멤버로 듭니다. 쓰는 쪽은 `Core/Container/deque.h` 를 include 합니다.
 * @details 번호 검사 · 레이스 검사는 부르는 쪽(`sw::deque`)이 한다. std::deque 는 용량을 알리지 않아 `reserve` 는 아무것도 하지 않고 `capacity` 는 원소 수다.
 */
#pragma once
#include "Core/Foundation/StdHeaders.h"
#include "Core/Memory/Memory.h"

namespace sw::internal
{
    /** @brief 앞에서 센 번호로 원소를 주는 std::deque 입니다. 검사하지 않습니다. */
    template <typename T, typename Allocator>
    class DequeStorage
    {
    public:
        using AllocTraits = std::allocator_traits<Allocator>;

        explicit DequeStorage( const Allocator& alloc )
            : _items( alloc )
        {
        }
        DequeStorage( const DequeStorage& other )
            : _items( other._items )
        {
        }
        DequeStorage( const DequeStorage& other, const Allocator& alloc )
            : _items( other._items, alloc )
        {
        }
        DequeStorage( DequeStorage&& other ) noexcept
            : _items( std::move( other._items ) )
        {
            other._items.clear();
        }
        ~DequeStorage() = default;

        DequeStorage& operator=( const DequeStorage& other )
        {
            _items = other._items;
            return *this;
        }
        DequeStorage& operator=( DequeStorage&& other ) noexcept
        {
            if ( this != &other )
            {
                _items = std::move( other._items );
                other._items.clear();
            }
            return *this;
        }

        Allocator get_allocator() const noexcept { return _items.get_allocator(); }
        size_t    size() const noexcept { return _items.size(); }
        size_t    capacity() const noexcept { return _items.size(); }
        T*        slotAt( size_t index ) { return std::addressof( _items[index] ); }
        const T*  slotAt( size_t index ) const { return std::addressof( _items[index] ); }
        void      reserve( size_t ) {}
        void      shrink_to_fit() { _items.shrink_to_fit(); }
        void      clear() noexcept { _items.clear(); }
        template <class... Args>
        T& emplaceBack( Args&&... args )
        {
            static_assert( std::is_move_constructible_v<T>, "sw::deque moves elements when it grows - use ChunkedArray for address-stable elements" );
            return _items.emplace_back( std::forward<Args>( args )... );
        }
        template <class... Args>
        T& emplaceFront( Args&&... args )
        {
            static_assert( std::is_move_constructible_v<T>, "sw::deque moves elements when it grows - use ChunkedArray for address-stable elements" );
            return _items.emplace_front( std::forward<Args>( args )... );
        }
        void popBack() { _items.pop_back(); }
        void popFront() { _items.pop_front(); }
        void erase( size_t index, size_t count )
        {
            const auto itFirst = _items.begin() + static_cast<std::ptrdiff_t>( index );
            _items.erase( itFirst, itFirst + static_cast<std::ptrdiff_t>( count ) );
        }
        void resize( size_t count ) { _items.resize( count ); }
        void resize( size_t count, const T& value ) { _items.resize( count, value ); }

        /** @brief [@p index, @p index + @p count) 자리를 비우고 @p source( i ) 로 채웁니다(`index <= size`, `count > 0`). */
        template <typename TSource>
        void insertGap( size_t index, size_t count, TSource&& source )
        {
            std::vector<T, ContainerAllocator<T>> listGathered;
            listGathered.reserve( count );
            for ( size_t filled = 0; filled < count; ++filled )
                listGathered.emplace_back( source( filled ) );
            _items.insert( _items.begin() + static_cast<std::ptrdiff_t>( index ), std::make_move_iterator( listGathered.begin() ), std::make_move_iterator( listGathered.end() ) );
        }

        void swap( DequeStorage& other ) noexcept { _items.swap( other._items ); }

    private:
        std::deque<T, Allocator> _items;
    };
} // namespace sw::internal
