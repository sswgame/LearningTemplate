/**
 * @file span.h
 * @brief 연속 구간을 소유하지 않고 가리키는 뷰입니다(C++20 `std::span` 과 같은 API, 동적 길이만). 함수 인자로 받을 때 씁니다.
 *        **코드에서는 별칭 `vector_reference<T>` 로 씁니다**(이름만 보고 "vector 류를 참조로 받는다" 가 읽히도록). `span` 은 그 구현이고,
 *        표준으로 옮길 때 `std::span` 과 바꿔 끼우는 자리입니다.
 *
 * @details `sw::vector<T>` · `small_vector<T, N>` · `sw::array<T, N>` · `std::vector<T>` · `std::array<T, N>` · C 배열 · 포인터 + 개수를
 *          **모두 같은 인자 하나로** 받습니다. `const vector<T>&` 로 받으면 `small_vector` 는 할당자가 달라 다른 타입이라 넘길 수 없고,
 *          `array` 도 마찬가지입니다(포인터와 개수를 따로 받거나 vector 로 복사해 넘기지 않아도 됩니다).
 *          언리얼의 `TArrayView` · `TConstArrayView` 와 같은 자리입니다.
 *
 *          - 읽기만 하면 `vector_reference<const T>`, 원소를 고쳐 쓰면 `vector_reference<T>` 로 받습니다. 쓰기 쪽은 읽기 쪽으로 바뀝니다.
 *          - 원소를 더하거나 지우지는 못합니다(길이는 고정입니다). 그런 일은 컨테이너를 받거나 `VectorUtil` 을 씁니다.
 *          - **소유하지 않습니다.** 가리키는 컨테이너가 커지거나(재할당) 사라지면 뷰는 무효입니다. 멤버로 오래 들고 있지 말고 인자로만 씁니다.
 *
 * @note C++ 최소 표준이 17 이라 `std::span` 을 쓸 수 없어 직접 둡니다. `SW_ENABLE_STL_CONTAINER` 로 표준 컨테이너를 쓰면서 C++20 이면
 *       `std::span` 의 별칭이 되므로, 여기 없는 기능(정적 길이 · `as_bytes`)을 더할 때는 `std::span` 과 같은 이름 · 의미로 더합니다.
 */
#pragma once
#include "Core/Common/Macros.h"

#include <cstddef>
#include <iterator>
#include <type_traits>

// `__cpp_lib_span` 을 정하는 곳은 표준 라이브러리마다 다르다(MSVC STL 은 아무 표준 헤더나, libstdc++ 는 `<version>` · `<span>` 만). 먼저 본다.

#if __has_include( <version> )
    #include <version>
#endif
#if defined( SW_ENABLE_STL_CONTAINER ) && defined( __cpp_lib_span )
    #include <span>
#endif

namespace sw
{
#if defined( SW_ENABLE_STL_CONTAINER ) && defined( __cpp_lib_span )
    inline constexpr size_t dynamic_extent = std::dynamic_extent;

    template <typename T>
    using span = std::span<T>;
#else
    /** @brief `subspan` 의 "끝까지" 입니다(`std::dynamic_extent` 와 같습니다). */
    inline constexpr size_t dynamic_extent = static_cast<size_t>( -1 );

    template <typename T>
    class span;

    namespace SpanInternal
    {
        template <typename T>
        struct IsSpan : std::false_type
        {
        };

        template <typename T>
        struct IsSpan<span<T>> : std::true_type
        {
        };

        /** @brief `std::data( container )` 가 가리키는 원소 타입입니다(const 가 붙을 수 있습니다). */
        template <typename TContainer>
        using DataElement = std::remove_pointer_t<decltype( std::data( std::declval<TContainer&>() ) )>;

        /**
         * @brief `TContainer` 로 `span<TElement>` 를 만들 수 있는지입니다. `std::data` · `std::size` 가 되는 연속 컨테이너이고, 그 원소 포인터가
         *        `TElement*` 로 **배열 변환**되어야 합니다(const 를 붙이는 것만 됩니다 — 파생 → 기반처럼 크기가 다른 변환은 막습니다).
         * @details span 과 C 배열은 제 생성자가 따로 있어 뺍니다.
         */
        template <typename TContainer, typename TElement, typename = void>
        struct IsCompatibleContainer : std::false_type
        {
        };

