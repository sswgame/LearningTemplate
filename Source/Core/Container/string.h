/**
 * @file string.h
 * @brief std::basic_string 래퍼입니다. 디버그 빌드에서는 RaceDetectContext 로 동시 접근을 잡아냅니다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/DataRaceDetector.h"
#include "Core/Memory/Memory.h"

namespace sw
{
#if defined( SW_ENABLE_STL_CONTAINER )
    template <typename CharT, typename Traits = std::char_traits<CharT>, typename Allocator = std::allocator<CharT>>
    using basic_string = std::basic_string<CharT, Traits, Allocator>;
#else
    /** @brief std::basic_string 에 디버그 레이스 탐지를 더한 것입니다. API 는 STL 과 같습니다. */
    template <typename CharT, typename Traits = std::char_traits<CharT>, typename Allocator = Allocator<CharT>>
    class basic_string : public std::basic_string<CharT, Traits, Allocator>
    {
        using Base = std::basic_string<CharT, Traits, Allocator>;
        SW_RACE_CTX_MEMBER

    public:
        using traits_type            = typename Base::traits_type;
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

        static const size_type npos = Base::npos;

        // ------------------------------------------------------------------------------
        // 1) 생성 · 대입 — 내용은 Base 에 두고, 레이스 컨텍스트는 인스턴스마다 따로 둔다
        // ------------------------------------------------------------------------------
        /** @brief 빈 문자열로 둡니다. */
        basic_string() noexcept( noexcept( Allocator() ) )
            : Base() {}

        /** @brief 지정한 할당자로 빈 문자열을 만듭니다. */
        explicit basic_string( const Allocator& alloc ) noexcept
            : Base( alloc ) {}

        /** @brief ch 를 count 개 채웁니다. */
        basic_string( size_type count, CharT ch, const Allocator& alloc = Allocator() )
            : Base( count, ch, alloc ) {}

        /** @brief other 의 pos 부터 끝까지를 복사해 만듭니다. */
        basic_string( const basic_string& other, size_type pos, const Allocator& alloc = Allocator() )
            : Base( static_cast<const Base&>( other ), pos, alloc ) {}

        /** @brief other 의 pos 부터 count 문자를 복사해 만듭니다. */
        basic_string( const basic_string& other, size_type pos, size_type count, const Allocator& alloc = Allocator() )
            : Base( static_cast<const Base&>( other ), pos, count, alloc ) {}

        /** @brief C 문자열의 앞 count 문자를 복사합니다. */
        basic_string( const CharT* pS, size_type count, const Allocator& alloc = Allocator() )
            : Base( pS, count, alloc ) {}

        /** @brief 널 종료 C 문자열을 복사합니다. */
        basic_string( const CharT* pS, const Allocator& alloc = Allocator() )
            : Base( pS, alloc ) {}

        /**
         * @brief 뷰의 문자를 복사해 소유합니다.
         * @details std 와 마찬가지로 explicit 입니다. 뷰를 반환하는 API 의 결과를 담을 때는 `string{ view }` 로 씁니다.
         */
        explicit basic_string( std::basic_string_view<CharT> sv, const Allocator& alloc = Allocator() )
            : Base( sv.data(), sv.size(), alloc )
        {
        }

        /** @brief [first, last) 를 복사해 채웁니다. */
        template <class InputIt>
        basic_string( InputIt first, InputIt last, const Allocator& alloc = Allocator() )
            : Base( first, last, alloc ) {}

        /** @brief std::basic_string 의 내용을 복사해 만듭니다. */
        basic_string( const Base& other )
            : Base( other ) {}

        /** @brief std::basic_string 의 내용을 옮겨 와 만듭니다. */
        basic_string( Base&& other ) noexcept
            : Base( std::move( other ) ) {}

        /** @brief 복사 생성합니다. */
        basic_string( const basic_string& other )
            : Base( static_cast<const Base&>( other ) ) {}

        /** @brief 지정한 할당자로 복사 생성합니다. */
        basic_string( const basic_string& other, const Allocator& alloc )
            : Base( static_cast<const Base&>( other ), alloc ) {}

        /** @brief 이동 생성합니다. */
        basic_string( basic_string&& other ) noexcept
            : Base( std::move( static_cast<Base&>( other ) ) ) {}

        /** @brief 지정한 할당자로 이동 생성합니다. */
        basic_string( basic_string&& other, const Allocator& alloc )
            : Base( std::move( static_cast<Base&>( other ) ), alloc ) {}

        /** @brief 초기화 리스트로 채웁니다. */
        basic_string( std::initializer_list<CharT> ilist, const Allocator& alloc = Allocator() )
            : Base( ilist, alloc ) {}

        /** @brief string_view 로 변환되는 타입에서 복사해 만듭니다. */
        template <class StringViewLike>
        explicit basic_string( const StringViewLike& t, const Allocator& alloc = Allocator() )
            : Base( t, alloc ) {}

        /** @brief string_view 로 변환되는 타입의 부분 문자열을 복사해 만듭니다. */
        template <class StringViewLike>
        basic_string( const StringViewLike& t, size_type pos, size_type n, const Allocator& alloc = Allocator() )
            : Base( t, pos, n, alloc ) {}

        /** @brief std::basic_string 의 내용을 복사해 대입합니다. */
        basic_string& operator=( const Base& other )
        {
            SW_SCOPED_RACE_WRITE();
            Base::operator=( other );
            return *this;
        }

        /** @brief std::basic_string 의 내용을 옮겨 와 대입합니다. */
        basic_string& operator=( Base&& other ) noexcept
        {
            SW_SCOPED_RACE_WRITE();
            Base::operator=( std::move( other ) );
            return *this;
        }

        /** @brief 복사 대입합니다. */
        basic_string& operator=( const basic_string& other )
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
        basic_string& operator=( basic_string&& other ) noexcept
        {
            if ( this != &other )
            {
                SW_SCOPED_RACE_WRITE();
                SW_SCOPED_RACE_WRITE_OTHER( other );
                Base::operator=( std::move( static_cast<Base&>( other ) ) );
            }
            return *this;
        }

        /** @brief 널 종료 C 문자열을 복사해 대입합니다. */
        basic_string& operator=( const CharT* s )
        {
            SW_SCOPED_RACE_WRITE();
            Base::operator=( s );
            return *this;
        }

        /** @brief 문자 하나로 된 문자열을 대입합니다. */
        basic_string& operator=( CharT ch )
        {
            SW_SCOPED_RACE_WRITE();
            Base::operator=( ch );
            return *this;
        }

        /** @brief 초기화 리스트로 대입합니다. */
        basic_string& operator=( std::initializer_list<CharT> ilist )
        {
            SW_SCOPED_RACE_WRITE();
            Base::operator=( ilist );
            return *this;
        }

        /** @brief string_view 로 변환되는 타입을 복사해 대입합니다. */
        template <class StringViewLike>
        basic_string& operator=( const StringViewLike& t )
        {
            SW_SCOPED_RACE_WRITE();
            Base::operator=( t );
            return *this;
        }

        // ------------------------------------------------------------------------------
        // 2) 조회 — 문자 · 이터레이터 · 크기. const 가 아닌 접근은 참조가 밖으로 나가므로 쓰기 가드를 잡는다
        // ------------------------------------------------------------------------------
        /** @brief 범위를 검사하고 pos 위치의 문자를 반환합니다. */
        reference at( size_type pos )
        {
            SW_SCOPED_RACE_WRITE();
            return Base::at( pos );
        }

        /** @brief 범위를 검사하고 pos 위치의 문자를 반환합니다. */
        const_reference at( size_type pos ) const
        {
            SW_SCOPED_RACE_READ();
            return Base::at( pos );
        }

        /** @brief pos 위치의 문자를 반환합니다(범위 검사 없음). */
        reference operator[]( size_type pos )
        {
            SW_SCOPED_RACE_WRITE();
            return Base::operator[]( pos );
        }

        /** @brief pos 위치의 문자를 반환합니다(범위 검사 없음). */
        const_reference operator[]( size_type pos ) const
        {
            SW_SCOPED_RACE_READ();
            return Base::operator[]( pos );
        }

        /** @brief 첫 문자를 반환합니다. */
        reference front()
        {
            SW_SCOPED_RACE_WRITE();
            return Base::front();
        }

        /** @brief 첫 문자를 반환합니다. */
        const_reference front() const
        {
            SW_SCOPED_RACE_READ();
            return Base::front();
        }

        /** @brief 마지막 문자를 반환합니다. */
        reference back()
        {
            SW_SCOPED_RACE_WRITE();
            return Base::back();
        }

        /** @brief 마지막 문자를 반환합니다. */
        const_reference back() const
        {
            SW_SCOPED_RACE_READ();
            return Base::back();
        }

        /** @brief 내부 버퍼 포인터를 반환합니다. */
        const CharT* data() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::data();
        }

        /** @brief 내부 버퍼 포인터를 반환합니다. */
        CharT* data() noexcept
        {
            SW_SCOPED_RACE_WRITE();
            return Base::data();
        }

        /** @brief 널 종료 C 문자열을 반환합니다. */
        const CharT* c_str() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::c_str();
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

        /** @brief 문자 수를 반환합니다. */
        size_type size() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::size();
        }

        /** @brief 문자 수를 반환합니다(size() 와 같습니다). */
        size_type length() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::length();
        }

        /** @brief 담을 수 있는 최대 문자 수를 반환합니다. */
        size_type max_size() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::max_size();
        }

        /** @brief 용량을 미리 확보합니다. */
        void reserve( size_type new_cap )
        {
            SW_SCOPED_RACE_WRITE();
            Base::reserve( new_cap );
        }

        /** @brief 현재 용량을 반환합니다. */
        size_type capacity() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::capacity();
        }

        /** @brief 용량을 크기에 맞게 줄입니다. */
        void shrink_to_fit()
        {
            SW_SCOPED_RACE_WRITE();
            Base::shrink_to_fit();
        }

        // ------------------------------------------------------------------------------
        // 3) 변경 — append/insert/erase. 쓰기 가드를 잡는다
        // ------------------------------------------------------------------------------
        /** @brief 모든 문자를 지웁니다. */
        void clear() noexcept
        {
            SW_SCOPED_RACE_WRITE();
            Base::clear();
        }

        /** @brief index 위치에 ch 를 count 개 삽입합니다. */
        basic_string& insert( size_type index, size_type count, CharT ch )
        {
            SW_SCOPED_RACE_WRITE();
            Base::insert( index, count, ch );
            return *this;
        }

        /** @brief index 위치에 널 종료 C 문자열을 삽입합니다. */
        basic_string& insert( size_type index, const CharT* pS )
        {
            SW_SCOPED_RACE_WRITE();
            Base::insert( index, pS );
            return *this;
        }

        /** @brief index 위치에 C 문자열의 앞 count 문자를 삽입합니다. */
        basic_string& insert( size_type index, const CharT* pS, size_type count )
        {
            SW_SCOPED_RACE_WRITE();
            Base::insert( index, pS, count );
            return *this;
        }

        /** @brief index 위치에 str 을 삽입합니다. */
        basic_string& insert( size_type index, const basic_string& str )
        {
            SW_SCOPED_RACE_WRITE();
            Base::insert( index, static_cast<const Base&>( str ) );
            return *this;
        }

        /** @brief index 위치에 str 의 index_str 부터 count 문자를 삽입합니다. */
        basic_string& insert( size_type index, const basic_string& str, size_type index_str, size_type count = npos )
        {
            SW_SCOPED_RACE_WRITE();
            Base::insert( index, static_cast<const Base&>( str ), index_str, count );
            return *this;
        }

        /** @brief pos 앞에 ch 를 삽입합니다. */
        iterator insert( const_iterator pos, CharT ch )
        {
            SW_SCOPED_RACE_WRITE();
            return Base::insert( pos, ch );
        }

        /** @brief pos 앞에 ch 를 count 개 삽입합니다. */
        iterator insert( const_iterator pos, size_type count, CharT ch )
        {
            SW_SCOPED_RACE_WRITE();
            return Base::insert( pos, count, ch );
        }

        /** @brief pos 앞에 [first, last) 를 삽입합니다. */
        template <class InputIt>
        iterator insert( const_iterator pos, InputIt first, InputIt last )
        {
            SW_SCOPED_RACE_WRITE();
            return Base::insert( pos, first, last );
        }

        /** @brief pos 앞에 초기화 리스트의 문자를 삽입합니다. */
        iterator insert( const_iterator pos, std::initializer_list<CharT> ilist )
        {
            SW_SCOPED_RACE_WRITE();
            return Base::insert( pos, ilist );
        }

        /** @brief index 부터 count 문자를 지웁니다. */
        basic_string& erase( size_type index = 0, size_type count = npos )
        {
            SW_SCOPED_RACE_WRITE();
            Base::erase( index, count );
            return *this;
        }

        /** @brief pos 가 가리키는 문자를 지웁니다. */
        iterator erase( const_iterator pos )
        {
            SW_SCOPED_RACE_WRITE();
            return Base::erase( pos );
        }

        /** @brief [first, last) 범위의 문자를 지웁니다. */
        iterator erase( const_iterator first, const_iterator last )
        {
            SW_SCOPED_RACE_WRITE();
            return Base::erase( first, last );
        }

        /** @brief 뒤에 문자 하나를 붙입니다. */
        void push_back( CharT ch )
        {
            SW_SCOPED_RACE_WRITE();
            Base::push_back( ch );
        }

        /** @brief 마지막 문자를 지웁니다. */
        void pop_back()
        {
            SW_SCOPED_RACE_WRITE();
            Base::pop_back();
        }

        /** @brief ch 를 count 개 이어 붙입니다. */
        basic_string& append( size_type count, CharT ch )
        {
            SW_SCOPED_RACE_WRITE();
            Base::append( count, ch );
            return *this;
        }

        /** @brief str 을 이어 붙입니다. */
        basic_string& append( const basic_string& str )
        {
            SW_SCOPED_RACE_WRITE();
            Base::append( static_cast<const Base&>( str ) );
            return *this;
        }

        /** @brief str 의 pos 부터 count 문자를 이어 붙입니다. */
        basic_string& append( const basic_string& str, size_type pos, size_type count = npos )
        {
            SW_SCOPED_RACE_WRITE();
            Base::append( static_cast<const Base&>( str ), pos, count );
            return *this;
        }

        /** @brief C 문자열의 앞 count 문자를 이어 붙입니다. */
        basic_string& append( const CharT* pS, size_type count )
        {
            SW_SCOPED_RACE_WRITE();
            Base::append( pS, count );
            return *this;
        }

        /** @brief 널 종료 C 문자열을 이어 붙입니다. */
        basic_string& append( const CharT* pS )
        {
            SW_SCOPED_RACE_WRITE();
            Base::append( pS );
            return *this;
        }

        /** @brief [first, last) 를 이어 붙입니다. */
        template <class InputIt>
        basic_string& append( InputIt first, InputIt last )
        {
            SW_SCOPED_RACE_WRITE();
            Base::append( first, last );
            return *this;
        }

        /** @brief 초기화 리스트의 문자를 이어 붙입니다. */
        basic_string& append( std::initializer_list<CharT> ilist )
        {
            SW_SCOPED_RACE_WRITE();
            Base::append( ilist );
            return *this;
        }

        /** @brief 뒤에 이어 붙입니다. */
        basic_string& operator+=( const Base& str )
        {
            SW_SCOPED_RACE_WRITE();
            Base::operator+=( str );
            return *this;
        }

        /** @brief std::basic_string 참조로 변환합니다. */
        operator const Base&() const noexcept { return *this; }
        /** @brief string_view 로 변환합니다. */
        operator std::string_view() const noexcept { return std::string_view( this->data(), this->size() ); }
        /** @brief 뒤에 이어 붙입니다. */
        basic_string& operator+=( const basic_string& str ) { return append( str ); }
        /** @brief 뒤에 문자 하나를 붙입니다. */
        basic_string& operator+=( CharT ch )
        {
            push_back( ch );
            return *this;
        }

        /** @brief 뒤에 이어 붙입니다. */
        basic_string& operator+=( const CharT* pS ) { return append( pS ); }
        /** @brief 뒤에 이어 붙입니다. */
        basic_string& operator+=( std::initializer_list<CharT> ilist ) { return append( ilist ); }
        /** @brief 뒤에 이어 붙입니다. */
        basic_string& operator+=( std::string_view sv )
        {
            SW_SCOPED_RACE_WRITE();
            Base::append( sv.data(), sv.size() );
            return *this;
        }

        /** @brief 사전순으로 비교합니다. 작으면 음수, 같으면 0, 크면 양수입니다. */
        int32 compare( const basic_string& str ) const noexcept
        {
            SW_SCOPED_RACE_READ();
            SW_SCOPED_RACE_READ_OTHER( str );
            return Base::compare( static_cast<const Base&>( str ) );
        }

        /** @brief 사전순으로 비교합니다. */
        int32 compare( size_type pos1, size_type count1, const basic_string& str ) const
        {
            SW_SCOPED_RACE_READ();
            SW_SCOPED_RACE_READ_OTHER( str );
            return Base::compare( pos1, count1, static_cast<const Base&>( str ) );
        }

        /** @brief 사전순으로 비교합니다. */
        int32 compare( size_type pos1, size_type count1, const basic_string& str, size_type pos2, size_type count2 = npos ) const
        {
            SW_SCOPED_RACE_READ();
            SW_SCOPED_RACE_READ_OTHER( str );
            return Base::compare( pos1, count1, static_cast<const Base&>( str ), pos2, count2 );
        }

        /** @brief 사전순으로 비교합니다. */
        int32 compare( const CharT* pS ) const
        {
            SW_SCOPED_RACE_READ();
            return Base::compare( pS );
        }

        /** @brief 사전순으로 비교합니다. */
        int32 compare( size_type pos1, size_type count1, const CharT* pS ) const
        {
            SW_SCOPED_RACE_READ();
            return Base::compare( pos1, count1, pS );
        }

        /** @brief 사전순으로 비교합니다. */
        int32 compare( size_type pos1, size_type count1, const CharT* pS, size_type count2 ) const
        {
            SW_SCOPED_RACE_READ();
            return Base::compare( pos1, count1, pS, count2 );
        }

        /** @brief 같은지 비교합니다. */
        bool operator==( const basic_string& rhs ) const noexcept { return compare( rhs ) == 0; }
        /** @brief 다른지 비교합니다. */
        bool operator!=( const basic_string& rhs ) const noexcept { return compare( rhs ) != 0; }
        /** @brief 사전순으로 작은지 비교합니다. */
        bool operator<( const basic_string& rhs ) const noexcept { return compare( rhs ) < 0; }
        /** @brief 작거나 같은지 비교합니다. */
        bool operator<=( const basic_string& rhs ) const noexcept { return compare( rhs ) <= 0; }
        /** @brief 사전순으로 큰지 비교합니다. */
        bool operator>( const basic_string& rhs ) const noexcept { return compare( rhs ) > 0; }
        /** @brief 크거나 같은지 비교합니다. */
        bool operator>=( const basic_string& rhs ) const noexcept { return compare( rhs ) >= 0; }

        /** @brief 같은지 비교합니다. */
        bool operator==( const CharT* pRhs ) const noexcept { return compare( pRhs ) == 0; }
        /** @brief 다른지 비교합니다. */
        bool operator!=( const CharT* pRhs ) const noexcept { return compare( pRhs ) != 0; }
        /** @brief 사전순으로 작은지 비교합니다. */
        bool operator<( const CharT* pRhs ) const noexcept { return compare( pRhs ) < 0; }
        /** @brief 작거나 같은지 비교합니다. */
        bool operator<=( const CharT* pRhs ) const noexcept { return compare( pRhs ) <= 0; }
        /** @brief 사전순으로 큰지 비교합니다. */
        bool operator>( const CharT* pRhs ) const noexcept { return compare( pRhs ) > 0; }
        /** @brief 크거나 같은지 비교합니다. */
        bool operator>=( const CharT* pRhs ) const noexcept { return compare( pRhs ) >= 0; }

        // 검색
        /** @brief pos 부터 str 이 처음 나오는 위치를 찾습니다. 없으면 npos 입니다. */
        size_type find( const basic_string& str, size_type pos = 0 ) const noexcept
        {
            SW_SCOPED_RACE_READ();
            SW_SCOPED_RACE_READ_OTHER( str );
            return Base::find( static_cast<const Base&>( str ), pos );
        }

        /** @brief pos 부터 C 문자열의 앞 count 문자가 처음 나오는 위치를 찾습니다. 없으면 npos 입니다. */
        size_type find( const CharT* pS, size_type pos, size_type count ) const
        {
            SW_SCOPED_RACE_READ();
            return Base::find( pS, pos, count );
        }

        /** @brief pos 부터 널 종료 C 문자열이 처음 나오는 위치를 찾습니다. 없으면 npos 입니다. */
        size_type find( const CharT* pS, size_type pos = 0 ) const
        {
            SW_SCOPED_RACE_READ();
            return Base::find( pS, pos );
        }

        /** @brief pos 부터 문자 ch 가 처음 나오는 위치를 찾습니다. 없으면 npos 입니다. */
        size_type find( CharT ch, size_type pos = 0 ) const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::find( ch, pos );
        }
    };
