/**
 * @file stable_unordered_map.h
 * @brief 원소의 주소가 맵이 자라도 · 다른 키를 지워도 바뀌지 않는 해시맵(sw::stable_unordered_map)입니다.
 *
 * [왜 따로 있는가]
 * `sw::unordered_map` 은 키-값 쌍을 밀집 배열 하나에 두므로 삽입이 배열을 키우면 모든 원소가 옮겨지고, `erase` 는 마지막 원소를 빈 자리로 옮깁니다.
 * `&it->second` 를 쥔 채 다른 키를 넣으면 매달린 포인터가 됩니다(`LiveReloadManager` 의 연쇄 리로드가 `ModuleContext*` 를 쥔 채 모듈 등록이 끼어드는 경우).
 * 이 형식은 원소를 힙에 따로 두고, 그 주소를 쥐어도 되는 맵을 **형식으로** 가립니다. 주소를 오래 쥐는 자리는 이 형식을 쓰고, 그 밖의 자리는 `unordered_map` 을 씁니다.
 *
 * [보장]
 * - `&map[key]` · `&it->second` 는 그 키를 `erase` 하거나 `clear` 하기 전까지 유효합니다. 다른 키의 삽입 · 삭제와 `rehash` 는 건드리지 않습니다.
 * - 순회 순서와 이터레이터 무효화는 안쪽 `unordered_map` 을 따릅니다 — 이터레이터는 `erase` · 삽입 뒤에 쓰지 않습니다. 보장하는 것은 **원소의 주소**뿐입니다.
 * - 원소는 `first` · `second` 를 가진 구조체라 `it->second` · 구조체 바인딩(`auto& [key, value]`)이 `unordered_map` 과 똑같이 됩니다.
 * - 키는 복사할 수 있어야 합니다(찾는 표와 원소가 각각 키 한 벌을 듭니다). 값은 이동 · 복사할 필요가 없습니다 — 한 번 만든 자리에서 옮겨지지 않기 때문입니다.
 */
#pragma once
#include "Core/Container/pair.h"
#include "Core/Container/unordered_map.h"
#include "Core/Foundation/Defines.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Hash/HashUtil.h"

namespace sw
{
    /**
     * @class stable_unordered_map
     * @brief 값 주소가 고정된 해시맵입니다. 값은 힙에 하나씩 놓이고, 주소를 내주는 길은 이 형식의 조회 · 순회뿐입니다.
     */
    template <typename Key, typename T, typename Hash = DefaultHash<Key>, typename KeyEqual = std::equal_to<>>
    class stable_unordered_map
    {
    public:
        using key_type    = Key;
        using mapped_type = T;
        using size_type   = size_t;
        using hasher      = Hash;
        using key_equal   = KeyEqual;

        /** @brief 힙에 놓이는 원소 하나입니다. `first` 는 키, `second` 는 값이라 `unordered_map` 의 원소와 같은 모양으로 읽힙니다. */
        struct value_type
        {
            template <typename... Args>
            explicit value_type( const Key& key, Args&&... args )
                : first( key )
                , second( std::forward<Args>( args )... )
            {
            }

            value_type( const value_type& )            = delete;
            value_type& operator=( const value_type& ) = delete;

            const Key first;
            T         second;
        };

        using reference       = value_type&;
        using const_reference = const value_type&;

    private:
        using Storage = unordered_map<Key, unique_ptr<value_type>, Hash, KeyEqual>;

    public:
        /** @brief 원소를 차례로 도는 이터레이터입니다. 쥐는 것은 안쪽 표의 이터레이터라 삽입 · 삭제 뒤에는 쓰지 않습니다(원소 주소만 고정입니다). */
        class iterator
        {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type        = typename stable_unordered_map::value_type;
            using difference_type   = ptrdiff_t;
            using pointer           = value_type*;
            using reference         = value_type&;

            explicit iterator( typename Storage::iterator inner )
                : _inner{ inner }
            {
            }

            iterator& operator++()
            {
                ++_inner;
                return *this;
            }

