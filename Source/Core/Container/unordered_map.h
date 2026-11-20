/**
 * @file unordered_map.h
 * @brief DoD(Data-Oriented Design) 기반의 밀집 배열 해시맵(sw::unordered_map)입니다.
 * @details 버킷 표 · 밀집 노드 배열 · 체인 검색 · 지우기 · 다시 해시는 `unordered_set` 과 함께 쓰는 몸통(`internal::DenseHashTable`, 구조 설명도 그 헤더)에 있고,
 *          여기에는 이터레이터와 키 → 값 접근 · 삽입(`operator[]` · `insert` · `try_emplace` · `emplace` · `insert_or_assign`)만 있습니다.
 *
 * 1. 레이스 탐지: 디버그 빌드에서 ScopedRaceRead / ScopedRaceWrite 로 동시 쓰기와 읽기/쓰기 충돌을 바로 잡아냅니다.
 * 2. 이종 키 조회: `string_view` 같은 이종 키로 찾을 때 키를 새로 만들지 않습니다(할당 없음).
 *    성립 조건은 **해시가 transparent** 라는 것 하나입니다. 기본 해시 `DefaultHash<sw::string>`(`Core/Container/string.h` 의 특수화)에
 *    `string_view` 오버로드가 있어서 같은 바이트에서 같은 해시가 나옵니다. 비교자의 기본값을 `std::equal_to<>` 로 둔 이유도 여기에 있습니다.
 *    `std::equal_to<Key>` 였다면 `SW_ENABLE_STL_CONTAINER` 로 std 컨테이너를 쓸 때만 이종 검색이 컴파일되지 않아,
 *    같은 코드가 빌드 옵션에 따라 달라졌을 것입니다.
 * 3. 기본 해시: `DefaultHash<Key>`(`Core/Hash/HashUtil.h`)입니다. 키에 `computeHash() const` 가 있으면 그것을 부르므로 키마다 해시 구조체를
 *    따로 두지 않습니다.
 * 4. 원소 주소: 삽입이 밀집 배열을 키우면 모든 원소가, `erase` 가 마지막 원소를 옮기면 그 원소가 다른 주소로 갑니다. `&it->second` 를 다른 삽입 · 삭제를 거쳐
 *    쥐어야 하면 값을 `unique_ptr<T>` 로 듭니다(`unordered_map<Key, unique_ptr<T>>` — 원소가 옮겨져도 가리키는 객체는 제자리다. 언리얼 `TMap<K, TUniquePtr<V>>`).
 * 5. 구성: 모든 컨테이너 형식이 두 구성(`SW_ENABLE_STL_CONTAINER`)에서 같은 구현이고 바뀌는 것은 안쪽 `vector` 의 할당자(`ContainerAllocator`)뿐입니다.
 *          순회 순서(넣은 순서, `erase` 는 마지막 원소를 빈 슬롯으로 옮긴다)와 무효화 규칙이 구성마다 달라지지 않습니다.
 */