        template <typename TContainer, typename TElement>
        struct IsCompatibleContainer<TContainer, TElement,
                                     std::void_t<decltype( std::data( std::declval<TContainer&>() ) ), decltype( std::size( std::declval<TContainer&>() ) )>>
            : std::bool_constant<IsSpan<std::remove_cv_t<TContainer>>::value == false && std::is_array_v<TContainer> == false &&
                                 std::is_convertible_v<DataElement<TContainer> ( * )[], TElement ( * )[]>>
        {
        };
    } // namespace SpanInternal

    /**
     * @class span
     * @brief 연속 구간(시작 포인터 + 개수)을 가리키는 뷰입니다. 복사가 싸므로(포인터 둘 크기) 값으로 넘깁니다.
     */
    template <typename T>
    class span
    {
    public:
        using element_type     = T;
        using value_type       = std::remove_cv_t<T>;
        using size_type        = size_t;
        using difference_type  = ptrdiff_t;
        using pointer          = T*;
        using const_pointer    = const T*;
        using reference        = T&;
        using const_reference  = const T&;
        using iterator         = T*;
        using reverse_iterator = std::reverse_iterator<iterator>;

        static constexpr size_t extent = dynamic_extent;

        // ------------------------------------------------------------------------------
        // 1) 생성
        // ------------------------------------------------------------------------------
        /** @brief 빈 뷰입니다. */
        constexpr span() noexcept = default;

        /** @brief `pData` 부터 `count` 개를 가리킵니다. */
        constexpr span( pointer pData, size_type count ) noexcept
            : _pData{ pData }
            , _size{ count }
        {
        }

        /** @brief `[pFirst, pLast)` 를 가리킵니다. */
        constexpr span( pointer pFirst, pointer pLast ) noexcept
            : _pData{ pFirst }
            , _size{ static_cast<size_type>( pLast - pFirst ) }
        {
        }

        /** @brief C 배열 전체를 가리킵니다. */
        template <size_t N>
        constexpr span( element_type ( &arrElement )[N] ) noexcept
            : _pData{ arrElement }
            , _size{ N }
        {
        }

        /** @brief 연속 컨테이너(`vector` · `small_vector` · `array` · 표준 컨테이너) 전체를 가리킵니다. */
        template <typename TContainer SW_REQUIRES( SpanInternal::IsCompatibleContainer<TContainer, T>::value )>
        constexpr span( TContainer& container ) noexcept
            : _pData{ std::data( container ) }
            , _size{ static_cast<size_type>( std::size( container ) ) }
        {
        }

        /**
         * @brief const 컨테이너 전체를 가리킵니다. `span<const T>` 만 됩니다.
         * @details 임시 컨테이너도 받습니다(`std::span<const T>` 와 같습니다). 인자로 넘길 때는 괜찮지만, 그 뷰를 변수에 담으면 문장이 끝날 때 무효입니다.
         */
        template <typename TContainer SW_REQUIRES( std::is_const_v<T>&& SpanInternal::IsCompatibleContainer<const TContainer, T>::value )>
        constexpr span( const TContainer& container ) noexcept
            : _pData{ std::data( container ) }
            , _size{ static_cast<size_type>( std::size( container ) ) }
        {
        }

        /** @brief `span<U>` 에서 바꿉니다. `span<T>` → `span<const T>` 처럼 const 를 붙이는 것만 됩니다. */
        template <typename U SW_REQUIRES( std::is_convertible_v<U ( * )[], T ( * )[]> )>
        constexpr span( const span<U>& other ) noexcept
            : _pData{ other.data() }
            , _size{ other.size() }
        {
        }

        constexpr span( const span& other ) noexcept            = default;
        constexpr span& operator=( const span& other ) noexcept = default;

        // ------------------------------------------------------------------------------
        // 2) 원소 접근
        // ------------------------------------------------------------------------------
        /** @brief `index` 번째 원소입니다. 범위는 Debug 에서만 확인합니다. */
        [[nodiscard]] constexpr reference operator[]( size_type index ) const noexcept
        {
            SW_ASSERT( index < _size );
            return _pData[index];
        }

