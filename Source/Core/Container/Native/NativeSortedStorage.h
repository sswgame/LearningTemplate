/**
 * @file NativeSortedStorage.h
 * @brief 정렬 컨테이너(`flat_map` · `flat_set`)의 자체 저장입니다(기본 구성) — 정렬된 `vector` 와 이진 검색. 쓰는 쪽은 `Core/Container/SortedContainerBase.h` 를 include 합니다.
 * @details `flat_map` 과 `flat_set` 은 원소에서 키를 읽는 법만 다르다(쌍의 `first` / 원소 자신). 그 차이를 비교자 `ValueLess` 하나로 받고 나머지를 여기 한 벌로 둔다
 *          (boost `flat_tree` · MSVC `_Tree` 가 map · set 을 한 몸통 위에 두는 것과 같다).
 *
 *          범위 삽입의 임시 버퍼는 컨테이너 자신의 할당자로 잡는다 — `std::stable_sort` · `std::inplace_merge` 는 `operator new` 로 버퍼를 잡아
 *          sw 할당자와 할당 추적을 비켜 간다. 할당은 버퍼 하나(원소 수만큼)뿐이고, 병합 결과가 그 버퍼째 저장소가 된다(언리얼 `TArray::StableSort` 도 자기 할당자로 버퍼를 잡는다).
 */
#pragma once
#include "Core/Container/BitwiseRelocatable.h"
#include "Core/Container/pair.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/StdHeaders.h"

namespace sw::internal
{
    /**
     * @brief 쌍의 키(`first`)만 견주는 비교자입니다(`flat_map` 의 `ValueLess`). 원소끼리, 원소와 이종 키를 양쪽 순서로 받습니다.
     * @details 이종 키 쪽에서는 `.first` 를 읽지 않습니다(`upper_bound` 는 키를 왼쪽에 넘긴다).
     */
    template <typename Value, typename Compare>
    struct PairKeyLess
    {
        Compare _comp;

        bool operator()( const Value& left, const Value& right ) const { return _comp( left.first, right.first ); }
        template <typename K>
        bool operator()( const Value& left, const K& right ) const { return _comp( left.first, right ); }
        template <typename K>
        bool operator()( const K& left, const Value& right ) const { return _comp( left, right.first ); }
    };
} // namespace sw::internal

namespace sw::internal
{
    /** @brief 키가 겹치지 않는 정렬 벡터입니다. @p T 가 void 면 원소 자신이 키(집합)이고, 아니면 원소가 키 · 값 쌍입니다. */
    template <typename Key, typename T, typename Compare, typename Allocator>
    class SortedStorage
    {
        static constexpr bool kIsSet = std::is_void_v<T>;

    public:
        using value_type     = typename std::conditional_t<kIsSet, std::enable_if<true, Key>, std::enable_if<true, pair<Key, T>>>::type;
        using insert_type    = value_type; ///< 넣을 때 받는 형식
        using Container      = vector<value_type, Allocator>;
        using iterator       = typename Container::iterator;
        using const_iterator = typename Container::const_iterator;

        static constexpr bool kBitwiseRelocatable = is_bitwise_relocatable_v<Container> && std::is_empty_v<Compare>;

        SortedStorage( const Compare& comp, const Allocator& alloc )
            : _data( alloc )
            , _less( makeLess( comp ) )
        {
        }

        Allocator get_allocator() const noexcept { return _data.get_allocator(); }

        iterator       begin() noexcept { return _data.begin(); }
        const_iterator begin() const noexcept { return _data.begin(); }
        iterator       end() noexcept { return _data.end(); }
        const_iterator end() const noexcept { return _data.end(); }
        size_t         size() const noexcept { return _data.size(); }
        size_t         max_size() const noexcept { return _data.max_size(); }
        size_t         capacity() const noexcept { return _data.capacity(); }
        void           reserve( size_t count ) { _data.reserve( count ); }
        void           clear() noexcept { _data.clear(); }

        template <typename K>
        iterator lower_bound( const K& key )
        {
            return std::lower_bound( _data.begin(), _data.end(), key, _less );
        }
        template <typename K>
        const_iterator lower_bound( const K& key ) const
        {
            return std::lower_bound( _data.begin(), _data.end(), key, _less );
        }
        template <typename K>
        iterator upper_bound( const K& key )
        {
            return std::upper_bound( _data.begin(), _data.end(), key, _less );
        }
        template <typename K>
        const_iterator upper_bound( const K& key ) const
        {
            return std::upper_bound( _data.begin(), _data.end(), key, _less );
        }
        template <typename K>
        iterator find( const K& key )
        {
            const iterator it = lower_bound( key );
            return it != _data.end() && _less( key, *it ) == false ? it : _data.end();
        }
        template <typename K>
        const_iterator find( const K& key ) const
        {
            const const_iterator it = lower_bound( key );
            return it != _data.end() && _less( key, *it ) == false ? it : _data.end();
        }

        template <typename V>
        pair<iterator, bool> insert( V&& value )
        {
            const iterator it = std::lower_bound( _data.begin(), _data.end(), value, _less );
            if ( it != _data.end() && _less( value, *it ) == false )
                return { it, false };
            return { _data.insert( it, std::forward<V>( value ) ), true };
        }

        template <typename K, class... Args>
        pair<iterator, bool> tryEmplace( K&& key, Args&&... args )
        {
            const iterator it = lower_bound( key );
            if ( it != _data.end() && _less( key, *it ) == false )
                return { it, false };
            return { _data.emplace( it, std::piecewise_construct, std::forward_as_tuple( std::forward<K>( key ) ), std::forward_as_tuple( std::forward<Args>( args )... ) ), true };
        }