#pragma once
#include "Core/Concurrency/DataRaceDetector.h"
#include "Core/Container/DenseHashTable.h"
#include "Core/Container/pair.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /**
     * @class unordered_map
     * @brief 데이터 지향(DoD) 밀집 배열 기반의 해시맵입니다.
     *
     * 주의: `erase` 는 마지막 원소를 빈 슬롯으로 옮기는 방식(swap-and-pop)이라, 기존 이터레이터가 무효화될 수 있습니다(두 구성 같음).
     */
    template <typename Key, typename T, typename Hash = DefaultHash<Key>, typename KeyEqual = std::equal_to<>, typename Allocator = ContainerAllocator<pair<const Key, T>>>
    class unordered_map : public internal::DenseHashTable<Key, internal::DenseHashMapNode<Key, T>, Hash, KeyEqual>
    {
        using Base = internal::DenseHashTable<Key, internal::DenseHashMapNode<Key, T>, Hash, KeyEqual>;
        using Node = internal::DenseHashMapNode<Key, T>;

    public:
        using key_type        = Key;
        using mapped_type     = T;
        using value_type      = pair<const Key, T>;
        using size_type       = size_t;
        using difference_type = ptrdiff_t;
        using hasher          = Hash;
        using key_equal       = KeyEqual;
        using allocator_type  = Allocator;
        using reference       = value_type&;
        using const_reference = const value_type&;
        using pointer         = value_type*;
        using const_pointer   = const value_type*;

    public:
        /**
         * @brief 밀집 데이터 배열(_listDenseData)을 차례로 순회하는 이터레이터입니다.
         */
        class iterator
        {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type        = value_type;
            using difference_type   = ptrdiff_t;
            using pointer           = value_type*;
            using reference         = value_type&;

            unordered_map* _pMap;
            size_t         _index;

            iterator( unordered_map* pMap, size_t index )
                : _pMap{ pMap }
                , _index{ index } {}

            iterator& operator++()
            {
                ++_index;
                return *this;
            }

            bool      operator==( const iterator& other ) const { return _index == other._index; }
            bool      operator!=( const iterator& other ) const { return _index != other._index; }
            reference operator*() const { return *reinterpret_cast<pointer>( const_cast<pair<Key, T>*>( &std::as_const( _pMap->_listDenseData )[_index]._keyValuePair ) ); }
            pointer   operator->() const { return reinterpret_cast<pointer>( const_cast<pair<Key, T>*>( &std::as_const( _pMap->_listDenseData )[_index]._keyValuePair ) ); }
        };

        /** @brief 밀집 배열을 순회하는 상수 이터레이터입니다. */
        class const_iterator
        {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type        = value_type;
            using difference_type   = ptrdiff_t;
            using pointer           = const value_type*;
            using reference         = const value_type&;

            const unordered_map* _pMap;
            size_t               _index;

            const_iterator( const unordered_map* pMap, size_t index )
                : _pMap{ pMap }
                , _index{ index } {}

            const_iterator( const iterator& other )
                : _pMap{ other._pMap }
                , _index{ other._index } {}

            const_iterator& operator++()
            {
                ++_index;
                return *this;
            }

            bool      operator==( const const_iterator& other ) const { return _index == other._index; }
            bool      operator!=( const const_iterator& other ) const { return _index != other._index; }
            reference operator*() const { return *reinterpret_cast<pointer>( &std::as_const( _pMap->_listDenseData )[_index]._keyValuePair ); }
            pointer   operator->() const { return reinterpret_cast<pointer>( &std::as_const( _pMap->_listDenseData )[_index]._keyValuePair ); }
        };

        // ------------------------------------------------------------------------------
        // 1) 생성 — 복사 · 이동 · 대입은 몸통의 것을 암시적으로 부른다
        // ------------------------------------------------------------------------------
        /** @brief 빈 맵으로 둡니다. */
        unordered_map() = default;

        /** @brief 초기화 리스트의 원소로 채웁니다. */
        unordered_map( std::initializer_list<value_type> init )
        {
            this->reserve( init.size() );
            for ( const auto& keyValuePair : init )
            {
                insert( keyValuePair );
            }
        }

        // ------------------------------------------------------------------------------
        // 2) 조회 — 밀집 순회 · 버킷 검색
        // ------------------------------------------------------------------------------
        iterator begin() noexcept
        {
            SW_SCOPED_RACE_READ();
            return iterator( this, 0 );
        }

        iterator end() noexcept
        {
            SW_SCOPED_RACE_READ();
            return iterator( this, this->_listDenseData.size() );
        }

        const_iterator begin() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return const_iterator( this, 0 );
        }

        const_iterator end() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return const_iterator( this, this->_listDenseData.size() );
        }

        const_iterator cbegin() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return const_iterator( this, 0 );
        }

        const_iterator cend() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return const_iterator( this, this->_listDenseData.size() );
        }

        iterator find( const Key& key )
        {
            SW_SCOPED_RACE_READ();
            return iterator( this, this->findDenseIndex( key ) );
        }

        const_iterator find( const Key& key ) const
        {
            SW_SCOPED_RACE_READ();
            return const_iterator( this, this->findDenseIndex( key ) );
        }

        /** @brief 이종 키(heterogeneous key, 예: string_view)로 찾습니다. */
        template <typename K, typename = std::enable_if_t<!std::is_same_v<std::decay_t<K>, Key>>>
        iterator find( const K& key )
        {
            SW_SCOPED_RACE_READ();
            return iterator( this, this->findDenseIndex( key ) );
        }

        /** @brief 이종 키(heterogeneous key, 예: string_view)로 찾습니다. */
        template <typename K, typename = std::enable_if_t<!std::is_same_v<std::decay_t<K>, Key>>>
        const_iterator find( const K& key ) const
        {
            SW_SCOPED_RACE_READ();
            return const_iterator( this, this->findDenseIndex( key ) );
        }

        // ------------------------------------------------------------------------------
        // 3) 변경 — insert/erase. erase 뒤 이터레이터가 무효화될 수 있다
        // ------------------------------------------------------------------------------
        T& operator[]( const Key& key )
        {
            SW_SCOPED_RACE_WRITE();
            const pair<uint32, bool> slot = this->getOrCreateNode( key, [&key]( uint32 next, uint32 tag )
            { return Node{ pair<Key, T>( key, T{} ), next, tag }; } );
            return this->_listDenseData[slot.first]._keyValuePair.second;
        }

        T& operator[]( Key&& key )
        {
            SW_SCOPED_RACE_WRITE();
            const pair<uint32, bool> slot = this->getOrCreateNode( key, [&key]( uint32 next, uint32 tag )
            { return Node{ pair<Key, T>( std::move( key ), T{} ), next, tag }; } );
            return this->_listDenseData[slot.first]._keyValuePair.second;
        }

        pair<iterator, bool> insert( const value_type& value )
        {
            SW_SCOPED_RACE_WRITE();
            const pair<uint32, bool> slot = this->getOrCreateNode( value.first, [&value]( uint32 next, uint32 tag )
            { return Node{ pair<Key, T>( value.first, value.second ), next, tag }; } );
            return { iterator( this, slot.first ), slot.second };
        }

        /**
         * @brief 키가 없을 때만 값을 만들어 넣습니다. **키가 있으면 인자를 건드리지 않습니다**(표준 계약).
         * @details 값을 먼저 만들어 넣어 보면 키가 있어도 인자를 소비합니다(`try_emplace( k, std::move( pOwned ) )` 가 키가 있을 때
         *          `pOwned` 를 없앤다). 값은 괄호로 만듭니다 — 중괄호면 `try_emplace( k, 5 )` 가 `vector<int>{ 5 }` 가 됩니다.
         *          키는 한 번만 해시합니다(찾기와 넣기가 같은 태그 · 버킷을 쓴다).
         */
        template <typename... Args>
        pair<iterator, bool> try_emplace( const Key& k, Args&&... args )
        {
            SW_SCOPED_RACE_WRITE();
            const pair<uint32, bool> slot = this->getOrCreateNode( k, [&]( uint32 next, uint32 tag )
            { return Node{ pair<Key, T>( k, T( std::forward<Args>( args )... ) ), next, tag }; } );
            return { iterator( this, slot.first ), slot.second };
        }

        /** @brief 키가 없을 때만 값을 만들어 넣습니다. 키가 있으면 인자를 건드리지 않습니다. */
        template <typename... Args>
        pair<iterator, bool> try_emplace( Key&& k, Args&&... args )
        {
            SW_SCOPED_RACE_WRITE();
            const pair<uint32, bool> slot = this->getOrCreateNode( k, [&]( uint32 next, uint32 tag )
            { return Node{ pair<Key, T>( std::move( k ), T( std::forward<Args>( args )... ) ), next, tag }; } );
            return { iterator( this, slot.first ), slot.second };
        }

        template <typename... Args>
        pair<iterator, bool> emplace( Args&&... args )
        {
            SW_SCOPED_RACE_WRITE();
            this->check_expand();
            // 키를 알려면 일단 만들어야 한다(표준 emplace 도 제자리에서 생성한다).
            // 밀집 배열 끝에 만든 뒤, 키가 이미 있으면 뗀다(`linkAppendedNode`).
            this->_listDenseData.push_back( { pair<Key, T>( std::forward<Args>( args )... ), Base::kEmptySlot, 0 } );
            const pair<uint32, bool> slot = this->linkAppendedNode();
            return { iterator( this, slot.first ), slot.second };
        }

        using Base::erase;

        iterator erase( iterator pos )
        {
            if ( pos == end() )
                return end();
            erase( pos->first );
            return iterator( this, pos._index );
        }

        iterator erase( const_iterator pos )
        {
            if ( pos == end() )
                return end();
            erase( pos->first );
            return iterator( this, pos._index );
        }

        /** @brief 키가 없으면 삽입하고, 있으면 값을 대입합니다. */
        template <typename M>
        pair<iterator, bool> insert_or_assign( const Key& key, M&& mappedValue )
        {
            SW_SCOPED_RACE_WRITE();
            const pair<uint32, bool> slot = this->getOrCreateNode( key, [&]( uint32 next, uint32 tag )
            { return Node{ pair<Key, T>( key, std::forward<M>( mappedValue ) ), next, tag }; } );
            if ( slot.second == false )
                this->_listDenseData[slot.first]._keyValuePair.second = std::forward<M>( mappedValue );
            return { iterator( this, slot.first ), slot.second };
        }

        /** @brief 키가 없으면 삽입하고, 있으면 값을 대입합니다. */
        template <typename M>
        pair<iterator, bool> insert_or_assign( Key&& key, M&& mappedValue )
        {
            SW_SCOPED_RACE_WRITE();
            const pair<uint32, bool> slot = this->getOrCreateNode( key, [&]( uint32 next, uint32 tag )
            { return Node{ pair<Key, T>( std::move( key ), std::forward<M>( mappedValue ) ), next, tag }; } );
            if ( slot.second == false )
                this->_listDenseData[slot.first]._keyValuePair.second = std::forward<M>( mappedValue );
            return { iterator( this, slot.first ), slot.second };
        }
    };
} // namespace sw
