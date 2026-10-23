/**
 * @file string.h
 * @brief std::basic_string 래퍼입니다. 디버그 빌드에서는 RaceDetectContext 로 동시 접근을 잡아냅니다.
 */
#pragma once
#include "Core/Common/HashUtil.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/DataRaceDetector.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /** @brief std::basic_string 에 디버그 레이스 탐지를 더한 것입니다. API 는 STL 과 같습니다. */
    template <typename CharT, typename Traits = std::char_traits<CharT>, typename Allocator = ContainerAllocator<CharT>>
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
        basic_string() noexcept( noexcept( Allocator() ) )
            : Base() {}

        explicit basic_string( const Allocator& alloc ) noexcept
            : Base( alloc ) {}

        basic_string( size_type count, CharT ch, const Allocator& alloc = Allocator() )
            : Base( count, ch, alloc ) {}

        basic_string( const basic_string& other, size_type pos, const Allocator& alloc = Allocator() )
            : Base( static_cast<const Base&>( other ), pos, alloc ) {}

        basic_string( const basic_string& other, size_type pos, size_type count, const Allocator& alloc = Allocator() )
            : Base( static_cast<const Base&>( other ), pos, count, alloc ) {}

        basic_string( const CharT* pS, size_type count, const Allocator& alloc = Allocator() )
            : Base( pS, count, alloc ) {}

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

        template <class InputIt>
        basic_string( InputIt first, InputIt last, const Allocator& alloc = Allocator() )
            : Base( first, last, alloc ) {}

        basic_string( const Base& other )
            : Base( other ) {}

        basic_string( Base&& other ) noexcept
            : Base( std::move( other ) ) {}

        basic_string( const basic_string& other )
            : Base( static_cast<const Base&>( other ) ) {}

        basic_string( const basic_string& other, const Allocator& alloc )
            : Base( static_cast<const Base&>( other ), alloc ) {}

        basic_string( basic_string&& other ) noexcept
            : Base( std::move( static_cast<Base&>( other ) ) ) {}

        basic_string( basic_string&& other, const Allocator& alloc )
            : Base( std::move( static_cast<Base&>( other ) ), alloc ) {}

        basic_string( std::initializer_list<CharT> ilist, const Allocator& alloc = Allocator() )
            : Base( ilist, alloc ) {}

        template <class StringViewLike>
        explicit basic_string( const StringViewLike& t, const Allocator& alloc = Allocator() )
            : Base( t, alloc ) {}

        template <class StringViewLike>
        basic_string( const StringViewLike& t, size_type pos, size_type n, const Allocator& alloc = Allocator() )
            : Base( t, pos, n, alloc ) {}

        basic_string& operator=( const Base& other )
        {
            SW_SCOPED_RACE_WRITE();
            Base::operator=( other );
            return *this;
        }

        basic_string& operator=( Base&& other ) noexcept
        {
            SW_SCOPED_RACE_WRITE();
            Base::operator=( std::move( other ) );
            return *this;
        }

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

        basic_string& operator=( const CharT* s )
        {
            SW_SCOPED_RACE_WRITE();
            Base::operator=( s );
            return *this;
        }

        basic_string& operator=( CharT ch )
        {
            SW_SCOPED_RACE_WRITE();
            Base::operator=( ch );
            return *this;
        }

        basic_string& operator=( std::initializer_list<CharT> ilist )
        {
            SW_SCOPED_RACE_WRITE();
            Base::operator=( ilist );
            return *this;
        }

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

        reference front()
        {
            SW_SCOPED_RACE_WRITE();
            return Base::front();
        }

        const_reference front() const
        {
            SW_SCOPED_RACE_READ();
            return Base::front();
        }

        reference back()
        {
            SW_SCOPED_RACE_WRITE();
            return Base::back();
        }

        const_reference back() const
        {
            SW_SCOPED_RACE_READ();
            return Base::back();
        }

        const CharT* data() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::data();
        }

        CharT* data() noexcept
        {
            SW_SCOPED_RACE_WRITE();
            return Base::data();
        }

        const CharT* c_str() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::c_str();
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

        /** @brief 문자 수를 반환합니다(size() 와 같습니다). */
        size_type length() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::length();
        }

        size_type max_size() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::max_size();
        }

        void reserve( size_type new_cap )
        {
            SW_SCOPED_RACE_WRITE();
            Base::reserve( new_cap );
        }

        size_type capacity() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::capacity();
        }

        void shrink_to_fit()
        {
            SW_SCOPED_RACE_WRITE();
            Base::shrink_to_fit();
        }

        // ------------------------------------------------------------------------------
        // 3) 변경 — append/insert/erase. 쓰기 가드를 잡는다
        // ------------------------------------------------------------------------------
        void clear() noexcept
        {
            SW_SCOPED_RACE_WRITE();
            Base::clear();
        }

        basic_string& insert( size_type index, size_type count, CharT ch )
        {
            SW_SCOPED_RACE_WRITE();
            Base::insert( index, count, ch );
            return *this;
        }

        basic_string& insert( size_type index, const CharT* pS )
        {
            SW_SCOPED_RACE_WRITE();
            Base::insert( index, pS );
            return *this;
        }

        basic_string& insert( size_type index, const CharT* pS, size_type count )
        {
            SW_SCOPED_RACE_WRITE();
            Base::insert( index, pS, count );
            return *this;
        }

        basic_string& insert( size_type index, const basic_string& str )
        {
            SW_SCOPED_RACE_WRITE();
            Base::insert( index, static_cast<const Base&>( str ) );
            return *this;
        }

        basic_string& insert( size_type index, const basic_string& str, size_type index_str, size_type count = npos )
        {
            SW_SCOPED_RACE_WRITE();
            Base::insert( index, static_cast<const Base&>( str ), index_str, count );
            return *this;
        }

        iterator insert( const_iterator pos, CharT ch )
        {
            SW_SCOPED_RACE_WRITE();
            return Base::insert( pos, ch );
        }

        iterator insert( const_iterator pos, size_type count, CharT ch )
        {
            SW_SCOPED_RACE_WRITE();
            return Base::insert( pos, count, ch );
        }

        template <class InputIt>
        iterator insert( const_iterator pos, InputIt first, InputIt last )
        {
            SW_SCOPED_RACE_WRITE();
            return Base::insert( pos, first, last );
        }

        iterator insert( const_iterator pos, std::initializer_list<CharT> ilist )
        {
            SW_SCOPED_RACE_WRITE();
            return Base::insert( pos, ilist );
        }

        basic_string& erase( size_type index = 0, size_type count = npos )
        {
            SW_SCOPED_RACE_WRITE();
            Base::erase( index, count );
            return *this;
        }

        iterator erase( const_iterator pos )
        {
            SW_SCOPED_RACE_WRITE();
            return Base::erase( pos );
        }

        iterator erase( const_iterator first, const_iterator last )
        {
            SW_SCOPED_RACE_WRITE();
            return Base::erase( first, last );
        }

        void push_back( CharT ch )
        {
            SW_SCOPED_RACE_WRITE();
            Base::push_back( ch );
        }

        void pop_back()
        {
            SW_SCOPED_RACE_WRITE();
            Base::pop_back();
        }

        basic_string& append( size_type count, CharT ch )
        {
            SW_SCOPED_RACE_WRITE();
            Base::append( count, ch );
            return *this;
        }

        basic_string& append( const basic_string& str )
        {
            SW_SCOPED_RACE_WRITE();
            Base::append( static_cast<const Base&>( str ) );
            return *this;
        }

        basic_string& append( const basic_string& str, size_type pos, size_type count = npos )
        {
            SW_SCOPED_RACE_WRITE();
            Base::append( static_cast<const Base&>( str ), pos, count );
            return *this;
        }

        basic_string& append( const CharT* pS, size_type count )
        {
            SW_SCOPED_RACE_WRITE();
            Base::append( pS, count );
            return *this;
        }

        basic_string& append( const CharT* pS )
        {
            SW_SCOPED_RACE_WRITE();
            Base::append( pS );
            return *this;
        }

        /** @brief 뷰의 글자를 이어 붙입니다(`std::string::append( string_view )` 와 같다). */
        basic_string& append( std::basic_string_view<CharT> view )
        {
            SW_SCOPED_RACE_WRITE();
            Base::append( view.data(), view.size() );
            return *this;
        }

        template <class InputIt>
        basic_string& append( InputIt first, InputIt last )
        {
            SW_SCOPED_RACE_WRITE();
            Base::append( first, last );
            return *this;
        }

        basic_string& append( std::initializer_list<CharT> ilist )
        {
            SW_SCOPED_RACE_WRITE();
            Base::append( ilist );
            return *this;
        }

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
        basic_string& operator+=( const basic_string& str ) { return append( str ); }
        basic_string& operator+=( CharT ch )
        {
            push_back( ch );
            return *this;
        }

        basic_string& operator+=( const CharT* pS ) { return append( pS ); }
        basic_string& operator+=( std::initializer_list<CharT> ilist ) { return append( ilist ); }
        basic_string& operator+=( std::string_view sv )
        {
            SW_SCOPED_RACE_WRITE();
            Base::append( sv.data(), sv.size() );
            return *this;
        }

        int32 compare( const basic_string& str ) const noexcept
        {
            SW_SCOPED_RACE_READ();
            SW_SCOPED_RACE_READ_OTHER( str );
            return Base::compare( static_cast<const Base&>( str ) );
        }

        int32 compare( size_type pos1, size_type count1, const basic_string& str ) const
        {
            SW_SCOPED_RACE_READ();
            SW_SCOPED_RACE_READ_OTHER( str );
            return Base::compare( pos1, count1, static_cast<const Base&>( str ) );
        }

        int32 compare( size_type pos1, size_type count1, const basic_string& str, size_type pos2, size_type count2 = npos ) const
        {
            SW_SCOPED_RACE_READ();
            SW_SCOPED_RACE_READ_OTHER( str );
            return Base::compare( pos1, count1, static_cast<const Base&>( str ), pos2, count2 );
        }

        int32 compare( const CharT* pS ) const
        {
            SW_SCOPED_RACE_READ();
            return Base::compare( pS );
        }

        int32 compare( size_type pos1, size_type count1, const CharT* pS ) const
        {
            SW_SCOPED_RACE_READ();
            return Base::compare( pos1, count1, pS );
        }

        int32 compare( size_type pos1, size_type count1, const CharT* pS, size_type count2 ) const
        {
            SW_SCOPED_RACE_READ();
            return Base::compare( pos1, count1, pS, count2 );
        }

        bool operator==( const basic_string& rhs ) const noexcept { return compare( rhs ) == 0; }
        bool operator!=( const basic_string& rhs ) const noexcept { return compare( rhs ) != 0; }
        bool operator<( const basic_string& rhs ) const noexcept { return compare( rhs ) < 0; }
        bool operator<=( const basic_string& rhs ) const noexcept { return compare( rhs ) <= 0; }
        bool operator>( const basic_string& rhs ) const noexcept { return compare( rhs ) > 0; }
        bool operator>=( const basic_string& rhs ) const noexcept { return compare( rhs ) >= 0; }

        bool operator==( const CharT* pRhs ) const noexcept { return compare( pRhs ) == 0; }
        bool operator!=( const CharT* pRhs ) const noexcept { return compare( pRhs ) != 0; }
        bool operator<( const CharT* pRhs ) const noexcept { return compare( pRhs ) < 0; }
        bool operator<=( const CharT* pRhs ) const noexcept { return compare( pRhs ) <= 0; }
        bool operator>( const CharT* pRhs ) const noexcept { return compare( pRhs ) > 0; }
        bool operator>=( const CharT* pRhs ) const noexcept { return compare( pRhs ) >= 0; }

        // 검색
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

        size_type find( CharT ch, size_type pos = 0 ) const noexcept
        {
            SW_SCOPED_RACE_READ();
            return Base::find( ch, pos );
        }
    };
} // namespace sw

