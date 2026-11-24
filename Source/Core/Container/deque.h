/**
 * @file deque.h
 * @brief 양 끝에서 넣고 빼는 줄 sw::deque 입니다(std::deque 와 인터페이스 호환). 디버그 빌드에서는 RaceDetectContext 로 동시 접근을 잡아냅니다.
 * @details 공개 API · 레이스 검사 · 범위 밖 계약은 이 클래스 하나에 있고 저장만 **여기 한 자리** 에서 구성(`SW_ENABLE_STL_CONTAINER`)으로 고른다: 기본은 2 의 거듭제곱 연속 링
 *          (`Native/NativeDequeStorage.h` — 언리얼 5 `TDeque` · Rust `VecDeque` 자리), 켜면 std::deque 를 멤버로 든 `Std/StdDequeStorage.h`.
 *          두 저장의 보장이 다른 곳(주소 안정)은 약한 쪽이 공통 계약이다 — 넣은 뒤에는 참조가 무효다.
 */
#pragma once
#include "Core/Concurrency/DataRaceDetector.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Memory/Memory.h"

#if defined( SW_ENABLE_STL_CONTAINER )
    #include "Core/Container/Std/StdDequeStorage.h"
#else
    #include "Core/Container/Native/NativeDequeStorage.h"
#endif

namespace sw
{
    /**
     * @brief 양 끝 넣기 · 빼기가 O(1) 인 줄입니다. API 는 std::deque 와 같고, 다른 점은 하나 — 커지면 원소를 옮기므로 넣은 뒤에는 참조가 무효다(반복자는 번호라 같은 자리를 가리킨다).
     * @details 주소가 그대로여야 하는 원소(옮길 수 없거나 바깥이 포인터를 든다)는 `ChunkedArray` 를 쓴다 — 옮길 수 없는 원소는 여기서 컴파일되지 않는다.
     */
    template <typename T, typename Allocator = ContainerAllocator<T>>
    class deque
    {
        using Storage = internal::DequeStorage<T, Allocator>;
        SW_RACE_CTX_MEMBER

        template <bool kConst>
        class Iterator
        {
            using Owner = std::conditional_t<kConst, const deque, deque>;

        public:
            using iterator_category = std::random_access_iterator_tag;
            using value_type        = T;
            using difference_type   = std::ptrdiff_t;
            using pointer           = std::conditional_t<kConst, const T*, T*>;
            using reference         = std::conditional_t<kConst, const T&, T&>;

            Iterator() = default;
            Iterator( Owner* pOwner, size_t index )
                : _pOwner{ pOwner }
                , _index{ index }
            {
            }
            /** @brief iterator → const_iterator 변환입니다. */
            template <bool kOtherConst, typename = std::enable_if_t<kConst && kOtherConst == false>>
            Iterator( const Iterator<kOtherConst>& other )
                : _pOwner{ other._pOwner }
                , _index{ other._index }
            {
            }

            // 끝 · 앞을 넘은 반복자를 역참조하면 링이 마스크로 감겨 남의 원소를 조용히 읽는다 — 개발 구성에서 알린다
            reference operator*() const
            {
                SW_CHECK_INDEX( _index, _pOwner->_storage.size() );
                return *_pOwner->_storage.slotAt( _index );
            }
            pointer operator->() const
            {
                SW_CHECK_INDEX( _index, _pOwner->_storage.size() );
                return _pOwner->_storage.slotAt( _index );
            }
            reference operator[]( difference_type offset ) const
            {
                SW_CHECK_INDEX( static_cast<difference_type>( _index ) + offset, _pOwner->_storage.size() );
                return *_pOwner->_storage.slotAt( _index + offset );
            }

            Iterator& operator++()
            {
                ++_index;
                return *this;
            }
            Iterator operator++( int )
            {
                Iterator previous = *this;
                ++_index;
                return previous;
            }
            Iterator& operator--()
            {
                --_index;
                return *this;
            }
            Iterator operator--( int )
            {
                Iterator previous = *this;
                --_index;
                return previous;
            }
            Iterator& operator+=( difference_type offset )
            {
                _index = static_cast<size_t>( static_cast<difference_type>( _index ) + offset );
                return *this;
            }
            Iterator& operator-=( difference_type offset ) { return *this += -offset; }
            Iterator  operator+( difference_type offset ) const
            {
                Iterator result = *this;
                return result += offset;
            }
            friend Iterator operator+( difference_type offset, const Iterator& iter ) { return iter + offset; }
            Iterator        operator-( difference_type offset ) const
            {
                Iterator result = *this;
                return result -= offset;
            }
            difference_type operator-( const Iterator& other ) const { return static_cast<difference_type>( _index ) - static_cast<difference_type>( other._index ); }

