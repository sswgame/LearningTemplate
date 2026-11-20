/**
 * @file SortedVectorBase.h
 * @brief 정렬 벡터 컨테이너(`map` · `set`)의 공통 몸통입니다 — 저장소 · 이진 검색 · 정렬을 지키는 삽입 · 삭제 · 범위 삽입.
 * @details `map` 과 `set` 은 원소에서 키를 읽는 법만 다르다(쌍의 `first` / 원소 자신). 그 차이를 비교자 `ValueLess` 하나로 받고 나머지를 여기 한 벌로 둔다
 *          (boost `flat_tree` · MSVC `_Tree` 가 map · set 을 한 몸통 위에 두는 것과 같다). 예전에는 두 헤더가 같은 300 줄을 따로 들어 한쪽만 고쳐질 수 있었다.
 *
 *          범위 삽입의 임시 버퍼는 컨테이너 자신의 할당자로 잡는다 — `std::stable_sort` · `std::inplace_merge` 는 `operator new` 로 버퍼를 잡아
 *          sw 할당자와 할당 추적을 비켜 간다. 할당은 버퍼 하나(원소 수만큼)뿐이고, 병합 결과가 그 버퍼째 저장소가 된다(언리얼 `TArray::StableSort` 도 자기 할당자로 버퍼를 잡는다).
 */
#pragma once
#include "Core/Concurrency/DataRaceDetector.h"
#include "Core/Container/pair.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/StdHeaders.h"

namespace sw::internal
{
    /**
     * @brief 쌍의 키(`first`)만 견주는 비교자입니다(`map` 의 `ValueLess`). 원소끼리, 원소와 이종 키를 양쪽 순서로 받습니다.
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
    /**
     * @brief 키가 겹치지 않는 정렬 벡터입니다. 조회는 이진 검색이고, 삽입할 때 정렬을 유지합니다.
     * @tparam Value     원소 형식(`set` 은 키, `map` 은 키 · 값 쌍).
     * @tparam Compare   키끼리 견주는 엄격 약순서.
     * @tparam ValueLess `Compare` 하나로 만드는 비교자 — 원소끼리, 원소와 (이종) 키를 양쪽 순서로 견준다. `set` 은 `Compare` 그대로, `map` 은 `PairKeyLess`.
     */
    template <typename Value, typename Compare, typename ValueLess, typename Allocator>
    class SortedVectorBase
    {
    public:
        using value_type             = Value;
        using size_type              = size_t;
        using difference_type        = std::ptrdiff_t;
        using key_compare            = Compare;
        using allocator_type         = Allocator;
        using reference              = value_type&;
        using const_reference        = const value_type&;
        using pointer                = value_type*;
        using const_pointer          = const value_type*;
        using Container              = std::vector<value_type, Allocator>;
        using iterator               = typename Container::iterator;
        using const_iterator         = typename Container::const_iterator;
        using reverse_iterator       = typename Container::reverse_iterator;
        using const_reverse_iterator = typename Container::const_reverse_iterator;

    public:
        // ------------------------------------------------------------------------------
        // 1) 생성 · 대입 — 정렬 벡터 + 비교자. 레이스 컨텍스트는 공유하지 않는다
        // ------------------------------------------------------------------------------
        /** @brief 빈 컨테이너로 둡니다. */
        SortedVectorBase()
            : _data{}
            , _comp{} {}

        /** @brief 비교자와 할당자를 지정합니다. */
        explicit SortedVectorBase( const Compare& comp, const Allocator& alloc = Allocator() )
            : _data{ alloc }
            , _comp{ comp } {}

        /** @brief 지정한 할당자로 빈 컨테이너를 만듭니다. */
        explicit SortedVectorBase( const Allocator& alloc )
            : _data{ alloc }
            , _comp{} {}

        /** @brief [first, last) 를 넣고 정렬합니다. */
        template <class InputIt>
        SortedVectorBase( InputIt first, InputIt last, const Compare& comp = Compare(), const Allocator& alloc = Allocator() )
            : _data{ alloc }
            , _comp{ comp } { insert( first, last ); }

        /** @brief 복사 생성합니다. */
        SortedVectorBase( const SortedVectorBase& other )
            : _data{ other._data }
            , _comp{ other._comp } {}

        /** @brief 이동 생성합니다. */
        SortedVectorBase( SortedVectorBase&& other ) noexcept
            : _data{ std::move( other._data ) }
            , _comp{ std::move( other._comp ) } {}

        /** @brief 초기화 리스트를 넣고 정렬합니다. */
        SortedVectorBase( std::initializer_list<value_type> init, const Compare& comp = Compare(), const Allocator& alloc = Allocator() )
            : _data{ alloc }
            , _comp{ comp } { insert( init.begin(), init.end() ); }

        SortedVectorBase& operator=( const SortedVectorBase& other )
        {
            if ( this != &other )
            {
                SW_SCOPED_RACE_WRITE();
                SW_SCOPED_RACE_READ_OTHER( other );
                _data = other._data;
                _comp = other._comp;
            }
            return *this;
        }

