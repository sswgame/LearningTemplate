/**
 * @file ChunkedArray.h
 * @brief 끝에 붙여도 원소 주소가 움직이지 않는 배열입니다(언리얼 `TChunkedArray` 자리). 디버그 빌드에서는 RaceDetectContext 로 동시 접근을 잡아냅니다.
 */
#pragma once
#include "Core/Concurrency/DataRaceDetector.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /**
     * @brief 블록 단위로 할당해 `push_back` · `push_front` · `emplace_*` 뒤에도 이미 있는 원소의 주소가 그대로인 배열입니다. API 는 std::deque 와 같습니다.
     * @details 옮길 수 없는 원소(잠금을 품은 엔드포인트)나 바깥이 주소를 들고 있는 원소에 씁니다. FIFO 줄은 `deque`(연속 링)를 씁니다 — 이 배열은
     *          MSVC STL 블록(16 B)을 그대로 써 큰 원소마다 힙 할당이 하나씩 붙는다(감사 C-9). 가운데 `insert` · `erase` 는 주소를 지키지 않는다.
     *          상속은 private 이다 — 여기 다시 선언하지 않은 std::deque 의 함수(뒤 표준이 더하는 것 · 비교 연산)가 레이스 · 범위 검사 없이 불리지 않는다.
     */
    template <typename T, typename Allocator = ContainerAllocator<T>>
    class ChunkedArray : private std::deque<T, Allocator>
    {
        using Base = std::deque<T, Allocator>;
        SW_RACE_CTX_MEMBER

    public:
        using value_type             = typename Base::value_type;
        using allocator_type         = typename Base::allocator_type;
        using size_type              = typename Base::size_type;
        using difference_type        = typename Base::difference_type;
        using reference              = typename Base::reference;
        using const_reference        = typename Base::const_reference;
        using pointer                = typename Base::pointer;
        using const_pointer          = typename Base::const_pointer;
        using iterator               = typename Base::iterator;
        using const_iterator         = typename Base::const_iterator;
        using reverse_iterator       = typename Base::reverse_iterator;
        using const_reverse_iterator = typename Base::const_reverse_iterator;

        // ------------------------------------------------------------------------------
        // 1) 생성 · 대입 — 내용은 Base 에 두고, 레이스 컨텍스트는 인스턴스마다 따로 둔다
        // ------------------------------------------------------------------------------
        ChunkedArray() noexcept( noexcept( Allocator() ) )
            : Base() {}

        explicit ChunkedArray( const Allocator& alloc ) noexcept
            : Base( alloc ) {}

        ChunkedArray( size_type count, const T& value, const Allocator& alloc = Allocator() )
            : Base( count, value, alloc ) {}

        explicit ChunkedArray( size_type count, const Allocator& alloc = Allocator() )
            : Base( count, alloc ) {}

        template <class InputIt>
        ChunkedArray( InputIt first, InputIt last, const Allocator& alloc = Allocator() )
            : Base( first, last, alloc ) {}

        ChunkedArray( const ChunkedArray& other )
            : Base( static_cast<const Base&>( other ) ) {}

        ChunkedArray( const ChunkedArray& other, const Allocator& alloc )
            : Base( static_cast<const Base&>( other ), alloc ) {}

        ChunkedArray( ChunkedArray&& other ) noexcept
            : Base( std::move( static_cast<Base&>( other ) ) ) {}

        ChunkedArray( ChunkedArray&& other, const Allocator& alloc )
            : Base( std::move( static_cast<Base&>( other ) ), alloc ) {}

        ChunkedArray( std::initializer_list<T> init, const Allocator& alloc = Allocator() )
            : Base( init, alloc ) {}

        ChunkedArray& operator=( const ChunkedArray& other )
        {
            if ( this != &other )
            {
                SW_SCOPED_RACE_WRITE();
                SW_SCOPED_RACE_READ_OTHER( other );
                Base::operator=( static_cast<const Base&>( other ) );
            }
            return *this;
        }

        ChunkedArray& operator=( ChunkedArray&& other ) noexcept( noexcept( std::allocator_traits<Allocator>::propagate_on_container_move_assignment::value || std::allocator_traits<Allocator>::is_always_equal::value ) )
        {
            if ( this != &other )
            {
                SW_SCOPED_RACE_WRITE();
                SW_SCOPED_RACE_WRITE_OTHER( other );
                Base::operator=( std::move( static_cast<Base&>( other ) ) );
            }
            return *this;
        }

        ChunkedArray& operator=( std::initializer_list<T> ilist )
        {
            SW_SCOPED_RACE_WRITE();
            Base::operator=( ilist );
            return *this;
        }

        void assign( size_type count, const T& value )
        {
            SW_SCOPED_RACE_WRITE();
            Base::assign( count, value );
        }

        template <class InputIt>
        void assign( InputIt first, InputIt last )
        {
            SW_SCOPED_RACE_WRITE();
            Base::assign( first, last );
        }

        void assign( std::initializer_list<T> ilist )
        {
            SW_SCOPED_RACE_WRITE();
            Base::assign( ilist );
        }

        // ------------------------------------------------------------------------------
        // 2) 조회 — 원소 · 이터레이터 · 크기. const 가 아닌 접근은 참조가 밖으로 나가므로 쓰기 가드를 잡는다
        // ------------------------------------------------------------------------------
        allocator_type get_allocator() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::get_allocator();
        }

        // 원소 접근
        reference at( size_type pos )
        {
            SW_SCOPED_RACE_WRITE();
            return Base::at( pos );
        }

        const_reference at( size_type pos ) const
        {
            SW_SCOPED_RACE_READ();
            return Base::at( pos );
        }

        /** @brief pos 위치의 원소를 반환합니다(범위는 개발 구성에서만 확인). */
        reference operator[]( size_type pos )
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX( pos, Base::size() );
            return Base::operator[]( pos );
        }

        /** @brief pos 위치의 원소를 반환합니다(범위는 개발 구성에서만 확인). */
        const_reference operator[]( size_type pos ) const
        {
            SW_SCOPED_RACE_READ();
            SW_CHECK_INDEX( pos, Base::size() );
            return Base::operator[]( pos );
        }

        reference front()
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX( 0, Base::size() );
            return Base::front();
        }

        const_reference front() const
        {
            SW_SCOPED_RACE_READ();
            SW_CHECK_INDEX( 0, Base::size() );
            return Base::front();
        }

        reference back()
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX( 0, Base::size() );
            return Base::back();
        }

        const_reference back() const
        {
            SW_SCOPED_RACE_READ();
            SW_CHECK_INDEX( 0, Base::size() );
            return Base::back();
        }

        // 이터레이터
        iterator begin() noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::begin();
        }

        const_iterator begin() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::begin();
        }

        const_iterator cbegin() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::cbegin();
        }

        iterator end() noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::end();
        }

        const_iterator end() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::end();
        }

        const_iterator cend() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::cend();
        }

        reverse_iterator rbegin() noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::rbegin();
        }

        const_reverse_iterator rbegin() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::rbegin();
        }

        const_reverse_iterator crbegin() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::crbegin();
        }

        reverse_iterator rend() noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::rend();
        }

        const_reverse_iterator rend() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::rend();
        }

        const_reverse_iterator crend() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::crend();
        }

        // 크기
        bool empty() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::empty();
        }

        size_type size() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::size();
        }

        size_type max_size() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::max_size();
        }

        void shrink_to_fit()
        {
            SW_SCOPED_RACE_WRITE();
            Base::shrink_to_fit();
        }

        // ------------------------------------------------------------------------------
        // 3) 변경 — insert/erase/push. 쓰기 가드를 잡는다
        // ------------------------------------------------------------------------------
        void clear() noexcept
        {
            SW_SCOPED_RACE_WRITE();
            Base::clear();
        }

        iterator insert( const_iterator pos, const T& value )
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX_OR_END( pos - Base::cbegin(), Base::size() );
            return Base::insert( pos, value );
        }

        iterator insert( const_iterator pos, T&& value )
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX_OR_END( pos - Base::cbegin(), Base::size() );
            return Base::insert( pos, std::move( value ) );
        }

        iterator insert( const_iterator pos, size_type count, const T& value )
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX_OR_END( pos - Base::cbegin(), Base::size() );
            return Base::insert( pos, count, value );
        }

        template <class InputIt>
        iterator insert( const_iterator pos, InputIt first, InputIt last )
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX_OR_END( pos - Base::cbegin(), Base::size() );
            return Base::insert( pos, first, last );
        }

        iterator insert( const_iterator pos, std::initializer_list<T> ilist )
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX_OR_END( pos - Base::cbegin(), Base::size() );
            return Base::insert( pos, ilist );
        }

        template <class... Args>
        iterator emplace( const_iterator pos, Args&&... args )
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX_OR_END( pos - Base::cbegin(), Base::size() );
            return Base::emplace( pos, std::forward<Args>( args )... );
        }

        iterator erase( const_iterator pos )
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX( pos - Base::cbegin(), Base::size() );
            return Base::erase( pos );
        }

        iterator erase( const_iterator first, const_iterator last )
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX_OR_END( last - Base::cbegin(), Base::size() );
            SW_CHECK_INDEX_OR_END( first - Base::cbegin(), last - Base::cbegin() );
            return Base::erase( first, last );
        }

        void push_back( const T& value )
        {
            SW_SCOPED_RACE_WRITE();
            Base::push_back( value );
        }

        void push_back( T&& value )
        {
            SW_SCOPED_RACE_WRITE();
            Base::push_back( std::move( value ) );
        }

        template <class... Args>
        reference emplace_back( Args&&... args )
        {
            SW_SCOPED_RACE_WRITE();
            return Base::emplace_back( std::forward<Args>( args )... );
        }

        void pop_back()
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX( 0, Base::size() );
            Base::pop_back();
        }

        void push_front( const T& value )
        {
            SW_SCOPED_RACE_WRITE();
            Base::push_front( value );
        }

        void push_front( T&& value )
        {
            SW_SCOPED_RACE_WRITE();
            Base::push_front( std::move( value ) );
        }

        template <class... Args>
        reference emplace_front( Args&&... args )
        {
            SW_SCOPED_RACE_WRITE();
            return Base::emplace_front( std::forward<Args>( args )... );
        }

        void pop_front()
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX( 0, Base::size() );
            Base::pop_front();
        }

        void resize( size_type count )
        {
            SW_SCOPED_RACE_WRITE();
            Base::resize( count );
        }

        void resize( size_type count, const value_type& value )
        {
            SW_SCOPED_RACE_WRITE();
            Base::resize( count, value );
        }

        void swap( ChunkedArray& other ) noexcept( noexcept( std::allocator_traits<Allocator>::propagate_on_container_swap::value || std::allocator_traits<Allocator>::is_always_equal::value ) )
        {
            SW_SCOPED_RACE_WRITE();
            SW_SCOPED_RACE_WRITE_OTHER( other );
            Base::swap( static_cast<Base&>( other ) );
        }
    };
} // namespace sw
