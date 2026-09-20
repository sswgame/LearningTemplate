/**
 * @file vector.h
 * @brief 독자적인 sw::vector 구현체 (std::vector 인터페이스 호환).
 * @details 디버그에서 RaceDetectContext 로 동시 접근을 잡으며, InlineAllocator 와 함께 사용 시 SBO 를 지원합니다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/DataRaceDetector.h"
#include "Core/Container/InlineAllocator.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

namespace sw
{
#if defined( SW_ENABLE_STL_CONTAINER )
    template <typename T, typename Allocator = std::allocator<T>>
    using vector = std::vector<T, Allocator>;
#else

    template <typename Alloc, typename = void>
    struct has_inline_allocator : std::false_type
    {
    };

    template <typename Alloc>
    struct has_inline_allocator<Alloc, std::void_t<decltype( std::declval<Alloc&>().get_inline_buffer() )>> : std::true_type
    {
    };

    /**
     * @brief 원소를 바이트 단위로 통째로 옮겨도 되는 타입인가.
     * @details 복사에도 소멸에도 사용자 코드가 없다는 뜻이다. 둘 다일 때만 "원소마다 placement new"
     *          루프를 `Memory::copy` 한 번으로 바꿀 수 있다 — 하나라도 아니면 생성자·소멸자가
     *          돌아야 하므로 루프가 정본이다.
     * @note 실측: `GpuInstance`(96바이트) 20,000 개를 옮기는 데 원소 루프 329us, 바이트 복사 ~130us.
     *       게임 스레드가 매 프레임 렌더 패킷에 스냅샷을 싣는 자리라 그대로 프레임 시간이었다.
     */
    template <typename T>
    inline constexpr bool is_bitwise_copyable_v = std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>;

    template <typename T, typename Allocator = sw::Allocator<T>>
    /** @brief 커스텀 vector. API 는 STL 과 같으며 SBO 및 레이스 탐지를 지원합니다. */
    class vector : private Allocator
    {
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
        vector() noexcept( noexcept( Allocator() ) );
        explicit vector( const Allocator& alloc ) noexcept;
        vector( size_type count, const T& value, const Allocator& alloc = Allocator() );
        explicit vector( size_type count, const Allocator& alloc = Allocator() );
        template <class InputIt, typename std::enable_if_t<!std::is_integral_v<InputIt>, int32> = 0>
        vector( InputIt first, InputIt last, const Allocator& alloc = Allocator() );
        vector( const vector& other );
        vector( vector&& other ) noexcept;
        vector( std::initializer_list<T> init, const Allocator& alloc = Allocator() );
        ~vector();

        vector& operator=( const vector& other );
        vector& operator=( vector&& other ) noexcept;
        vector& operator=( std::initializer_list<T> ilist );
        void    assign( size_type count, const T& value );
        template <class InputIt, typename std::enable_if_t<!std::is_integral_v<InputIt>, int32> = 0>
        void assign( InputIt first, InputIt last );
        void assign( std::initializer_list<T> ilist );

        // ------------------------------------------------------------------------------
        // 2) 조회
        // ------------------------------------------------------------------------------
        allocator_type         get_allocator() const noexcept;
        reference              at( size_type pos );
        const_reference        at( size_type pos ) const;
        reference              operator[]( size_type pos );
        const_reference        operator[]( size_type pos ) const;
        reference              front();
        const_reference        front() const;
        reference              back();
        const_reference        back() const;
        T*                     data() noexcept;
        const T*               data() const noexcept;
        iterator               begin() noexcept;
        const_iterator         begin() const noexcept;
        const_iterator         cbegin() const noexcept;
        iterator               end() noexcept;
        const_iterator         end() const noexcept;
        const_iterator         cend() const noexcept;
        reverse_iterator       rbegin() noexcept;
        const_reverse_iterator rbegin() const noexcept;
        const_reverse_iterator crbegin() const noexcept;
        reverse_iterator       rend() noexcept;
        const_reverse_iterator rend() const noexcept;
        const_reverse_iterator crend() const noexcept;
        bool                   empty() const noexcept;
        size_type              size() const noexcept;
        size_type              capacity() const noexcept;
        size_type              max_size() const noexcept;
        void                   reserve( size_type new_cap );
        void                   shrink_to_fit();

        // ------------------------------------------------------------------------------
        // 3) 변경
        // ------------------------------------------------------------------------------
        void     clear() noexcept;
        iterator insert( const_iterator pos, const T& value );
        iterator insert( const_iterator pos, T&& value );
        iterator insert( const_iterator pos, size_type count, const T& value );
        template <class InputIt, typename std::enable_if_t<!std::is_integral_v<InputIt>, int32> = 0>
        iterator insert( const_iterator pos, InputIt first, InputIt last );
        iterator insert( const_iterator pos, std::initializer_list<T> ilist );
        template <class... Args>
        iterator emplace( const_iterator pos, Args&&... args );
        iterator erase( const_iterator pos );
        iterator erase( const_iterator first, const_iterator last );
        void     push_back( const T& value );
        void     push_back( T&& value );
        template <class... Args>
        reference emplace_back( Args&&... args );
        void      pop_back();
        void      resize( size_type count );
        void      resize( size_type count, const value_type& value );
        void      swap( vector& other ) noexcept;

        // ------------------------------------------------------------------------------
        // 4) 비교 연산자
        // ------------------------------------------------------------------------------
        bool operator==( const vector& other ) const;
        bool operator!=( const vector& other ) const;

    private:
        constexpr bool has_inline_buffer() const;
        T*             get_inline_ptr();
        const T*       get_inline_ptr() const;
        size_t         get_inline_cap() const;
        bool           is_inline( const T* p ) const;
        T*             do_allocate( size_t n );
        void           do_deallocate( T* p, size_t n );
        void           reserveInternal( size_t new_cap );
        /** @brief 비어 있는(size=0, 용량 확보된) 버퍼 앞에서부터 count 개를 복사해 넣습니다. */
        void copyFromInternal( const T* pSource, size_t count );
        void clearInternal() noexcept;

    private:
        SW_RACE_CTX_MEMBER

        T*     _pData    = nullptr;
        size_t _size     = 0;
        size_t _capacity = 0;
    };

    // ------------------------------------------------------------------------------
    // 구현부 (Implementation)
    // ------------------------------------------------------------------------------

    template <typename T, typename Allocator>
    inline constexpr bool vector<T, Allocator>::has_inline_buffer() const
    {
        return has_inline_allocator<Allocator>::value;
    }

    template <typename T, typename Allocator>
    inline T* vector<T, Allocator>::get_inline_ptr()
    {
        if constexpr ( has_inline_allocator<Allocator>::value )
            return this->Allocator::get_inline_buffer();
        return nullptr;
    }

    template <typename T, typename Allocator>
    inline const T* vector<T, Allocator>::get_inline_ptr() const
    {
        if constexpr ( has_inline_allocator<Allocator>::value )
            return const_cast<vector*>( this )->Allocator::get_inline_buffer();
        return nullptr;
    }

    template <typename T, typename Allocator>
    inline size_t vector<T, Allocator>::get_inline_cap() const
    {
        if constexpr ( has_inline_allocator<Allocator>::value )
            return const_cast<vector*>( this )->Allocator::get_inline_capacity();
        return 0;
    }

    template <typename T, typename Allocator>
    inline bool vector<T, Allocator>::is_inline( const T* p ) const
    {
        return has_inline_buffer() && p == get_inline_ptr();
    }

    template <typename T, typename Allocator>
    inline T* vector<T, Allocator>::do_allocate( size_t n )
    {
        return Allocator::allocate( n );
    }

    template <typename T, typename Allocator>
    inline void vector<T, Allocator>::do_deallocate( T* p, size_t n )
    {
        if ( p )
            Allocator::deallocate( p, n );
    }

    template <typename T, typename Allocator>
    inline void vector<T, Allocator>::reserveInternal( size_t new_cap )
    {
        if ( new_cap <= _capacity )
            return;
        T* pNewData = do_allocate( new_cap );
        if constexpr ( is_bitwise_copyable_v<T> )
        {
            // 생성자도 소멸자도 할 일이 없는 타입이면 옛 버퍼는 그냥 바이트다. 새 버퍼와 겹치지
            // 않으므로(방금 할당했다) copy 로 충분하다.
            if ( _size > 0 )
                Memory::copy( pNewData, _pData, _size * sizeof( T ) );
        }
        else
        {
            // 1·3번 분기의 본문이 같다 — 중복이 아니라 **순서가 규약이다.** 이동이 던질 수 있으면
            // 복사를 먼저 고른다(복사는 실패해도 원본이 남아 되돌릴 수 있다). 복사조차 안 되면 그때
            // 던지는 이동을 쓴다. 셋을 합치면 이 우선순위가 사라진다.
            for ( size_t index = 0; index < _size; ++index )
            {
                if constexpr ( std::is_nothrow_move_constructible_v<T> )
                    // NOLINTNEXTLINE(bugprone-branch-clone)
                    sw_placement_new( ( pNewData + ( index ) ) ) T( std::move( _pData[index] ) );
                else if constexpr ( std::is_copy_constructible_v<T> )
                    sw_placement_new( ( pNewData + ( index ) ) ) T( _pData[index] );
                else if constexpr ( std::is_move_constructible_v<T> )
                    sw_placement_new( ( pNewData + ( index ) ) ) T( std::move( _pData[index] ) );
                _pData[index].~T();
            }
        }
        if ( is_inline( _pData ) == false )
            do_deallocate( _pData, _capacity );
        _pData    = pNewData;
        _capacity = new_cap;
    }

    template <typename T, typename Allocator>
    inline void vector<T, Allocator>::copyFromInternal( const T* pSource, size_t count )
    {
        if ( count == 0 )
            return;
        if constexpr ( is_bitwise_copyable_v<T> )
            Memory::copy( _pData, pSource, count * sizeof( T ) );
        else
        {
            for ( size_t index = 0; index < count; ++index )
                sw_placement_new( ( _pData + ( index ) ) ) T( pSource[index] );
        }
    }

    template <typename T, typename Allocator>
    inline void vector<T, Allocator>::clearInternal() noexcept
    {
        for ( size_t index = 0; index < _size; ++index )
            _pData[index].~T();
        _size = 0;
    }

    template <typename T, typename Allocator>
    inline vector<T, Allocator>::vector() noexcept( noexcept( Allocator() ) )
        : Allocator()
    {
        if ( has_inline_buffer() )
        {
            _pData    = get_inline_ptr();
            _capacity = get_inline_cap();
        }
    }

    template <typename T, typename Allocator>
    inline vector<T, Allocator>::vector( const Allocator& alloc ) noexcept
        : Allocator( alloc )
    {
        if ( has_inline_buffer() )
        {
            _pData    = get_inline_ptr();
            _capacity = get_inline_cap();
        }
    }

    template <typename T, typename Allocator>
    inline vector<T, Allocator>::vector( size_type count, const T& value, const Allocator& alloc )
        : Allocator( alloc )
    {
        if ( has_inline_buffer() )
        {
            _pData    = get_inline_ptr();
            _capacity = get_inline_cap();
        }
        assign( count, value );
    }

    template <typename T, typename Allocator>
    inline vector<T, Allocator>::vector( size_type count, const Allocator& alloc )
        : Allocator( alloc )
    {
        if ( has_inline_buffer() )
        {
            _pData    = get_inline_ptr();
            _capacity = get_inline_cap();
        }
        resize( count );
    }

    template <typename T, typename Allocator>
    template <class InputIt, typename std::enable_if_t<!std::is_integral_v<InputIt>, int32>>
    inline vector<T, Allocator>::vector( InputIt first, InputIt last, const Allocator& alloc )
        : Allocator( alloc )
    {
        if ( has_inline_buffer() )
        {
            _pData    = get_inline_ptr();
            _capacity = get_inline_cap();
        }
        assign( first, last );
    }

    template <typename T, typename Allocator>
    inline vector<T, Allocator>::vector( const vector& other )
        : Allocator( std::allocator_traits<Allocator>::select_on_container_copy_construction( other.get_allocator() ) )
    {
        SW_SCOPED_RACE_READ_OTHER( other );
        if ( has_inline_buffer() )
        {
            _pData    = get_inline_ptr();
            _capacity = get_inline_cap();
        }
        reserveInternal( other._size );
        copyFromInternal( other._pData, other._size );
        _size = other._size;
    }

    template <typename T, typename Allocator>
    inline vector<T, Allocator>::vector( vector&& other ) noexcept
        : Allocator( std::move( other.get_allocator() ) )
    {
        SW_SCOPED_RACE_WRITE_OTHER( other );
        if ( other.is_inline( other._pData ) )
        {
            _pData    = get_inline_ptr();
            _capacity = get_inline_cap();
            for ( size_t index = 0; index < other._size; ++index )
            {
                sw_placement_new( ( _pData + ( index ) ) ) T( std::move( other._pData[index] ) );
                other._pData[index].~T();
            }
            _size       = other._size;
            other._size = 0;
        }
        else
        {
            _pData    = other._pData;
            _size     = other._size;
            _capacity = other._capacity;
            if ( other.has_inline_buffer() )
            {
                other._pData    = other.get_inline_ptr();
                other._capacity = other.get_inline_cap();
            }
            else
            {
                other._pData    = nullptr;
                other._capacity = 0;
            }
            other._size = 0;
        }
    }

    template <typename T, typename Allocator>
    inline vector<T, Allocator>::vector( std::initializer_list<T> init, const Allocator& alloc )
        : Allocator( alloc )
    {
        if ( has_inline_buffer() )
        {
            _pData    = get_inline_ptr();
            _capacity = get_inline_cap();
        }
        assign( init.begin(), init.end() );
    }

    template <typename T, typename Allocator>
    inline vector<T, Allocator>::~vector()
    {
        clearInternal();
        if ( is_inline( _pData ) == false )
            do_deallocate( _pData, _capacity );
    }

    template <typename T, typename Allocator>
    inline vector<T, Allocator>& vector<T, Allocator>::operator=( const vector& other )
    {
        if ( this != &other )
        {
            SW_SCOPED_RACE_WRITE();
            SW_SCOPED_RACE_READ_OTHER( other );
            // 용량이 넉넉하면 겹치는 앞부분은 **대입**한다 — 원소 안의 힙(문자열·벡터·맵)이 제 용량을 남긴다.
            // 예전에는 전부 부수고 새로 만들었다: 프레임마다 링 자리와 바꿔 가며 다시 채우는 스냅샷의 그룹 목록이
            // 겉 벡터는 용량을 남기면서도 안의 문자열은 매번 새로 할당했다(std::vector 의 대입과 같은 규칙으로 맞춘다).
            if constexpr ( std::is_copy_assignable_v<T> && is_bitwise_copyable_v<T> == false )
            {
                if ( other._size <= _capacity )
                {
                    const size_t assignCount = ( _size < other._size ) ? _size : other._size;
                    for ( size_t index = 0; index < assignCount; ++index )
                        _pData[index] = other._pData[index];
                    for ( size_t index = assignCount; index < other._size; ++index )
                        sw_placement_new( ( _pData + ( index ) ) ) T( other._pData[index] );
                    for ( size_t index = other._size; index < _size; ++index )
                        _pData[index].~T();
                    _size = other._size;
                    return *this;
                }
            }
            clearInternal();
            reserveInternal( other._size );
            copyFromInternal( other._pData, other._size );
            _size = other._size;
        }
        return *this;
    }

    template <typename T, typename Allocator>
    inline vector<T, Allocator>& vector<T, Allocator>::operator=( vector&& other ) noexcept
    {
        if ( this != &other )
        {
            SW_SCOPED_RACE_WRITE();
            SW_SCOPED_RACE_WRITE_OTHER( other );
            clearInternal();
            if ( is_inline( _pData ) == false )
            {
                do_deallocate( _pData, _capacity );
                if ( has_inline_buffer() )
                {
                    _pData    = get_inline_ptr();
                    _capacity = get_inline_cap();
                }
                else
                {
                    _pData    = nullptr;
                    _capacity = 0;
                }
            }

            if ( other.is_inline( other._pData ) )
            {
                for ( size_t index = 0; index < other._size; ++index )
                {
                    sw_placement_new( ( _pData + ( index ) ) ) T( std::move( other._pData[index] ) );
                    other._pData[index].~T();
                }
            }
            else
            {
                _pData    = other._pData;
                _capacity = other._capacity;
                if ( other.has_inline_buffer() )
                {
                    other._pData    = other.get_inline_ptr();
                    other._capacity = other.get_inline_cap();
                }
                else
                {
                    other._pData    = nullptr;
                    other._capacity = 0;
                }
            }
            _size       = other._size;
            other._size = 0;
        }
        return *this;
    }

    template <typename T, typename Allocator>
    inline vector<T, Allocator>& vector<T, Allocator>::operator=( std::initializer_list<T> ilist )
    {
        assign( ilist.begin(), ilist.end() );
        return *this;
    }

    template <typename T, typename Allocator>
    inline void vector<T, Allocator>::assign( size_type count, const T& value )
    {
        SW_SCOPED_RACE_WRITE();
        clearInternal();
        reserveInternal( count );
        for ( size_t index = 0; index < count; ++index )
        {
            sw_placement_new( ( _pData + ( index ) ) ) T( value );
        }
        _size = count;
    }

    template <typename T, typename Allocator>
    template <class InputIt, typename std::enable_if_t<!std::is_integral_v<InputIt>, int32>>
    inline void vector<T, Allocator>::assign( InputIt first, InputIt last )
    {
        SW_SCOPED_RACE_WRITE();
        clearInternal();
        size_t count = static_cast<size_t>( std::distance( first, last ) );
        reserveInternal( count );
        for ( auto it = first; it != last; ++it )
        {
            sw_placement_new( ( _pData + ( _size++ ) ) ) T( *it );
        }
    }

    template <typename T, typename Allocator>
    inline void vector<T, Allocator>::assign( std::initializer_list<T> ilist )
    {
        assign( ilist.begin(), ilist.end() );
    }

    template <typename T, typename Allocator>
    inline typename vector<T, Allocator>::allocator_type vector<T, Allocator>::get_allocator() const noexcept
    {
        SW_SCOPED_RACE_READ();
        return *this;
    }

    template <typename T, typename Allocator>
    inline typename vector<T, Allocator>::reference vector<T, Allocator>::at( size_type pos )
    {
        SW_SCOPED_RACE_WRITE();
        SW_ASSERT( pos < _size );
        return _pData[pos];
    }

    template <typename T, typename Allocator>
    inline typename vector<T, Allocator>::const_reference vector<T, Allocator>::at( size_type pos ) const
    {
        SW_SCOPED_RACE_READ();
        SW_ASSERT( pos < _size );
        return _pData[pos];
    }

    template <typename T, typename Allocator>
    inline typename vector<T, Allocator>::reference vector<T, Allocator>::operator[]( size_type pos )
    {
        SW_SCOPED_RACE_WRITE();
        SW_ASSERT( pos < _size );
        return _pData[pos];
    }

    template <typename T, typename Allocator>
    inline typename vector<T, Allocator>::const_reference vector<T, Allocator>::operator[]( size_type pos ) const
    {
        SW_SCOPED_RACE_READ();
        SW_ASSERT( pos < _size );
        return _pData[pos];
    }

    template <typename T, typename Allocator>
    inline typename vector<T, Allocator>::reference vector<T, Allocator>::front()
    {
        SW_SCOPED_RACE_WRITE();
        SW_ASSERT( _size > 0 );
        return _pData[0];
    }

    template <typename T, typename Allocator>
    inline typename vector<T, Allocator>::const_reference vector<T, Allocator>::front() const
    {
        SW_SCOPED_RACE_READ();
        SW_ASSERT( _size > 0 );
        return _pData[0];
    }

    template <typename T, typename Allocator>
    inline typename vector<T, Allocator>::reference vector<T, Allocator>::back()
    {
        SW_SCOPED_RACE_WRITE();
        SW_ASSERT( _size > 0 );
        return _pData[_size - 1];
    }

    template <typename T, typename Allocator>
    inline typename vector<T, Allocator>::const_reference vector<T, Allocator>::back() const
    {
        SW_SCOPED_RACE_READ();
        SW_ASSERT( _size > 0 );
        return _pData[_size - 1];
    }

    template <typename T, typename Allocator>
    inline T* vector<T, Allocator>::data() noexcept
    {
        SW_SCOPED_RACE_WRITE();
        return _pData;
    }

    template <typename T, typename Allocator>
    inline const T* vector<T, Allocator>::data() const noexcept
    {
        SW_SCOPED_RACE_READ();
        return _pData;
    }

    template <typename T, typename Allocator>
    inline typename vector<T, Allocator>::iterator vector<T, Allocator>::begin() noexcept
    {
        SW_SCOPED_RACE_READ();
        return _pData;
    }

    template <typename T, typename Allocator>
    inline typename vector<T, Allocator>::const_iterator vector<T, Allocator>::begin() const noexcept
    {
        SW_SCOPED_RACE_READ();
        return _pData;
    }

    template <typename T, typename Allocator>
    inline typename vector<T, Allocator>::const_iterator vector<T, Allocator>::cbegin() const noexcept
    {
        SW_SCOPED_RACE_READ();
        return _pData;
    }

    template <typename T, typename Allocator>
    inline typename vector<T, Allocator>::iterator vector<T, Allocator>::end() noexcept
    {
        SW_SCOPED_RACE_READ();
        return _pData + _size;
    }

    template <typename T, typename Allocator>
    inline typename vector<T, Allocator>::const_iterator vector<T, Allocator>::end() const noexcept
    {
        SW_SCOPED_RACE_READ();
        return _pData + _size;
    }

    template <typename T, typename Allocator>
    inline typename vector<T, Allocator>::const_iterator vector<T, Allocator>::cend() const noexcept
    {
        SW_SCOPED_RACE_READ();
        return _pData + _size;
    }

    template <typename T, typename Allocator>
    inline typename vector<T, Allocator>::reverse_iterator vector<T, Allocator>::rbegin() noexcept
    {
        SW_SCOPED_RACE_READ();
        return reverse_iterator( end() );
    }

    template <typename T, typename Allocator>
    inline typename vector<T, Allocator>::const_reverse_iterator vector<T, Allocator>::rbegin() const noexcept
    {
        SW_SCOPED_RACE_READ();
        return const_reverse_iterator( end() );
    }

    template <typename T, typename Allocator>
    inline typename vector<T, Allocator>::const_reverse_iterator vector<T, Allocator>::crbegin() const noexcept
    {
        SW_SCOPED_RACE_READ();
        return const_reverse_iterator( cend() );
    }

    template <typename T, typename Allocator>
    inline typename vector<T, Allocator>::reverse_iterator vector<T, Allocator>::rend() noexcept
    {
        SW_SCOPED_RACE_READ();
        return reverse_iterator( begin() );
    }

    template <typename T, typename Allocator>
    inline typename vector<T, Allocator>::const_reverse_iterator vector<T, Allocator>::rend() const noexcept
    {
        SW_SCOPED_RACE_READ();
        return const_reverse_iterator( begin() );
    }

    template <typename T, typename Allocator>
    inline typename vector<T, Allocator>::const_reverse_iterator vector<T, Allocator>::crend() const noexcept
    {
        SW_SCOPED_RACE_READ();
        return const_reverse_iterator( cbegin() );
    }

    template <typename T, typename Allocator>
    inline bool vector<T, Allocator>::empty() const noexcept
    {
        SW_SCOPED_RACE_READ();
        return _size == 0;
    }

    template <typename T, typename Allocator>
    inline typename vector<T, Allocator>::size_type vector<T, Allocator>::size() const noexcept
    {
        SW_SCOPED_RACE_READ();
        return _size;
    }

    template <typename T, typename Allocator>
    inline typename vector<T, Allocator>::size_type vector<T, Allocator>::capacity() const noexcept
    {
        SW_SCOPED_RACE_READ();
        return _capacity;
    }

    template <typename T, typename Allocator>
    inline typename vector<T, Allocator>::size_type vector<T, Allocator>::max_size() const noexcept
    {
        return size_type( -1 ) / sizeof( T );
    }

    template <typename T, typename Allocator>
    inline void vector<T, Allocator>::reserve( size_type new_cap )
    {
        SW_SCOPED_RACE_WRITE();
        reserveInternal( new_cap );
    }

    template <typename T, typename Allocator>
    inline void vector<T, Allocator>::shrink_to_fit()
    {
        SW_SCOPED_RACE_WRITE();
        if ( _capacity > _size && is_inline( _pData ) == false )
        {
            if ( has_inline_buffer() && _size <= get_inline_cap() )
            {
                T* pNewData = get_inline_ptr();
                for ( size_t index = 0; index < _size; ++index )
                {
                    if constexpr ( std::is_nothrow_move_constructible_v<T> )
                        sw_placement_new( ( pNewData + ( index ) ) ) T( std::move( _pData[index] ) );
                    else if constexpr ( std::is_copy_constructible_v<T> )
                        sw_placement_new( ( pNewData + ( index ) ) ) T( _pData[index] );
                    else if constexpr ( std::is_move_constructible_v<T> )
                        sw_placement_new( ( pNewData + ( index ) ) ) T( std::move( _pData[index] ) );
                    _pData[index].~T();
                }
                do_deallocate( _pData, _capacity );
                _pData    = pNewData;
                _capacity = get_inline_cap();
            }
            else
            {
                if ( _size == 0 )
                    return;
                T* pNewData = do_allocate( _size );
                for ( size_t index = 0; index < _size; ++index )
                {
                    if constexpr ( std::is_nothrow_move_constructible_v<T> )
                        sw_placement_new( ( pNewData + ( index ) ) ) T( std::move( _pData[index] ) );
                    else if constexpr ( std::is_copy_constructible_v<T> )
                        sw_placement_new( ( pNewData + ( index ) ) ) T( _pData[index] );
                    else if constexpr ( std::is_move_constructible_v<T> )
                        sw_placement_new( ( pNewData + ( index ) ) ) T( std::move( _pData[index] ) );
                    _pData[index].~T();
                }
                do_deallocate( _pData, _capacity );
                _pData    = pNewData;
                _capacity = _size;
            }
        }
    }

    template <typename T, typename Allocator>
    inline void vector<T, Allocator>::clear() noexcept
    {
        SW_SCOPED_RACE_WRITE();
        clearInternal();
    }

    template <typename T, typename Allocator>
    inline typename vector<T, Allocator>::iterator vector<T, Allocator>::insert( const_iterator pos, const T& value )
    {
        return insert( pos, 1, value );
    }

    template <typename T, typename Allocator>
    inline typename vector<T, Allocator>::iterator vector<T, Allocator>::insert( const_iterator pos, T&& value )
    {
        SW_SCOPED_RACE_WRITE();
        const size_t offset = static_cast<size_t>( pos - _pData );
        SW_ASSERT( offset <= _size );
        // `SW_ASSERT` 는 Release 에서 통째로 사라진다 — `erase` 가 같은 이유로 진짜 가드를 들고 있다.
        if ( offset > _size )
            return _pData + _size;

        // **`value` 가 이 벡터 안의 원소일 수 있다.** 형제들(`push_back` 둘 · `emplace_back` ·
        // `insert( pos, count, value )`)은 전부 손대기 전에 떠 두는데 이 오버로드만 빠져 있었다.
        // 여기서는 재할당이 없어도 위험하다: 아래 밀기 루프가 `value` 가 가리키는 칸을 **먼저**
        // 덮으므로(`v.insert( v.begin(), std::move( v[2] ) )`) 엉뚱한 값이 들어간다. 재할당까지
        // 겹치면 옛 버퍼가 해제된 뒤라 죽은 자리를 읽는다.
        T movedValue( std::move( value ) );

        if ( _size >= _capacity )
            reserveInternal( _capacity == 0 ? 4 : _capacity * 2 );

        if ( offset < _size )
        {
            sw_placement_new( ( _pData + ( _size ) ) ) T( std::move( _pData[_size - 1] ) );
            for ( size_t itemIndex = _size - 1; itemIndex > offset; --itemIndex )
            {
                _pData[itemIndex] = std::move( _pData[itemIndex - 1] );
            }
            _pData[offset] = std::move( movedValue );
        }
        else
        {
            sw_placement_new( ( _pData + ( offset ) ) ) T( std::move( movedValue ) );
        }
        ++_size;
        return _pData + offset;
    }

    template <typename T, typename Allocator>
    inline typename vector<T, Allocator>::iterator vector<T, Allocator>::insert( const_iterator pos, size_type count, const T& value )
    {
        SW_SCOPED_RACE_WRITE();
        const size_t offset = static_cast<size_t>( pos - _pData );
        SW_ASSERT( offset <= _size );
        // `erase` 와 같은 이유 — Release 에서는 위 단언이 없으므로 아래 `_size - offset` 이 뒤집힌다.
        if ( offset > _size )
            return _pData + _size;

        // 0개 끼워 넣기는 아무 일도 하지 않는다. 예전엔 여기서 빠져나가지 않아 아래 "뒤로 밀기"
        // 루프의 종료 조건이 `itemIndex >= offset + 0` 이 되었고, offset 이 0 이면 그것이 **언제나
        // 참**이라 인덱스가 0 에서 한 번 더 줄어 size_t 로 뒤집혔다 — 범위 밖에 계속 쓰면서 끝나지
        // 않았다(ASan: heap-buffer-overflow).
        if ( count == 0 )
            return _pData + offset;

        // `value` 가 **이 벡터 안의 원소**일 수 있다(`v.insert( v.begin(), 3, v[0] )` 는 적법하다).
        // 아래에서 그 자리를 덮어쓰고, 그 전에 reserveInternal 이 버퍼를 통째로 옮길 수도 있다.
        // 그래서 손대기 전에 값으로 떠 둔다.
        const T valueCopy = value;

        // `_size + count` 가 넘치면 **더 작은** 값이 되어 확보를 건너뛰고, 아래 밀기 루프가
        // `fromIndex + count` 라는 범위 밖 주소에 쓴다. 담을 수 없는 개수는 여기서 끝낸다.
        if ( count > ( ~size_t{ 0 } ) - _size )
            return _pData + offset;

        if ( _size + count > _capacity )
            reserveInternal( MathUtil::max( _capacity * 2, _size + count ) );

        // 1) 뒤쪽 원소들을 count 칸 뒤로 민다. **뒤에서부터** 가야 아직 안 읽은 원소를 덮지 않는다.
        //    목적지가 아직 살아 있는 칸이면 이동 대입, 미초기화 칸이면 placement new 다.
        //    예전에는 이 구분을 `itemIndex - count >= offset` 으로 했는데, `count > _size` 면 그
        //    뺄셈이 뒤집혀 조건이 언제나 참이 되고 **없는 원소에서 move 해 왔다**
        //    (`{10}` 에 `insert( begin, 2, 7 )` 이면 바로 걸린다).
        const size_t tailCount = _size - offset;
        for ( size_t movedCount = 0; movedCount < tailCount; ++movedCount )
        {
            const size_t fromIndex = _size - 1 - movedCount;
            const size_t toIndex   = fromIndex + count;
            if ( toIndex >= _size )
                sw_placement_new( ( _pData + ( toIndex ) ) ) T( std::move( _pData[fromIndex] ) );
            else
                _pData[toIndex] = std::move( _pData[fromIndex] );
        }

        // 2) 빈 자리 [offset, offset + count) 를 채운다. 원래 살아 있던 칸은 대입, 그 뒤는 생성.
        for ( size_t index = 0; index < count; ++index )
        {
            const size_t targetIndex = offset + index;
            if ( targetIndex < _size )
                _pData[targetIndex] = valueCopy;
            else
                sw_placement_new( ( _pData + ( targetIndex ) ) ) T( valueCopy );
        }

        _size += count;
        return _pData + offset;
    }

    template <typename T, typename Allocator>
    template <class InputIt, typename std::enable_if_t<!std::is_integral_v<InputIt>, int32>>
    inline typename vector<T, Allocator>::iterator vector<T, Allocator>::insert( const_iterator pos, InputIt first, InputIt last )
    {
        size_t offset = static_cast<size_t>( pos - _pData );
        for ( auto it = first; it != last; ++it )
        {
            insert( _pData + offset, *it );
            ++offset;
        }
        return _pData + ( pos - _pData );
    }

    template <typename T, typename Allocator>
    inline typename vector<T, Allocator>::iterator vector<T, Allocator>::insert( const_iterator pos, std::initializer_list<T> ilist )
    {
        return insert( pos, ilist.begin(), ilist.end() );
    }

    template <typename T, typename Allocator>
    template <class... Args>
    inline typename vector<T, Allocator>::iterator vector<T, Allocator>::emplace( const_iterator pos, Args&&... args )
    {
        return insert( pos, T( std::forward<Args>( args )... ) );
    }

    template <typename T, typename Allocator>
    inline typename vector<T, Allocator>::iterator vector<T, Allocator>::erase( const_iterator pos )
    {
        SW_SCOPED_RACE_WRITE();
        size_t offset = static_cast<size_t>( pos - _pData );
        SW_ASSERT( offset < _size );
        // `SW_ASSERT` 는 Release 에서 **통째로 사라진다.** 그 뒤의 `_size - 1` 은 빈 벡터에서
        // 뒤집히므로, 배포본에서만 범위 밖을 훑게 된다.
        if ( offset >= _size )
            return _pData + _size;
        for ( size_t itemIndex = offset; itemIndex < _size - 1; ++itemIndex )
        {
            _pData[itemIndex] = std::move( _pData[itemIndex + 1] );
        }
        _pData[_size - 1].~T();
        --_size;
        return _pData + offset;
    }

    template <typename T, typename Allocator>
    inline typename vector<T, Allocator>::iterator vector<T, Allocator>::erase( const_iterator first, const_iterator last )
    {
        SW_SCOPED_RACE_WRITE();
        size_t offset = static_cast<size_t>( first - _pData );
        size_t count  = static_cast<size_t>( last - first );
        SW_ASSERT( offset + count <= _size );
        // **뺄셈으로 잰다.** `offset + count` 는 `size_t` 안에서 넘칠 수 있다 — `last < first` 인
        // 이터레이터 쌍이면 `last - first` 가 음수라 `count` 가 거대한 값이 되고, 그 합이 작은
        // 수로 접혀 이 가드를 그냥 지나간다. 그러면 아래 `_size - count` 도 뒤집혀 루프가 범위
        // 밖을 쓴다. 같은 모양을 `fixed_string::erase` 에서도 고쳤다.
        if ( offset > _size || count > _size - offset )
            return _pData + _size;
        if ( count > 0 )
        {
            for ( size_t itemIndex = offset; itemIndex < _size - count; ++itemIndex )
            {
                _pData[itemIndex] = std::move( _pData[itemIndex + count] );
            }
            for ( size_t itemIndex = _size - count; itemIndex < _size; ++itemIndex )
            {
                _pData[itemIndex].~T();
            }
            _size -= count;
        }
        return _pData + offset;
    }

    template <typename T, typename Allocator>
    inline void vector<T, Allocator>::push_back( const T& value )
    {
        SW_SCOPED_RACE_WRITE();
        if ( _size >= _capacity )
        {
            T copy = value;
            reserveInternal( _capacity == 0 ? 4 : _capacity * 2 );
            sw_placement_new( ( _pData + ( _size ) ) ) T( std::move( copy ) );
        }
        else
        {
            sw_placement_new( ( _pData + ( _size ) ) ) T( value );
        }
        ++_size;
    }

    template <typename T, typename Allocator>
    inline void vector<T, Allocator>::push_back( T&& value )
    {
        SW_SCOPED_RACE_WRITE();
        if ( _size >= _capacity )
        {
            T temp( std::move( value ) );
            reserveInternal( _capacity == 0 ? 4 : _capacity * 2 );
            sw_placement_new( ( _pData + ( _size ) ) ) T( std::move( temp ) );
        }
        else
        {
            sw_placement_new( ( _pData + ( _size ) ) ) T( std::move( value ) );
        }
        ++_size;
    }

    template <typename T, typename Allocator>
    template <class... Args>
    inline typename vector<T, Allocator>::reference vector<T, Allocator>::emplace_back( Args&&... args )
    {
        SW_SCOPED_RACE_WRITE();
        if ( _size >= _capacity )
        {
            T temp( std::forward<Args>( args )... );
            reserveInternal( _capacity == 0 ? 4 : _capacity * 2 );
            sw_placement_new( ( _pData + ( _size ) ) ) T( std::move( temp ) );
        }
        else
        {
            sw_placement_new( ( _pData + ( _size ) ) ) T( std::forward<Args>( args )... );
        }
        ++_size;
        return _pData[_size - 1];
    }

    template <typename T, typename Allocator>
    inline void vector<T, Allocator>::pop_back()
    {
        SW_SCOPED_RACE_WRITE();
        SW_ASSERT( _size > 0 );
        // 단언은 Release 에서 사라진다. 그 뒤의 `_size - 1` 은 빈 벡터에서 뒤집혀
        // `_pData[SIZE_MAX]` 의 소멸자를 부른다 — `erase` 가 막아 둔 것과 같은 모양이다.
        if ( _size == 0 )
            return;
        _pData[_size - 1].~T();
        --_size;
    }

    template <typename T, typename Allocator>
    inline void vector<T, Allocator>::resize( size_type count )
    {
        SW_SCOPED_RACE_WRITE();
        if ( count < _size )
        {
            for ( size_t itemIndex = count; itemIndex < _size; ++itemIndex )
                _pData[itemIndex].~T();
        }
        else if ( count > _size )
        {
            reserveInternal( count );
            for ( size_t itemIndex = _size; itemIndex < count; ++itemIndex )
                sw_placement_new( ( _pData + ( itemIndex ) ) ) T();
        }
        _size = count;
    }

    template <typename T, typename Allocator>
    inline void vector<T, Allocator>::resize( size_type count, const value_type& value )
    {
        SW_SCOPED_RACE_WRITE();
        if ( count < _size )
        {
            for ( size_t itemIndex = count; itemIndex < _size; ++itemIndex )
                _pData[itemIndex].~T();
        }
        else if ( count > _size )
        {
            reserveInternal( count );
            for ( size_t itemIndex = _size; itemIndex < count; ++itemIndex )
                sw_placement_new( ( _pData + ( itemIndex ) ) ) T( value );
        }
        _size = count;
    }

    template <typename T, typename Allocator>
    inline void vector<T, Allocator>::swap( vector& other ) noexcept
    {
        SW_SCOPED_RACE_WRITE();
        SW_SCOPED_RACE_WRITE_OTHER( other );

        if ( is_inline( _pData ) || other.is_inline( other._pData ) )
        {
            vector temp = std::move( *this );
            *this       = std::move( other );
            other       = std::move( temp );
        }
        else
        {
            std::swap( _pData, other._pData );
            std::swap( _size, other._size );
            std::swap( _capacity, other._capacity );
        }
    }

    template <typename T, typename Allocator>
    inline bool vector<T, Allocator>::operator==( const vector& other ) const
    {
        SW_SCOPED_RACE_READ();
        SW_SCOPED_RACE_READ_OTHER( other );
        if ( _size != other._size )
            return false;
        for ( size_t index = 0; index < _size; ++index )
        {
            if ( ( _pData[index] == other._pData[index] ) == false )
                return false;
        }
        return true;
    }

    template <typename T, typename Allocator>
    inline bool vector<T, Allocator>::operator!=( const vector& other ) const
    {
        return ( *this == other ) == false;
    }

#endif
    template <typename T, size_t N = 32>
    using small_vector = vector<T, InlineAllocator<T, N>>;
} // namespace sw
