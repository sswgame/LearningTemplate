/**
 * @file list.h
 * @brief std::list 래퍼입니다. 디버그 빌드에서는 RaceDetectContext 로 동시 접근을 잡아냅니다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/DataRaceDetector.h"
#include "Core/Memory/Memory.h"

namespace sw
{
#if defined( SW_ENABLE_STL_CONTAINER )
    template <typename T, typename Allocator = std::allocator<T>>
    using list = std::list<T, Allocator>;
#else
    /** @brief std::list 에 디버그 레이스 탐지를 더한 것입니다. API 는 STL 과 같습니다. */
    template <typename T, typename Allocator = Allocator<T>>
    class list : public std::list<T, Allocator>
    {
        using Base = std::list<T, Allocator>;
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
        /** @brief 빈 리스트로 둡니다. */
        list() noexcept( noexcept( Allocator() ) )
            : Base() {}

        /** @brief 지정한 할당자로 빈 리스트를 만듭니다. */
        explicit list( const Allocator& alloc ) noexcept
            : Base( alloc ) {}

        /** @brief count 개를 value 로 채웁니다. */
        list( size_type count, const T& value, const Allocator& alloc = Allocator() )
            : Base( count, value, alloc ) {}

        /** @brief 기본값 원소 count 개로 채웁니다. */
        explicit list( size_type count, const Allocator& alloc = Allocator() )
            : Base( count, alloc ) {}

        /** @brief [first, last) 를 복사해 채웁니다. */
        template <class InputIt>
        list( InputIt first, InputIt last, const Allocator& alloc = Allocator() )
            : Base( first, last, alloc ) {}

        /** @brief std::list 의 내용을 복사해 만듭니다. */
        list( const Base& other )
            : Base( other ) {}

        /** @brief std::list 의 내용을 옮겨 와 만듭니다. */
        list( Base&& other ) noexcept
            : Base( std::move( other ) ) {}

        /** @brief 복사 생성합니다. */
        list( const list& other )
            : Base( static_cast<const Base&>( other ) ) {}

        /** @brief 지정한 할당자로 복사 생성합니다. */
        list( const list& other, const Allocator& alloc )
            : Base( static_cast<const Base&>( other ), alloc ) {}

        /** @brief 이동 생성합니다. */
        list( list&& other ) noexcept
            : Base( std::move( static_cast<Base&>( other ) ) ) {}

        /** @brief 지정한 할당자로 이동 생성합니다. */
        list( list&& other, const Allocator& alloc )
            : Base( std::move( static_cast<Base&>( other ) ), alloc ) {}

        /** @brief 초기화 리스트로 채웁니다. */
        list( std::initializer_list<T> init, const Allocator& alloc = Allocator() )
            : Base( init, alloc ) {}

        /** @brief std::list 의 내용을 복사해 대입합니다. */
        list& operator=( const Base& other )
        {
            SW_SCOPED_RACE_WRITE();
            Base::operator=( other );
            return *this;
        }

        /** @brief std::list 의 내용을 옮겨 와 대입합니다. */
        list& operator=( Base&& other ) noexcept
        {
            SW_SCOPED_RACE_WRITE();
            Base::operator=( std::move( other ) );
            return *this;
        }

        /** @brief 복사 대입합니다. */
        list& operator=( const list& other )
        {
            if ( this != &other )
            {
                SW_SCOPED_RACE_WRITE();
                SW_SCOPED_RACE_READ_OTHER( other );
                Base::operator=( static_cast<const Base&>( other ) );
            }
            return *this;
        }

        /** @brief 이동 대입합니다. */
        list& operator=( list&& other ) noexcept( noexcept( std::allocator_traits<Allocator>::is_always_equal::value ) )
        {
            if ( this != &other )
            {
                SW_SCOPED_RACE_WRITE();
                SW_SCOPED_RACE_WRITE_OTHER( other );
                Base::operator=( std::move( static_cast<Base&>( other ) ) );
            }
            return *this;
        }

        /** @brief 초기화 리스트로 대입합니다. */
        list& operator=( std::initializer_list<T> ilist )
        {
            SW_SCOPED_RACE_WRITE();
            Base::operator=( ilist );
            return *this;
        }

        /** @brief 내용을 value 원소 count 개로 바꿉니다. */
        void assign( size_type count, const T& value )
        {
            SW_SCOPED_RACE_WRITE();
            Base::assign( count, value );
        }

        /** @brief 내용을 [first, last) 로 바꿉니다. */
        template <class InputIt>
        void assign( InputIt first, InputIt last )
        {
            SW_SCOPED_RACE_WRITE();
            Base::assign( first, last );
        }

        /** @brief 내용을 초기화 리스트로 바꿉니다. */
        void assign( std::initializer_list<T> ilist )
        {
            SW_SCOPED_RACE_WRITE();
            Base::assign( ilist );
        }

        // ------------------------------------------------------------------------------
        // 2) 조회 — 원소 · 이터레이터 · 크기. const 가 아닌 접근은 참조가 밖으로 나가므로 쓰기 가드를 잡는다
        // ------------------------------------------------------------------------------
        /** @brief 쓰고 있는 할당자를 반환합니다. */
        allocator_type get_allocator() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::get_allocator();
        }

        // 원소 접근
        /** @brief 첫 원소를 반환합니다. */
        reference front()
        {
            SW_SCOPED_RACE_WRITE();
            return Base::front();
        }

        /** @brief 첫 원소를 반환합니다. */
        const_reference front() const
        {
            SW_SCOPED_RACE_READ();
            return Base::front();
        }

        /** @brief 마지막 원소를 반환합니다. */
        reference back()
        {
            SW_SCOPED_RACE_WRITE();
            return Base::back();
        }

        /** @brief 마지막 원소를 반환합니다. */
        const_reference back() const
        {
            SW_SCOPED_RACE_READ();
            return Base::back();
        }

        // 이터레이터
        /** @brief 시작 이터레이터를 반환합니다. */
        iterator begin() noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::begin();
        }

        /** @brief 시작 이터레이터를 반환합니다. */
        const_iterator begin() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::begin();
        }

        /** @brief 상수 시작 이터레이터를 반환합니다. */
        const_iterator cbegin() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::cbegin();
        }

        /** @brief 끝 이터레이터를 반환합니다. */
        iterator end() noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::end();
        }

        /** @brief 끝 이터레이터를 반환합니다. */
        const_iterator end() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::end();
        }

        /** @brief 상수 끝 이터레이터를 반환합니다. */
        const_iterator cend() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::cend();
        }

        /** @brief 역방향 시작 이터레이터를 반환합니다. */
        reverse_iterator rbegin() noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::rbegin();
        }

        /** @brief 역방향 시작 이터레이터를 반환합니다. */
        const_reverse_iterator rbegin() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::rbegin();
        }

        /** @brief 상수 역방향 시작 이터레이터를 반환합니다. */
        const_reverse_iterator crbegin() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::crbegin();
        }

        /** @brief 역방향 끝 이터레이터를 반환합니다. */
        reverse_iterator rend() noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::rend();
        }

        /** @brief 역방향 끝 이터레이터를 반환합니다. */
        const_reverse_iterator rend() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::rend();
        }

        /** @brief 상수 역방향 끝 이터레이터를 반환합니다. */
        const_reverse_iterator crend() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::crend();
        }

        // 크기
        /** @brief 비어 있는지 반환합니다. */
        bool empty() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::empty();
        }

        /** @brief 원소 개수를 반환합니다. */
        size_type size() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::size();
        }

        /** @brief 담을 수 있는 최대 원소 개수를 반환합니다. */
        size_type max_size() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::max_size();
        }

        // ------------------------------------------------------------------------------
        // 3) 변경 — insert/erase/splice. 쓰기 가드를 잡는다
        // ------------------------------------------------------------------------------
        /** @brief 모든 원소를 제거합니다. */
        void clear() noexcept
        {
            SW_SCOPED_RACE_WRITE();
            Base::clear();
        }

        /** @brief pos 앞에 원소를 삽입합니다. */
        iterator insert( const_iterator pos, const T& value )
        {
            SW_SCOPED_RACE_WRITE();
            return Base::insert( pos, value );
        }

        /** @brief pos 앞에 원소를 삽입합니다. */
        iterator insert( const_iterator pos, T&& value )
        {
            SW_SCOPED_RACE_WRITE();
            return Base::insert( pos, std::move( value ) );
        }

        /** @brief pos 앞에 value 를 count 개 삽입합니다. */
        iterator insert( const_iterator pos, size_type count, const T& value )
        {
            SW_SCOPED_RACE_WRITE();
            return Base::insert( pos, count, value );
        }

        /** @brief pos 앞에 [first, last) 를 삽입합니다. */
        template <class InputIt>
        iterator insert( const_iterator pos, InputIt first, InputIt last )
        {
            SW_SCOPED_RACE_WRITE();
            return Base::insert( pos, first, last );
        }

        /** @brief pos 앞에 초기화 리스트의 원소를 삽입합니다. */
        iterator insert( const_iterator pos, std::initializer_list<T> ilist )
        {
            SW_SCOPED_RACE_WRITE();
            return Base::insert( pos, ilist );
        }

        /** @brief pos 앞에 원소를 제자리에서 생성합니다. */
        template <class... Args>
        iterator emplace( const_iterator pos, Args&&... args )
        {
            SW_SCOPED_RACE_WRITE();
            return Base::emplace( pos, std::forward<Args>( args )... );
        }

        /** @brief pos 가 가리키는 원소를 제거합니다. */
        iterator erase( const_iterator pos )
        {
            SW_SCOPED_RACE_WRITE();
            return Base::erase( pos );
        }

        /** @brief [first, last) 범위의 원소를 제거합니다. */
        iterator erase( const_iterator first, const_iterator last )
        {
            SW_SCOPED_RACE_WRITE();
            return Base::erase( first, last );
        }

        /** @brief 뒤에 원소를 추가합니다. */
        void push_back( const T& value )
        {
            SW_SCOPED_RACE_WRITE();
            Base::push_back( value );
        }

        /** @brief 뒤에 원소를 추가합니다. */
        void push_back( T&& value )
        {
            SW_SCOPED_RACE_WRITE();
            Base::push_back( std::move( value ) );
        }

        /** @brief 뒤에 원소를 제자리에서 생성합니다. */
        template <class... Args>
        reference emplace_back( Args&&... args )
        {
            SW_SCOPED_RACE_WRITE();
            return Base::emplace_back( std::forward<Args>( args )... );
        }

        /** @brief 마지막 원소를 제거합니다. */
        void pop_back()
        {
            SW_SCOPED_RACE_WRITE();
            Base::pop_back();
        }

        /** @brief 앞에 원소를 추가합니다. */
        void push_front( const T& value )
        {
            SW_SCOPED_RACE_WRITE();
            Base::push_front( value );
        }

        /** @brief 앞에 원소를 추가합니다. */
        void push_front( T&& value )
        {
            SW_SCOPED_RACE_WRITE();
            Base::push_front( std::move( value ) );
        }

        /** @brief 앞에 원소를 제자리에서 생성합니다. */
        template <class... Args>
        reference emplace_front( Args&&... args )
        {
            SW_SCOPED_RACE_WRITE();
            return Base::emplace_front( std::forward<Args>( args )... );
        }

        /** @brief 첫 원소를 제거합니다. */
        void pop_front()
        {
            SW_SCOPED_RACE_WRITE();
            Base::pop_front();
        }

        /** @brief 크기를 바꿉니다. 늘어난 자리는 기본값으로 채웁니다. */
        void resize( size_type count )
        {
            SW_SCOPED_RACE_WRITE();
            Base::resize( count );
        }

        /** @brief 크기를 바꿉니다. 늘어난 자리는 value 로 채웁니다. */
        void resize( size_type count, const value_type& value )
        {
            SW_SCOPED_RACE_WRITE();
            Base::resize( count, value );
        }

        /** @brief 내용을 교환합니다. */
        void swap( list& other ) noexcept( noexcept( std::allocator_traits<Allocator>::is_always_equal::value ) )
        {
            SW_SCOPED_RACE_WRITE();
            SW_SCOPED_RACE_WRITE_OTHER( other );
            Base::swap( static_cast<Base&>( other ) );
        }

        // 리스트 연산
        /** @brief 정렬된 other 를 이 리스트에 병합합니다. other 는 비게 됩니다. */
        void merge( list& other )
        {
            SW_SCOPED_RACE_WRITE();
            SW_SCOPED_RACE_WRITE_OTHER( other );
            Base::merge( static_cast<Base&>( other ) );
        }

        /** @brief 정렬된 other 를 이 리스트에 병합합니다. other 는 비게 됩니다. */
        void merge( list&& other )
        {
            SW_SCOPED_RACE_WRITE();
            SW_SCOPED_RACE_WRITE_OTHER( other );
            Base::merge( std::move( static_cast<Base&>( other ) ) );
        }

        /** @brief 비교자로 정렬된 other 를 이 리스트에 병합합니다. other 는 비게 됩니다. */
        template <class Compare>
        void merge( list& other, Compare comp )
        {
            SW_SCOPED_RACE_WRITE();
            SW_SCOPED_RACE_WRITE_OTHER( other );
            Base::merge( static_cast<Base&>( other ), comp );
        }

        /** @brief 비교자로 정렬된 other 를 이 리스트에 병합합니다. other 는 비게 됩니다. */
        template <class Compare>
        void merge( list&& other, Compare comp )
        {
            SW_SCOPED_RACE_WRITE();
            SW_SCOPED_RACE_WRITE_OTHER( other );
            Base::merge( std::move( static_cast<Base&>( other ) ), comp );
        }

        /** @brief other 의 모든 원소를 pos 앞으로 옮깁니다. */
        void splice( const_iterator pos, list& other )
        {
            SW_SCOPED_RACE_WRITE();
            SW_SCOPED_RACE_WRITE_OTHER( other );
            Base::splice( pos, static_cast<Base&>( other ) );
        }

        /** @brief other 의 모든 원소를 pos 앞으로 옮깁니다. */
        void splice( const_iterator pos, list&& other )
        {
            SW_SCOPED_RACE_WRITE();
            SW_SCOPED_RACE_WRITE_OTHER( other );
            Base::splice( pos, std::move( static_cast<Base&>( other ) ) );
        }

        /** @brief other 의 원소 it 하나를 pos 앞으로 옮깁니다. */
        void splice( const_iterator pos, list& other, const_iterator it )
        {
            SW_SCOPED_RACE_WRITE();
            SW_SCOPED_RACE_WRITE_OTHER( other );
            Base::splice( pos, static_cast<Base&>( other ), it );
        }

        /** @brief other 의 원소 it 하나를 pos 앞으로 옮깁니다. */
        void splice( const_iterator pos, list&& other, const_iterator it )
        {
            SW_SCOPED_RACE_WRITE();
            SW_SCOPED_RACE_WRITE_OTHER( other );
            Base::splice( pos, std::move( static_cast<Base&>( other ) ), it );
        }

        /** @brief other 의 [first, last) 원소를 pos 앞으로 옮깁니다. */
        void splice( const_iterator pos, list& other, const_iterator first, const_iterator last )
        {
            SW_SCOPED_RACE_WRITE();
            SW_SCOPED_RACE_WRITE_OTHER( other );
            Base::splice( pos, static_cast<Base&>( other ), first, last );
        }

        /** @brief other 의 [first, last) 원소를 pos 앞으로 옮깁니다. */
        void splice( const_iterator pos, list&& other, const_iterator first, const_iterator last )
        {
            SW_SCOPED_RACE_WRITE();
            SW_SCOPED_RACE_WRITE_OTHER( other );
            Base::splice( pos, std::move( static_cast<Base&>( other ) ), first, last );
        }

        /** @brief value 와 같은 원소를 모두 제거합니다. */
        void remove( const T& value )
        {
            SW_SCOPED_RACE_WRITE();
            Base::remove( value );
        }

        /** @brief 조건을 만족하는 원소를 모두 제거합니다. */
        template <class UnaryPredicate>
        void remove_if( UnaryPredicate p )
        {
            SW_SCOPED_RACE_WRITE();
            Base::remove_if( p );
        }

        /** @brief 순서를 뒤집습니다. */
        void reverse() noexcept
        {
            SW_SCOPED_RACE_WRITE();
            Base::reverse();
        }

        /** @brief 연속으로 중복된 원소를 하나만 남기고 제거합니다. */
        void unique()
        {
            SW_SCOPED_RACE_WRITE();
            Base::unique();
        }

        /** @brief 연속으로 중복된 원소(판정은 조건자로 합니다)를 하나만 남기고 제거합니다. */
        template <class BinaryPredicate>
        void unique( BinaryPredicate p )
        {
            SW_SCOPED_RACE_WRITE();
            Base::unique( p );
        }

        /** @brief operator< 로 정렬합니다. */
        void sort()
        {
            SW_SCOPED_RACE_WRITE();
            Base::sort();
        }

        /** @brief 비교자로 정렬합니다. */
        template <class Compare>
        void sort( Compare comp )
        {
            SW_SCOPED_RACE_WRITE();
            Base::sort( comp );
        }
    };
#endif
} // namespace sw
