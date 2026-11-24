/**
 * @file string.h
 * @brief 글자 수를 아는 널 종료 문자열 sw::basic_string 입니다(std::basic_string(C++17)과 인터페이스 호환). 검사 구성에서는 RaceDetectContext 로 동시 접근을 잡아냅니다.
 * @details 공개 API · 레이스 검사 · 범위 밖 계약은 이 클래스 한 벌이고, 글자를 담는 일만 `internal::StringStorage` 가 한다. 저장 형식은 **여기 한 자리** 에서 구성(`SW_ENABLE_STL_CONTAINER`)으로 고른다:
 *          기본은 자체 구현(`Native/NativeStringStorage.h` — 세 칸 배치), 켜면 std::basic_string 을 멤버로 든 것(`Std/StdStringStorage.h`)이다. 쓰는 쪽 코드는 구성을 모른다.
 *          std 를 상속하지 않는 까닭: 다시 선언하지 않은 함수(`assign` · `resize` · `rfind` …)가 레이스 · 범위 검사 없이 기반 클래스로 샌다. 변경은 모두 저장 형식의 `openGap` 한 길을 지난다.
 */
#pragma once
#include "Core/Concurrency/DataRaceDetector.h"
#include "Core/Container/BitwiseRelocatable.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Memory/Memory.h"

#if defined( SW_ENABLE_STL_CONTAINER )
    #include "Core/Container/Std/StdStringStorage.h"
#else
    #include "Core/Container/Native/NativeStringStorage.h"
#endif

namespace sw
{
    /** @brief 글자 수를 아는 널 종료 문자열입니다. API 는 STL(C++17)과 같고, 위치가 범위를 넘으면 예외 대신 개발 구성에서 단언하고 배포에서는 끝으로 자릅니다. 파일 머리말 참고. */
    template <typename CharT, typename Traits = std::char_traits<CharT>, typename Allocator = ContainerAllocator<CharT>>
    class basic_string
    {
        static_assert( std::is_trivially_copyable_v<CharT> && std::is_trivially_destructible_v<CharT>, "sw::basic_string needs a plain character type" );
        static_assert( std::is_empty_v<Allocator>, "sw::basic_string does not store its allocator - it must be stateless" );

        using View = std::basic_string_view<CharT, Traits>;

        /** @brief @p It 가 글자를 내는 입력 반복자면 선언을 켭니다(정수 둘 `string( 5, 'c' )` 이 반복자 쌍으로 잡히지 않게). */
        template <typename It>
        using EnableIfIterator = std::enable_if_t<std::is_convertible_v<typename std::iterator_traits<It>::iterator_category, std::input_iterator_tag>, int32>;
        /** @brief @p T 가 뷰로 바뀌고 C 문자열로는 바뀌지 않는 형식(뷰 · 고정 문자열 …)이면 선언을 켭니다(std 의 string_view 오버로드와 같은 조건). */
        template <typename T>
        using EnableIfViewLike = std::enable_if_t<std::is_same_v<T, basic_string> == false && std::is_convertible_v<const T&, View> && std::is_convertible_v<const T&, const CharT*> == false, int32>;
        /** @brief @p It 가 이 문자열의 반복자면 선언을 켭니다 — 반복자가 맨 포인터라 리터럴 0(널 포인터 상수)이 위치 · 반복자 두 오버로드 사이에서 비기지 않게. */
        template <typename It>
        using EnableIfOwnIterator = std::enable_if_t<std::is_same_v<It, CharT*> || std::is_same_v<It, const CharT*>, int32>;

    public:
        using traits_type            = Traits;
        using value_type             = CharT;
        using allocator_type         = Allocator;
        using size_type              = size_t;
        using difference_type        = ptrdiff_t;
        using reference              = CharT&;
        using const_reference        = const CharT&;
        using pointer                = CharT*;
        using const_pointer          = const CharT*;
        using iterator               = CharT*;
        using const_iterator         = const CharT*;
        using reverse_iterator       = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        static constexpr size_type npos = std::numeric_limits<size_type>::max();

        // ------------------------------------------------------------------------------
        // 1) 생성 · 소멸 · 대입 — 레이스 컨텍스트는 인스턴스마다 따로 둔다
        // ------------------------------------------------------------------------------
        basic_string() noexcept = default;
        explicit basic_string( [[maybe_unused]] const Allocator& alloc ) noexcept {}

        basic_string( size_type count, CharT ch, [[maybe_unused]] const Allocator& alloc = Allocator() )
        {
            fillGap( 0, 0, count, ch );
        }

        basic_string( const basic_string& other, size_type pos, [[maybe_unused]] const Allocator& alloc = Allocator() )
        {
            SW_SCOPED_RACE_READ_OTHER( other );
            const View source = slice( other.viewInternal(), pos, npos );
            _storage.initialize( source.data(), source.size() );
        }

