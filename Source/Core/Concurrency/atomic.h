/**
 * @file atomic.h
 * @brief PROPERTY/직렬화가 가능한 원자 값 래퍼
 * @details std::atomic은 복사·memcpy가 불가해서 리플렉션 프로퍼티로 쓸 수 없습니다.
 *          이 래퍼는 load/store로 값을 옮기므로 텍스트·바이너리 핸들러와 PROPERTY()가 가능합니다.
 */
#pragma once
#include "Core/Common/Types.h"

#include <atomic>

namespace sw
{
	/**
	 * @brief std::atomic<T>를 감싼 값입니다. 복사·직렬화는 저장된 값만 옮깁니다.
	 */
	template <typename T>
	class atomic
	{
	public:
		constexpr atomic() noexcept
			: _value{ T{} }
		{
		}

		constexpr atomic( T desired ) noexcept
			: _value{ desired }
		{
		}

		atomic( const atomic& other ) noexcept
			: _value{ other.load() }
		{
		}

		atomic( atomic&& other ) noexcept
			: _value{ other.load() }
		{
		}

		~atomic() = default;

		atomic& operator=( const atomic& other ) noexcept
		{
			store( other.load() );
			return *this;
		}

		atomic& operator=( atomic&& other ) noexcept
		{
			store( other.load() );
			return *this;
		}

		atomic& operator=( T desired ) noexcept
		{
			store( desired );
			return *this;
		}

		T load( std::memory_order order = std::memory_order_seq_cst ) const noexcept
		{
			return _value.load( order );
		}

		void store( T desired, std::memory_order order = std::memory_order_seq_cst ) noexcept
		{
			_value.store( desired, order );
		}

		T exchange( T desired, std::memory_order order = std::memory_order_seq_cst ) noexcept
		{
			return _value.exchange( desired, order );
		}

		bool compare_exchange_weak( T& expected, T desired,
									std::memory_order success = std::memory_order_seq_cst,
									std::memory_order failure = std::memory_order_seq_cst ) noexcept
		{
			return _value.compare_exchange_weak( expected, desired, success, failure );
		}

		bool compare_exchange_strong( T& expected, T desired,
									  std::memory_order success = std::memory_order_seq_cst,
									  std::memory_order failure = std::memory_order_seq_cst ) noexcept
		{
			return _value.compare_exchange_strong( expected, desired, success, failure );
		}

		T fetch_add( T arg, std::memory_order order = std::memory_order_seq_cst ) noexcept
		{
			return _value.fetch_add( arg, order );
		}

		T fetch_sub( T arg, std::memory_order order = std::memory_order_seq_cst ) noexcept
		{
			return _value.fetch_sub( arg, order );
		}

		T fetch_or( T arg, std::memory_order order = std::memory_order_seq_cst ) noexcept
		{
			return _value.fetch_or( arg, order );
		}

		T fetch_and( T arg, std::memory_order order = std::memory_order_seq_cst ) noexcept
		{
			return _value.fetch_and( arg, order );
		}

		T fetch_xor( T arg, std::memory_order order = std::memory_order_seq_cst ) noexcept
		{
			return _value.fetch_xor( arg, order );
		}

		T operator++() noexcept { return fetch_add( 1 ) + 1; }
		T operator++( int32 ) noexcept { return fetch_add( 1 ); }
		T operator--() noexcept { return fetch_sub( 1 ) - 1; }
		T operator--( int32 ) noexcept { return fetch_sub( 1 ); }

		operator T() const noexcept { return load(); }

		bool operator==( const atomic& other ) const noexcept { return load() == other.load(); }
		bool operator!=( const atomic& other ) const noexcept { return ( *this == other ) == false; }
		bool operator==( T other ) const noexcept { return load() == other; }
		bool operator!=( T other ) const noexcept { return ( *this == other ) == false; }

	private:
		std::atomic<T> _value;
	};
} // namespace sw