        SortedVectorBase& operator=( SortedVectorBase&& other ) noexcept
        {
            if ( this != &other )
            {
                SW_SCOPED_RACE_WRITE();
                SW_SCOPED_RACE_WRITE_OTHER( other );
                _data = std::move( other._data );
                _comp = std::move( other._comp );
            }
            return *this;
        }

        // ------------------------------------------------------------------------------
        // 2) 조회 — 이진 검색 · 이터레이터 · 크기
        // ------------------------------------------------------------------------------
        allocator_type get_allocator() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return _data.get_allocator();
        }

        iterator begin() noexcept
        {
            SW_SCOPED_RACE_READ();
            return _data.begin();
        }

        const_iterator begin() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return _data.begin();
        }

        const_iterator cbegin() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return _data.cbegin();
        }

        iterator end() noexcept
        {
            SW_SCOPED_RACE_READ();
            return _data.end();
        }

        const_iterator end() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return _data.end();
        }

        const_iterator cend() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return _data.cend();
        }

        reverse_iterator rbegin() noexcept
        {
            SW_SCOPED_RACE_READ();
            return _data.rbegin();
        }

        const_reverse_iterator rbegin() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return _data.rbegin();
        }

        const_reverse_iterator crbegin() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return _data.crbegin();
        }

        reverse_iterator rend() noexcept
        {
            SW_SCOPED_RACE_READ();
            return _data.rend();
        }

        const_reverse_iterator rend() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return _data.rend();
        }

        const_reverse_iterator crend() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return _data.crend();
        }

        bool empty() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return _data.empty();
        }

        size_type size() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return _data.size();
        }

        size_type max_size() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return _data.max_size();
        }

        void reserve( size_type new_cap )
        {
            SW_SCOPED_RACE_WRITE();
            _data.reserve( new_cap );
        }

        size_type capacity() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return _data.capacity();
        }

        /** @brief 키와 같은 원소의 개수(0 또는 1)를 반환합니다. */
        template <typename K>
        size_type count( const K& key ) const
        {
            SW_SCOPED_RACE_READ();
            const ValueLess less{ _comp };
            auto            it = std::lower_bound( _data.begin(), _data.end(), key, less );
            if ( it != _data.end() && less( key, *it ) == false )
                return 1;
            return 0;
        }

        template <typename K>
        iterator find( const K& key )
        {
            SW_SCOPED_RACE_READ();
            const ValueLess less{ _comp };
            auto            it = std::lower_bound( _data.begin(), _data.end(), key, less );
            return ( it != _data.end() && less( key, *it ) == false ) ? it : _data.end();
        }

        template <typename K>
        const_iterator find( const K& key ) const
        {
            SW_SCOPED_RACE_READ();
            const ValueLess less{ _comp };
            auto            it = std::lower_bound( _data.begin(), _data.end(), key, less );
            return ( it != _data.end() && less( key, *it ) == false ) ? it : _data.end();
        }

        template <typename K>
        bool contains( const K& key ) const { return count( key ) > 0; }

        /** @brief 키와 같은 원소의 범위를 반환합니다(equal_range). */
        template <typename K>
        pair<iterator, iterator> equal_range( const K& key )
        {
            SW_SCOPED_RACE_READ();
            const ValueLess less{ _comp };
            auto            first = std::lower_bound( _data.begin(), _data.end(), key, less );
            auto            last  = first;
            if ( first != _data.end() && less( key, *first ) == false )
                ++last;
            return { first, last };
        }

        /** @brief 키와 같은 원소의 범위를 반환합니다(equal_range). */
        template <typename K>
        pair<const_iterator, const_iterator> equal_range( const K& key ) const
        {
            SW_SCOPED_RACE_READ();
            const ValueLess less{ _comp };
            auto            first = std::lower_bound( _data.begin(), _data.end(), key, less );
            auto            last  = first;
            if ( first != _data.end() && less( key, *first ) == false )
                ++last;
            return { first, last };
        }

        /** @brief 키보다 작지 않은 첫 원소를 가리키는 이터레이터를 반환합니다(lower_bound). */
        template <typename K>
        iterator lower_bound( const K& key )
        {
            SW_SCOPED_RACE_READ();
            return std::lower_bound( _data.begin(), _data.end(), key, ValueLess{ _comp } );
        }

        /** @brief 키보다 작지 않은 첫 원소를 가리키는 이터레이터를 반환합니다(lower_bound). */
        template <typename K>
        const_iterator lower_bound( const K& key ) const
        {
            SW_SCOPED_RACE_READ();
            return std::lower_bound( _data.begin(), _data.end(), key, ValueLess{ _comp } );
        }

        /** @brief 키보다 큰 첫 원소를 가리키는 이터레이터를 반환합니다(upper_bound). */
        template <typename K>
        iterator upper_bound( const K& key )
        {
            SW_SCOPED_RACE_READ();
            return std::upper_bound( _data.begin(), _data.end(), key, ValueLess{ _comp } );
        }

        /** @brief 키보다 큰 첫 원소를 가리키는 이터레이터를 반환합니다(upper_bound). */
        template <typename K>
        const_iterator upper_bound( const K& key ) const
        {
            SW_SCOPED_RACE_READ();
            return std::upper_bound( _data.begin(), _data.end(), key, ValueLess{ _comp } );
        }

        // ------------------------------------------------------------------------------
        // 3) 변경 — 정렬을 유지하며 insert/erase
        // ------------------------------------------------------------------------------
        void clear() noexcept
        {
            SW_SCOPED_RACE_WRITE();
            _data.clear();
        }

        pair<iterator, bool> insert( const value_type& value )
        {
            SW_SCOPED_RACE_WRITE();
            const ValueLess less{ _comp };
            auto            it = std::lower_bound( _data.begin(), _data.end(), value, less );
            if ( it != _data.end() && less( value, *it ) == false )
                return { it, false };
            it = _data.insert( it, value );
            return { it, true };
        }

        pair<iterator, bool> insert( value_type&& value )
        {
            SW_SCOPED_RACE_WRITE();
            const ValueLess less{ _comp };
            auto            it = std::lower_bound( _data.begin(), _data.end(), value, less );
            if ( it != _data.end() && less( value, *it ) == false )
                return { it, false };
            it = _data.insert( it, std::move( value ) );
            return { it, true };
        }

        iterator insert( [[maybe_unused]] const_iterator hint, const value_type& value ) { return insert( value ).first; }

        iterator insert( [[maybe_unused]] const_iterator hint, value_type&& value ) { return insert( std::move( value ) ).first; }

        /**
         * @brief [first, last) 를 넣습니다. 키가 이미 있거나 범위 안에서 겹치면 먼저 온 것(있던 원소, 그다음 입력 순서) 하나만 남습니다.
         * @details 꼬리에 붙여 안정 정렬하고 겹친 키를 걸러 한 번 병합한다(`mergeAppendedRange`) — O(n log n)(감사 C-9: 원소마다 정렬 위치에 끼우면 O(n²)였다.
         *          std::map 범위 구축 · 언리얼 `TSortedMap` 과 같은 결과).
         */
        template <class InputIt>
        void insert( InputIt first, InputIt last )
        {
            SW_SCOPED_RACE_WRITE();
            const size_t oldSize = _data.size();
            for ( ; first != last; ++first )
                _data.emplace_back( *first );

            mergeAppendedRange( _data, oldSize, ValueLess{ _comp } );
        }

        void insert( std::initializer_list<value_type> ilist ) { insert( ilist.begin(), ilist.end() ); }

        template <class... Args>
        pair<iterator, bool> emplace( Args&&... args )
        {
            value_type val( std::forward<Args>( args )... );
            return insert( std::move( val ) );
        }

        /** @brief 원소를 제자리에서 생성해 삽입합니다. 힌트는 쓰지 않습니다. */
        template <class... Args>
        iterator emplace_hint( [[maybe_unused]] const_iterator hint, Args&&... args ) { return emplace( std::forward<Args>( args )... ).first; }

        iterator erase( iterator pos )
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX( pos - _data.begin(), _data.size() );
            return _data.erase( pos );
        }

        iterator erase( const_iterator pos )
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX( pos - _data.cbegin(), _data.size() );
            return _data.erase( pos );
        }

        iterator erase( const_iterator first, const_iterator last )
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX_OR_END( last - _data.cbegin(), _data.size() );
            SW_CHECK_INDEX_OR_END( first - _data.cbegin(), last - _data.cbegin() );
            return _data.erase( first, last );
        }

        /** @brief 키와 같은 원소를 제거하고, 제거한 개수(0 또는 1)를 반환합니다. */
        template <typename K>
        size_type erase( const K& key )
        {
            SW_SCOPED_RACE_WRITE();
            const ValueLess less{ _comp };
            auto            it = std::lower_bound( _data.begin(), _data.end(), key, less );
            if ( it != _data.end() && less( key, *it ) == false )
            {
                _data.erase( it );
                return 1;
            }
            return 0;
        }

        void swap( SortedVectorBase& other ) noexcept
        {
            SW_SCOPED_RACE_WRITE();
            SW_SCOPED_RACE_WRITE_OTHER( other );
            _data.swap( other._data );
            std::swap( _comp, other._comp );
        }

    protected:
        /** @brief 내용을 @p ilist 로 바꿉니다 — 파생 형식의 `operator=( initializer_list )` 가 자기 형식을 돌려주며 부릅니다. */
        void assign( std::initializer_list<value_type> ilist )
        {
            SW_SCOPED_RACE_WRITE();
            clear();
            insert( ilist.begin(), ilist.end() );
        }

    private:
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

    protected:
        Container _data;
        Compare   _comp;
        SW_RACE_CTX_MEMBER
    };
} // namespace sw::internal