        basic_string( const basic_string& other, size_type pos, size_type count, [[maybe_unused]] const Allocator& alloc = Allocator() )
        {
            SW_SCOPED_RACE_READ_OTHER( other );
            const View source = slice( other.viewInternal(), pos, count );
            _storage.initialize( source.data(), source.size() );
        }

        basic_string( const CharT* pS, size_type count, [[maybe_unused]] const Allocator& alloc = Allocator() ) { _storage.initialize( pS, count ); }
        basic_string( const CharT* pS, [[maybe_unused]] const Allocator& alloc = Allocator() ) { _storage.initialize( pS, Traits::length( pS ) ); }

        /**
         * @brief 뷰의 문자를 복사해 소유합니다.
         * @details std 와 마찬가지로 explicit 입니다. 뷰를 반환하는 API 의 결과를 담을 때는 `string{ view }` 로 씁니다.
         */
        template <typename T, EnableIfViewLike<T> = 0>
        explicit basic_string( const T& text, [[maybe_unused]] const Allocator& alloc = Allocator() )
        {
            const View source = text;
            _storage.initialize( source.data(), source.size() );
        }

        template <typename T, EnableIfViewLike<T> = 0>
        basic_string( const T& text, size_type pos, size_type count, [[maybe_unused]] const Allocator& alloc = Allocator() )
        {
            const View source = slice( View( text ), pos, count );
            _storage.initialize( source.data(), source.size() );
        }

        template <typename InputIt, EnableIfIterator<InputIt> = 0>
        basic_string( InputIt first, InputIt last, [[maybe_unused]] const Allocator& alloc = Allocator() )
        {
            appendRange( first, last );
        }

        basic_string( std::initializer_list<CharT> ilist, [[maybe_unused]] const Allocator& alloc = Allocator() ) { _storage.initialize( ilist.begin(), ilist.size() ); }

        basic_string( const basic_string& other )
        {
            SW_SCOPED_RACE_READ_OTHER( other );
            _storage.initialize( other.dataInternal(), other.sizeInternal() );
        }

        basic_string( const basic_string& other, [[maybe_unused]] const Allocator& alloc )
        {
            SW_SCOPED_RACE_READ_OTHER( other );
            _storage.initialize( other.dataInternal(), other.sizeInternal() );
        }

        basic_string( basic_string&& other ) noexcept
        {
            SW_SCOPED_RACE_WRITE_OTHER( other );
            _storage = std::move( other._storage );
        }

        basic_string( basic_string&& other, [[maybe_unused]] const Allocator& alloc ) noexcept
        {
            SW_SCOPED_RACE_WRITE_OTHER( other );
            _storage = std::move( other._storage );
        }

        ~basic_string() = default;

        basic_string& operator=( const basic_string& other )
        {
            if ( this != &other )
            {
                SW_SCOPED_RACE_WRITE();
                SW_SCOPED_RACE_READ_OTHER( other );
                replaceGap( 0, sizeInternal(), other.dataInternal(), other.sizeInternal() );
            }
            return *this;
        }

        basic_string& operator=( basic_string&& other ) noexcept
        {
            if ( this != &other )
            {
                SW_SCOPED_RACE_WRITE();
                SW_SCOPED_RACE_WRITE_OTHER( other );
                _storage = std::move( other._storage );
            }
            return *this;
        }

        basic_string& operator=( const CharT* pS ) { return assign( pS ); }
        basic_string& operator=( CharT ch ) { return assign( 1, ch ); }
        basic_string& operator=( std::initializer_list<CharT> ilist ) { return assign( ilist ); }

        template <typename T, EnableIfViewLike<T> = 0>
        basic_string& operator=( const T& text )
        {
            return assign( text );
        }

        basic_string& assign( const basic_string& str ) { return *this = str; }
        basic_string& assign( basic_string&& str ) noexcept { return *this = std::move( str ); }
        basic_string& assign( size_type count, CharT ch )
        {
            SW_SCOPED_RACE_WRITE();
            fillGap( 0, sizeInternal(), count, ch );
            return *this;
        }

        basic_string& assign( const CharT* pS, size_type count )
        {
            SW_SCOPED_RACE_WRITE();
            replaceGap( 0, sizeInternal(), pS, count );
            return *this;
        }

        basic_string& assign( const CharT* pS ) { return assign( pS, Traits::length( pS ) ); }
        basic_string& assign( std::initializer_list<CharT> ilist ) { return assign( ilist.begin(), ilist.size() ); }
        basic_string& assign( const basic_string& str, size_type pos, size_type count = npos )
        {
            const View source = slice( View( str ), pos, count );
            return assign( source.data(), source.size() );
        }