        /**
         * @brief [first, last) 를 넣습니다. 키가 이미 있거나 범위 안에서 겹치면 먼저 온 것(있던 원소, 그다음 입력 순서) 하나만 남습니다.
         * @details 꼬리에 붙여 안정 정렬하고 겹친 키를 걸러 한 번 병합한다(`mergeAppendedRange`) — O(n log n)(감사 C-9: 원소마다 정렬 위치에 끼우면 O(n²)였다.
         *          std::map 범위 구축 · 언리얼 `TSortedMap` 과 같은 결과).
         */
        template <class InputIt>
        void insertRange( InputIt first, InputIt last )
        {
            const size_t oldSize = _data.size();
            for ( ; first != last; ++first )
                _data.emplace_back( *first );

            mergeAppendedRange( _data, oldSize, _less );
        }

        iterator erase( const_iterator pos )
        {
            SW_CHECK_INDEX( pos - _data.cbegin(), _data.size() );
            return _data.erase( pos );
        }
        iterator erase( const_iterator first, const_iterator last )
        {
            SW_CHECK_INDEX_OR_END( last - _data.cbegin(), _data.size() );
            SW_CHECK_INDEX_OR_END( first - _data.cbegin(), last - _data.cbegin() );
            return _data.erase( first, last );
        }

        void swap( SortedStorage& other ) noexcept
        {
            _data.swap( other._data );
            std::swap( _less, other._less );
        }

    private:
        struct SetLess
        {
            Compare _comp;
            template <typename L, typename R>
            bool operator()( const L& left, const R& right ) const
            {
                return _comp( left, right );
            }
        };
        using ValueLess = std::conditional_t<kIsSet, SetLess, PairKeyLess<value_type, Compare>>;

        static ValueLess makeLess( const Compare& comp ) { return ValueLess{ comp }; }

        /**
         * @brief data[oldSize, end) 에 붙인 원소를 [0, oldSize) 의 정렬 순서에 합칩니다. 같은 키는 먼저 온 것(있던 원소, 그다음 붙인 순서)만 남습니다.
         * @param data   [0, oldSize) 는 정렬되어 있고 키가 겹치지 않는다.
         * @param less   원소 둘을 받는 엄격 약순서 비교자.
         */
        static void mergeAppendedRange( Container& data, const size_t oldSize, const ValueLess& less )
        {
            using container_difference_type = typename Container::difference_type;
            const size_t tailCount          = data.size() - oldSize;
            if ( tailCount == 0 )
                return;

            const auto middle = data.begin() + static_cast<container_difference_type>( oldSize );

            // 1) 꼬리 안정 정렬 — 짧은 구간은 삽입 정렬(제자리), 그 위는 아래에서 위로 병합 정렬(버퍼는 컨테이너 할당자).
            static constexpr size_t kRunLength = 16;
            for ( size_t runStart = 0; runStart < tailCount; runStart += kRunLength )
            {
                const auto runFirst = middle + static_cast<container_difference_type>( runStart );
                const auto runLast  = middle + static_cast<container_difference_type>( std::min( runStart + kRunLength, tailCount ) );
                for ( auto it = runFirst; it != runLast; ++it )
                    std::rotate( std::upper_bound( runFirst, it, *it, less ), it, it + 1 ); // upper_bound — 같은 키 뒤에 둬 안정
            }

            Container buffer( data.get_allocator() ); // 필요할 때만 잡는다 — 짧은 꼬리를 정렬된 채 붙이면 할당이 없다
            for ( size_t width = kRunLength; width < tailCount; width *= 2 )
            {
                buffer.clear();
                buffer.reserve( data.size() ); // 병합 결과(3)까지 담을 크기 — 두 번째부터는 아무것도 하지 않는다
                for ( size_t leftStart = 0; leftStart < tailCount; leftStart += width * 2 )
                {
                    const auto leftFirst  = middle + static_cast<container_difference_type>( leftStart );
                    const auto rightFirst = middle + static_cast<container_difference_type>( std::min( leftStart + width, tailCount ) );
                    const auto rightLast  = middle + static_cast<container_difference_type>( std::min( leftStart + width * 2, tailCount ) );
                    std::merge( std::make_move_iterator( leftFirst ), std::make_move_iterator( rightFirst ), std::make_move_iterator( rightFirst ),
                                std::make_move_iterator( rightLast ), std::back_inserter( buffer ), less ); // 같으면 왼쪽 먼저 — 안정
                }
                std::move( buffer.begin(), buffer.end(), middle );
            }

            // 2) 정렬된 꼬리에서 같은 키의 첫 원소만 남기고, 앞쪽(있던 원소)에 있는 키를 뺀다.
            auto tailEnd = std::unique( middle, data.end(), [&less]( const value_type& kept, const value_type& next )
            { return less( kept, next ) == false; } );
            tailEnd      = std::remove_if( middle, tailEnd, [&]( const value_type& value )
                 { return std::binary_search( data.begin(), middle, value, less ); } );
            data.erase( tailEnd, data.end() );

            // 3) 앞쪽과 꼬리를 버퍼로 한 번 병합하고 버퍼를 저장소로 삼는다. 꼬리가 모두 앞쪽 뒤면(정렬된 입력 붙이기) 병합하지 않는다.
            const auto merged = data.begin() + static_cast<container_difference_type>( oldSize );
            if ( oldSize == 0 || merged == data.end() || less( *( merged - 1 ), *merged ) )
                return;
            buffer.clear();
            buffer.reserve( data.size() );
            std::merge( std::make_move_iterator( data.begin() ), std::make_move_iterator( merged ), std::make_move_iterator( merged ), std::make_move_iterator( data.end() ),
                        std::back_inserter( buffer ), less );
            data.swap( buffer );
        }

    private:
        Container _data;
        ValueLess _less;
    };
} // namespace sw::internal
