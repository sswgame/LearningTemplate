/**
 * @file fixed_string.h
 * @brief 고정 용량 문자열(basic_fixed_string)입니다. 힙을 쓰지 않습니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/string.h"
#include "Core/Log/Logger.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /**
     * @struct FixedStringWarningGuard
     * @brief 이 스레드가 지금 `basic_fixed_string` 의 잘림 경고를 남기는 중인지 지키는 가드입니다.
     * @details 잘림 경고는 로거를 부르고, 로그 싱크가 다시 `basic_fixed_string` 에 넘치게 쓰면 경고가 경고를 부른다(끝없는 재귀).
     *          가드가 서 있는 동안의 잘림은 경고 없이 자르기만 합니다. 용량이 다른 인스턴스끼리도 같은 플래그를 봐야 하므로
     *          템플릿 밖에 둡니다.
     */
    struct FixedStringWarningGuard
    {
        FixedStringWarningGuard()
            : _bEntered{ _s_bWarning == false }
        {
            if ( _bEntered )
                _s_bWarning = true;
        }

        ~FixedStringWarningGuard()
        {
            if ( _bEntered )
                _s_bWarning = false;
        }

        FixedStringWarningGuard( const FixedStringWarningGuard& )            = delete;
        FixedStringWarningGuard& operator=( const FixedStringWarningGuard& ) = delete;

        explicit operator bool() const { return _bEntered; }

        bool _bEntered;

    private:
        /** @brief 이 스레드가 지금 잘림 경고 안인지입니다. */
        static inline thread_local bool _s_bWarning{ false };
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) basic_fixed_string — 최대 N 문자. 넘치면 잘라 내고 경고, 힙 할당 없음
    // ------------------------------------------------------------------------------
    /**
     * @class basic_fixed_string
     * @brief 힙 할당 없이 최대 N 개의 문자를 내부 고정 배열(`_arrData[N + 1]`)에 저장하는 널 종료 문자열입니다.
     * @tparam T 문자 타입(`utf8` 또는 `utf16`)
     * @tparam N 담을 수 있는 최대 문자 수(널 종료 문자 `\0` 자리 하나는 안에서 따로 잡습니다)
     * @details
     * - **길이 필드가 없습니다**:
     *   객체는 배열 하나뿐이라 `sizeof` 는 `(N + 1) * sizeof( T )` 입니다. `size()` 는 앞 N 칸에서 첫 널을 찾는 경계 있는
     *   검색이고(O(길이)), const 호출은 아무것도 쓰지 않아 여러 스레드가 동시에 읽어도 됩니다.
     * - **`data()` 로 바로 써도 됩니다**:
     *   바깥(ImGui 입력칸 · `formatstring` · Win32 API)은 `data()` 에 `max_size()` 자까지 널 종료 여부와 상관없이 쓸 수 있고, 그 뒤
     *   `size()` 가 맞습니다. 이를 위해 모든 생성자가 배열 전체를 0 으로 채우고, 길이를 정하는 연산(대입 · `clear` · `erase` ·
     *   `pop_back`)은 새 끝 뒤의 꼬리를 0 으로 채웁니다. 끝 칸(`_arrData[N]`)은 늘 널이라 `c_str()` 은 언제나 종료됩니다.
     * - **용량을 넘으면 자릅니다**:
     *   `N` 을 넘는 입력은 `N` 까지만 담고 경고를 남깁니다. 넘는 부분은 **버려지고**, 버퍼 밖은 절대 건드리지 않습니다.
     *   잘림이 문제인 곳은 `try_assign` · `try_append`(넘치면 바꾸지 않고 false, 로그 없음)를 씁니다.
     *   길이를 잃으면 안 되는 문자열에는 `sw::string` 을 쓰십시오.
     * - **할당 없음과 캐시 지역성**:
     *   문자열 데이터를 객체 안에 바로 들고 있어 힙 단편화가 생기지 않고 캐시 적중률이 높습니다.
     * - **std::basic_string_view 호환**:
     *   `view()` 와 `operator std::basic_string_view<T>()` 로 복사 없이 표준 문자열 뷰로 넘길 수 있습니다.
     * - **std::hash 지원**:
     *   `sw::string` 과 같은 `RuntimeStringHash` 기반의 `std::hash` 특수화가 있어(프로세스 안 전용 — 파일 · 네트워크에 남기지 않는다) `std::unordered_map` 이나 `std::unordered_set` 의 키로
     *   바로 쓸 수 있습니다.
     * - **같음 · 사전순 비교(`equals`, `compare`)**:
     *   `StringUtil::equals` 와 `StringUtil::compare` 를 그대로 써서 식별자와 파일 경로를 빠르게 비교합니다.
     */
    template <typename T, uint32 N>
    class basic_fixed_string
    {
        static_assert( N > 0, "basic_fixed_string must have a positive capacity" );
        static_assert( std::is_same_v<T, utf8> || std::is_same_v<T, utf16>, "basic_fixed_string only supports utf8 or utf16" );

    public:
        /** @brief 검색 실패 등을 나타내는 무효 인덱스(-1)입니다. */
        static constexpr uint32 npos = invalid_index::kUint32;

        using value_type      = T;
        using size_type       = uint32;
        using reference       = T&;
        using const_reference = const T&;
        using pointer         = T*;
        using const_pointer   = const T*;
        using iterator        = T*;
        using const_iterator  = const T*;

        // ------------------------------------------------------------------------------
        // 2) 생성 · 대입
        // ------------------------------------------------------------------------------
        /** @brief 빈 문자열로 초기화합니다(배열 전체가 0). */
        constexpr basic_fixed_string() noexcept
            : _arrData{} {}

        /** @brief 소멸자입니다. */
        ~basic_fixed_string() = default;

        /** @brief 널 종료 C 문자열을 복사해 만듭니다. */
        basic_fixed_string( const T* pStr );

        /** @brief std::basic_string 의 내용을 복사해 만듭니다. */
        basic_fixed_string( const std::basic_string<T>& str );

        /** @brief std::basic_string_view 의 내용을 복사해 만듭니다. */
        basic_fixed_string( const std::basic_string_view<T>& str );

        /** @brief 문자 ch 를 count 개 채워 만듭니다. */
        basic_fixed_string( uint32 count, T ch );

        /** @brief 복사 생성자입니다. 배열 전체(0 으로 채운 꼬리 포함)를 그대로 복사합니다. */
        basic_fixed_string( const basic_fixed_string& rhs ) = default;

        /** @brief 용량이 다른 고정 문자열을 복사해 만듭니다. */
        template <uint32 M>
        basic_fixed_string( const basic_fixed_string<T, M>& rhs )
            : _arrData{}
        {
            const uint32 length = clampToCapacity( rhs.c_str(), rhs.size() );
            Memory::copy( _arrData, rhs.c_str(), sizeof( T ) * length );
        }

        /** @brief 이동 생성자입니다. */
        basic_fixed_string( basic_fixed_string&& rhs ) noexcept = default;

        /** @brief 다른 고정 문자열을 복사 대입합니다. 배열 전체를 그대로 복사합니다. */
        basic_fixed_string& operator=( const basic_fixed_string& rhs ) = default;

        /** @brief 용량이 다른 고정 문자열을 복사 대입합니다. */
        template <uint32 M>
        basic_fixed_string& operator=( const basic_fixed_string<T, M>& rhs )
        {
            const uint32 length = clampToCapacity( rhs.c_str(), rhs.size() );
            Memory::copy( _arrData, rhs.c_str(), sizeof( T ) * length );
            fillZeroFrom( length );
            return *this;
        }

        /** @brief 널 종료 C 문자열을 복사 대입합니다. */
        basic_fixed_string& operator=( const T* pStr );

        /** @brief std::basic_string 을 복사 대입합니다. */
        basic_fixed_string& operator=( const std::basic_string<T>& str );

        /** @brief std::basic_string_view 를 복사 대입합니다. */
        basic_fixed_string& operator=( const std::basic_string_view<T>& str );

        /** @brief 이동 대입 연산자입니다. */
        basic_fixed_string& operator=( basic_fixed_string&& rhs ) noexcept = default;

        // ------------------------------------------------------------------------------
        // 3) 원소 접근 · 이터레이터
        // ------------------------------------------------------------------------------
        /** @brief pos 위치의 문자를 참조합니다(디버그 빌드에서 범위 검사). */
        reference       operator[]( uint32 pos );
        const_reference operator[]( uint32 pos ) const;

        /** @brief pos 위치의 문자를 반환합니다. */
        reference       at( uint32 pos );
        const_reference at( uint32 pos ) const;

        /** @brief 첫 문자를 반환합니다. */
        reference       front();
        const_reference front() const;

        /** @brief 마지막 문자를 반환합니다. */
        reference       back();
        const_reference back() const;

        /** @brief 내부 버퍼 포인터를 반환합니다. */
        pointer       data() noexcept { return _arrData; }
        const_pointer data() const noexcept { return _arrData; }

        /** @brief 널 종료 C 문자열 포인터를 반환합니다. */
        const_pointer c_str() const noexcept { return _arrData; }

        /** @brief 힙 할당 없는 string_view 를 반환합니다. */
        std::basic_string_view<T> view() const noexcept { return { _arrData, size() }; }

        /** @brief 첫 문자를 가리키는 이터레이터입니다. */
        iterator       begin() noexcept { return _arrData; }
        const_iterator begin() const noexcept { return _arrData; }
        const_iterator cbegin() const noexcept { return _arrData; }

        /** @brief 끝(마지막 문자 다음)을 가리키는 이터레이터입니다. */
        iterator       end() noexcept { return _arrData + size(); }
        const_iterator end() const noexcept { return _arrData + size(); }
        const_iterator cend() const noexcept { return _arrData + size(); }

        // ------------------------------------------------------------------------------
        // 4) 용량 · 상태 조회
        // ------------------------------------------------------------------------------
        /** @brief 비어 있는지 반환합니다(O(1)). */
        bool empty() const noexcept { return _arrData[0] == T{ 0 }; }

        /**
         * @brief 현재 문자 수(널 제외)를 반환합니다.
         * @details 앞 N 칸에서 첫 널을 찾습니다(O(길이)). `data()` 로 N 자를 널 없이 채웠어도 N 을 넘지 않고, 아무것도 쓰지 않습니다.
         */
        uint32 size() const noexcept
        {
            const T* pTerminator = std::char_traits<T>::find( _arrData, N, T{ 0 } );
            return ( pTerminator != nullptr ) ? static_cast<uint32>( pTerminator - _arrData ) : N;
        }
        uint32 length() const noexcept { return size(); }

        /** @brief 담을 수 있는 최대 문자 수(N)를 반환합니다. */
        static constexpr uint32 max_size() noexcept { return N; }
        static constexpr uint32 capacity() noexcept { return N; }

        /** @brief 빈 문자열로 되돌립니다. 배열 전체를 0 으로 채웁니다(O(N)). */
        void clear() noexcept;

        // ------------------------------------------------------------------------------
        // 5) 문자열 조작(insert / erase / append / substr)
        // ------------------------------------------------------------------------------
        /** @brief pos 위치 앞에 C 문자열을 삽입합니다. */
        basic_fixed_string& insert( uint32 pos, const T* pStr );
        basic_fixed_string& insert( const uint32 pos, const basic_fixed_string& str ) { return insert( pos, str.c_str() ); }
        template <uint32 M>
        basic_fixed_string& insert( const uint32 pos, const basic_fixed_string<T, M>& str ) { return insert( pos, str.c_str() ); }

        /** @brief pos 부터 length 개의 문자를 지웁니다. */
        basic_fixed_string& erase( uint32 pos = 0, uint32 length = npos );

        /** @brief 끝에 문자 하나를 붙입니다. */
        void push_back( T ch );

        /** @brief 마지막 문자를 제거합니다. */
        void pop_back();

        /** @brief 문자열을 새로 대입합니다. */
        basic_fixed_string& assign( const T* pStr ) { return *this = pStr; }
        basic_fixed_string& assign( const basic_fixed_string& str ) { return *this = str; }
        template <uint32 M>
        basic_fixed_string& assign( const basic_fixed_string<T, M>& str ) { return *this = str; }
        basic_fixed_string& assign( const std::basic_string<T>& str ) { return *this = str; }
        basic_fixed_string& assign( const std::basic_string_view<T>& str ) { return *this = str; }

        /** @brief 끝에 C 문자열을 붙입니다. */
        basic_fixed_string& append( const T* pStr );
        basic_fixed_string& append( const basic_fixed_string& str ) { return append( str.c_str() ); }
        template <uint32 M>
        basic_fixed_string& append( const basic_fixed_string<T, M>& str ) { return append( str.c_str() ); }
        basic_fixed_string& append( uint32 count, T c );
        basic_fixed_string& append( const std::basic_string_view<T>& str );
        basic_fixed_string& append( const std::basic_string<T>& str ) { return append( std::basic_string_view<T>( str ) ); }

        /**
         * @brief 용량 안에 들어가면 대입하고 true 를 반환합니다. 넘치면 바꾸지 않고 false 를 반환하며 경고를 남기지 않습니다.
         * @details 잘린 글이 틀린 값이 되는 곳(경로 · 식별자)에서 씁니다. `nullptr` 은 빈 글입니다. 자기 버퍼 안쪽도 맞게 옮깁니다.
         */
        [[nodiscard]] bool try_assign( const T* pStr ) noexcept;
        [[nodiscard]] bool try_assign( const std::basic_string_view<T>& str ) noexcept;

        /** @brief 남은 자리에 들어가면 끝에 붙이고 true 를 반환합니다. 넘치면 바꾸지 않고 false 를 반환하며 경고를 남기지 않습니다. */
        [[nodiscard]] bool try_append( const T* pStr ) noexcept;
        [[nodiscard]] bool try_append( const std::basic_string_view<T>& str ) noexcept;

        /** @brief pos 부터 C 문자열이 처음 나오는 위치를 찾습니다. */
        uint32 find( const T* pStr, uint32 pos = 0 ) const;
        uint32 find( T c, uint32 pos = 0 ) const;
        uint32 find( const basic_fixed_string& str, uint32 pos = 0 ) const { return find( str.c_str(), pos ); }
        template <uint32 M>
        uint32 find( const basic_fixed_string<T, M>& str, uint32 pos = 0 ) const { return find( str.c_str(), pos ); }

        /** @brief pos 부터 length 길이의 부분 문자열을 반환합니다. */
        basic_fixed_string substr( uint32 pos = 0, uint32 length = npos ) const;

        // ------------------------------------------------------------------------------
        // 6) 비교 · 연산자
        // ------------------------------------------------------------------------------
        /** @brief 사전순으로 비교합니다(같으면 0). */
        int32 compare( const basic_fixed_string& other, bool bIgnoreCase = false ) const { return StringUtil::compare( _arrData, other._arrData, bIgnoreCase ); }
        template <uint32 M>
        int32 compare( const basic_fixed_string<T, M>& other, bool bIgnoreCase = false ) const { return StringUtil::compare( _arrData, other.c_str(), bIgnoreCase ); }
        int32 compare( const T* pStr, bool bIgnoreCase = false ) const { return ( pStr != nullptr ) ? StringUtil::compare( _arrData, pStr, bIgnoreCase ) : 1; }

        /** @brief 같은지 비교합니다(대소문자 무시 옵션). */
        bool equals( const basic_fixed_string& other, bool bIgnoreCase = false ) const noexcept { return StringUtil::equals( _arrData, other._arrData, bIgnoreCase ); }
        template <uint32 M>
        bool equals( const basic_fixed_string<T, M>& other, bool bIgnoreCase = false ) const noexcept { return StringUtil::equals( _arrData, other.c_str(), bIgnoreCase ); }
        bool equals( const T* pStr, bool bIgnoreCase = false ) const noexcept { return pStr != nullptr && StringUtil::equals( _arrData, pStr, bIgnoreCase ); }

        basic_fixed_string& operator+=( const basic_fixed_string& other ) { return append( other ); }
        template <uint32 M>
        basic_fixed_string& operator+=( const basic_fixed_string<T, M>& other ) { return append( other ); }
        basic_fixed_string& operator+=( const T* pStr ) { return append( pStr ); }
        basic_fixed_string& operator+=( T ch )
        {
            push_back( ch );
            return *this;
        }

        bool operator==( const basic_fixed_string& other ) const { return compare( other ) == 0; }
        bool operator!=( const basic_fixed_string& other ) const { return compare( other ) != 0; }
        bool operator<( const basic_fixed_string& other ) const { return compare( other ) < 0; }
        bool operator<=( const basic_fixed_string& other ) const { return compare( other ) <= 0; }
        bool operator>( const basic_fixed_string& other ) const { return compare( other ) > 0; }
        bool operator>=( const basic_fixed_string& other ) const { return compare( other ) >= 0; }

        template <uint32 M>
        bool operator==( const basic_fixed_string<T, M>& other ) const { return compare( other ) == 0; }
        template <uint32 M>
        bool operator!=( const basic_fixed_string<T, M>& other ) const { return compare( other ) != 0; }
        template <uint32 M>
        bool operator<( const basic_fixed_string<T, M>& other ) const { return compare( other ) < 0; }
        template <uint32 M>
        bool operator<=( const basic_fixed_string<T, M>& other ) const { return compare( other ) <= 0; }
        template <uint32 M>
        bool operator>( const basic_fixed_string<T, M>& other ) const { return compare( other ) > 0; }
        template <uint32 M>
        bool operator>=( const basic_fixed_string<T, M>& other ) const { return compare( other ) >= 0; }

        bool operator==( const T* pStr ) const { return compare( pStr ) == 0; }
        bool operator!=( const T* pStr ) const { return compare( pStr ) != 0; }

        /** @brief std::basic_string 으로 변환합니다(동적 할당이 생깁니다). */
        operator std::basic_string<T>() const { return std::basic_string<T>{ _arrData, size() }; }

        /** @brief 힙 할당 없는 std::basic_string_view 로 암시적 변환합니다. */
        operator std::basic_string_view<T>() const noexcept { return { _arrData, size() }; }

    private:
        /**
         * @brief 용량 N 을 넘는 길이를 N 으로 잘라 반환합니다.
         * @details 단언은 실행을 멈추지 않으므로(Debug 는 브레이크, 그 밖은 로그만) 알리기만 하고 원래 길이로 복사하면 `_arrData` 뒤를
         *          덮어쓰는 버퍼 오버플로입니다. 넘치는 길이는 **데이터에서 옵니다**(긴 대사 · 긴 경로). 프로그래밍 계약 위반이 아니므로
         *          단언으로 멈추지 않고, 잘라 낸 뒤 경고를 남깁니다. 경고는 Shipping 에도 남습니다. 경고 안에서 다시 넘치면(로그 싱크가
         *          fixed_string 에 쓰는 경우) 경고 없이 자릅니다(`FixedStringWarningGuard`).
         */
        static uint32 clampToCapacity( const T* pSource, size_t length )
        {
            if ( length <= static_cast<size_t>( N ) )
                return static_cast<uint32>( length );

            const FixedStringWarningGuard guard;
            if ( guard )
                SW_LOG_WARNING( "basic_fixed_string capacity %# exceeded by length %# - truncated", N, static_cast<uint32>( length ) );
            return backOffToCharacterStart( pSource, N );
        }

        /** @brief 남은 자리(N - currentSize)에 맞게 추가할 길이를 잘라 반환합니다. `pSource` 는 글자 경계를 보려고 받습니다(채우기는 nullptr). */
        static uint32 clampToRemaining( uint32 currentSize, const T* pSource, size_t length )
        {
            const size_t remaining = static_cast<size_t>( N ) - static_cast<size_t>( currentSize );
            if ( length <= remaining )
                return static_cast<uint32>( length );

            const FixedStringWarningGuard guard;
            if ( guard )
            {
                SW_LOG_WARNING( "basic_fixed_string capacity %# exceeded - %# of %# characters truncated", N, static_cast<uint32>( length - remaining ),
                                static_cast<uint32>( length ) );
            }
            return backOffToCharacterStart( pSource, static_cast<uint32>( remaining ) );
        }

        /**
         * @brief 자를 자리 `cut` 이 글자 한가운데면(UTF-8 의 이어지는 바이트 · UTF-16 의 뒤 서로게이트) 그 글자의 시작으로 물립니다.
         * @details 바이트 수로만 자르면 긴 한글 이름의 끝 글자가 반 토막(잘못된 UTF-8)으로 남는다 — ImGui 는 그 자리를 `?` 로 그리고
         *          로그는 줄을 통째로 바꾼다. `pSource[cut]` 은 잘려 나가는 첫 단위다. 잘못된 UTF-8 을 끝없이 거슬러
         *          가지 않도록 UTF-8 은 세 바이트까지만 물린다.
         */
        static uint32 backOffToCharacterStart( const T* pSource, uint32 cut )
        {
            if ( pSource == nullptr )
                return cut;
            if constexpr ( sizeof( T ) == 1 )
            {
                for ( uint32 step = 0; step < 3 && cut > 0 && ( static_cast<uint8>( pSource[cut] ) & 0xC0 ) == 0x80; ++step )
                {
                    --cut;
                }
            }
            else if constexpr ( sizeof( T ) == 2 )
            {
                const uint32 unit = static_cast<uint32>( static_cast<uint16>( pSource[cut] ) );
                if ( cut > 0 && unit >= 0xDC00 && unit <= 0xDFFF )
                    --cut;
            }
            return cut;
        }

        /**
         * @brief `length` 칸부터 끝 칸(`_arrData[N]`)까지를 0 으로 채웁니다. 길이를 정하는 연산이 끝에 부릅니다.
         * @details 꼬리가 0 이어야 바깥이 `data()` 에 널 없이 짧게 써도 `size()` 가 맞습니다(클래스 주석의 계약). `Memory::set` 이 아니라
         *          `std::fill_n` 인 것은 컴파일러가 그 자리에 펼치게 하려는 것입니다 — `Memory::set` 은 Engine.dll 을 건너는 호출이라 짧은 꼬리에서
         *          대입 한 번이 눈에 띄게 느려집니다.
         */
        void fillZeroFrom( uint32 length ) noexcept { std::fill_n( _arrData + length, N + 1 - length, T{ 0 } ); }

        /**
         * @brief 길이를 알고 있는 글을 대입합니다(`length` <= N). 자기 버퍼 안쪽일 수 있어 겹쳐도 맞는 `move`(memmove)로 옮깁니다.
         * @details 배열이 작으면(`kWholeFillBytes` 이하) 바깥 글은 배열 전체를 먼저 0 으로 채우고 복사합니다. 크기가 컴파일 때 정해진
         *          채우기는 그 자리에 펼쳐지지만, 길이가 실행 때 정해지는 꼬리 채우기는 memset 호출이 하나 더 듭니다.
         */
        void assignKnownLength( const T* pSource, uint32 length ) noexcept
        {
            const bool bSourceInside = ( _arrData <= pSource ) && ( pSource < _arrData + N + 1 );
            if constexpr ( sizeof( _arrData ) <= kWholeFillBytes )
            {
                if ( bSourceInside == false )
                {
                    std::fill_n( _arrData, N + 1, T{ 0 } );
                    std::char_traits<T>::copy( _arrData, pSource, length );
                    return;
                }
            }
            if ( pSource != _arrData )
                std::char_traits<T>::move( _arrData, pSource, length );
            fillZeroFrom( length );
        }

        /** @brief 대입이 배열 전체를 먼저 0 으로 채우는 크기 상한(바이트)입니다. */
        static constexpr size_t kWholeFillBytes = 256;

        T _arrData[N + 1];
    };

    template <uint32 N>
    using fixed_string = basic_fixed_string<utf8, N>;

    template <uint32 N>
    using fixed_wstring = basic_fixed_string<utf16, N>;