        template <typename T, EnableIfViewLike<T> = 0>
        basic_string& assign( const T& text )
        {
            const View source = text;
            return assign( source.data(), source.size() );
        }

        template <typename T, EnableIfViewLike<T> = 0>
        basic_string& assign( const T& text, size_type pos, size_type count = npos )
        {
            const View source = slice( View( text ), pos, count );
            return assign( source.data(), source.size() );
        }

        template <typename InputIt, EnableIfIterator<InputIt> = 0>
        basic_string& assign( InputIt first, InputIt last )
        {
            SW_SCOPED_RACE_WRITE();
            replaceRange( 0, sizeInternal(), first, last );
            return *this;
        }

        allocator_type get_allocator() const noexcept { return Allocator(); }

        // ------------------------------------------------------------------------------
        // 2) 조회 — 문자 · 이터레이터 · 크기. const 가 아닌 접근은 참조가 밖으로 나가므로 쓰기 가드를 잡는다
        // ------------------------------------------------------------------------------
        /** @brief pos 위치의 문자를 반환합니다(범위는 개발 구성에서만 확인 — `vector::at` 과 같다). */
        reference at( size_type pos )
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX( pos, sizeInternal() );
            return dataInternal()[pos];
        }

        /** @brief pos 위치의 문자를 반환합니다(범위는 개발 구성에서만 확인 — `vector::at` 과 같다). */
        const_reference at( size_type pos ) const
        {
            SW_SCOPED_RACE_READ();
            SW_CHECK_INDEX( pos, sizeInternal() );
            return dataInternal()[pos];
        }

