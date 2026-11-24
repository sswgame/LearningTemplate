/**
 * @file pair.h
 * @brief 값 쌍(sw::pair)과 빈 함수 객체를 0 바이트로 담는 쌍(sw::CompressedPair)입니다.
 *
 * - sw::pair: std::pair 와 같이 **늘** first, second 데이터 멤버를 둡니다. 예전에는 빈 형식이 들어가면 그쪽을 EBO 로 압축해
 *   `first` 가 함수 `first()` 로 바뀌었다 — 제네릭 코드와 구조적 바인딩이 원소 형식에 따라 깨졌다. 압축은 접근자가 늘 함수인 `CompressedPair` 가 맡는다.
 *   `SW_ENABLE_STL_CONTAINER` 의 교체 대상이 아니다 — 공개 칸 `first` · `second` 가 곧 API 라 std::pair 를 멤버로 들면 그 칸을 내줄 수 없다(참조 칸은 쌍을 세 배로 키우고 복사 ·
 *   대입 · 바이트 복사를 깬다). 저장할 버퍼도 없어 바꿀 구현이 없다. std::pair 와는 양쪽으로 바뀐다.
 * - sw::make_pair: 인자 타입을 decay 해서 알맞은 sw::pair 를 만드는 팩토리 함수입니다.
 */
#pragma once
#include "Core/Container/BitwiseRelocatable.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"

#include <tuple>
#include <type_traits>
#include <utility>

namespace sw
{
    namespace internal
    {
        template <typename Type, size_t Index, bool = std::is_empty_v<Type> && std::is_final_v<Type> == false>
        struct EmptyElementTag;

        template <typename Type, size_t Index>
        struct EmptyElementTag<Type, Index, true> : private Type
        {
            constexpr EmptyElementTag() = default;
            constexpr EmptyElementTag( const Type& ) {}
            constexpr EmptyElementTag( Type&& ) noexcept {}

            template <typename... Args>
            constexpr EmptyElementTag( std::in_place_t, Args&&... ) {}

            constexpr Type&       get() noexcept { return *this; }
            constexpr const Type& get() const noexcept { return *this; }
        };

        template <typename Type, size_t Index>
        struct EmptyElementTag<Type, Index, false>
        {
            Type _value;

            constexpr EmptyElementTag()
                : _value{} {}
            constexpr EmptyElementTag( const Type& val )
                : _value{ val } {}
            constexpr EmptyElementTag( Type&& val ) noexcept( std::is_nothrow_move_constructible_v<Type> )
                : _value{ std::move( val ) } {}

            template <typename... Args>
            constexpr EmptyElementTag( std::in_place_t, Args&&... args )
                : _value{ std::forward<Args>( args )... } {}

            constexpr Type&       get() noexcept { return _value; }
            constexpr const Type& get() const noexcept { return _value; }
        };
    } // namespace internal

    /**
     * @brief 빈 함수 객체(해시 · 비교자)를 0 바이트로 담는 쌍입니다. EBO 접근자(`first()` · `second()`)를 가집니다.
     * @details 해시 · 비교자 같은 함수 객체는 대개 비어 있다. 늘 접근자 `first()` · `second()` 로 읽으므로 호출부가 원소가 비었는지에 흔들리지 않는다(`unordered_map` · `unordered_set`).
     */
    template <typename T1, typename T2>
    struct CompressedPair : private internal::EmptyElementTag<T1, 0>, private internal::EmptyElementTag<T2, 1>
    {
        using FirstBase  = internal::EmptyElementTag<T1, 0>;
        using SecondBase = internal::EmptyElementTag<T2, 1>;

        constexpr T1&       first() noexcept { return FirstBase::get(); }
        constexpr const T1& first() const noexcept { return FirstBase::get(); }
        constexpr T2&       second() noexcept { return SecondBase::get(); }
        constexpr const T2& second() const noexcept { return SecondBase::get(); }
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) pair. first, second 를 늘 데이터 멤버로 둔다(std::pair 와 같다)
    // ------------------------------------------------------------------------------
    template <typename T1, typename T2>
    struct pair
    {
        using first_type  = T1;
        using second_type = T2;

        T1 first;
        T2 second;

        template <typename U1, typename U2,
                  std::enable_if_t<std::is_constructible_v<T1, U1&&> && std::is_constructible_v<T2, U2&&>, int32> = 0>
        constexpr pair( U1&& a, U2&& b )
            : first{ std::forward<U1>( a ) }
            , second{ std::forward<U2>( b ) } {}

        template <typename U1, typename U2,
                  std::enable_if_t<std::is_constructible_v<T1, const U1&> && std::is_constructible_v<T2, const U2&>, int32> = 0>
        constexpr pair( const pair<U1, U2>& other )
            : first{ other.first }
            , second{ other.second } {}

        template <typename U1, typename U2,
                  std::enable_if_t<std::is_constructible_v<T1, U1&&> && std::is_constructible_v<T2, U2&&>, int32> = 0>
        constexpr pair( pair<U1, U2>&& other ) noexcept( std::is_nothrow_constructible_v<T1, U1&&> && std::is_nothrow_constructible_v<T2, U2&&> )
            : first{ std::forward<U1>( other.first ) }
            , second{ std::forward<U2>( other.second ) } {}

        template <typename U1, typename U2,
                  std::enable_if_t<std::is_constructible_v<T1, const U1&> && std::is_constructible_v<T2, const U2&>, int32> = 0>
        constexpr pair( const std::pair<U1, U2>& other )
            : first{ other.first }
            , second{ other.second } {}