#pragma region IMPLEMENTATION

    template <typename T, uint32 N>
    basic_fixed_string<T, N>::basic_fixed_string( const T* pStr )
        : _arrData{}
    {
        if ( pStr != nullptr )
        {
            const uint32 length = clampToCapacity( pStr, StringUtil::strlen( pStr ) );
            Memory::copy( _arrData, pStr, sizeof( T ) * length );
        }
    }

    template <typename T, uint32 N>
    basic_fixed_string<T, N>::basic_fixed_string( const std::basic_string<T>& str )
        : _arrData{}
    {
        const uint32 length = clampToCapacity( str.data(), str.length() );
        Memory::copy( _arrData, str.data(), sizeof( T ) * length );
    }

    template <typename T, uint32 N>
    basic_fixed_string<T, N>::basic_fixed_string( const std::basic_string_view<T>& str )
        : _arrData{}
    {
        const uint32 length = clampToCapacity( str.data(), str.length() );
        Memory::copy( _arrData, str.data(), sizeof( T ) * length );
    }

    template <typename T, uint32 N>
    basic_fixed_string<T, N>::basic_fixed_string( const uint32 count, T ch )
        : _arrData{}
    {
        std::fill_n( _arrData, clampToCapacity( nullptr, count ), ch );
    }

    // 자기 대입은 `fs = fs.c_str()` 처럼 들어온다. 시작 주소가 같으면 옮길 것이 없고, 자기 버퍼 **안쪽**(`s = s.c_str() + 2`)은 겹치므로
    // `assignKnownLength` 가 memmove 로 옮긴다. 검사기는 copy-and-swap 이 아니라고 짚지만, 고정 버퍼에는 교환할 동적 자원이 없다.
    // NOLINTNEXTLINE(bugprone-unhandled-self-assignment)
    template <typename T, uint32 N>
    basic_fixed_string<T, N>& basic_fixed_string<T, N>::operator=( const T* pStr )
    {
        if ( pStr == nullptr )
        {
            clear();
            return *this;
        }
        assignKnownLength( pStr, clampToCapacity( pStr, StringUtil::strlen( pStr ) ) );
        return *this;
    }

    template <typename T, uint32 N>
    basic_fixed_string<T, N>& basic_fixed_string<T, N>::operator=( const std::basic_string<T>& str )
    {
        assignKnownLength( str.data(), clampToCapacity( str.data(), str.length() ) );
        return *this;
    }

    template <typename T, uint32 N>
    basic_fixed_string<T, N>& basic_fixed_string<T, N>::operator=( const std::basic_string_view<T>& str )
    {
        // 뷰가 자기 버퍼를 볼 수 있다(`s = s.view().substr( 1 )`).
        assignKnownLength( str.data(), clampToCapacity( str.data(), str.length() ) );
        return *this;
    }

    template <typename T, uint32 N>
    typename basic_fixed_string<T, N>::reference basic_fixed_string<T, N>::operator[]( uint32 pos )
    {
        SW_LOG_ASSERT( pos < N, "basic_fixed_string::operator[] - position out of range" );
        return _arrData[pos];
    }

    template <typename T, uint32 N>
    typename basic_fixed_string<T, N>::const_reference basic_fixed_string<T, N>::operator[]( uint32 pos ) const
    {
        SW_LOG_ASSERT( pos < N, "basic_fixed_string::operator[] - position out of range" );
        return _arrData[pos];
    }

    template <typename T, uint32 N>
    typename basic_fixed_string<T, N>::reference basic_fixed_string<T, N>::at( uint32 pos )
    {
        SW_LOG_ASSERT( pos < size(), "basic_fixed_string::at - position out of range" );
        return _arrData[pos];
    }

    template <typename T, uint32 N>
    typename basic_fixed_string<T, N>::const_reference basic_fixed_string<T, N>::at( uint32 pos ) const
    {
        SW_LOG_ASSERT( pos < size(), "basic_fixed_string::at - position out of range" );
        return _arrData[pos];
    }

    template <typename T, uint32 N>
    typename basic_fixed_string<T, N>::reference basic_fixed_string<T, N>::front()
    {
        SW_LOG_ASSERT( empty() == false, "basic_fixed_string::front on empty string" );
        return _arrData[0];
    }

    template <typename T, uint32 N>
    typename basic_fixed_string<T, N>::const_reference basic_fixed_string<T, N>::front() const
    {
        SW_LOG_ASSERT( empty() == false, "basic_fixed_string::front on empty string" );
        return _arrData[0];
    }

    template <typename T, uint32 N>
    typename basic_fixed_string<T, N>::reference basic_fixed_string<T, N>::back()
    {
        SW_LOG_ASSERT( empty() == false, "basic_fixed_string::back on empty string" );
        return _arrData[size() - 1];
    }

    template <typename T, uint32 N>
    typename basic_fixed_string<T, N>::const_reference basic_fixed_string<T, N>::back() const
    {
        SW_LOG_ASSERT( empty() == false, "basic_fixed_string::back on empty string" );
        return _arrData[size() - 1];
    }

    template <typename T, uint32 N>
    void basic_fixed_string<T, N>::clear() noexcept
    {
        fillZeroFrom( 0 );
    }

    template <typename T, uint32 N>
    basic_fixed_string<T, N>& basic_fixed_string<T, N>::insert( uint32 pos, const T* pStr )
    {
        if ( pStr == nullptr )
            return *this;

        const uint32 length = StringUtil::strlen( pStr );
        if ( length == 0 )
            return *this;

        const uint32 currentSize = size();
        SW_LOG_ASSERT( pos <= currentSize, "Insert position out of range" );
        if ( pos > currentSize )
            return *this;

        const uint32 fitLength = clampToRemaining( currentSize, pStr, length );
        if ( fitLength == 0 )
            return *this;

        // 넣을 글자가 이 버퍼 안에 있으면(`s.insert( 1, s.c_str() )`) 아래에서 뒤를 미는 순간 원본이 바뀐다. 먼저 떠 둔다.
        T          arrSource[N + 1];
        const bool bSourceInside = ( _arrData <= pStr ) && ( pStr < _arrData + N + 1 );
        if ( bSourceInside )
        {
            Memory::copy( arrSource, pStr, sizeof( T ) * fitLength );
            pStr = arrSource;
        }

        // 종료 문자까지 민다(`currentSize + fitLength` <= N 이라 끝 칸 안이다).
        Memory::move( _arrData + pos + fitLength, _arrData + pos, sizeof( T ) * ( currentSize - pos + 1 ) );
        Memory::copy( _arrData + pos, pStr, sizeof( T ) * fitLength );

        return *this;
    }

    template <typename T, uint32 N>
    basic_fixed_string<T, N>& basic_fixed_string<T, N>::erase( uint32 pos, uint32 length )
    {
        const uint32 currentSize = size();
        SW_LOG_ASSERT( pos <= currentSize, "basic_fixed_string::erase position out of range" );

        if ( pos >= currentSize )
            return *this;

        // **뺄셈으로 비교한다.** `pos + length` 는 `uint32` 범위에서 오버플로할 수 있다. `npos` 가 아닌 큰 길이가 들어오면
        // (끝과 시작을 거꾸로 뺀 계산 등) 합이 작은 수로 돌아와 아래 `else` 로 빠지고, 거기서 `_arrData + pos + length` 라는
        // 엉뚱한 주소를 읽는다. `pos < currentSize` 는 위에서 걸렀으므로 이 뺄셈은 안전하다(형제 함수인 `substr` 도 같은 형태다).
        if ( length == npos || length >= currentSize - pos )
        {
            fillZeroFrom( pos );
        }
        else
        {
            Memory::move( _arrData + pos, _arrData + pos + length, sizeof( T ) * ( currentSize - pos - length ) );
            fillZeroFrom( currentSize - length );
        }

        return *this;
    }

    template <typename T, uint32 N>
    void basic_fixed_string<T, N>::push_back( T ch )
    {
        const uint32 currentSize = size();
        if ( currentSize >= N )
        {
            const FixedStringWarningGuard guard;
            if ( guard )
                SW_LOG_WARNING( "basic_fixed_string capacity %# exceeded - push_back dropped", N );
            return;
        }

        _arrData[currentSize]     = ch;
        _arrData[currentSize + 1] = T{ 0 };
    }

    template <typename T, uint32 N>
    void basic_fixed_string<T, N>::pop_back()
    {
        const uint32 currentSize = size();
        if ( currentSize == 0 )
            return;
        fillZeroFrom( currentSize - 1 );
    }

    template <typename T, uint32 N>
    basic_fixed_string<T, N>& basic_fixed_string<T, N>::append( const T* pStr )
    {
        if ( pStr != nullptr )
            append( std::basic_string_view<T>( pStr ) );
        return *this;
    }

    template <typename T, uint32 N>
    basic_fixed_string<T, N>& basic_fixed_string<T, N>::append( uint32 count, T c )
    {
        if ( count == 0 )
            return *this;
        const uint32 currentSize = size();
        const uint32 fitCount    = clampToRemaining( currentSize, nullptr, count );
        std::fill_n( _arrData + currentSize, fitCount, c );
        _arrData[currentSize + fitCount] = T{ 0 };
        return *this;
    }

    template <typename T, uint32 N>
    basic_fixed_string<T, N>& basic_fixed_string<T, N>::append( const std::basic_string_view<T>& str )
    {
        // 붙일 글이 자기 버퍼면(`s.append( s.view() )`) 원본 [0, size) 와 대상 [size, size + length) 는 겹치지 않는다.
        const uint32 currentSize = size();
        const uint32 length      = clampToRemaining( currentSize, str.data(), str.length() );
        Memory::copy( _arrData + currentSize, str.data(), sizeof( T ) * length );
        _arrData[currentSize + length] = T{ 0 };
        return *this;
    }

    template <typename T, uint32 N>
    bool basic_fixed_string<T, N>::try_assign( const T* pStr ) noexcept
    {
        if ( pStr == nullptr )
        {
            clear();
            return true;
        }
        return try_assign( std::basic_string_view<T>( pStr ) );
    }

    template <typename T, uint32 N>
    bool basic_fixed_string<T, N>::try_assign( const std::basic_string_view<T>& str ) noexcept
    {
        if ( str.length() > static_cast<size_t>( N ) )
            return false;
        assignKnownLength( str.data(), static_cast<uint32>( str.length() ) );
        return true;
    }

    template <typename T, uint32 N>
    bool basic_fixed_string<T, N>::try_append( const T* pStr ) noexcept
    {
        if ( pStr == nullptr )
            return true;
        return try_append( std::basic_string_view<T>( pStr ) );
    }

    template <typename T, uint32 N>
    bool basic_fixed_string<T, N>::try_append( const std::basic_string_view<T>& str ) noexcept
    {
        const uint32 currentSize = size();
        if ( str.length() > static_cast<size_t>( N - currentSize ) )
            return false;
        Memory::copy( _arrData + currentSize, str.data(), sizeof( T ) * str.length() );
        _arrData[currentSize + str.length()] = T{ 0 };
        return true;
    }

    template <typename T, uint32 N>
    uint32 basic_fixed_string<T, N>::find( const T* pStr, uint32 pos ) const
    {
        const uint32 currentSize = size();
        if ( pStr == nullptr || currentSize <= pos )
            return npos;
        const T* pResult = StringUtil::strstr( _arrData + pos, pStr );
        return pResult != nullptr ? static_cast<uint32>( pResult - _arrData ) : npos;
    }

    template <typename T, uint32 N>
    uint32 basic_fixed_string<T, N>::find( T c, uint32 pos ) const
    {
        const uint32 currentSize = size();
        if ( currentSize <= pos )
            return npos;
        const T* pResult = StringUtil::strchr( _arrData + pos, c );
        return pResult != nullptr ? static_cast<uint32>( pResult - _arrData ) : npos;
    }

    template <typename T, uint32 N>
    basic_fixed_string<T, N> basic_fixed_string<T, N>::substr( uint32 pos, uint32 length ) const
    {
        const uint32 currentSize = size();
        SW_LOG_ASSERT( pos <= currentSize, "basic_fixed_string::substr out of range" );

        if ( pos >= currentSize )
            return basic_fixed_string{};

        const uint32       actualLength = MathUtil::min( length, currentSize - pos );
        basic_fixed_string result{};
        Memory::copy( result._arrData, _arrData + pos, sizeof( T ) * actualLength );
        return result;
    }

    template <typename T, uint32 N, uint32 M>
    basic_fixed_string<T, N + M> operator+( const basic_fixed_string<T, N>& lhs, const basic_fixed_string<T, M>& rhs )
    {
        basic_fixed_string<T, N + M> result{ lhs };
        result += rhs;
        return result;
    }

    template <typename T, uint32 N>
    basic_fixed_string<T, N> operator+( const basic_fixed_string<T, N>& lhs, const T* rhs )
    {
        basic_fixed_string<T, N> result = lhs;
        result += rhs;
        return result;
    }

    template <typename T, uint32 N>
    basic_fixed_string<T, N> operator+( const T* lhs, const basic_fixed_string<T, N>& rhs )
    {
        basic_fixed_string<T, N> result{ lhs };
        result += rhs;
        return result;
    }

    template <typename T, uint32 N>
    std::ostream& operator<<( std::ostream& os, const basic_fixed_string<T, N>& str ) { return os << str.c_str(); }

    template <typename T, uint32 N>
    std::istream& operator>>( std::istream& is, basic_fixed_string<T, N>& str )
    {
        std::basic_string<T> temp;
        is >> temp;
        str = temp;
        return is;
    }

#pragma endregion

} // namespace sw

namespace std
{
    template <typename T, uint32 N>
    /** @brief basic_fixed_string 을 std::unordered_map · set 키로 쓰는 해시입니다. `sw::string` 과 같은 `RuntimeStringHash`(문자 바이트, 대소문자 구분)라 같은 내용이면 같은 값입니다. */
    struct hash<sw::basic_fixed_string<T, N>>
    {
        size_t operator()( const sw::basic_fixed_string<T, N>& key ) const noexcept
        {
            return static_cast<size_t>( sw::RuntimeStringHash::compute( key.c_str(), static_cast<size_t>( key.size() ) * sizeof( T ) ) );
        }
    };
} // namespace std
