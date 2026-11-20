/**
 * @file deque.h
 * @brief 2 의 거듭제곱 연속 링 버퍼 deque 입니다(언리얼 5 `TDeque` · Rust `VecDeque` 자리). 디버그 빌드에서는 RaceDetectContext 로 동시 접근을 잡아냅니다.
 */
#pragma once
#include "Core/Concurrency/DataRaceDetector.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /**
     * @brief 양 끝 넣기 · 빼기가 O(1) 인 연속 링입니다. API 는 std::deque 와 같고, 다른 점은 하나 — 커지면 원소를 옮기므로 넣은 뒤에는 참조 · 반복자가 무효다.
     * @details std::deque 래퍼를 버린 까닭(감사 C-9): MSVC STL 의 블록은 16 B 라 8 B 넘는 원소마다 힙 할당이 하나씩 붙는다. 링은 용량이 찬 뒤로는 할당하지 않는다.
     *          주소가 그대로여야 하는 원소(옮길 수 없거나 바깥이 포인터를 든다)는 `ChunkedArray` 를 쓴다 — 옮길 수 없는 원소는 여기서 컴파일되지 않는다.
     */
    template <typename T, typename Allocator = ContainerAllocator<T>>
    class deque
    {
        using AllocTraits = std::allocator_traits<Allocator>;
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
                SW_CHECK_INDEX( _index, _pOwner->_size );
                return *_pOwner->slotAt( _index );
            }
            pointer operator->() const
            {
                SW_CHECK_INDEX( _index, _pOwner->_size );
                return _pOwner->slotAt( _index );
            }
            reference operator[]( difference_type offset ) const
            {
                SW_CHECK_INDEX( static_cast<difference_type>( _index ) + offset, _pOwner->_size );
                return *_pOwner->slotAt( _index + offset );
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
            : _allocator{}
        {
        }

        explicit deque( const Allocator& alloc ) noexcept
            : _allocator{ alloc }
        {
        }

        deque( size_type count, const T& value, const Allocator& alloc = Allocator() )
            : _allocator{ alloc }
        {
            assignFill( count, value );
        }

        explicit deque( size_type count, const Allocator& alloc = Allocator() )
            : _allocator{ alloc }
        {
            resize( count );
        }

        template <class InputIt, typename = std::enable_if_t<kIsInputIterator<InputIt>>>
        deque( InputIt first, InputIt last, const Allocator& alloc = Allocator() )
            : _allocator{ alloc }
        {
            for ( ; first != last; ++first )
                emplace_back( *first );
        }

        deque( std::initializer_list<T> init, const Allocator& alloc = Allocator() )
            : deque( init.begin(), init.end(), alloc )
        {
        }

        deque( const deque& other )
            : _allocator{ AllocTraits::select_on_container_copy_construction( other._allocator ) }
        {
            SW_SCOPED_RACE_READ_OTHER( other );
            copyFrom( other );
        }

        deque( const deque& other, const Allocator& alloc )
            : _allocator{ alloc }
        {
            SW_SCOPED_RACE_READ_OTHER( other );
            copyFrom( other );
        }

        deque( deque&& other ) noexcept
            : _allocator{ std::move( other._allocator ) }
        {
            SW_SCOPED_RACE_WRITE_OTHER( other );
            stealFrom( other );
        }

        ~deque() { releaseStorage(); }

        deque& operator=( const deque& other )
        {
            if ( this != &other )
            {
                SW_SCOPED_RACE_WRITE();
                SW_SCOPED_RACE_READ_OTHER( other );
                clearInternal();
                copyFrom( other );
            }
            return *this;
        }

        deque& operator=( deque&& other ) noexcept
        {
            if ( this != &other )
            {
                SW_SCOPED_RACE_WRITE();
                SW_SCOPED_RACE_WRITE_OTHER( other );
                releaseStorage();
                _allocator = std::move( other._allocator );
                stealFrom( other );
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
            clearInternal();
            assignFill( count, valueCopy );
        }

        template <class InputIt, typename = std::enable_if_t<kIsInputIterator<InputIt>>>
        void assign( InputIt first, InputIt last )
        {
            deque gathered( first, last, _allocator ); // 범위가 이 deque 안일 수 있어 먼저 뜬다
            *this = std::move( gathered );
        }

        void assign( std::initializer_list<T> ilist ) { assign( ilist.begin(), ilist.end() ); }

        // ------------------------------------------------------------------------------
        // 2) 조회 — const 가 아닌 접근은 참조가 밖으로 나가므로 쓰기 가드를 잡는다
        // ------------------------------------------------------------------------------
        allocator_type get_allocator() const noexcept { return _allocator; }

        reference at( size_type pos )
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX( pos, _size );
            return *slotAt( pos );
        }

        const_reference at( size_type pos ) const
        {
            SW_SCOPED_RACE_READ();
            SW_CHECK_INDEX( pos, _size );
            return *slotAt( pos );
        }

        /** @brief pos 위치의 원소를 반환합니다(범위는 개발 구성에서만 확인). */
        reference operator[]( size_type pos )
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX( pos, _size );
            return *slotAt( pos );
        }

        /** @brief pos 위치의 원소를 반환합니다(범위는 개발 구성에서만 확인). */
        const_reference operator[]( size_type pos ) const
        {
            SW_SCOPED_RACE_READ();
            SW_CHECK_INDEX( pos, _size );
            return *slotAt( pos );
        }

        reference front()
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX( 0, _size );
            return *slotAt( 0 );
        }

        const_reference front() const
        {
            SW_SCOPED_RACE_READ();
            SW_CHECK_INDEX( 0, _size );
            return *slotAt( 0 );
        }

        reference back()
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX( 0, _size );
            return *slotAt( _size - 1 );
        }

        const_reference back() const
        {
            SW_SCOPED_RACE_READ();
            SW_CHECK_INDEX( 0, _size );
            return *slotAt( _size - 1 );
        }

        iterator               begin() noexcept { return iterator( this, 0 ); }
        const_iterator         begin() const noexcept { return const_iterator( this, 0 ); }
        const_iterator         cbegin() const noexcept { return begin(); }
        iterator               end() noexcept { return iterator( this, _size ); }
        const_iterator         end() const noexcept { return const_iterator( this, _size ); }
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
            return _size == 0;
        }

        size_type size() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return _size;
        }

        size_type max_size() const noexcept { return AllocTraits::max_size( _allocator ); }
        size_type capacity() const noexcept { return _capacity; }

        /** @brief @p count 개를 담을 용량을 미리 잡습니다(2 의 거듭제곱으로 올린다). */
        void reserve( size_type count )
        {
            SW_SCOPED_RACE_WRITE();
            if ( count > _capacity )
                growTo( count );
        }

        void shrink_to_fit()
        {
            SW_SCOPED_RACE_WRITE();
            if ( _size == 0 )
            {
                releaseStorage();
                return;
            }
            if ( roundUpCapacity( _size ) < _capacity )
                moveToNewBuffer( roundUpCapacity( _size ) );
        }

        // ------------------------------------------------------------------------------
        // 3) 변경
        // ------------------------------------------------------------------------------
        void clear() noexcept
        {
            SW_SCOPED_RACE_WRITE();
            clearInternal();
        }

        template <class... Args>
        reference emplace_back( Args&&... args )
        {
            SW_SCOPED_RACE_WRITE();
            if ( _size == _capacity )
            {
                T value( std::forward<Args>( args )... ); // 인자가 이 deque 의 원소일 수 있다 — 키우기 전에 뜬다
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
        reference emplace_front( Args&&... args )
        {
            SW_SCOPED_RACE_WRITE();
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

        void push_back( const T& value ) { emplace_back( value ); }
        void push_back( T&& value ) { emplace_back( std::move( value ) ); }
        void push_front( const T& value ) { emplace_front( value ); }
        void push_front( T&& value ) { emplace_front( std::move( value ) ); }

        void pop_back()
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX( 0, _size );
            if ( _size == 0 )
                return;
            --_size;
            AllocTraits::destroy( _allocator, slotAt( _size ) );
        }

        void pop_front()
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX( 0, _size );
            if ( _size == 0 )
                return;
            AllocTraits::destroy( _allocator, _pData + _head );
            _head = ( _head + 1 ) & ( _capacity - 1 );
            --_size;
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
            SW_CHECK_INDEX_OR_END( index, _size );
            if ( index == 0 )
            {
                emplace_front( std::forward<Args>( args )... );
                return begin();
            }
            if ( index >= _size )
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
            SW_CHECK_INDEX_OR_END( last._index, _size );        // erase( end() ) 는 last 가 끝 + 1 이 되어 여기서 걸린다
            SW_CHECK_INDEX_OR_END( first._index, last._index ); // 뒤집힌 쌍
            if ( index > _size || last._index > _size || last._index < first._index || count == 0 )
                return iterator( this, index );

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
            return iterator( this, index );
        }

        void resize( size_type count )
        {
            SW_SCOPED_RACE_WRITE();
            if ( count > _capacity )
                growTo( count );
            while ( _size > count )
                AllocTraits::destroy( _allocator, slotAt( --_size ) );
            for ( ; _size < count; ++_size )
                AllocTraits::construct( _allocator, slotAt( _size ) );
        }

        void resize( size_type count, const value_type& value )
        {
            SW_SCOPED_RACE_WRITE();
            const T valueCopy = value;
            if ( count > _capacity )
                growTo( count );
            while ( _size > count )
                AllocTraits::destroy( _allocator, slotAt( --_size ) );
            for ( ; _size < count; ++_size )
                AllocTraits::construct( _allocator, slotAt( _size ), valueCopy );
        }

        void swap( deque& other ) noexcept
        {
            SW_SCOPED_RACE_WRITE();
            SW_SCOPED_RACE_WRITE_OTHER( other );
            std::swap( _allocator, other._allocator );
            std::swap( _pData, other._pData );
            std::swap( _capacity, other._capacity );
            std::swap( _head, other._head );
            std::swap( _size, other._size );
        }

    private:
        /** @brief 앞에서 @p index 번째 슬롯입니다(무검사 — 공개 입구가 `SW_CHECK_INDEX` 로 확인한 뒤, 또는 끝 너머 빈 슬롯을 만들 때 부른다). 용량은 2 의 거듭제곱이라 나머지 대신 마스크다. */
        T*       slotAt( size_t index ) { return _pData + ( ( _head + index ) & ( _capacity - 1 ) ); }
        const T* slotAt( size_t index ) const { return _pData + ( ( _head + index ) & ( _capacity - 1 ) ); }

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
            for ( size_t index = 0; index < _size; ++index )
            {
                T* pOld = slotAt( index );
                AllocTraits::construct( _allocator, pNewData + index, std::move( *pOld ) );
                AllocTraits::destroy( _allocator, pOld );
            }
            if ( _pData != nullptr )
                AllocTraits::deallocate( _allocator, _pData, _capacity );
            _pData    = pNewData;
            _capacity = newCapacity;
            _head     = 0;
        }

        /**
         * @brief [@p index, @p index + @p count) 자리를 비우고 @p source( i ) 로 채웁니다. 뒤쪽 원소를 한 번만 민다.
         * @details 살아 있는 슬롯은 대입, 끝 너머 빈 슬롯은 생성한다(`vector::insert( pos, count, value )` 와 같은 규칙).
         */
        template <typename TSource>
        iterator insertGap( size_t index, size_t count, TSource&& source )
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX_OR_END( index, _size );
            if ( index > _size )
                index = _size;
            if ( count == 0 )
                return iterator( this, index );
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
            return iterator( this, index );
        }

        void assignFill( size_t count, const T& value )
        {
            if ( count > _capacity )
                growTo( count );
            for ( ; _size < count; ++_size )
                AllocTraits::construct( _allocator, slotAt( _size ), value );
        }

        void copyFrom( const deque& other )
        {
            if ( other._size > _capacity )
                growTo( other._size );
            for ( size_t index = 0; index < other._size; ++index )
            {
                AllocTraits::construct( _allocator, slotAt( index ), *other.slotAt( index ) );
                ++_size;
            }
        }

        void stealFrom( deque& other ) noexcept
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

        void clearInternal() noexcept
        {
            for ( size_t index = 0; index < _size; ++index )
                AllocTraits::destroy( _allocator, slotAt( index ) );
            _size = 0;
            _head = 0;
        }

        void releaseStorage() noexcept
        {
            clearInternal();
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
