/**
 * @file unordered_set.h
 * @brief 밀집 배열 해시 집합(sw::unordered_set)입니다.
 * @details 순회는 밀집 배열 순서입니다. 버킷 표 · 밀집 노드 배열 · 체인 검색 · 지우기 · 다시 해시는 `unordered_map` 과 함께 쓰는 몸통(`internal::DenseHashTable`)에 있고,
 *          여기에는 이터레이터와 삽입(`insert` · `emplace`)만 있습니다.
 *          모든 컨테이너 형식은 두 구성(`SW_ENABLE_STL_CONTAINER`)에서 같은 구현이고 바뀌는 것은 안쪽 `vector` 의 할당자(`ContainerAllocator`)뿐입니다.
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
    /** @brief 밀집 배열 기반 해시 집합입니다. erase 하면 이터레이터가 무효화될 수 있습니다(두 구성 같음). */
    template <typename Key, typename Hash = DefaultHash<Key>, typename KeyEqual = std::equal_to<>, typename Allocator = ContainerAllocator<Key>>
    class unordered_set : public internal::DenseHashTable<Key, internal::DenseHashSetNode<Key>, Hash, KeyEqual>
    {
        using Base = internal::DenseHashTable<Key, internal::DenseHashSetNode<Key>, Hash, KeyEqual>;
        using Node = internal::DenseHashSetNode<Key>;

    public:
        using key_type        = Key;
        using value_type      = Key;
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
        /** @brief 밀집 배열을 순회하는 이터레이터입니다. */
        class iterator
        {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type        = value_type;
            using difference_type   = ptrdiff_t;
            using pointer           = const value_type*;
            using reference         = const value_type&;

            const unordered_set* _pSet;
            size_t               _index;

            iterator( const unordered_set* pSet, size_t index )
                : _pSet{ pSet }
                , _index{ index } {}

            iterator& operator++()
            {
                ++_index;
                return *this;
            }

            bool      operator==( const iterator& other ) const { return _index == other._index; }
            bool      operator!=( const iterator& other ) const { return _index != other._index; }
            reference operator*() const { return std::as_const( _pSet->_listDenseData ).data()[_index]._key; }
            pointer   operator->() const { return &std::as_const( _pSet->_listDenseData ).data()[_index]._key; }
        };

        using const_iterator = iterator; // 집합의 이터레이터는 언제나 const 다

        // ------------------------------------------------------------------------------
        // 1) 생성 — 복사 · 이동 · 대입은 몸통의 것을 암시적으로 부른다
        // ------------------------------------------------------------------------------
        /** @brief 빈 집합으로 둡니다. */
        unordered_set() = default;

        /** @brief 초기화 리스트의 원소로 채웁니다. */
        unordered_set( std::initializer_list<value_type> init )
        {
            this->reserve( init.size() );
            for ( const auto& value : init )
            {
                insert( value );
            }
        }

        // ------------------------------------------------------------------------------
        // 2) 조회 — 밀집 순회 · 버킷 검색
        // ------------------------------------------------------------------------------
        iterator begin() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return iterator( this, 0 );
        }

        iterator end() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return iterator( this, this->_listDenseData.size() );
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

        iterator find( const Key& key ) const
        {
            SW_SCOPED_RACE_READ();
            return iterator( this, this->findDenseIndex( key ) );
        }

        /**
         * @brief 이종 키(heterogeneous key, 예: string_view)로 찾습니다.
         * @details `unordered_map` 과 같은 계약입니다 — `unordered_set<string>` 을 `string_view` 로 찾아도 키를 새로 만들지 않습니다
         *          (비교자의 기본값이 `std::equal_to<>` 인 이유입니다).
         */
        template <typename K, typename = std::enable_if_t<!std::is_same_v<std::decay_t<K>, Key>>>
        iterator find( const K& key ) const
        {
            SW_SCOPED_RACE_READ();
            return iterator( this, this->findDenseIndex( key ) );
        }

        // ------------------------------------------------------------------------------
        // 3) 변경 — insert/erase. erase 뒤 이터레이터가 무효화될 수 있다
        // ------------------------------------------------------------------------------
        pair<iterator, bool> insert( const value_type& value )
        {
            SW_SCOPED_RACE_WRITE();
            const pair<uint32, bool> slot = this->getOrCreateNode( value, [&value]( uint32 next, uint32 tag )
            { return Node{ value, next, tag }; } );
            return { iterator( this, slot.first ), slot.second };
        }

        template <typename... Args>
        pair<iterator, bool> emplace( Args&&... args )
        {
            SW_SCOPED_RACE_WRITE();
            this->check_expand();
            // 키를 알려면 일단 만들어야 한다 — 밀집 배열 끝에 만든 뒤, 키가 이미 있으면 뗀다(`linkAppendedNode`).
            this->_listDenseData.push_back( { Key( std::forward<Args>( args )... ), Base::kEmptySlot, 0 } );
            const pair<uint32, bool> slot = this->linkAppendedNode();
            return { iterator( this, slot.first ), slot.second };
        }

        using Base::erase;

        iterator erase( const_iterator pos )
        {
            if ( pos == end() )
                return end();
            erase( *pos );
            return iterator( this, pos._index );
        }
    };
} // namespace sw
