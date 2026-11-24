/**
 * @file StdSortedStorage.h
 * @brief 정렬 컨테이너(`map` · `set`)의 std 저장입니다(`SW_ENABLE_STL_CONTAINER`). std::map · std::set 을 멤버로 듭니다. 쓰는 쪽은 `Core/Container/SortedContainerBase.h` 를 include 합니다.
 * @details 노드는 자리를 옮기지 않지만 공통 계약은 약한 쪽(삽입 · 삭제 뒤 무효)이다 — 세대 검사가 두 구성에서 같이 잡는다. 이종 키 조회는 비교자에 `is_transparent` 를 붙여 얻는다.
 */
#pragma once
#include "Core/Container/pair.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Memory/Memory.h"

namespace sw::internal
{
    /** @brief 비교자를 이종 키도 받게 감쌉니다(`Compare` 에 `is_transparent` 가 없어도 자체 저장처럼 이종 키로 찾는다). */
    template <typename Compare>
    struct TransparentLess
    {
        using is_transparent = void;

        Compare _comp;
        template <typename L, typename R>
        bool operator()( const L& left, const R& right ) const
        {
            return _comp( left, right );
        }
    };
} // namespace sw::internal

namespace sw::internal
{
    /** @brief 키가 겹치지 않는 std::map · std::set 입니다. @p T 가 void 면 집합입니다. */
    template <typename Key, typename T, typename Compare, typename Allocator>
    class SortedStorage
    {
        static constexpr bool kIsSet = std::is_void_v<T>;

        template <bool kSet, typename TDummy = void>
        struct Select
        {
            using Mapped      = T;
            using insert_type = pair<Key, T>;
            using Container   = std::map<Key, T, TransparentLess<Compare>, typename std::allocator_traits<Allocator>::template rebind_alloc<std::pair<const Key, T>>>;
        };
        template <typename TDummy>
        struct Select<true, TDummy>
        {
            using insert_type = Key;
            using Container   = std::set<Key, TransparentLess<Compare>, Allocator>;
        };

    public:
        using Container      = typename Select<kIsSet>::Container;
        using value_type     = typename Container::value_type;
        using insert_type    = typename Select<kIsSet>::insert_type; ///< 넣을 때 받는 형식
        using iterator       = typename Container::iterator;
        using const_iterator = typename Container::const_iterator;

        static constexpr bool kBitwiseRelocatable = false; // 노드가 컨테이너 안의 머리를 가리킬 수 있다(libstdc++)

        SortedStorage( const Compare& comp, const Allocator& alloc )
            : _items( TransparentLess<Compare>{ comp }, typename Container::allocator_type( alloc ) )
        {
        }
        SortedStorage( const SortedStorage& other )
            : _items( other._items )
        {
        }
        SortedStorage( SortedStorage&& other ) noexcept
            : _items( std::move( other._items ) )
        {
            other._items.clear();
        }
        ~SortedStorage() = default;
        SortedStorage& operator=( const SortedStorage& other )
        {
            _items = other._items;
            return *this;
        }
        SortedStorage& operator=( SortedStorage&& other ) noexcept
        {
            if ( this != &other )
            {
                _items = std::move( other._items );
                other._items.clear();
            }
            return *this;
        }

        Allocator get_allocator() const noexcept { return Allocator( _items.get_allocator() ); }

        iterator       begin() noexcept { return _items.begin(); }
        const_iterator begin() const noexcept { return _items.begin(); }
        iterator       end() noexcept { return _items.end(); }
        const_iterator end() const noexcept { return _items.end(); }
        size_t         size() const noexcept { return _items.size(); }
        size_t         max_size() const noexcept { return _items.max_size(); }
        size_t         capacity() const noexcept { return _items.size(); }
        void           reserve( size_t ) {}
        void           clear() noexcept { _items.clear(); }

        template <typename K>
        iterator lower_bound( const K& key )
        {
            return _items.lower_bound( key );
        }
        template <typename K>
        const_iterator lower_bound( const K& key ) const
        {
            return _items.lower_bound( key );
        }
        template <typename K>
        iterator upper_bound( const K& key )
        {
            return _items.upper_bound( key );
        }
        template <typename K>
        const_iterator upper_bound( const K& key ) const
        {
            return _items.upper_bound( key );
        }
        template <typename K>
        iterator find( const K& key )
        {
            return _items.find( key );
        }
        template <typename K>
        const_iterator find( const K& key ) const
        {
            return _items.find( key );
        }

        template <typename V>
        pair<iterator, bool> insert( V&& value )
        {
            if constexpr ( kIsSet )
                return _items.insert( std::forward<V>( value ) );
            else if constexpr ( std::is_lvalue_reference_v<V> )
                return _items.try_emplace( value.first, value.second );
            else
                return _items.try_emplace( std::move( value.first ), std::move( value.second ) );
        }

        template <typename K, class... Args>
        pair<iterator, bool> tryEmplace( K&& key, Args&&... args )
        {
            return _items.try_emplace( std::forward<K>( key ), std::forward<Args>( args )... );
        }

        template <class InputIt>
        void insertRange( InputIt first, InputIt last )
        {
            for ( ; first != last; ++first )
                insert( insert_type( *first ) );
        }

        iterator erase( const_iterator pos )
        {
            SW_CHECK_INDEX( pos == _items.cend() ? _items.size() : 0, _items.size() );
            return pos == _items.cend() ? _items.end() : _items.erase( pos );
        }
        iterator erase( const_iterator first, const_iterator last ) { return _items.erase( first, last ); }

        void swap( SortedStorage& other ) noexcept { _items.swap( other._items ); }

    private:
        Container _items;
    };
} // namespace sw::internal
