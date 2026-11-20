/**
 * @file EnumArray.h
 * @brief 열거자로 색인하는 고정 배열(`EnumArray<E, T>`)과, 항목 수가 열거자 수와 다르면 컴파일이 멈추는 표 만들기(`makeEnumTable`)입니다.
 * @details 손 배열 `T arr[static_cast<size_t>( E::Count )]` 은 정수로 색인해 다른 열거형의 값으로도 읽히고, 초기화 목록이 모자라면 남은 칸이 조용히 0 이 된다
 *          (`static_assert( SW_COUNT_OF( arr ) == … )` 는 크기를 적지 않은 배열에만 듣는다). 여기서는 색인 형식이 열거형 하나로 고정되고,
 *          `makeEnumTable` 은 항목 수가 정확히 열거자 수일 때만 컴파일된다. magic_enum `containers::array` · 언리얼 `TEnumArray` 와 같은 모양이다.
 *          열거자 수는 끝 열거자 `Count` 에서 읽는다(리플렉션도 같은 끝 열거자를 센티널로 본다). `Count` 가 없는 열거형은 `EnumCount<E>` 를 특수화한다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include <type_traits>
#include <utility>

namespace sw
{
    /**
     * @struct EnumCount
     * @brief 열거형 @p E 의 열거자 수(`kValue`)입니다. 끝 열거자 `Count` 가 있으면 그 값이고, 없으면 정의가 없어 컴파일이 멈춘다 — 특수화로 준다.
     */
    template <typename E, typename = void>
    struct EnumCount;

    template <typename E>
    struct EnumCount<E, std::void_t<decltype( E::Count )>>
    {
        static_assert( std::is_enum_v<E>, "EnumCount needs an enum type" );
        static constexpr size_t kValue = static_cast<size_t>( E::Count );
    };

    /** @brief 열거형 @p E 의 열거자 수입니다(`EnumCount<E>::kValue`). */
    template <typename E>
    inline constexpr size_t kEnumCount = EnumCount<E>::kValue;

    /**
     * @class EnumRange
     * @brief 열거자를 `0` 부터 끝 표식 앞까지 차례로 도는 범위입니다(언리얼 `TEnumRange`, magic_enum `enum_values`). `for ( const E key : EnumRange<E>{} )`.
     * @details 정수 첨자를 돌며 `static_cast<E>( index )` 를 적지 않는다. 열거자는 `0..kEnumCount<E>-1` 로 빈틈없어야 한다(`EnumArray` 와 같은 전제).
     */
    template <typename E>
    class EnumRange
    {
        static_assert( std::is_enum_v<E>, "EnumRange needs an enum type" );

    public:
        class Iterator
        {
        public:
            constexpr explicit Iterator( size_t index ) noexcept
                : _index{ index }
            {
            }

            [[nodiscard]] constexpr E operator*() const noexcept { return static_cast<E>( _index ); }
            constexpr Iterator&       operator++() noexcept
            {
                ++_index;
                return *this;
            }
            [[nodiscard]] constexpr bool operator!=( const Iterator& other ) const noexcept { return _index != other._index; }

        private:
            size_t _index;
        };

        [[nodiscard]] constexpr Iterator begin() const noexcept { return Iterator{ 0 }; }
        [[nodiscard]] constexpr Iterator end() const noexcept { return Iterator{ kEnumCount<E> }; }
    };

    template <typename E, typename T>
    class EnumArray;

    template <typename E, typename T, typename... Args>
    constexpr EnumArray<E, T> makeEnumTable( Args&&... args );

    template <typename E, typename T, size_t N>
    constexpr EnumArray<E, T> makeEnumTable( const T ( &arrValue )[N] );

    /**
     * @class EnumArray
     * @brief 열거자 @p E 로 색인하는 `T` 의 고정 배열입니다. 크기는 `kEnumCount<E>` 이고 원소는 값 초기화된다.
     * @details 집합체가 아니다 — 원소를 나열해 채우는 길은 `makeEnumTable` 하나라, 모자란 목록이 남은 칸을 0 으로 채우는 일이 없다.
     */
    template <typename E, typename T>
    class EnumArray
    {
        static_assert( std::is_enum_v<E>, "EnumArray is indexed by an enum type" );

    public:
        using value_type     = T;
        using key_type       = E; ///< 리플렉션은 열거자 → 값 맵으로 본다(`EnumArrayWrapper`)
        using mapped_type    = T;
        using iterator       = T*;
        using const_iterator = const T*;

        static constexpr size_t kSize = kEnumCount<E>;
        static_assert( kSize > 0, "EnumArray needs at least one enumerator" );

        constexpr EnumArray() = default;

        [[nodiscard]] constexpr T& operator[]( E key ) noexcept
        {
            SW_CHECK_INDEX( key, kSize ); // `Count` 같은 끝 표식 · 정수에서 바꾼 값이 칸을 넘는다
            return _arrValue[static_cast<size_t>( key )];
        }
        [[nodiscard]] constexpr const T& operator[]( E key ) const noexcept
        {
            SW_CHECK_INDEX( key, kSize );
            return _arrValue[static_cast<size_t>( key )];
        }

        [[nodiscard]] static constexpr size_t size() noexcept { return kSize; }
        [[nodiscard]] constexpr T*            data() noexcept { return _arrValue; }
        [[nodiscard]] constexpr const T*      data() const noexcept { return _arrValue; }

        [[nodiscard]] constexpr iterator       begin() noexcept { return _arrValue; }
        [[nodiscard]] constexpr iterator       end() noexcept { return _arrValue + kSize; }
        [[nodiscard]] constexpr const_iterator begin() const noexcept { return _arrValue; }
        [[nodiscard]] constexpr const_iterator end() const noexcept { return _arrValue + kSize; }

        /** @brief 모든 원소를 @p value 로 덮습니다. */
        constexpr void fill( const T& value )
        {
            for ( T& element : _arrValue )
            {
                element = value;
            }
        }

    private:
        /** @brief 원소 나열로 채우는 생성자 표식입니다. */
        struct FillTag
        {
        };
        /** @brief 배열 하나를 베껴 채우는 생성자 표식입니다(나열 생성자와 겹치지 않게 따로 둔다). */
        struct CopyTag
        {
        };

        template <typename... Args>
        constexpr explicit EnumArray( FillTag, Args&&... args )
            : _arrValue{ T( std::forward<Args>( args ) )... }
        {
        }

        template <size_t... Index>
        constexpr EnumArray( CopyTag, const T* pValue, std::index_sequence<Index...> )
            : _arrValue{ pValue[Index]... }
        {
        }

        template <typename E2, typename T2, typename... Args>
        friend constexpr EnumArray<E2, T2> makeEnumTable( Args&&... args );
        template <typename E2, typename T2, size_t N>
        friend constexpr EnumArray<E2, T2> makeEnumTable( const T2 ( &arrValue )[N] );

    private:
        T _arrValue[kSize]{};
    };

    /**
     * @brief 열거자 순서대로 항목을 나열해 `EnumArray<E, T>` 를 만듭니다. 항목 수가 열거자 수와 다르면 컴파일 오류다.
     * @details 표 `static constexpr auto kArrName = makeEnumTable<LogLevel, const utf8*>( "Trace", "Debug", … );` 로 쓴다.
     *          열거자를 더하고 표를 잊으면 이 줄에서 멈춘다(손 배열은 남은 칸이 조용히 비었다).
     */
    template <typename E, typename T, typename... Args>
    constexpr EnumArray<E, T> makeEnumTable( Args&&... args )
    {
        static_assert( sizeof...( Args ) == kEnumCount<E>, "makeEnumTable needs exactly one entry per enumerator" );
        return EnumArray<E, T>( typename EnumArray<E, T>::FillTag{}, std::forward<Args>( args )... );
    }

    /**
     * @brief 중괄호 목록 하나로 표를 만듭니다 — 줄이 구조체라 원소마다 형식을 적지 않을 때 쓴다. 줄 수가 열거자 수와 다르면 컴파일 오류다.
     * @details `static constexpr auto kArrRow = makeEnumTable<SceneDataKind, KindRow>( { { "Transform", … }, { "Bounds", … } } );`
     *          배열 길이(@p N)를 목록에서 읽으므로 모자란 목록이 남은 칸을 0 으로 채우지 못한다.
     */
    template <typename E, typename T, size_t N>
    constexpr EnumArray<E, T> makeEnumTable( const T ( &arrValue )[N] )
    {
        static_assert( N == kEnumCount<E>, "makeEnumTable needs exactly one entry per enumerator" );
        return EnumArray<E, T>( typename EnumArray<E, T>::CopyTag{}, arrValue, std::make_index_sequence<N>{} );
    }
} // namespace sw