namespace sw
{
    using string  = basic_string<utf8>;
    using wstring = basic_string<utf16>;
#if defined( __cpp_char8_t )
    using u8string = basic_string<char8_t>;
#endif
    using u16string = basic_string<char16_t>;
    using u32string = basic_string<char32_t>;

    /**
     * @struct RuntimeStringHash
     * @brief 프로세스 안에서만 쓰는 바이트 해시입니다. 해시 컨테이너의 `std::hash<sw::string>` · `std::hash<sw::wstring>` · `std::hash<fixed_string>` 이 씁니다.
     * @details **파일이나 네트워크에 남기지 마십시오.** 이 구현이 바뀌면 값도 달라집니다. 밖에 남는 해시(쿠킹 산출물 · intern
     *          이름)의 기준은 `HashUtil::computeStringHash64`(FNV-1a)이고, 그쪽은 바꾸지 않습니다.
     *          `std::hash<std::string_view>` 로 넘기지 않습니다. MSVC STL 의 그 구현은 바이트마다 앞 결과를 기다리는 곱셈이
     *          하나씩 있는 FNV-1a 라서, 35자짜리 경로 키 하나에 곱셈 35번이 줄지어 이어집니다(`ContainerBenchTest.StringKeyLookup`).
     *          여기서는 8바이트씩 읽어 섞으므로 기다리는 곱셈이 8분의 1 입니다. 마지막에 splitmix64 의 마무리 단계로 비트를 고르게
     *          흩어서, 버킷 번호를 어느 비트에서 뽑아도 됩니다. 길이를 씨앗에 넣어, 끝의 0 바이트만 다른 두 키도 구별됩니다.
     */
    struct RuntimeStringHash
    {
        /** @brief @p pData 의 @p byteCount 바이트를 해시합니다. */
        static uint64 compute( const void* pData, size_t byteCount ) noexcept
        {
            const uint8* pByte = static_cast<const uint8*>( pData );
            uint64       hash  = HashUtil::kGoldenRatio64 ^ ( static_cast<uint64>( byteCount ) * 0xC2B2AE3D27D4EB4Full );
            while ( byteCount >= 8 )
            {
                uint64 word = 0;
                // 정렬되지 않은 8바이트 읽기. 크기가 상수라 load 한 번으로 접힌다(`Memory::copy` 는 함수 호출이다).
                std::memcpy( &word, pByte, 8 );
                hash ^= word * HashUtil::kSplitMixMultiplier0;
                hash = ( ( hash << 27 ) | ( hash >> 37 ) ) * HashUtil::kSplitMixMultiplier1;
                pByte += 8;
                byteCount -= 8;
            }
            if ( byteCount > 0 )
            {
                uint64 word = 0;
                for ( size_t byteIndex = 0; byteIndex < byteCount; ++byteIndex )
                {
                    word |= static_cast<uint64>( pByte[byteIndex] ) << ( byteIndex * 8 );
                }
                hash ^= word * HashUtil::kSplitMixMultiplier0;
                hash = ( ( hash << 27 ) | ( hash >> 37 ) ) * HashUtil::kSplitMixMultiplier1;
            }
            hash ^= hash >> 30;
            hash *= HashUtil::kSplitMixMultiplier0;
            hash ^= hash >> 27;
            hash *= HashUtil::kSplitMixMultiplier1;
            hash ^= hash >> 31;
            return hash;
        }
    };