            bool      operator==( const iterator& other ) const { return _inner == other._inner; }
            bool      operator!=( const iterator& other ) const { return _inner != other._inner; }
            reference operator*() const { return *_inner->second; }
            pointer   operator->() const { return _inner->second.get(); }

            typename Storage::iterator _inner;
        };

        /** @brief 상수 이터레이터입니다. */
        class const_iterator
        {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type        = typename stable_unordered_map::value_type;
            using difference_type   = ptrdiff_t;
            using pointer           = const value_type*;
            using reference         = const value_type&;

            explicit const_iterator( typename Storage::const_iterator inner )
                : _inner{ inner }
            {
            }

            const_iterator( const iterator& other )
                : _inner{ other._inner }
            {
            }

            const_iterator& operator++()
            {
                ++_inner;
                return *this;
            }

            bool      operator==( const const_iterator& other ) const { return _inner == other._inner; }
            bool      operator!=( const const_iterator& other ) const { return _inner != other._inner; }
            reference operator*() const { return *_inner->second; }
            pointer   operator->() const { return _inner->second.get(); }

            typename Storage::const_iterator _inner;
        };

        stable_unordered_map()                                             = default;
        stable_unordered_map( const stable_unordered_map& )                = delete;
        stable_unordered_map& operator=( const stable_unordered_map& )     = delete;
        stable_unordered_map( stable_unordered_map&& ) noexcept            = default;
        stable_unordered_map& operator=( stable_unordered_map&& ) noexcept = default;

        iterator       begin() noexcept { return iterator{ _mapEntry.begin() }; }
        iterator       end() noexcept { return iterator{ _mapEntry.end() }; }
        const_iterator begin() const noexcept { return const_iterator{ _mapEntry.begin() }; }
        const_iterator end() const noexcept { return const_iterator{ _mapEntry.end() }; }

        bool      empty() const noexcept { return _mapEntry.empty(); }
        size_type size() const noexcept { return _mapEntry.size(); }

        /** @brief 모든 원소를 지웁니다. 쥐고 있던 원소 주소는 모두 무효입니다. */
        void clear() noexcept { _mapEntry.clear(); }

        /** @brief 남는 용량을 놓습니다. 원소 주소는 그대로입니다. */
        void shrink_to_fit() { _mapEntry.shrink_to_fit(); }

        void reserve( size_type count ) { _mapEntry.reserve( count ); }

        /** @brief 키로 찾습니다. 이종 키(`string_view` 등)도 안쪽 표의 규칙대로 받습니다. */
        template <typename K>
        iterator find( const K& key )
        {
            return iterator{ _mapEntry.find( key ) };
        }

        template <typename K>
        const_iterator find( const K& key ) const
        {
            return const_iterator{ _mapEntry.find( key ) };
        }

        template <typename K>
        bool contains( const K& key ) const
        {
            return _mapEntry.contains( key );
        }

        /** @brief 키가 없을 때만 원소를 만듭니다(값은 @p args 로 만듭니다). 키가 있으면 인자를 건드리지 않습니다. */
        template <typename... Args>
        pair<iterator, bool> try_emplace( const Key& key, Args&&... args )
        {
            typename Storage::iterator found = _mapEntry.find( key );
            if ( found != _mapEntry.end() )
                return { iterator{ found }, false };
            unique_ptr<value_type> pEntry = make_unique<value_type>( key, std::forward<Args>( args )... );
            auto                   result = _mapEntry.emplace( key, std::move( pEntry ) );
            return { iterator{ result.first }, true };
        }

        /** @brief 없으면 값을 기본 생성해 넣고, 있으면 있는 값을 돌려줍니다. 돌려준 참조의 주소는 이 키를 지우기 전까지 고정입니다. */
        T& operator[]( const Key& key ) { return try_emplace( key ).first->second; }

        /** @brief 키의 원소를 지웁니다. 지운 개수(0 또는 1)를 돌려줍니다. 다른 키의 원소 주소는 그대로입니다. */
        size_type erase( const Key& key ) { return _mapEntry.erase( key ); }

    private:
        Storage _mapEntry;
    };
} // namespace sw
