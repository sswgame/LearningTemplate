/**
 * @file array.h
 * @brief std::array 래퍼. 디버그에서 RaceDetectContext 로 동시 접근을 잡습니다.
 */
#pragma once
#include "Core/Concurrency/DataRaceDetector.h"
#include "Core/Container/array.h"

#include <array>
#include <cstddef>
#include <initializer_list>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace sw
{
#if defined( SW_ENABLE_STL_CONTAINER )
	template <typename T, size_t N>
	using array = std::array<T, N>;
#else
	/** @brief std::array + 디버그 레이스 탐지. API는 STL과 같습니다. */
	template <typename T, size_t N>
	class array
	{
		SW_RACE_CTX_MEMBER

	public:
		using value_type			 = T;
		using size_type				 = size_t;
		using difference_type		 = ptrdiff_t;
		using reference				 = value_type&;
		using const_reference		 = const value_type&;
		using pointer				 = value_type*;
		using const_pointer			 = const value_type*;
		using iterator				 = value_type*;
		using const_iterator		 = const value_type*;
		using reverse_iterator		 = std::reverse_iterator<iterator>;
		using const_reverse_iterator = std::reverse_iterator<const_iterator>;

		// Underlying C array storage
		T _elems[N > 0 ? N : 1]{};

		// ------------------------------------------------------------------------------
		// 1) 생성자 및 대입
		// ------------------------------------------------------------------------------
		array() = default;

		template <typename... Args, typename = std::enable_if_t<( sizeof...( Args ) > 0 && sizeof...( Args ) <= N && ( std::is_convertible_v<Args, T> && ... ) )>>
		array( Args&&... args )
			: _elems{ static_cast<T>( std::forward<Args>( args ) )... } {}

		array( std::initializer_list<T> list )
		{
			size_t i{ 0 };
			for ( const auto& item : list )
			{
				if ( i < N )
					_elems[i++] = item;
			}
		}

		array( const array& other )
		{
			SW_SCOPED_RACE_READ_OTHER( other );
			for ( size_type index = 0; index < N; ++index )
			{
				_elems[index] = other._elems[index];
			}
		}

		array& operator=( const array& other )
		{
			if ( this != &other )
			{
				SW_SCOPED_RACE_WRITE();
				SW_SCOPED_RACE_READ_OTHER( other );
				for ( size_type index = 0; index < N; ++index )
				{
					_elems[index] = other._elems[index];
				}
			}
			return *this;
		}

		array( array&& other ) noexcept
		{
			SW_SCOPED_RACE_WRITE_OTHER( other );
			for ( size_type index = 0; index < N; ++index )
			{
				_elems[index] = std::move( other._elems[index] );
			}
		}

		array& operator=( array&& other ) noexcept
		{
			if ( this != &other )
			{
				SW_SCOPED_RACE_WRITE();
				SW_SCOPED_RACE_WRITE_OTHER( other );
				for ( size_type index = 0; index < N; ++index )
				{
					_elems[index] = std::move( other._elems[index] );
				}
			}
			return *this;
		}

		// ------------------------------------------------------------------------------
		// 2) 원소 접근
		// ------------------------------------------------------------------------------
		[[nodiscard]] reference at( size_type pos )
		{
			SW_SCOPED_RACE_WRITE();
			if ( pos >= N )
				throw std::out_of_range( "array::at: index out of range" );
			return _elems[pos];
		}

		[[nodiscard]] const_reference at( size_type pos ) const
		{
			SW_SCOPED_RACE_READ();
			if ( pos >= N )
				throw std::out_of_range( "array::at: index out of range" );
			return _elems[pos];
		}

		[[nodiscard]] reference operator[]( size_type pos ) noexcept
		{
			SW_SCOPED_RACE_WRITE();
			return _elems[pos];
		}

		[[nodiscard]] const_reference operator[]( size_type pos ) const noexcept
		{
			SW_SCOPED_RACE_READ();
			return _elems[pos];
		}

		[[nodiscard]] reference front() noexcept
		{
			SW_SCOPED_RACE_WRITE();
			return _elems[0];
		}

		[[nodiscard]] const_reference front() const noexcept
		{
			SW_SCOPED_RACE_READ();
			return _elems[0];
		}

		[[nodiscard]] reference back() noexcept
		{
			SW_SCOPED_RACE_WRITE();
			return _elems[N > 0 ? N - 1 : 0];
		}

		[[nodiscard]] const_reference back() const noexcept
		{
			SW_SCOPED_RACE_READ();
			return _elems[N > 0 ? N - 1 : 0];
		}

		[[nodiscard]] pointer data() noexcept
		{
			SW_SCOPED_RACE_WRITE();
			return _elems;
		}

		[[nodiscard]] const_pointer data() const noexcept
		{
			SW_SCOPED_RACE_READ();
			return _elems;
		}

		// ------------------------------------------------------------------------------
		// 3) 반복자
		// ------------------------------------------------------------------------------
		[[nodiscard]] iterator begin() noexcept
		{
			SW_SCOPED_RACE_WRITE();
			return _elems;
		}

		[[nodiscard]] const_iterator begin() const noexcept
		{
			SW_SCOPED_RACE_READ();
			return _elems;
		}

		[[nodiscard]] const_iterator cbegin() const noexcept
		{
			SW_SCOPED_RACE_READ();
			return _elems;
		}

		[[nodiscard]] iterator end() noexcept
		{
			SW_SCOPED_RACE_WRITE();
			return _elems + N;
		}

		[[nodiscard]] const_iterator end() const noexcept
		{
			SW_SCOPED_RACE_READ();
			return _elems + N;
		}

		[[nodiscard]] const_iterator cend() const noexcept
		{
			SW_SCOPED_RACE_READ();
			return _elems + N;
		}

		[[nodiscard]] reverse_iterator rbegin() noexcept { return reverse_iterator( end() ); }

		[[nodiscard]] const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator( end() ); }

		[[nodiscard]] const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator( cend() ); }

		[[nodiscard]] reverse_iterator rend() noexcept { return reverse_iterator( begin() ); }

		[[nodiscard]] const_reverse_iterator rend() const noexcept { return const_reverse_iterator( begin() ); }

		[[nodiscard]] const_reverse_iterator crend() const noexcept { return const_reverse_iterator( cbegin() ); }

		// ------------------------------------------------------------------------------
		// 4) 용량
		// ------------------------------------------------------------------------------
		[[nodiscard]] constexpr bool empty() const noexcept { return N == 0; }

		[[nodiscard]] constexpr size_type size() const noexcept { return N; }

		[[nodiscard]] constexpr size_type max_size() const noexcept { return N; }

		// ------------------------------------------------------------------------------
		// 5) 작업
		// ------------------------------------------------------------------------------
		void fill( const T& value )
		{
			SW_SCOPED_RACE_WRITE();
			for ( size_type index = 0; index < N; ++index )
			{
				_elems[index] = value;
			}
		}

		void swap( array& other ) noexcept( std::is_nothrow_swappable_v<T> )
		{
			SW_SCOPED_RACE_WRITE();
			SW_SCOPED_RACE_WRITE_OTHER( other );
			for ( size_type index = 0; index < N; ++index )
			{
				using std::swap;
				swap( _elems[index], other._elems[index] );
			}
		}
	};

	// ------------------------------------------------------------------------------
	// 6) 비교 연산자
	// ------------------------------------------------------------------------------
	template <typename T, size_t N>
	[[nodiscard]] bool operator==( const array<T, N>& lhs, const array<T, N>& rhs )
	{
		for ( size_t elementIndex = 0; elementIndex < N; ++elementIndex )
		{
			if ( ( lhs[elementIndex] == rhs[elementIndex] ) == false )
				return false;
		}
		return true;
	}

	template <typename T, size_t N>
	[[nodiscard]] bool operator!=( const array<T, N>& lhs, const array<T, N>& rhs ) { return ( lhs == rhs ) == false; }

	template <typename T, size_t N>
	[[nodiscard]] bool operator<( const array<T, N>& lhs, const array<T, N>& rhs )
	{
		for ( size_t elementIndex = 0; elementIndex < N; ++elementIndex )
		{
			if ( lhs[elementIndex] < rhs[elementIndex] )
				return true;
			if ( rhs[elementIndex] < lhs[elementIndex] )
				return false;
		}
		return false;
	}

	template <typename T, size_t N>
	[[nodiscard]] bool operator<=( const array<T, N>& lhs, const array<T, N>& rhs ) { return !( rhs < lhs ); }

	template <typename T, size_t N>
	[[nodiscard]] bool operator>( const array<T, N>& lhs, const array<T, N>& rhs ) { return rhs < lhs; }

	template <typename T, size_t N>
	[[nodiscard]] bool operator>=( const array<T, N>& lhs, const array<T, N>& rhs ) { return !( lhs < rhs ); }

	template <size_t I, typename T, size_t N>
	[[nodiscard]] T& get( array<T, N>& a ) noexcept
	{
		static_assert( I < N, "array index out of bounds" );
		return a[I];
	}

	template <size_t I, typename T, size_t N>
	[[nodiscard]] const T& get( const array<T, N>& a ) noexcept
	{
		static_assert( I < N, "array index out of bounds" );
		return a[I];
	}

	template <size_t I, typename T, size_t N>
	[[nodiscard]] T&& get( array<T, N>&& a ) noexcept
	{
		static_assert( I < N, "array index out of bounds" );
		return std::move( a[I] );
	}

	template <size_t I, typename T, size_t N>
	[[nodiscard]] const T&& get( const array<T, N>&& a ) noexcept
	{
		static_assert( I < N, "array index out of bounds" );
		return std::move( a[I] );
	}
#endif
} // namespace sw

// ------------------------------------------------------------------------------
// std::tuple_size and std::tuple_element specialization for structured binding
// ------------------------------------------------------------------------------
namespace std
{
	template <typename T, size_t N>
	struct tuple_size<sw::array<T, N>> : std::integral_constant<size_t, N>
	{
	};

	template <size_t I, typename T, size_t N>
	struct tuple_element<I, sw::array<T, N>>
	{
		using type = T;
	};
} // namespace std