        template <typename U1, typename U2,
                  std::enable_if_t<std::is_constructible_v<T1, U1&&> && std::is_constructible_v<T2, U2&&>, int32> = 0>
        constexpr pair( std::pair<U1, U2>&& other ) noexcept( std::is_nothrow_constructible_v<T1, U1&&> && std::is_nothrow_constructible_v<T2, U2&&> )
            : first{ std::forward<U1>( other.first ) }
            , second{ std::forward<U2>( other.second ) } {}

        constexpr pair()
            : first{}
            , second{} {}

        constexpr pair( const T1& a, const T2& b )
            : first{ a }
            , second{ b } {}

        template <typename... Args1, typename... Args2>
        constexpr pair( std::piecewise_construct_t, std::tuple<Args1...> firstArgs, std::tuple<Args2...> secondArgs )
            : pair( firstArgs, secondArgs, std::index_sequence_for<Args1...>{}, std::index_sequence_for<Args2...>{} ) {}

        constexpr pair( const pair& )                = default;
        constexpr pair( pair&& ) noexcept            = default;
        constexpr pair& operator=( const pair& )     = default;
        constexpr pair& operator=( pair&& ) noexcept = default;

        template <typename U1, typename U2>
        constexpr pair& operator=( const pair<U1, U2>& other )
        {
            first  = other.first;
            second = other.second;
            return *this;
        }

        template <typename U1, typename U2>
        constexpr pair& operator=( pair<U1, U2>&& other )
        {
            first  = std::forward<U1>( other.first );
            second = std::forward<U2>( other.second );
            return *this;
        }

        template <typename U1, typename U2>
        constexpr pair& operator=( const std::pair<U1, U2>& other )
        {
            first  = other.first;
            second = other.second;
            return *this;
        }

        template <typename U1, typename U2>
        constexpr pair& operator=( std::pair<U1, U2>&& other )
        {
            first  = std::forward<U1>( other.first );
            second = std::forward<U2>( other.second );
            return *this;
        }

        constexpr void swap( pair& other ) noexcept( std::is_nothrow_swappable_v<T1> && std::is_nothrow_swappable_v<T2> )
        {
            using std::swap;
            swap( first, other.first );
            swap( second, other.second );
        }

        constexpr operator std::pair<T1, T2>() const { return std::pair<T1, T2>( first, second ); }

    private:
        template <typename... Args1, typename... Args2, size_t... Index1, size_t... Index2>
        constexpr pair( std::tuple<Args1...>& firstArgs, std::tuple<Args2...>& secondArgs,
                        std::index_sequence<Index1...>, std::index_sequence<Index2...> )
            : first{ std::forward<Args1>( std::get<Index1>( firstArgs ) )... }
            , second{ std::forward<Args2>( std::get<Index2>( secondArgs ) )... } {}
    };

    template <typename T1, typename T2>
    constexpr pair<std::decay_t<T1>, std::decay_t<T2>> make_pair( T1&& a, T2&& b )
    {
        return pair<std::decay_t<T1>, std::decay_t<T2>>( std::forward<T1>( a ), std::forward<T2>( b ) );
    }

    template <typename T1, typename T2>
    constexpr bool operator==( const pair<T1, T2>& lhs, const pair<T1, T2>& rhs )
    {
        return lhs.first == rhs.first && lhs.second == rhs.second;
    }

    template <typename T1, typename T2>
    constexpr bool operator!=( const pair<T1, T2>& lhs, const pair<T1, T2>& rhs )
    {
        return !( lhs == rhs );
    }

    template <typename T1, typename T2>
    constexpr bool operator<( const pair<T1, T2>& lhs, const pair<T1, T2>& rhs )
    {
        if ( lhs.first < rhs.first )
            return true;
        if ( rhs.first < lhs.first )
            return false;
        return lhs.second < rhs.second;
    }

    template <typename T1, typename T2>
    constexpr bool operator<=( const pair<T1, T2>& lhs, const pair<T1, T2>& rhs )
    {
        return ( rhs < lhs ) == false;
    }

    template <typename T1, typename T2>
    constexpr bool operator>( const pair<T1, T2>& lhs, const pair<T1, T2>& rhs )
    {
        return rhs < lhs;
    }

    template <size_t Index, typename T1, typename T2>
    constexpr decltype( auto ) get( pair<T1, T2>& p ) noexcept
    {
        if constexpr ( Index == 0 )
            return ( p.first );
        else
            return ( p.second );
    }

    template <size_t Index, typename T1, typename T2>
    constexpr decltype( auto ) get( const pair<T1, T2>& p ) noexcept
    {
        if constexpr ( Index == 0 )
            return ( p.first );
        else
            return ( p.second );
    }

    template <size_t Index, typename T1, typename T2>
    constexpr decltype( auto ) get( pair<T1, T2>&& p ) noexcept
    {
        if constexpr ( Index == 0 )
            return std::move( p.first );
        else
            return std::move( p.second );
    }

    template <typename T1, typename T2>
    struct is_bitwise_relocatable<pair<T1, T2>> : std::bool_constant<is_bitwise_relocatable_v<T1> && is_bitwise_relocatable_v<T2>>
    {
    };
} // namespace sw

// 구조적 바인딩 · tuple 인터페이스
namespace std
{
    template <typename T1, typename T2>
    struct tuple_size<sw::pair<T1, T2>> : std::integral_constant<size_t, 2>
    {
    };

    template <typename T1, typename T2>
    struct tuple_element<0, sw::pair<T1, T2>>
    {
        using type = T1;
    };

    template <typename T1, typename T2>
    struct tuple_element<1, sw::pair<T1, T2>>
    {
        using type = T2;
    };
} // namespace std