            bool operator==( const Iterator& other ) const { return _index == other._index; }
            bool operator!=( const Iterator& other ) const { return _index != other._index; }
            bool operator<( const Iterator& other ) const { return _index < other._index; }
            bool operator>( const Iterator& other ) const { return _index > other._index; }
            bool operator<=( const Iterator& other ) const { return _index <= other._index; }
            bool operator>=( const Iterator& other ) const { return _index >= other._index; }

        private:
            template <bool>
            friend class Iterator;
            friend class deque;

            Owner* _pOwner = nullptr;
            size_t _index  = 0; ///< 앞(`front`)에서 센 논리 번호
        };

    public:
        using value_type             = T;
        using allocator_type         = Allocator;
        using size_type              = size_t;
        using difference_type        = std::ptrdiff_t;
        using reference              = T&;
        using const_reference        = const T&;
        using pointer                = T*;
        using const_pointer          = const T*;
        using iterator               = Iterator<false>;
        using const_iterator         = Iterator<true>;
        using reverse_iterator       = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        // ------------------------------------------------------------------------------
        // 1) 생성 · 대입
        // ------------------------------------------------------------------------------
        deque() noexcept( noexcept( Allocator() ) )
            : _storage{ Allocator() }
        {
        }

        explicit deque( const Allocator& alloc ) noexcept
            : _storage{ alloc }
        {
        }

        deque( size_type count, const T& value, const Allocator& alloc = Allocator() )
            : _storage{ alloc }
        {
            _storage.resize( count, value );
        }

        explicit deque( size_type count, const Allocator& alloc = Allocator() )
            : _storage{ alloc }
        {
            _storage.resize( count );
        }

        template <class InputIt, typename = std::enable_if_t<kIsInputIterator<InputIt>>>
        deque( InputIt first, InputIt last, const Allocator& alloc = Allocator() )
            : _storage{ alloc }
        {
            for ( ; first != last; ++first )
                emplace_back( *first );
        }

        deque( std::initializer_list<T> init, const Allocator& alloc = Allocator() )
            : deque( init.begin(), init.end(), alloc )
        {
        }

        deque( const deque& other )
            : _storage{ other._storage }
        {
            SW_SCOPED_RACE_READ_OTHER( other );
        }

        deque( const deque& other, const Allocator& alloc )
            : _storage{ other._storage, alloc }
        {
            SW_SCOPED_RACE_READ_OTHER( other );
        }

        deque( deque&& other ) noexcept
            : _storage{ std::move( other._storage ) }
        {
            SW_SCOPED_RACE_WRITE_OTHER( other );
        }

        ~deque() = default;

        deque& operator=( const deque& other )
        {
            if ( this != &other )
            {
                SW_SCOPED_RACE_WRITE();
                SW_SCOPED_RACE_READ_OTHER( other );
                _storage = other._storage;
            }
            return *this;
        }

        deque& operator=( deque&& other ) noexcept
        {
            if ( this != &other )
            {
                SW_SCOPED_RACE_WRITE();
                SW_SCOPED_RACE_WRITE_OTHER( other );
                _storage = std::move( other._storage );
            }
            return *this;
        }

        deque& operator=( std::initializer_list<T> ilist )
        {
            assign( ilist.begin(), ilist.end() );
            return *this;
        }

        void assign( size_type count, const T& value )
        {
            SW_SCOPED_RACE_WRITE();
            const T valueCopy = value; // 이 deque 의 원소일 수 있다
            _storage.clear();
            _storage.resize( count, valueCopy );
        }

        template <class InputIt, typename = std::enable_if_t<kIsInputIterator<InputIt>>>
        void assign( InputIt first, InputIt last )
        {
            deque gathered( first, last, _storage.get_allocator() ); // 범위가 이 deque 안일 수 있어 먼저 뜬다
            *this = std::move( gathered );
        }

