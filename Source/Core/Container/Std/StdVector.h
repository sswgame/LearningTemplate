/**
 * @file StdVector.h
 * @brief sw::vector 의 std 구현입니다(`SW_ENABLE_STL_CONTAINER`). std::vector 를 멤버로 듭니다. 쓰는 쪽은 `Core/Container/vector.h` 를 include 합니다.
 * @details 공개 API · 계약은 자체 구현(Native/NativeVector.h)과 같다: 반복자는 맨 포인터, 범위 밖 위치는 개발 구성에서 단언하고 배포에서는 아무것도 하지 않거나 끝을 돌려준다,
 *          자기 원소 · 자기 구간 · 입력 반복자를 넣어도 된다, 옮긴 뒤에는 빈 목록이다. 성장 정책은 표준 라이브러리의 것이다.
 *          인라인 할당자는 std::vector 가 옮길 수 없어(버퍼가 할당자 안에 있다) 자체 구현을 저장으로 쓴다.
 */
#pragma once
#include "Core/Concurrency/DataRaceDetector.h"
#include "Core/Container/BitwiseRelocatable.h"
#include "Core/Container/Native/NativeVector.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /** @brief std::vector 를 멤버로 든 vector 입니다. API 는 자체 구현과 같습니다. */
    template <typename T, typename Allocator = ContainerAllocator<T>>
    class vector
    {
        using ElementCheck = typename VectorElementCheck<T>::type;
        using Storage      = std::conditional_t<has_inline_allocator<Allocator>::value, native::vector<T, Allocator>, std::vector<T, Allocator>>;

    public:
        using value_type             = T;
        using allocator_type         = Allocator;
        using size_type              = size_t;
        using difference_type        = ptrdiff_t;
        using reference              = T&;
        using const_reference        = const T&;
        using pointer                = T*;
        using const_pointer          = const T*;
        using iterator               = T*;
        using const_iterator         = const T*;
        using reverse_iterator       = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        // ------------------------------------------------------------------------------
        // 1) 생성 · 소멸
        // ------------------------------------------------------------------------------
        vector() noexcept( noexcept( Allocator() ) )
            : _storage() {}
        explicit vector( const Allocator& alloc ) noexcept
            : _storage( alloc ) {}
        vector( size_type count, const T& value, const Allocator& alloc = Allocator() )
            : _storage( count, value, alloc ) {}
        explicit vector( size_type count, const Allocator& alloc = Allocator() )
            : _storage( count, alloc ) {}
        template <class InputIt, typename std::enable_if_t<kIsInputIterator<InputIt>, int32> = 0>
        vector( InputIt first, InputIt last, const Allocator& alloc = Allocator() )
            : _storage( alloc )
        {
            appendRange( first, last );
        }
        vector( std::initializer_list<T> init, const Allocator& alloc = Allocator() )
            : _storage( alloc )
        {
            appendRange( init.begin(), init.end() );
        }
        vector( const vector& other )
            : _storage( other._storage ) {}
        vector( vector&& other ) noexcept
            : _storage( std::move( other._storage ) )
        {
            SW_SCOPED_RACE_WRITE_OTHER( other );
            other._storage.clear();
        }
        ~vector() = default;

        vector& operator=( const vector& other )
        {
            if ( this != &other )
            {
                SW_SCOPED_RACE_WRITE();
                SW_SCOPED_RACE_READ_OTHER( other );
                _storage = other._storage;
            }
            return *this;
        }
        vector& operator=( vector&& other ) noexcept
        {
            if ( this != &other )
            {
                SW_SCOPED_RACE_WRITE();
                SW_SCOPED_RACE_WRITE_OTHER( other );
                _storage = std::move( other._storage );
                other._storage.clear();
            }
            return *this;
        }
        vector& operator=( std::initializer_list<T> ilist )
        {
            assign( ilist.begin(), ilist.end() );
            return *this;
        }

        void assign( size_type count, const T& value )
        {
            SW_SCOPED_RACE_WRITE();
            const T valueCopy( value );
            _storage.assign( count, valueCopy );
        }
        template <class InputIt, typename std::enable_if_t<kIsInputIterator<InputIt>, int32> = 0>
        void assign( InputIt first, InputIt last )
        {
            SW_SCOPED_RACE_WRITE();
            _storage.clear();
            appendRange( first, last );
        }
        void assign( std::initializer_list<T> ilist ) { assign( ilist.begin(), ilist.end() ); }

        // ------------------------------------------------------------------------------
        // 2) 조회
        // ------------------------------------------------------------------------------
        allocator_type  get_allocator() const noexcept { return _storage.get_allocator(); }
        reference       at( size_type pos ) { return ( *this )[pos]; }
        const_reference at( size_type pos ) const { return ( *this )[pos]; }
        reference       operator[]( size_type pos )
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX( pos, _storage.size() );
            return _storage.data()[pos];
        }
        const_reference operator[]( size_type pos ) const
        {
            SW_SCOPED_RACE_READ();
            SW_CHECK_INDEX( pos, _storage.size() );
            return _storage.data()[pos];
        }
        reference       front() { return ( *this )[0]; }
        const_reference front() const { return ( *this )[0]; }
        reference       back() { return ( *this )[_storage.size() - 1]; }
        const_reference back() const { return ( *this )[_storage.size() - 1]; }
        T*              data() noexcept
        {
            SW_SCOPED_RACE_WRITE();
            return _storage.data();
        }
        const T* data() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return _storage.data();
        }
        iterator begin() noexcept
        {
            SW_SCOPED_RACE_READ();
            return _storage.data();
        }
        const_iterator begin() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return _storage.data();
        }
        const_iterator cbegin() const noexcept { return begin(); }
        iterator       end() noexcept
        {
            SW_SCOPED_RACE_READ();
            return _storage.data() + _storage.size();
        }
        const_iterator end() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return _storage.data() + _storage.size();
        }
        const_iterator         cend() const noexcept { return end(); }
        reverse_iterator       rbegin() noexcept { return reverse_iterator( end() ); }
        const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator( end() ); }
        const_reverse_iterator crbegin() const noexcept { return rbegin(); }
        reverse_iterator       rend() noexcept { return reverse_iterator( begin() ); }
        const_reverse_iterator rend() const noexcept { return const_reverse_iterator( begin() ); }
        const_reverse_iterator crend() const noexcept { return rend(); }
        bool                   empty() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return _storage.empty();
        }
        size_type size() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return _storage.size();
        }
        size_type capacity() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return _storage.capacity();
        }
        size_type max_size() const noexcept { return std::numeric_limits<size_type>::max() / sizeof( T ); }
        void      reserve( size_type newCapacity )
        {
            SW_SCOPED_RACE_WRITE();
            _storage.reserve( newCapacity );
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
        iterator insert( const_iterator pos, const T& value ) { return insert( pos, 1, value ); }
        iterator insert( const_iterator pos, T&& value )
        {
            SW_SCOPED_RACE_WRITE();
            const size_t offset = toOffset( pos );
            if ( offset > _storage.size() )
                return _storage.data() + _storage.size();
            T movedValue( std::move( value ) ); // 이 벡터의 원소일 수 있다
            _storage.insert( _storage.begin() + static_cast<difference_type>( offset ), std::move( movedValue ) );
            return _storage.data() + offset;
        }
        iterator insert( const_iterator pos, size_type count, const T& value )
        {
            SW_SCOPED_RACE_WRITE();
            const size_t offset = toOffset( pos );
            if ( offset > _storage.size() )
                return _storage.data() + _storage.size();
            _storage.insert( _storage.begin() + static_cast<difference_type>( offset ), count, value );
            return _storage.data() + offset;
        }
        template <class InputIt, typename std::enable_if_t<kIsInputIterator<InputIt>, int32> = 0>
        iterator insert( const_iterator pos, InputIt first, InputIt last )
        {
            const size_t offset = toOffset( pos );
            if ( offset > _storage.size() )
                return _storage.data() + _storage.size();
            // 자기 구간 · 입력 반복자일 수 있고 값으로 명시적으로만 바뀌는 원소일 수 있다 — 먼저 모은 뒤 옮겨 넣는다.
            std::vector<T, ContainerAllocator<T>> listGathered;
            for ( ; first != last; ++first )
                listGathered.emplace_back( *first );
            SW_SCOPED_RACE_WRITE();
            _storage.insert( _storage.begin() + static_cast<difference_type>( offset ), std::make_move_iterator( listGathered.begin() ), std::make_move_iterator( listGathered.end() ) );
            return _storage.data() + offset;
        }
        iterator insert( const_iterator pos, std::initializer_list<T> ilist ) { return insert( pos, ilist.begin(), ilist.end() ); }
        template <class... Args>
        iterator emplace( const_iterator pos, Args&&... args )
        {
            return insert( pos, T( std::forward<Args>( args )... ) );
        }
        iterator erase( const_iterator pos )
        {
            SW_SCOPED_RACE_WRITE();
            const size_t offset = static_cast<size_t>( pos - _storage.data() );
            SW_CHECK_INDEX( offset, _storage.size() );
            if ( offset >= _storage.size() )
                return _storage.data() + _storage.size();
            _storage.erase( _storage.begin() + static_cast<difference_type>( offset ) );
            return _storage.data() + offset;
        }
        iterator erase( const_iterator first, const_iterator last )
        {
            SW_SCOPED_RACE_WRITE();
            const size_t offset = toOffset( first );
            const size_t count  = static_cast<size_t>( last - first );
            if ( offset <= _storage.size() )
                SW_CHECK_INDEX_OR_END( count, _storage.size() - offset );
            if ( offset > _storage.size() || count > _storage.size() - offset )
                return _storage.data() + _storage.size();
            const auto itFirst = _storage.begin() + static_cast<difference_type>( offset );
            _storage.erase( itFirst, itFirst + static_cast<difference_type>( count ) );
            return _storage.data() + offset;
        }
        void push_back( const T& value )
        {
            SW_SCOPED_RACE_WRITE();
            _storage.push_back( value );
        }
        void push_back( T&& value )
        {
            SW_SCOPED_RACE_WRITE();
            _storage.push_back( std::move( value ) );
        }
        template <class... Args>
        reference emplace_back( Args&&... args )
        {
            SW_SCOPED_RACE_WRITE();
            return _storage.emplace_back( std::forward<Args>( args )... );
        }
        void pop_back()
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX( 0, _storage.size() );
            if ( _storage.empty() == false )
                _storage.pop_back();
        }
        void resize( size_type count )
        {
            SW_SCOPED_RACE_WRITE();
            _storage.resize( count );
        }
        void resize( size_type count, const value_type& value )
        {
            SW_SCOPED_RACE_WRITE();
            const T valueCopy( value );
            _storage.resize( count, valueCopy );
        }
        /** @brief 크기를 @p count 로 바꿉니다. std::vector 는 초기화를 건너뛸 수 없어 늘어난 원소는 값 초기화된다(부르는 쪽이 곧 덮어쓰는 버퍼다). */
        void resizeForOverwrite( size_type count )
        {
            static_assert( std::is_trivially_default_constructible_v<T> && std::is_trivially_destructible_v<T>, "resizeForOverwrite: 초기화 · 소멸이 필요 없는 타입만 됩니다" );
            resize( count );
        }
        void swap( vector& other ) noexcept
        {
            SW_SCOPED_RACE_WRITE();
            SW_SCOPED_RACE_WRITE_OTHER( other );
            _storage.swap( other._storage );
        }

        // ------------------------------------------------------------------------------
        // 4) 비교 연산자
        // ------------------------------------------------------------------------------
        bool operator==( const vector& other ) const
        {
            SW_SCOPED_RACE_READ();
            SW_SCOPED_RACE_READ_OTHER( other );
            if ( _storage.size() != other._storage.size() )
                return false;
            for ( size_t index = 0; index < _storage.size(); ++index )
            {
                if ( ( _storage.data()[index] == other._storage.data()[index] ) == false )
                    return false;
            }
            return true;
        }
        bool operator!=( const vector& other ) const { return ( *this == other ) == false; }
        /** @brief 사전식 비교입니다(`std::vector` 와 같다 — 정렬 컨테이너의 키로 쓸 수 있게). 원소에 `operator<` 가 있어야 인스턴스화됩니다. */
        bool operator<( const vector& other ) const
        {
            SW_SCOPED_RACE_READ();
            SW_SCOPED_RACE_READ_OTHER( other );
            return std::lexicographical_compare( _storage.data(), _storage.data() + _storage.size(), other._storage.data(), other._storage.data() + other._storage.size() );
        }

    private:
        size_t toOffset( const_iterator pos ) const noexcept
        {
            const size_t offset = static_cast<size_t>( pos - _storage.data() );
            SW_CHECK_INDEX_OR_END( offset, _storage.size() );
            return offset;
        }

        /** @brief 구간을 끝에 붙입니다(값으로 명시적으로만 바뀌는 원소도 만든다). 순방향 반복자면 자리를 한 번에 잡습니다. */
        template <class InputIt>
        void appendRange( InputIt first, InputIt last )
        {
            if constexpr ( std::is_convertible_v<typename std::iterator_traits<InputIt>::iterator_category, std::forward_iterator_tag> )
                _storage.reserve( static_cast<size_type>( std::distance( first, last ) ) );
            for ( ; first != last; ++first )
                _storage.emplace_back( *first );
        }

    private:
        SW_RACE_CTX_MEMBER

        Storage _storage;
    };
} // namespace sw