        /** @brief 첫 원소입니다. 빈 뷰에 부르면 안 됩니다. */
        [[nodiscard]] constexpr reference front() const noexcept
        {
            SW_ASSERT( _size > 0 );
            return _pData[0];
        }

        /** @brief 마지막 원소입니다. 빈 뷰에 부르면 안 됩니다. */
        [[nodiscard]] constexpr reference back() const noexcept
        {
            SW_ASSERT( _size > 0 );
            return _pData[_size - 1];
        }

        /** @brief 첫 원소의 주소입니다. 빈 뷰면 nullptr 일 수 있습니다. */
        [[nodiscard]] constexpr pointer data() const noexcept { return _pData; }

        // ------------------------------------------------------------------------------
        // 3) 크기
        // ------------------------------------------------------------------------------
        /** @brief 원소 개수입니다. */
        [[nodiscard]] constexpr size_type size() const noexcept { return _size; }
        /** @brief 바이트 크기입니다(`size() * sizeof( T )`). */
        [[nodiscard]] constexpr size_type size_bytes() const noexcept { return _size * sizeof( element_type ); }
        /** @brief 원소가 없으면 true 입니다. */
        [[nodiscard]] constexpr bool empty() const noexcept { return _size == 0; }

        // ------------------------------------------------------------------------------
        // 4) 순회
        // ------------------------------------------------------------------------------
        [[nodiscard]] constexpr iterator         begin() const noexcept { return _pData; }
        [[nodiscard]] constexpr iterator         end() const noexcept { return _pData + _size; }
        [[nodiscard]] constexpr reverse_iterator rbegin() const noexcept { return reverse_iterator( end() ); }
        [[nodiscard]] constexpr reverse_iterator rend() const noexcept { return reverse_iterator( begin() ); }

        // ------------------------------------------------------------------------------
        // 5) 부분 뷰
        // ------------------------------------------------------------------------------
        /** @brief 앞의 `count` 개입니다. */
        [[nodiscard]] constexpr span first( size_type count ) const noexcept
        {
            SW_ASSERT( count <= _size );
            return span( _pData, count );
        }

        /** @brief 뒤의 `count` 개입니다. */
        [[nodiscard]] constexpr span last( size_type count ) const noexcept
        {
            SW_ASSERT( count <= _size );
            return span( _pData + ( _size - count ), count );
        }

        /** @brief `offset` 부터 `count` 개입니다. `count` 가 `dynamic_extent` 면 끝까지입니다. */
        [[nodiscard]] constexpr span subspan( size_type offset, size_type count = dynamic_extent ) const noexcept
        {
            SW_ASSERT( offset <= _size );
            SW_ASSERT( count == dynamic_extent || count <= _size - offset );
            return span( _pData + offset, ( count == dynamic_extent ) ? ( _size - offset ) : count );
        }

    private:
        pointer   _pData{ nullptr }; /**< 첫 원소 */
        size_type _size{ 0 };        /**< 원소 개수 */
    };

    // 타입을 적지 않고 `span( container )` 로 만들 때 원소 타입을 컨테이너에서 가져옵니다(`std::span` 과 같습니다).
    template <typename T, size_t N>
    span( T ( & )[N] ) -> span<T>;

    template <typename TContainer>
    span( TContainer& ) -> span<SpanInternal::DataElement<TContainer>>;

    template <typename TContainer>
    span( const TContainer& ) -> span<SpanInternal::DataElement<const TContainer>>;
#endif

    /**
     * @brief 함수 인자로 vector 류(`vector` · `small_vector` · `array` · C 배열 · 포인터 + 개수)를 받을 때 쓰는 이름입니다. `span<T>` 와 같은 타입입니다.
     * @details 읽기만 하면 `vector_reference<const T>`, 원소를 고쳐 쓰면 `vector_reference<T>` 입니다. 별칭이라 C++17 에서는 타입을 적지 않는
     *          선언(`vector_reference view = list;`)은 안 됩니다 — 원소 타입을 적습니다.
     */
    template <typename T>
    using vector_reference = span<T>;
} // namespace sw