#endif

    using string  = basic_string<utf8>;
    using wstring = basic_string<utf16>;
#if defined( __cpp_char8_t )
    using u8string = basic_string<char8_t>;
#endif
    using u16string = basic_string<char16_t>;
    using u32string = basic_string<char32_t>;

    /**
     * @struct RuntimeStringHash
     * @brief 프로세스 안에서만 쓰는 바이트 해시입니다. 해시 컨테이너의 `std::hash<sw::string>` · `std::hash<sw::wstring>` 이 씁니다.
     * @details **파일이나 네트워크에 남기지 마십시오.** 이 구현이 바뀌면 값도 달라집니다. 밖에 남는 해시(쿠킹 산출물 · intern
     *          이름)의 기준은 `StringUtil::computeHash64`(FNV-1a)이고, 그쪽은 바꾸지 않습니다.
     *          예전에는 `std::hash<std::string_view>` 로 넘겼습니다. MSVC STL 의 그 구현은 바이트마다 앞 결과를 기다리는 곱셈이
     *          하나씩 있는 FNV-1a 라서, 35자짜리 경로 키 하나에 곱셈 35번이 줄지어 이어졌습니다(`ContainerBenchTest.StringKeyLookup`).
     *          여기서는 8바이트씩 읽어 섞으므로 기다리는 곱셈이 8분의 1 입니다. 마지막에 splitmix64 의 마무리 단계로 비트를 고르게
     *          흩어서, 버킷 번호를 어느 비트에서 뽑아도 됩니다. 길이를 씨앗에 넣어, 끝의 0 바이트만 다른 두 키도 구별됩니다.
     */
    struct RuntimeStringHash
    {
        /** @brief @p pData 의 @p byteCount 바이트를 해시합니다. */
        static uint64 compute( const void* pData, size_t byteCount ) noexcept
        {
            constexpr uint64 kWordMultiplier  = 0xBF58476D1CE4E5B9ull;
            constexpr uint64 kStateMultiplier = 0x94D049BB133111EBull;
            const uint8*     pByte            = static_cast<const uint8*>( pData );
            uint64           hash             = 0x9E3779B97F4A7C15ull ^ ( static_cast<uint64>( byteCount ) * 0xC2B2AE3D27D4EB4Full );
            while ( byteCount >= 8 )
            {
                uint64 word = 0;
                // 정렬되지 않은 8바이트 읽기. 크기가 상수라 load 한 번으로 접힌다(`Memory::copy` 는 함수 호출이다).
                std::memcpy( &word, pByte, 8 );
                hash ^= word * kWordMultiplier;
                hash = ( ( hash << 27 ) | ( hash >> 37 ) ) * kStateMultiplier;
                pByte += 8;
                byteCount -= 8;
            }
            if ( byteCount > 0 )
            {
                uint64 word = 0;
                for ( size_t byteIndex = 0; byteIndex < byteCount; ++byteIndex )
                    word |= static_cast<uint64>( pByte[byteIndex] ) << ( byteIndex * 8 );
                hash ^= word * kWordMultiplier;
                hash = ( ( hash << 27 ) | ( hash >> 37 ) ) * kStateMultiplier;
            }
            hash ^= hash >> 30;
            hash *= kWordMultiplier;
            hash ^= hash >> 27;
            hash *= kStateMultiplier;
            hash ^= hash >> 31;
            return hash;
        }
    };
} // namespace sw