        /** @brief pos 위치의 문자를 반환합니다(범위는 개발 구성에서만 확인). */
        reference operator[]( size_type pos )
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX_OR_END( pos, sizeInternal() ); // 끝 자리(널 문자)까지 읽을 수 있다(std 와 같다)
            return dataInternal()[pos];
        }

        /** @brief pos 위치의 문자를 반환합니다(범위는 개발 구성에서만 확인). */
        const_reference operator[]( size_type pos ) const
        {
            SW_SCOPED_RACE_READ();
            SW_CHECK_INDEX_OR_END( pos, sizeInternal() ); // 끝 자리(널 문자)까지 읽을 수 있다(std 와 같다)
            return dataInternal()[pos];
        }

        reference front()
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX( 0, sizeInternal() );
            return dataInternal()[0];
        }

        const_reference front() const
        {
            SW_SCOPED_RACE_READ();
            SW_CHECK_INDEX( 0, sizeInternal() );
            return dataInternal()[0];
        }

        reference back()
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX( 0, sizeInternal() );
            return dataInternal()[sizeInternal() - 1];
        }

        const_reference back() const
        {
            SW_SCOPED_RACE_READ();
            SW_CHECK_INDEX( 0, sizeInternal() );
            return dataInternal()[sizeInternal() - 1];
        }

        const CharT* data() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return dataInternal();
        }

        CharT* data() noexcept
        {
            SW_SCOPED_RACE_WRITE();
            return dataInternal();
        }

        const CharT* c_str() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return dataInternal();
        }

        /** @brief string_view 로 변환합니다. */
        operator View() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return viewInternal();
        }

        // 이터레이터
        iterator begin() noexcept
        {
            SW_SCOPED_RACE_READ();
            return dataInternal();
        }

        const_iterator begin() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return dataInternal();
        }

        const_iterator cbegin() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return dataInternal();
        }

        iterator end() noexcept
        {
            SW_SCOPED_RACE_READ();
            return dataInternal() + sizeInternal();
        }

        const_iterator end() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return dataInternal() + sizeInternal();
        }

        const_iterator cend() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return dataInternal() + sizeInternal();
        }

        reverse_iterator       rbegin() noexcept { return reverse_iterator( end() ); }
        const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator( end() ); }
        const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator( cend() ); }
        reverse_iterator       rend() noexcept { return reverse_iterator( begin() ); }
        const_reverse_iterator rend() const noexcept { return const_reverse_iterator( begin() ); }
        const_reverse_iterator crend() const noexcept { return const_reverse_iterator( cbegin() ); }

        // 크기
        bool empty() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return sizeInternal() == 0;
        }

        size_type size() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return sizeInternal();
        }

        /** @brief 문자 수를 반환합니다(size() 와 같습니다). */
        size_type length() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return sizeInternal();
        }

        /** @brief 담을 수 있는 가장 긴 글자 수입니다. */
        size_type max_size() const noexcept { return _storage.max_size(); }

        size_type capacity() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return _storage.capacity();
        }

        /** @brief 글자 @p newCapacity 개를 더 할당하지 않고 담게 합니다. 줄이지 않습니다. */
        void reserve( size_type newCapacity )
        {
            SW_SCOPED_RACE_WRITE();
            if ( newCapacity > _storage.capacity() )
                _storage.reserve( newCapacity );
        }

        /** @brief 남는 용량을 놓습니다. 짧은 글 버퍼에 들면 힙 버퍼를 놓습니다. */
        void shrink_to_fit()
        {
            SW_SCOPED_RACE_WRITE();
            _storage.shrink_to_fit();
        }

        // ------------------------------------------------------------------------------
        // 3) 변경 — append/insert/erase/replace. 쓰기 가드를 잡는다. 넣는 글이 자기 글의 일부여도 된다(std 와 같다)
        // ------------------------------------------------------------------------------
        /** @brief 글을 비웁니다. 용량은 남깁니다. */
        void clear() noexcept
        {
            SW_SCOPED_RACE_WRITE();
            _storage.truncate( 0 );
        }

        void resize( size_type count ) { resize( count, CharT() ); }
        /** @brief 글자 수를 @p count 로 맞춥니다. 늘어난 자리는 @p ch 로 채웁니다. */
        void resize( size_type count, CharT ch )
        {
            SW_SCOPED_RACE_WRITE();
            const size_type size = sizeInternal();
            if ( count <= size )
                _storage.truncate( count );
            else
                fillGap( size, 0, count - size, ch );
        }

        void swap( basic_string& other ) noexcept
        {
            if ( this == &other )
                return;
            SW_SCOPED_RACE_WRITE();
            SW_SCOPED_RACE_WRITE_OTHER( other );
            _storage.swap( other._storage );
        }

        basic_string& insert( size_type index, size_type count, CharT ch )
        {
            SW_SCOPED_RACE_WRITE();
            fillGap( clampPosition( index ), 0, count, ch );
            return *this;
        }

        basic_string& insert( size_type index, const CharT* pS ) { return insert( index, pS, Traits::length( pS ) ); }
        basic_string& insert( size_type index, const CharT* pS, size_type count )
        {
            SW_SCOPED_RACE_WRITE();
            replaceGap( clampPosition( index ), 0, pS, count );
            return *this;
        }

        basic_string& insert( size_type index, const basic_string& str ) { return insert( index, str.data(), str.size() ); }
        basic_string& insert( size_type index, const basic_string& str, size_type indexStr, size_type count = npos )
        {
            const View source = slice( View( str ), indexStr, count );
            return insert( index, source.data(), source.size() );
        }

        template <typename T, EnableIfViewLike<T> = 0>
        basic_string& insert( size_type index, const T& text )
        {
            const View source = text;
            return insert( index, source.data(), source.size() );
        }

        template <typename T, EnableIfViewLike<T> = 0>
        basic_string& insert( size_type index, const T& text, size_type indexText, size_type count = npos )
        {
            const View source = slice( View( text ), indexText, count );
            return insert( index, source.data(), source.size() );
        }

        template <typename It, EnableIfOwnIterator<It> = 0>
        iterator insert( It pos, CharT ch )
        {
            return insert( pos, 1, ch );
        }

        template <typename It, EnableIfOwnIterator<It> = 0>
        iterator insert( It pos, size_type count, CharT ch )
        {
            SW_SCOPED_RACE_WRITE();
            const size_type index = clampPosition( static_cast<size_type>( pos - dataInternal() ) );
            fillGap( index, 0, count, ch );
            return dataInternal() + index;
        }

        template <typename It, typename InputIt, EnableIfOwnIterator<It> = 0, EnableIfIterator<InputIt> = 0>
        iterator insert( It pos, InputIt first, InputIt last )
        {
            SW_SCOPED_RACE_WRITE();
            const size_type index = clampPosition( static_cast<size_type>( pos - dataInternal() ) );
            replaceRange( index, 0, first, last );
            return dataInternal() + index;
        }

        template <typename It, EnableIfOwnIterator<It> = 0>
        iterator insert( It pos, std::initializer_list<CharT> ilist )
        {
            return insert( pos, ilist.begin(), ilist.end() );
        }

        /** @brief @p index 부터 @p count 글자를 지웁니다(한 번 민다). */
        basic_string& erase( size_type index = 0, size_type count = npos )
        {
            SW_SCOPED_RACE_WRITE();
            const size_type pos = clampPosition( index );
            (void)_storage.openGap( pos, clampCount( pos, count ), 0 );
            return *this;
        }

        template <typename It, EnableIfOwnIterator<It> = 0>
        iterator erase( It pos )
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX( pos - dataInternal(), sizeInternal() );
            return _storage.openGap( static_cast<size_type>( pos - dataInternal() ), 1, 0 );
        }

        template <typename ItFirst, typename ItLast, EnableIfOwnIterator<ItFirst> = 0, EnableIfOwnIterator<ItLast> = 0>
        iterator erase( ItFirst first, ItLast last )
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX_OR_END( last - dataInternal(), sizeInternal() );
            SW_CHECK_INDEX_OR_END( first - dataInternal(), last - dataInternal() );
            return _storage.openGap( static_cast<size_type>( first - dataInternal() ), static_cast<size_type>( last - first ), 0 );
        }

        void push_back( CharT ch )
        {
            SW_SCOPED_RACE_WRITE();
            _storage.push_back( ch );
        }

        void pop_back()
        {
            SW_SCOPED_RACE_WRITE();
            SW_CHECK_INDEX( 0, sizeInternal() );
            _storage.truncate( sizeInternal() - 1 );
        }

        basic_string& append( size_type count, CharT ch )
        {
            SW_SCOPED_RACE_WRITE();
            fillGap( sizeInternal(), 0, count, ch );
            return *this;
        }

        basic_string& append( const CharT* pS, size_type count )
        {
            SW_SCOPED_RACE_WRITE();
            replaceGap( sizeInternal(), 0, pS, count );
            return *this;
        }

        basic_string& append( const CharT* pS ) { return append( pS, Traits::length( pS ) ); }
        basic_string& append( const basic_string& str ) { return append( str.data(), str.size() ); }
        basic_string& append( const basic_string& str, size_type pos, size_type count = npos )
        {
            const View source = slice( View( str ), pos, count );
            return append( source.data(), source.size() );
        }

        basic_string& append( std::initializer_list<CharT> ilist ) { return append( ilist.begin(), ilist.size() ); }

        /** @brief 뷰의 글자를 이어 붙입니다(`std::string::append( string_view )` 와 같다). */
        template <typename T, EnableIfViewLike<T> = 0>
        basic_string& append( const T& text )
        {
            const View source = text;
            return append( source.data(), source.size() );
        }

        template <typename T, EnableIfViewLike<T> = 0>
        basic_string& append( const T& text, size_type pos, size_type count = npos )
        {
            const View source = slice( View( text ), pos, count );
            return append( source.data(), source.size() );
        }

        template <typename InputIt, EnableIfIterator<InputIt> = 0>
        basic_string& append( InputIt first, InputIt last )
        {
            SW_SCOPED_RACE_WRITE();
            appendRange( first, last );
            return *this;
        }

        basic_string& operator+=( const basic_string& str ) { return append( str ); }
        basic_string& operator+=( const CharT* pS ) { return append( pS ); }
        basic_string& operator+=( std::initializer_list<CharT> ilist ) { return append( ilist ); }
        basic_string& operator+=( CharT ch )
        {
            push_back( ch );
            return *this;
        }

        template <typename T, EnableIfViewLike<T> = 0>
        basic_string& operator+=( const T& text )
        {
            return append( text );
        }

        basic_string& replace( size_type pos, size_type count, const CharT* pS, size_type countS )
        {
            SW_SCOPED_RACE_WRITE();
            const size_type index = clampPosition( pos );
            replaceGap( index, clampCount( index, count ), pS, countS );
            return *this;
        }

        basic_string& replace( size_type pos, size_type count, const CharT* pS ) { return replace( pos, count, pS, Traits::length( pS ) ); }
        basic_string& replace( size_type pos, size_type count, const basic_string& str ) { return replace( pos, count, str.data(), str.size() ); }
        basic_string& replace( size_type pos, size_type count, const basic_string& str, size_type posStr, size_type countStr = npos )
        {
            const View source = slice( View( str ), posStr, countStr );
            return replace( pos, count, source.data(), source.size() );
        }

        basic_string& replace( size_type pos, size_type count, size_type countCh, CharT ch )
        {
            SW_SCOPED_RACE_WRITE();
            const size_type index = clampPosition( pos );
            fillGap( index, clampCount( index, count ), countCh, ch );
            return *this;
        }

        template <typename T, EnableIfViewLike<T> = 0>
        basic_string& replace( size_type pos, size_type count, const T& text )
        {
            const View source = text;
            return replace( pos, count, source.data(), source.size() );
        }

        template <typename T, EnableIfViewLike<T> = 0>
        basic_string& replace( size_type pos, size_type count, const T& text, size_type posText, size_type countText = npos )
        {
            const View source = slice( View( text ), posText, countText );
            return replace( pos, count, source.data(), source.size() );
        }

        /** @brief [pos, pos + count) 를 @p pDest 에 복사하고 복사한 글자 수를 돌려줍니다(널 문자를 붙이지 않는다 — std 와 같다). */
        size_type copy( CharT* pDest, size_type count, size_type pos = 0 ) const
        {
            const View source = slice( View( *this ), pos, count );
            copyChars( pDest, source.data(), source.size() );
            return source.size();
        }

        // ------------------------------------------------------------------------------
        // 4) 비교 · 검색 — 뷰의 것을 그대로 쓴다
        // ------------------------------------------------------------------------------
        int32 compare( const basic_string& str ) const noexcept { return View( *this ).compare( View( str ) ); }
        int32 compare( const CharT* pS ) const { return View( *this ).compare( View( pS ) ); }
        int32 compare( size_type pos, size_type count, const basic_string& str ) const { return slice( View( *this ), pos, count ).compare( View( str ) ); }
        int32 compare( size_type pos, size_type count, const CharT* pS ) const { return slice( View( *this ), pos, count ).compare( View( pS ) ); }
        int32 compare( size_type pos, size_type count, const CharT* pS, size_type countS ) const { return slice( View( *this ), pos, count ).compare( View( pS, countS ) ); }
        int32 compare( size_type pos, size_type count, const basic_string& str, size_type posStr, size_type countStr = npos ) const
        {
            return slice( View( *this ), pos, count ).compare( slice( View( str ), posStr, countStr ) );
        }

        template <typename T, EnableIfViewLike<T> = 0>
        int32 compare( const T& text ) const noexcept
        {
            return View( *this ).compare( View( text ) );
        }

        template <typename T, EnableIfViewLike<T> = 0>
        int32 compare( size_type pos, size_type count, const T& text ) const
        {
            return slice( View( *this ), pos, count ).compare( View( text ) );
        }

        bool operator==( const basic_string& rhs ) const noexcept { return View( *this ) == View( rhs ); }
        bool operator!=( const basic_string& rhs ) const noexcept { return View( *this ) != View( rhs ); }
        bool operator<( const basic_string& rhs ) const noexcept { return compare( rhs ) < 0; }
        bool operator<=( const basic_string& rhs ) const noexcept { return compare( rhs ) <= 0; }
        bool operator>( const basic_string& rhs ) const noexcept { return compare( rhs ) > 0; }
        bool operator>=( const basic_string& rhs ) const noexcept { return compare( rhs ) >= 0; }

        bool operator==( const CharT* pRhs ) const noexcept { return View( *this ) == View( pRhs ); }
        bool operator!=( const CharT* pRhs ) const noexcept { return View( *this ) != View( pRhs ); }
        bool operator<( const CharT* pRhs ) const noexcept { return compare( pRhs ) < 0; }
        bool operator<=( const CharT* pRhs ) const noexcept { return compare( pRhs ) <= 0; }
        bool operator>( const CharT* pRhs ) const noexcept { return compare( pRhs ) > 0; }
        bool operator>=( const CharT* pRhs ) const noexcept { return compare( pRhs ) >= 0; }

        /** @brief pos 부터 찾습니다. 없으면 npos 입니다(아래 검색 함수 모두). */
        size_type find( const basic_string& str, size_type pos = 0 ) const noexcept { return View( *this ).find( View( str ), pos ); }
        size_type find( View text, size_type pos = 0 ) const noexcept { return View( *this ).find( text, pos ); }
        size_type find( const CharT* pS, size_type pos, size_type count ) const { return View( *this ).find( pS, pos, count ); }
        size_type find( const CharT* pS, size_type pos = 0 ) const { return View( *this ).find( pS, pos ); }
        size_type find( CharT ch, size_type pos = 0 ) const noexcept { return View( *this ).find( ch, pos ); }

        size_type rfind( const basic_string& str, size_type pos = npos ) const noexcept { return View( *this ).rfind( View( str ), pos ); }
        size_type rfind( View text, size_type pos = npos ) const noexcept { return View( *this ).rfind( text, pos ); }
        size_type rfind( const CharT* pS, size_type pos, size_type count ) const { return View( *this ).rfind( pS, pos, count ); }
        size_type rfind( const CharT* pS, size_type pos = npos ) const { return View( *this ).rfind( pS, pos ); }
        size_type rfind( CharT ch, size_type pos = npos ) const noexcept { return View( *this ).rfind( ch, pos ); }

        size_type find_first_of( const basic_string& str, size_type pos = 0 ) const noexcept { return View( *this ).find_first_of( View( str ), pos ); }
        size_type find_first_of( View text, size_type pos = 0 ) const noexcept { return View( *this ).find_first_of( text, pos ); }
        size_type find_first_of( const CharT* pS, size_type pos, size_type count ) const { return View( *this ).find_first_of( pS, pos, count ); }
        size_type find_first_of( const CharT* pS, size_type pos = 0 ) const { return View( *this ).find_first_of( pS, pos ); }
        size_type find_first_of( CharT ch, size_type pos = 0 ) const noexcept { return View( *this ).find_first_of( ch, pos ); }

        size_type find_last_of( const basic_string& str, size_type pos = npos ) const noexcept { return View( *this ).find_last_of( View( str ), pos ); }
        size_type find_last_of( View text, size_type pos = npos ) const noexcept { return View( *this ).find_last_of( text, pos ); }
        size_type find_last_of( const CharT* pS, size_type pos, size_type count ) const { return View( *this ).find_last_of( pS, pos, count ); }
        size_type find_last_of( const CharT* pS, size_type pos = npos ) const { return View( *this ).find_last_of( pS, pos ); }
        size_type find_last_of( CharT ch, size_type pos = npos ) const noexcept { return View( *this ).find_last_of( ch, pos ); }

        size_type find_first_not_of( const basic_string& str, size_type pos = 0 ) const noexcept { return View( *this ).find_first_not_of( View( str ), pos ); }
        size_type find_first_not_of( View text, size_type pos = 0 ) const noexcept { return View( *this ).find_first_not_of( text, pos ); }
        size_type find_first_not_of( const CharT* pS, size_type pos, size_type count ) const { return View( *this ).find_first_not_of( pS, pos, count ); }
        size_type find_first_not_of( const CharT* pS, size_type pos = 0 ) const { return View( *this ).find_first_not_of( pS, pos ); }
        size_type find_first_not_of( CharT ch, size_type pos = 0 ) const noexcept { return View( *this ).find_first_not_of( ch, pos ); }

        size_type find_last_not_of( const basic_string& str, size_type pos = npos ) const noexcept { return View( *this ).find_last_not_of( View( str ), pos ); }
        size_type find_last_not_of( View text, size_type pos = npos ) const noexcept { return View( *this ).find_last_not_of( text, pos ); }
        size_type find_last_not_of( const CharT* pS, size_type pos, size_type count ) const { return View( *this ).find_last_not_of( pS, pos, count ); }
        size_type find_last_not_of( const CharT* pS, size_type pos = npos ) const { return View( *this ).find_last_not_of( pS, pos ); }
        size_type find_last_not_of( CharT ch, size_type pos = npos ) const noexcept { return View( *this ).find_last_not_of( ch, pos ); }

        /** @brief pos 부터 count 문자를 새 문자열로 돌려줍니다. */
        basic_string substr( size_type pos = 0, size_type count = npos ) const { return basic_string( *this, pos, count ); }

    private:
        const CharT* dataInternal() const noexcept { return _storage.data(); }
        CharT*       dataInternal() noexcept { return _storage.data(); }
        size_type    sizeInternal() const noexcept { return _storage.size(); }
        View         viewInternal() const noexcept { return View( _storage.data(), _storage.size() ); }

        /** @brief 글자 수가 0 이면 널 포인터를 넘겨도 되는 복사입니다(겹치지 않는 구간). */
        static void copyChars( CharT* pDest, const CharT* pSource, size_type count )
        {
            if ( count > 0 )
                Traits::copy( pDest, pSource, count );
        }

        size_type clampPosition( size_type pos ) const noexcept
        {
            const size_type size = sizeInternal();
            SW_CHECK_INDEX_OR_END( pos, size );
            return pos < size ? pos : size;
        }

        size_type clampCount( size_type pos, size_type count ) const noexcept
        {
            const size_type remaining = sizeInternal() - pos;
            return count < remaining ? count : remaining;
        }

        /** @brief 뷰의 [pos, pos + count) 입니다(`count` 는 끝에서 잘린다). 예외를 던지지 않는 `string_view::substr` 다. */
        static View slice( View text, size_type pos, size_type count ) noexcept
        {
            SW_CHECK_INDEX_OR_END( pos, text.size() );
            const size_type first = pos < text.size() ? pos : text.size();
            return View( text.data() + first, count < text.size() - first ? count : text.size() - first );
        }

        /** @brief 자기 글의 일부면 참입니다 — 그 글을 넣으려면 미리 떠 둔다(키우기는 옛 버퍼를 놓고 밀기는 덮는다). */
        bool isOwnText( const CharT* pChars ) const noexcept
        {
            const CharT* const pData = dataInternal();
            return std::less_equal<const CharT*>{}( pData, pChars ) && std::less_equal<const CharT*>{}( pChars, pData + sizeInternal() );
        }

        void replaceGap( size_type pos, size_type removeCount, const CharT* pSource, size_type sourceCount )
        {
            if ( sourceCount > 0 && isOwnText( pSource ) )
            {
                const basic_string copied( pSource, sourceCount );
                copyChars( _storage.openGap( pos, removeCount, sourceCount ), copied.dataInternal(), sourceCount );
                return;
            }
            copyChars( _storage.openGap( pos, removeCount, sourceCount ), pSource, sourceCount );
        }

        void fillGap( size_type pos, size_type removeCount, size_type fillCount, CharT ch )
        {
            CharT* const pGap = _storage.openGap( pos, removeCount, fillCount );
            if ( fillCount > 0 )
                Traits::assign( pGap, fillCount, ch );
        }

        /** @brief 반복자 구간으로 바꿉니다. 글자 포인터 구간은 그대로, 나머지는 한 번 모은 뒤 한 번에 넣는다(글자마다 밀지 않는다). */
        template <typename InputIt>
        void replaceRange( size_type pos, size_type removeCount, InputIt first, InputIt last )
        {
            if constexpr ( std::is_same_v<InputIt, CharT*> || std::is_same_v<InputIt, const CharT*> )
            {
                replaceGap( pos, removeCount, first, static_cast<size_type>( last - first ) );
            }
            else
            {
                basic_string gathered;
                gathered.appendRange( first, last );
                replaceGap( pos, removeCount, gathered.dataInternal(), gathered.sizeInternal() );
            }
        }

        /** @brief 반복자 구간을 끝에 붙입니다. 순방향 반복자면 길이를 먼저 세어 한 번만 키운다. */
        template <typename InputIt>
        void appendRange( InputIt first, InputIt last )
        {
            if constexpr ( std::is_same_v<InputIt, CharT*> || std::is_same_v<InputIt, const CharT*> )
            {
                replaceGap( sizeInternal(), 0, first, static_cast<size_type>( last - first ) );
            }
            else
            {
                if constexpr ( std::is_convertible_v<typename std::iterator_traits<InputIt>::iterator_category, std::forward_iterator_tag> )
                {
                    const size_type required = sizeInternal() + static_cast<size_type>( std::distance( first, last ) );
                    const size_type capacity = _storage.capacity();
                    const size_type grown    = capacity + capacity / 2; // 작은 구간을 거듭 붙여도 전체가 O(n) 이게 1.5 배보다 작게 키우지 않는다
                    if ( required > capacity )
                        _storage.reserve( required > grown ? required : grown );
                }
                for ( ; first != last; ++first )
                    push_back( static_cast<CharT>( *first ) );
            }
        }

    private:
        internal::StringStorage<CharT, Traits, Allocator> _storage; ///< 글자 — 자체 구현 또는 std::basic_string(파일 머리말)
        SW_RACE_CTX_MEMBER
    };

    /** @brief 저장 형식이 바이트로 옮겨도 되면 문자열도 그렇다(나머지 칸은 검사 구성의 레이스 컨텍스트뿐이다). */
    template <typename CharT, typename Traits, typename Allocator>
    struct is_bitwise_relocatable<basic_string<CharT, Traits, Allocator>> : std::bool_constant<internal::StringStorage<CharT, Traits, Allocator>::kBitwiseRelocatable>
    {
    };

    // C 문자열이 왼쪽인 비교. 뷰와의 비교는 std 의 `basic_string_view` 비교가 암시적 뷰 변환으로 받는다.
    template <typename CharT, typename Traits, typename Allocator>
    bool operator==( const CharT* pLhs, const basic_string<CharT, Traits, Allocator>& rhs ) noexcept
    {
        return rhs == pLhs;
    }
    template <typename CharT, typename Traits, typename Allocator>
    bool operator!=( const CharT* pLhs, const basic_string<CharT, Traits, Allocator>& rhs ) noexcept
    {
        return rhs != pLhs;
    }
    template <typename CharT, typename Traits, typename Allocator>
    bool operator<( const CharT* pLhs, const basic_string<CharT, Traits, Allocator>& rhs ) noexcept
    {
        return rhs > pLhs;
    }
    template <typename CharT, typename Traits, typename Allocator>
    bool operator<=( const CharT* pLhs, const basic_string<CharT, Traits, Allocator>& rhs ) noexcept
    {
        return rhs >= pLhs;
    }
    template <typename CharT, typename Traits, typename Allocator>
    bool operator>( const CharT* pLhs, const basic_string<CharT, Traits, Allocator>& rhs ) noexcept
    {
        return rhs < pLhs;
    }
    template <typename CharT, typename Traits, typename Allocator>
    bool operator>=( const CharT* pLhs, const basic_string<CharT, Traits, Allocator>& rhs ) noexcept
    {
        return rhs <= pLhs;
    }

    // 이어 붙이기(`+`)는 두지 않는다 — 식마다 임시 문자열을 할당했다 버린다(사용자 지적 2026-11-20). 한 번 쓰는 글은 `fixed_string` + `formatstring`, 남기는 글은
    // `StringBuilder::appendFormat` 으로 스택에서 짓고 한 번만 할당한다(docs/agents/CppStyle.md Strings). `+=` 로 한 조각씩 덧붙이는 것은 임시를 만들지 않아 남긴다.
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
     *          하나씩 있는 FNV-1a 라서, 35자짜리 경로 키 하나에 곱셈 35번이 줄지어 이어집니다.
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