    /**
     * @brief sw::string 을 해시 컨테이너 키로 쓸 때의 기본 해시(`sw::RuntimeStringHash`)입니다. 세 오버로드가 같은 바이트에 같은 값을 내므로 이종 조회가 됩니다.
     * @details 원본은 `std::hash` 특수화가 아니라 이 `DefaultHash` 특수화입니다. 표준 해시에는 `string_view` 오버로드가 없어 이종 조회가 안 되므로 여기 둡니다.
     */
    template <>
    struct DefaultHash<string>
    {
        using is_transparent = void;
        /** @brief 내용 바이트를 해시합니다. */
        [[nodiscard]] size_t operator()( std::string_view s ) const noexcept { return static_cast<size_t>( RuntimeStringHash::compute( s.data(), s.size() ) ); }
        /** @brief 내용 바이트를 해시합니다. */
        [[nodiscard]] size_t operator()( const string& s ) const noexcept { return operator()( std::string_view{ s.data(), s.size() } ); }
        /** @brief 널 종료 문자열의 바이트를 해시합니다. */
        [[nodiscard]] size_t operator()( const utf8* s ) const noexcept { return operator()( std::string_view{ s } ); }
    };

    /**
     * @brief sw::wstring 을 해시 컨테이너 키로 쓸 때의 기본 해시입니다. 문자 바이트를 `sw::RuntimeStringHash` 로 해시합니다(`DefaultHash<string>` 과 같은 이유로 여기 둡니다).
     */
    template <>
    struct DefaultHash<wstring>
    {
        using is_transparent = void;
        /** @brief 내용 바이트를 해시합니다. */
        [[nodiscard]] size_t operator()( std::wstring_view s ) const noexcept
        {
            return static_cast<size_t>( RuntimeStringHash::compute( s.data(), s.size() * sizeof( utf16 ) ) );
        }
        /** @brief 내용 바이트를 해시합니다. */
        [[nodiscard]] size_t operator()( const wstring& s ) const noexcept { return operator()( std::wstring_view{ s.data(), s.size() } ); }
        /** @brief 널 종료 문자열의 바이트를 해시합니다. */
        [[nodiscard]] size_t operator()( const utf16* s ) const noexcept { return operator()( std::wstring_view{ s } ); }
    };
} // namespace sw

// `std::hash` 로 직접 부르는 쪽(std 컨테이너 등)도 같은 값을 받게 합니다.
namespace std
{
    /** @brief sw::string 의 `std::hash` 입니다. 본문은 `sw::DefaultHash<sw::string>` 입니다. */
    template <>
    struct hash<sw::string> : sw::DefaultHash<sw::string>
    {
    };

    /** @brief sw::wstring 의 `std::hash` 입니다. 본문은 `sw::DefaultHash<sw::wstring>` 입니다. */
    template <>
    struct hash<sw::wstring> : sw::DefaultHash<sw::wstring>
    {
    };
} // namespace std
