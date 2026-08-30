/**
 * @file pair.h
 * @brief 값 쌍(sw::pair) 컨테이너 및 EBO(Empty Base Optimization) 압축 지원.
 *
 * - SW_ENABLE_STL_CONTAINER 정의 시 std::pair 및 std::make_pair로 전환됩니다.
 * - sw::pair:
 *   1) 상태가 있는 일반 타입 쌍: std::pair와 100% 동일하게 first, second 데이터 멤버를 제공합니다.
 *   2) 상태 없는(Stateless) 빈 클래스 포함 시: 상속(EBO)을 적용하여 0바이트로 압축 보관합니다.
 * - sw::make_pair: 인자의 타입을 decay하여 적절한 sw::pair를 생성하는 팩토리 함수.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"

#include <tuple>
#include <type_traits>
#include <utility>

namespace sw
{
#if defined( SW_ENABLE_STL_CONTAINER )
	template <typename T1, typename T2>
	using pair = std::pair<T1, T2>;

	template <typename T1, typename T2>
	constexpr std::pair<std::decay_t<T1>, std::decay_t<T2>> make_pair( T1&& a, T2&& b )
	{
		return std::make_pair( std::forward<T1>( a ), std::forward<T2>( b ) );
	}
#else
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

			constexpr Type&		  get() noexcept { return *this; }
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

			constexpr Type&		  get() noexcept { return _value; }
			constexpr const Type& get() const noexcept { return _value; }
		};
	} // namespace internal

	// ------------------------------------------------------------------------------
	// 1) Case 1: T1, T2 모두 일반 타입 (비어있지 않음) -> first, second 직접 멤버
	// ------------------------------------------------------------------------------
	template <typename T1, typename T2,
			  bool T1Empty = ( std::is_empty_v<T1> && std::is_final_v<T1> == false ),
			  bool T2Empty = ( std::is_empty_v<T2> && std::is_final_v<T2> == false )>
	struct pair
	{
		using first_type  = T1;
		using second_type = T2;

		T1 first;
		T2 second;

		constexpr pair()
			: first{}
			, second{} {}

		constexpr pair( const T1& a, const T2& b )
			: first{ a }
			, second{ b } {}

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

		template <typename... Args1, typename... Args2>
		constexpr pair( std::piecewise_construct_t, std::tuple<Args1...> firstArgs, std::tuple<Args2...> secondArgs )
			: pair( firstArgs, secondArgs, std::index_sequence_for<Args1...>{}, std::index_sequence_for<Args2...>{} ) {}

		constexpr pair( const pair& )				 = default;
		constexpr pair( pair&& ) noexcept			 = default;
		constexpr pair& operator=( const pair& )	 = default;
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

	// ------------------------------------------------------------------------------
	// 2) Case 2: T1 빈 클래스 (EBO 압축), T2 일반 타입
	// ------------------------------------------------------------------------------
	template <typename T1, typename T2>
	struct pair<T1, T2, true, false> : private internal::EmptyElementTag<T1, 0>
	{
		using first_type  = T1;
		using second_type = T2;
		using BaseTag	  = internal::EmptyElementTag<T1, 0>;

		T2 second;

		constexpr pair()
			: BaseTag{}
			, second{} {}

		constexpr pair( const T1& a, const T2& b )
			: BaseTag{ a }
			, second{ b } {}

		template <typename U1, typename U2>
		constexpr pair( U1&& a, U2&& b )
			: BaseTag{ std::forward<U1>( a ) }
			, second{ std::forward<U2>( b ) } {}

		constexpr pair( const pair& )				 = default;
		constexpr pair( pair&& ) noexcept			 = default;
		constexpr pair& operator=( const pair& )	 = default;
		constexpr pair& operator=( pair&& ) noexcept = default;

		constexpr T1&		first() noexcept { return BaseTag::get(); }
		constexpr const T1& first() const noexcept { return BaseTag::get(); }

		constexpr void swap( pair& other ) noexcept( std::is_nothrow_swappable_v<T2> )
		{
			using std::swap;
			swap( second, other.second );
		}

	private:
		template <typename... Args1, typename... Args2, size_t... Index1, size_t... Index2>
		constexpr pair( std::tuple<Args1...>& firstArgs, std::tuple<Args2...>& secondArgs,
						std::index_sequence<Index1...>, std::index_sequence<Index2...> )
			: BaseTag{ std::in_place, std::forward<Args1>( std::get<Index1>( firstArgs ) )... }
			, second{ std::forward<Args2>( std::get<Index2>( secondArgs ) )... } {}
	};

	// ------------------------------------------------------------------------------
	// 3) Case 3: T1 일반 타입, T2 빈 클래스 (EBO 압축)
	// ------------------------------------------------------------------------------
	template <typename T1, typename T2>
	struct pair<T1, T2, false, true> : private internal::EmptyElementTag<T2, 1>
	{
		using first_type  = T1;
		using second_type = T2;
		using BaseTag	  = internal::EmptyElementTag<T2, 1>;

		T1 first;

		constexpr pair()
			: BaseTag{}
			, first{} {}

		constexpr pair( const T1& a, const T2& b )
			: BaseTag{ b }
			, first{ a } {}

		template <typename U1, typename U2>
		constexpr pair( U1&& a, U2&& b )
			: BaseTag{ std::forward<U2>( b ) }
			, first{ std::forward<U1>( a ) } {}

		constexpr pair( const pair& )				 = default;
		constexpr pair( pair&& ) noexcept			 = default;
		constexpr pair& operator=( const pair& )	 = default;
		constexpr pair& operator=( pair&& ) noexcept = default;

		constexpr T2&		second() noexcept { return BaseTag::get(); }
		constexpr const T2& second() const noexcept { return BaseTag::get(); }

		constexpr void swap( pair& other ) noexcept( std::is_nothrow_swappable_v<T1> )
		{
			using std::swap;
			swap( first, other.first );
		}

	private:
		template <typename... Args1, typename... Args2, size_t... Index1, size_t... Index2>
		constexpr pair( std::tuple<Args1...>& firstArgs, std::tuple<Args2...>& secondArgs,
						std::index_sequence<Index1...>, std::index_sequence<Index2...> )
			: BaseTag{ std::in_place, std::forward<Args2>( std::get<Index2>( secondArgs ) )... }
			, first{ std::forward<Args1>( std::get<Index1>( firstArgs ) )... } {}
	};

	// ------------------------------------------------------------------------------
	// 4) Case 4: T1, T2 모두 빈 클래스 (둘 다 EBO 압축 -> 1바이트)
	// ------------------------------------------------------------------------------
	template <typename T1, typename T2>
	struct pair<T1, T2, true, true> : private internal::EmptyElementTag<T1, 0>, private internal::EmptyElementTag<T2, 1>
	{
		using first_type  = T1;
		using second_type = T2;
		using FirstBase	  = internal::EmptyElementTag<T1, 0>;
		using SecondBase  = internal::EmptyElementTag<T2, 1>;

		constexpr pair() = default;

		constexpr pair( const T1& a, const T2& b )
			: FirstBase{ a }
			, SecondBase{ b } {}

		template <typename U1, typename U2>
		constexpr pair( U1&& a, U2&& b )
			: FirstBase{ std::forward<U1>( a ) }
			, SecondBase{ std::forward<U2>( b ) } {}

		constexpr pair( const pair& )				 = default;
		constexpr pair( pair&& ) noexcept			 = default;
		constexpr pair& operator=( const pair& )	 = default;
		constexpr pair& operator=( pair&& ) noexcept = default;

		constexpr T1&		first() noexcept { return FirstBase::get(); }
		constexpr const T1& first() const noexcept { return FirstBase::get(); }
		constexpr T2&		second() noexcept { return SecondBase::get(); }
		constexpr const T2& second() const noexcept { return SecondBase::get(); }

		constexpr void swap( pair& ) noexcept {}

	private:
		template <typename... Args1, typename... Args2, size_t... Index1, size_t... Index2>
		constexpr pair( std::tuple<Args1...>& firstArgs, std::tuple<Args2...>& secondArgs,
						std::index_sequence<Index1...>, std::index_sequence<Index2...> )
			: FirstBase{ std::in_place, std::forward<Args1>( std::get<Index1>( firstArgs ) )... }
			, SecondBase{ std::in_place, std::forward<Args2>( std::get<Index2>( secondArgs ) )... } {}
	};

	template <typename T1, typename T2>
	constexpr pair<std::decay_t<T1>, std::decay_t<T2>> make_pair( T1&& a, T2&& b )
	{
		return pair<std::decay_t<T1>, std::decay_t<T2>>( std::forward<T1>( a ), std::forward<T2>( b ) );
	}

	template <typename T1, typename T2>
	constexpr bool operator==( const pair<T1, T2>& lhs, const pair<T1, T2>& rhs )
	{
		if constexpr ( std::is_empty_v<T1> && std::is_empty_v<T2> )
			return true;
		else if constexpr ( std::is_empty_v<T1> )
			return lhs.second == rhs.second;
		else if constexpr ( std::is_empty_v<T2> )
			return lhs.first == rhs.first;
		else
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
		if constexpr ( std::is_empty_v<T1> == false && std::is_empty_v<T2> == false )
		{
			if ( lhs.first < rhs.first )
				return true;
			if ( rhs.first < lhs.first )
				return false;
			return lhs.second < rhs.second;
		}
		else if constexpr ( std::is_empty_v<T1> == false )
		{
			return lhs.first < rhs.first;
		}
		else if constexpr ( std::is_empty_v<T2> == false )
		{
			return lhs.second < rhs.second;
		}
		else
		{
			return false;
		}
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
		{
			if constexpr ( std::is_empty_v<T1> && std::is_final_v<T1> == false )
				return p.first();
			else
				return ( p.first );
		}
		else
		{
			if constexpr ( std::is_empty_v<T2> && std::is_final_v<T2> == false )
				return p.second();
			else
				return ( p.second );
		}
	}

	template <size_t Index, typename T1, typename T2>
	constexpr decltype( auto ) get( const pair<T1, T2>& p ) noexcept
	{
		if constexpr ( Index == 0 )
		{
			if constexpr ( std::is_empty_v<T1> && std::is_final_v<T1> == false )
				return p.first();
			else
				return ( p.first );
		}
		else
		{
			if constexpr ( std::is_empty_v<T2> && std::is_final_v<T2> == false )
				return p.second();
			else
				return ( p.second );
		}
	}

	template <size_t Index, typename T1, typename T2>
	constexpr decltype( auto ) get( pair<T1, T2>&& p ) noexcept
	{
		if constexpr ( Index == 0 )
		{
			if constexpr ( std::is_empty_v<T1> && std::is_final_v<T1> == false )
				return std::move( p.first() );
			else
				return std::move( p.first );
		}
		else
		{
			if constexpr ( std::is_empty_v<T2> && std::is_final_v<T2> == false )
				return std::move( p.second() );
			else
				return std::move( p.second );
		}
	}
#endif
} // namespace sw

// Structured Binding & Tuple interface
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