        void assign( std::initializer_list<T> ilist ) { assign( ilist.begin(), ilist.end() ); }

        // ------------------------------------------------------------------------------
        // 2) 조회 — const 가 아닌 접근은 참조가 밖으로 나가므로 쓰기 가드를 잡는다
        // ------------------------------------------------------------------------------
        allocator_type get_allocator() const noexcept { return _storage.get_allocator(); }

        reference at( size_type pos )
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX( pos, _storage.size() );
            return *_storage.slotAt( pos );
        }

        const_reference at( size_type pos ) const
        {
            SW_SCOPED_RACE_READ();
            SW_CHECK_INDEX( pos, _storage.size() );
            return *_storage.slotAt( pos );
        }

        /** @brief pos 위치의 원소를 반환합니다(범위는 개발 구성에서만 확인). */
        reference operator[]( size_type pos )
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX( pos, _storage.size() );
            return *_storage.slotAt( pos );
        }

        /** @brief pos 위치의 원소를 반환합니다(범위는 개발 구성에서만 확인). */
        const_reference operator[]( size_type pos ) const
        {
            SW_SCOPED_RACE_READ();
            SW_CHECK_INDEX( pos, _storage.size() );
            return *_storage.slotAt( pos );
        }

        reference front()
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX( 0, _storage.size() );
            return *_storage.slotAt( 0 );
        }

        const_reference front() const
        {
            SW_SCOPED_RACE_READ();
            SW_CHECK_INDEX( 0, _storage.size() );
            return *_storage.slotAt( 0 );
        }

        reference back()
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX( 0, _storage.size() );
            return *_storage.slotAt( _storage.size() - 1 );
        }

        const_reference back() const
        {
            SW_SCOPED_RACE_READ();
            SW_CHECK_INDEX( 0, _storage.size() );
            return *_storage.slotAt( _storage.size() - 1 );
        }

        iterator               begin() noexcept { return iterator( this, 0 ); }
        const_iterator         begin() const noexcept { return const_iterator( this, 0 ); }
        const_iterator         cbegin() const noexcept { return begin(); }
        iterator               end() noexcept { return iterator( this, _storage.size() ); }
        const_iterator         end() const noexcept { return const_iterator( this, _storage.size() ); }
        const_iterator         cend() const noexcept { return end(); }
        reverse_iterator       rbegin() noexcept { return reverse_iterator( end() ); }
        const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator( end() ); }
        const_reverse_iterator crbegin() const noexcept { return rbegin(); }
        reverse_iterator       rend() noexcept { return reverse_iterator( begin() ); }
        const_reverse_iterator rend() const noexcept { return const_reverse_iterator( begin() ); }
        const_reverse_iterator crend() const noexcept { return rend(); }

        bool empty() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return _storage.size() == 0;
        }

        size_type size() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return _storage.size();
        }

        size_type max_size() const noexcept { return Storage::AllocTraits::max_size( _storage.get_allocator() ); }
        size_type capacity() const noexcept { return _storage.capacity(); }

        /** @brief @p count 개를 담을 용량을 미리 잡습니다(std 구성의 std::deque 는 용량이 없어 아무것도 하지 않는다). */
        void reserve( size_type count )
        {
            SW_SCOPED_RACE_WRITE();
            _storage.reserve( count );
        }

        void shrink_to_fit()
        {
            SW_SCOPED_RACE_WRITE();
            _storage.shrink_to_fit();
        }

        // ------------------------------------------------------------------------------
        // 3) 변경
        // ------------------------------------------------------------------------------
        void clear() noexcept
        {
            SW_SCOPED_RACE_WRITE();
            _storage.clear();
        }

        template <class... Args>
        reference emplace_back( Args&&... args )
        {
            SW_SCOPED_RACE_WRITE();
            return _storage.emplaceBack( std::forward<Args>( args )... );
        }

        template <class... Args>
        reference emplace_front( Args&&... args )
        {
            SW_SCOPED_RACE_WRITE();
            return _storage.emplaceFront( std::forward<Args>( args )... );
        }

        void push_back( const T& value ) { emplace_back( value ); }
        void push_back( T&& value ) { emplace_back( std::move( value ) ); }
        void push_front( const T& value ) { emplace_front( value ); }
        void push_front( T&& value ) { emplace_front( std::move( value ) ); }

        void pop_back()
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX( 0, _storage.size() );
            if ( _storage.size() == 0 )
                return;
            _storage.popBack();
        }

        void pop_front()
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX( 0, _storage.size() );
            if ( _storage.size() == 0 )
                return;
            _storage.popFront();
        }

        iterator insert( const_iterator pos, const T& value ) { return emplace( pos, value ); }
        iterator insert( const_iterator pos, T&& value ) { return emplace( pos, std::move( value ) ); }

        iterator insert( const_iterator pos, size_type count, const T& value )
        {
            const T valueCopy = value;
            return insertGap( pos._index, count, [&valueCopy]( size_t ) -> const T&
            { return valueCopy; } );
        }

        template <class InputIt, typename = std::enable_if_t<kIsInputIterator<InputIt>>>
        iterator insert( const_iterator pos, InputIt first, InputIt last )
        {
            // 범위가 이 deque 안일 수 있고 입력 반복자는 개수를 모른다 — 연속 버퍼로 먼저 뜬다(가운데 범위 삽입은 드물다).
            vector<T> listGathered;
            for ( ; first != last; ++first )
                listGathered.emplace_back( *first );
            return insertGap( pos._index, listGathered.size(), [&listGathered]( size_t index ) -> T&&
            { return std::move( listGathered[index] ); } );
        }

        iterator insert( const_iterator pos, std::initializer_list<T> ilist ) { return insert( pos, ilist.begin(), ilist.end() ); }

        template <class... Args>
        iterator emplace( const_iterator pos, Args&&... args )
        {
            const size_t index = pos._index;
            SW_CHECK_INDEX_OR_END( index, _storage.size() );
            if ( index == 0 )
            {
                emplace_front( std::forward<Args>( args )... );
                return begin();
            }
            if ( index >= _storage.size() )
            {
                emplace_back( std::forward<Args>( args )... );
                return end() - 1;
            }
            T value( std::forward<Args>( args )... );
            return insertGap( index, 1, [&value]( size_t ) -> T&&
            { return std::move( value ); } );
        }

        iterator erase( const_iterator pos ) { return erase( pos, pos + 1 ); }

        iterator erase( const_iterator first, const_iterator last )
        {
            SW_SCOPED_RACE_WRITE();
            const size_t index = first._index;
            const size_t count = last._index - first._index;
            SW_CHECK_INDEX_OR_END( last._index, _storage.size() ); // erase( end() ) 는 last 가 끝 + 1 이 되어 여기서 걸린다
            SW_CHECK_INDEX_OR_END( first._index, last._index );    // 뒤집힌 쌍
            if ( index > _storage.size() || last._index > _storage.size() || last._index < first._index || count == 0 )
                return iterator( this, index );
            _storage.erase( index, count );
            return iterator( this, index );
        }

        void resize( size_type count )
        {
            SW_SCOPED_RACE_WRITE();
            _storage.resize( count );
        }

        void resize( size_type count, const value_type& value )
        {
            SW_SCOPED_RACE_WRITE();
            const T valueCopy = value;
            _storage.resize( count, valueCopy );
        }

        void swap( deque& other ) noexcept
        {
            SW_SCOPED_RACE_WRITE();
            SW_SCOPED_RACE_WRITE_OTHER( other );
            _storage.swap( other._storage );
        }

    private:
        /** @brief [@p index, @p index + @p count) 자리를 비우고 @p source( i ) 로 채웁니다. 범위 밖이면 단언하고 끝에 붙입니다. */
        template <typename TSource>
        iterator insertGap( size_t index, size_t count, TSource&& source )
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX_OR_END( index, _storage.size() );
            if ( index > _storage.size() )
                index = _storage.size();
            if ( count > 0 )
                _storage.insertGap( index, count, std::forward<TSource>( source ) );
            return iterator( this, index );
        }

    private:
        Storage _storage;
    };

    template <typename T, typename Allocator>
    bool operator==( const deque<T, Allocator>& lhs, const deque<T, Allocator>& rhs )
    {
        return lhs.size() == rhs.size() && std::equal( lhs.begin(), lhs.end(), rhs.begin() );
    }

    template <typename T, typename Allocator>
    bool operator!=( const deque<T, Allocator>& lhs, const deque<T, Allocator>& rhs )
    {
        return ( lhs == rhs ) == false;
    }
} // namespace sw