namespace std
{
    /**
     * @brief sw::string 을 해시 컨테이너 키로 쓸 때의 해시(`sw::RuntimeStringHash`)입니다. 세 오버로드가 같은 바이트에 같은 값을 내므로 이종 조회가 됩니다.
     */
    template <>
    struct hash<sw::string>
    {
        using is_transparent = void;
        /** @brief 내용 바이트를 해시합니다. */
        size_t operator()( std::string_view s ) const noexcept { return static_cast<size_t>( sw::RuntimeStringHash::compute( s.data(), s.size() ) ); }
        /** @brief 내용 바이트를 해시합니다. */
        size_t operator()( const sw::string& s ) const noexcept { return operator()( std::string_view{ s.data(), s.size() } ); }
        /** @brief 널 종료 문자열의 바이트를 해시합니다. */
        size_t operator()( const utf8* s ) const noexcept { return operator()( std::string_view{ s } ); }
    };

    /**
     * @brief sw::wstring 을 해시 컨테이너 키로 쓸 때의 해시입니다. 문자 바이트를 `sw::RuntimeStringHash` 로 해시합니다.
     */
    template <>
    struct hash<sw::wstring>
    {
        using is_transparent = void;
        /** @brief 내용 바이트를 해시합니다. */
        size_t operator()( std::wstring_view s ) const noexcept
        {
            return static_cast<size_t>( sw::RuntimeStringHash::compute( s.data(), s.size() * sizeof( utf16 ) ) );
        }
        /** @brief 내용 바이트를 해시합니다. */
        size_t operator()( const sw::wstring& s ) const noexcept { return operator()( std::wstring_view{ s.data(), s.size() } ); }
        /** @brief 널 종료 문자열의 바이트를 해시합니다. */
        size_t operator()( const utf16* s ) const noexcept { return operator()( std::wstring_view{ s } ); }
    };
} // namespace std
