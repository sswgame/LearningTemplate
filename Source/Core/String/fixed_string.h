/**
 * @file fixed_string.h
 * @brief 고정 용량 스택 할당 문자열 (basic_fixed_string)
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Log/Logger.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"
namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) basic_fixed_string — 스택 N 문자. 넘치면 assert, 힙 동적 할당 0건
	// ------------------------------------------------------------------------------
	/**
	 * @class basic_fixed_string
	 * @brief 힙 동적 할당(new/delete) 없이 N개의 문자를 스택 내부 고정 배열(`_data[N+1]`)에 직접 저장하는 고성능 널 종료 문자열 클래스입니다.
	 * @tparam T 문자 타입 (`utf8` 또는 `utf16`)
	 * @tparam N 저장 가능한 최대 문자 개수 (널 종료 문자 `\0` 공간 1바이트는 내부에 자동 확보됨)
	 * @details
	 * - **0-Allocation & Cache Locality**:
	 *   문자열 데이터를 스택에 즉시 인라인으로 보관하여 힙 파편화를 100% 방지하고 CPU L1/L2 캐시 적중률을 극대화합니다.
	 * - **std::basic_string_view 완벽 호환**:
	 *   `view()` 멤버 함수 및 `operator std::basic_string_view<T>()`를 지원하여 복사 비용 없이 표준 문자열 뷰로 즉시 전달 가능합니다.
	 * - **std::hash 지원**:
	 *   `StringUtil::computeHash64/32` 기반의 `std::hash` 특수화가 기본 내장되어 있어 `std::unordered_map`이나 `std::unordered_set`의 키로 직접 사용 가능합니다.
	 * - **대소문자 무시 비교 (`equalsIgnoreCase`)**:
	 *   `StringUtil::equalsIgnoreCase`와 직결 연동되어 식별자 및 파일 경로 대소문자 무시 비교를 빠르게 수행합니다.
	 */
	template <typename T, uint32 N>
	class basic_fixed_string
	{
		static_assert( N > 0, "basic_fixed_string must have a positive capacity" );
		static_assert( std::is_same_v<T, utf8> || std::is_same_v<T, utf16>, "basic_fixed_string only supports utf8 or utf16" );

	public:
		/** @brief 검색 실패 등을 나타내는 무효 인덱스 상수 (-1) */
		static constexpr uint32 npos = static_cast<uint32>( -1 );

		using value_type	  = T;
		using size_type		  = uint32;
		using reference		  = T&;
		using const_reference = const T&;
		using pointer		  = T*;
		using const_pointer	  = const T*;
		using iterator		  = T*;
		using const_iterator  = const T*;

		// ------------------------------------------------------------------------------
		// 2) 생성자 및 대입 연산자
		// ------------------------------------------------------------------------------
		/** @brief 빈 문자열로 초기화하는 기본 constexpr 생성자 */
		constexpr basic_fixed_string() noexcept
			: _arrData{ T{ 0 } }
			, _size{ 0 } {}

		/** @brief 소멸자 */
		~basic_fixed_string() = default;

		/** @brief 널 종료 C 문자열을 복사하여 생성합니다. */
		basic_fixed_string( const T* str );

		/** @brief std::basic_string 내용을 복사하여 생성합니다. */
		basic_fixed_string( const std::basic_string<T>& str );

		/** @brief std::basic_string_view 내용을 복사하여 생성합니다. */
		basic_fixed_string( const std::basic_string_view<T>& str );

		/** @brief count개의 ch 문자로 채워 생성합니다. */
		basic_fixed_string( uint32 count, T ch );

		/** @brief 복사 생성자 */
		basic_fixed_string( const basic_fixed_string& rhs );

		/** @brief 이동 생성자 */
		basic_fixed_string( basic_fixed_string&& rhs ) noexcept = default;

		/** @brief 다른 고정 문자열을 복사 대입합니다. */
		basic_fixed_string& operator=( const basic_fixed_string& rhs );

		/** @brief 널 종료 C 문자열을 복사 대입합니다. */
		basic_fixed_string& operator=( const T* str );

		/** @brief std::basic_string을 복사 대입합니다. */
		basic_fixed_string& operator=( const std::basic_string<T>& str );

		/** @brief std::basic_string_view를 복사 대입합니다. */
		basic_fixed_string& operator=( const std::basic_string_view<T>& str );

		/** @brief 이동 대입 연산자 */
		basic_fixed_string& operator=( basic_fixed_string&& rhs ) noexcept = default;

		// ------------------------------------------------------------------------------
		// 3) 원소 접근 및 이터레이터
		// ------------------------------------------------------------------------------
		/** @brief 인덱스의 문자를 참조합니다 (디버그 범위 검사). */
		reference		operator[]( uint32 pos );
		const_reference operator[]( uint32 pos ) const;

		/** @brief 인덱스의 문자를 반환합니다. */
		reference		at( uint32 pos );
		const_reference at( uint32 pos ) const;

		/** @brief 첫 번째 문자를 반환합니다. */
		reference		front();
		const_reference front() const;

		/** @brief 마지막 문자를 반환합니다. */
		reference		back();
		const_reference back() const;

		/** @brief 내부 버퍼 포인터를 반환합니다. */
		pointer		  data() noexcept { return _arrData; }
		const_pointer data() const noexcept { return _arrData; }

		/** @brief 널 종료 C 문자열 포인터를 반환합니다. */
		const_pointer c_str() const noexcept { return _arrData; }

		/** @brief 힙 할당 없는 가벼운 string_view를 반환합니다. */
		std::basic_string_view<T> view() const noexcept { return { _arrData, _size }; }

		/** @brief 첫 문자 이터레이터 */
		iterator	   begin() noexcept { return _arrData; }
		const_iterator begin() const noexcept { return _arrData; }
		const_iterator cbegin() const noexcept { return _arrData; }

		/** @brief 끝 문자 이터레이터 */
		iterator	   end() noexcept { return _arrData + _size; }
		const_iterator end() const noexcept { return _arrData + _size; }
		const_iterator cend() const noexcept { return _arrData + _size; }

		// ------------------------------------------------------------------------------
		// 4) 용량 및 상태 조회
		// ------------------------------------------------------------------------------
		/** @brief 문자열이 비어 있는지 여부를 반환합니다. */
		bool empty() const noexcept { return _size == 0; }

		/** @brief 현재 문자 수(널 제외)를 반환합니다. */
		uint32 size() const noexcept { return _size; }
		uint32 length() const noexcept { return _size; }

		/** @brief 최대 수용 가능한 문자 수(N)를 반환합니다. */
		static constexpr uint32 max_size() noexcept { return N; }
		static constexpr uint32 capacity() noexcept { return N; }

		/** @brief 내부 상태를 빈 문자열로 초기화합니다. */
		void clear() noexcept;

		// ------------------------------------------------------------------------------
		// 5) 문자열 조작 (insert / erase / append / substr)
		// ------------------------------------------------------------------------------
		/** @brief pos 위치 앞에 C 문자열을 삽입합니다. */
		basic_fixed_string& insert( uint32 pos, const T* pStr );
		basic_fixed_string& insert( const uint32 pos, const basic_fixed_string& str ) { return insert( pos, str.c_str() ); }

		/** @brief pos 위치부터 length개의 문자를 제거합니다. */
		basic_fixed_string& erase( uint32 pos = 0, uint32 length = npos );

		/** @brief 끝에 단일 문자를 추가합니다. */
		void push_back( T ch );

		/** @brief 마지막 문자를 제거합니다. */
		void pop_back();

		/** @brief 문자열을 새로 대입합니다. */
		basic_fixed_string& assign( const T* pStr ) { return *this = pStr; }
		basic_fixed_string& assign( const basic_fixed_string& str ) { return *this = str; }
		basic_fixed_string& assign( const std::basic_string<T>& str ) { return *this = str; }
		basic_fixed_string& assign( const std::basic_string_view<T>& str ) { return *this = str; }

		/** @brief 끝에 C 문자열을 추가합니다. */
		basic_fixed_string& append( const T* pStr );
		basic_fixed_string& append( const basic_fixed_string& str ) { return append( str.c_str() ); }
		basic_fixed_string& append( uint32 count, T c );
		basic_fixed_string& append( const std::basic_string_view<T>& str );

		/** @brief pos 위치부터 C 부분 문자열을 검색합니다. */
		uint32 find( const T* str, uint32 pos = 0 ) const;
		uint32 find( T c, uint32 pos = 0 ) const;
		uint32 find( const basic_fixed_string& str, uint32 pos = 0 ) const { return find( str.c_str(), pos ); }

		/** @brief pos 위치부터 length 길이의 부분 문자열을 추출하여 반환합니다. */
		basic_fixed_string substr( uint32 pos = 0, uint32 length = npos ) const;

		// ------------------------------------------------------------------------------
		// 6) 비교 및 연산자
		// ------------------------------------------------------------------------------
		/** @brief 사전순 비교 (같으면 0) */
		int32 compare( const basic_fixed_string& other ) const { return StringUtil::strcmp( _arrData, other._arrData ); }
		int32 compare( const T* str ) const { return ( str != nullptr ) ? StringUtil::strcmp( _arrData, str ) : 1; }

		/** @brief 대소문자 무시 동등성 비교 */
		bool equalsIgnoreCase( const basic_fixed_string& other ) const noexcept { return StringUtil::equalsIgnoreCase( _arrData, other._arrData ); }
		bool equalsIgnoreCase( const T* str ) const noexcept { return str != nullptr && StringUtil::equalsIgnoreCase( _arrData, str ); }

		basic_fixed_string& operator+=( const basic_fixed_string& other ) { return append( other ); }
		basic_fixed_string& operator+=( const T* str ) { return append( str ); }
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
		bool operator==( const T* str ) const { return compare( str ) == 0; }
		bool operator!=( const T* str ) const { return compare( str ) != 0; }

		/** @brief 동적 std::basic_string으로의 명시적/암시적 변환 */
		operator std::basic_string<T>() const { return std::basic_string<T>{ _arrData, _size }; }

		/** @brief 힙 할당 없는 std::basic_string_view로의 암시적 변환 */
		operator std::basic_string_view<T>() const noexcept { return { _arrData, _size }; }

	private:
		T	   _arrData[N + 1];
		uint32 _size;
	};

	template <uint32 N>
	using fixed_string = basic_fixed_string<utf8, N>;

	template <uint32 N>
	using fixed_wstring = basic_fixed_string<utf16, N>;

#pragma region IMPLEMENTATION

	template <typename T, uint32 N>
	basic_fixed_string<T, N>::basic_fixed_string( const T* str )
		: _arrData{}
		, _size{ 0 }
	{
		if ( str != nullptr )
		{
			const uint32 length = StringUtil::strlen( str );
			SW_LOG_ASSERT( length <= N, "String too int32 for basic_fixed_string capacity" );
			Memory::copy( _arrData, str, sizeof( T ) * length );
			_size = length;
		}
		_arrData[_size] = T{ 0 };
	}

	template <typename T, uint32 N>
	basic_fixed_string<T, N>::basic_fixed_string( const std::basic_string<T>& str )
		: _arrData{}
		, _size{ 0 }
	{
		SW_LOG_ASSERT( str.length() <= N, "String too int32 for basic_fixed_string capacity" );
		Memory::copy( _arrData, str.data(), sizeof( T ) * str.length() );
		_size			= static_cast<uint32>( str.length() );
		_arrData[_size] = T{ 0 };
	}

	template <typename T, uint32 N>
	basic_fixed_string<T, N>::basic_fixed_string( const std::basic_string_view<T>& str )
		: _arrData{}
		, _size{ 0 }
	{
		SW_LOG_ASSERT( str.length() <= N, "String too int32 for basic_fixed_string capacity" );
		Memory::copy( _arrData, str.data(), sizeof( T ) * str.length() );
		_size			= static_cast<uint32>( str.length() );
		_arrData[_size] = T{ 0 };
	}

	template <typename T, uint32 N>
	basic_fixed_string<T, N>::basic_fixed_string( const uint32 count, T ch )
		: _arrData{}
		, _size{ count }
	{
		SW_LOG_ASSERT( count <= N, "String too int32 for basic_fixed_string capacity" );
		std::fill_n( _arrData, count, ch );
		_arrData[_size] = T{ 0 };
	}

	template <typename T, uint32 N>
	basic_fixed_string<T, N>::basic_fixed_string( const basic_fixed_string& rhs )
		: _arrData{}
		, _size{ rhs._size }
	{
		Memory::copy( _arrData, rhs._arrData, sizeof( T ) * ( _size + 1 ) );
	}

	template <typename T, uint32 N>
	basic_fixed_string<T, N>& basic_fixed_string<T, N>::operator=( const basic_fixed_string& rhs )
	{
		if ( this != &rhs )
		{
			_size = rhs._size;
			Memory::copy( _arrData, rhs._arrData, sizeof( T ) * ( _size + 1 ) );
		}
		return *this;
	}

	template <typename T, uint32 N>
	basic_fixed_string<T, N>& basic_fixed_string<T, N>::operator=( const T* str )
	{
		if ( str != nullptr )
		{
			const uint32 length = StringUtil::strlen( str );
			SW_LOG_ASSERT( length <= N, "String too int32 for basic_fixed_string capacity" );
			Memory::copy( _arrData, str, sizeof( T ) * length );
			_size = length;
		}
		else
		{
			_size = 0;
		}
		_arrData[_size] = T{ 0 };
		return *this;
	}

	template <typename T, uint32 N>
	basic_fixed_string<T, N>& basic_fixed_string<T, N>::operator=( const std::basic_string<T>& str )
	{
		SW_LOG_ASSERT( str.length() <= N, "String too int32 for basic_fixed_string capacity" );
		Memory::copy( _arrData, str.data(), sizeof( T ) * str.length() );
		_size			= static_cast<uint32>( str.length() );
		_arrData[_size] = T{ 0 };
		return *this;
	}

	template <typename T, uint32 N>
	basic_fixed_string<T, N>& basic_fixed_string<T, N>::operator=( const std::basic_string_view<T>& str )
	{
		SW_LOG_ASSERT( str.length() <= N, "String too int32 for basic_fixed_string capacity" );
		Memory::copy( _arrData, str.data(), sizeof( T ) * str.length() );
		_size			= static_cast<uint32>( str.length() );
		_arrData[_size] = T{ 0 };
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
		SW_LOG_ASSERT( pos < _size, "basic_fixed_string::at - position out of range" );
		return _arrData[pos];
	}

	template <typename T, uint32 N>
	typename basic_fixed_string<T, N>::const_reference basic_fixed_string<T, N>::at( uint32 pos ) const
	{
		SW_LOG_ASSERT( pos < _size, "basic_fixed_string::at - position out of range" );
		return _arrData[pos];
	}

	template <typename T, uint32 N>
	typename basic_fixed_string<T, N>::reference basic_fixed_string<T, N>::front()
	{
		SW_LOG_ASSERT( _size > 0, "basic_fixed_string::front on empty string" );
		return _arrData[0];
	}

	template <typename T, uint32 N>
	typename basic_fixed_string<T, N>::const_reference basic_fixed_string<T, N>::front() const
	{
		SW_LOG_ASSERT( _size > 0, "basic_fixed_string::front on empty string" );
		return _arrData[0];
	}

	template <typename T, uint32 N>
	typename basic_fixed_string<T, N>::reference basic_fixed_string<T, N>::back()
	{
		SW_LOG_ASSERT( _size > 0, "basic_fixed_string::back on empty string" );
		return _arrData[_size - 1];
	}

	template <typename T, uint32 N>
	typename basic_fixed_string<T, N>::const_reference basic_fixed_string<T, N>::back() const
	{
		SW_LOG_ASSERT( _size > 0, "basic_fixed_string::back on empty string" );
		return _arrData[_size - 1];
	}

	template <typename T, uint32 N>
	void basic_fixed_string<T, N>::clear() noexcept
	{
		_size		= 0;
		_arrData[0] = T{ 0 };
	}

	template <typename T, uint32 N>
	basic_fixed_string<T, N>& basic_fixed_string<T, N>::insert( uint32 pos, const T* pStr )
	{
		if ( pStr == nullptr )
			return *this;

		const uint32 length = StringUtil::strlen( pStr );
		if ( length == 0 )
			return *this;

		SW_LOG_ASSERT( pos <= _size, "Insert position out of range" );
		SW_LOG_ASSERT( _size + length <= N, "Resulting string too int32" );

		Memory::move( _arrData + pos + length, _arrData + pos, sizeof( T ) * ( _size - pos + 1 ) );
		Memory::copy( _arrData + pos, pStr, sizeof( T ) * length );
		_size += length;

		return *this;
	}

	template <typename T, uint32 N>
	basic_fixed_string<T, N>& basic_fixed_string<T, N>::erase( uint32 pos, uint32 length )
	{
		SW_LOG_ASSERT( pos <= _size, "basic_fixed_string::erase position out of range" );

		if ( pos >= _size )
			return *this;

		if ( length == npos || pos + length >= _size )
		{
			_size			= pos;
			_arrData[_size] = T{ 0 };
		}
		else
		{
			Memory::move( _arrData + pos, _arrData + pos + length, sizeof( T ) * ( _size - pos - length + 1 ) );
			_size -= length;
		}

		return *this;
	}

	template <typename T, uint32 N>
	void basic_fixed_string<T, N>::push_back( T ch )
	{
		SW_LOG_ASSERT( _size < N, "basic_fixed_string capacity exceeded" );
		_arrData[_size] = ch;
		++_size;
		_arrData[_size] = T{ 0 };
	}

	template <typename T, uint32 N>
	void basic_fixed_string<T, N>::pop_back()
	{
		if ( _size == 0 )
			return;
		--_size;
		_arrData[_size] = T{ 0 };
	}

	template <typename T, uint32 N>
	basic_fixed_string<T, N>& basic_fixed_string<T, N>::append( const T* pStr )
	{
		if ( pStr != nullptr )
		{
			const uint32 length = StringUtil::strlen( pStr );
			SW_LOG_ASSERT( _size + length <= N, "Resulting string too int32" );
			Memory::copy( _arrData + _size, pStr, sizeof( T ) * length );
			_size += length;
			_arrData[_size] = T{ 0 };
		}
		return *this;
	}

	template <typename T, uint32 N>
	basic_fixed_string<T, N>& basic_fixed_string<T, N>::append( uint32 count, T c )
	{
		if ( count == 0 )
			return *this;
		SW_LOG_ASSERT( _size + count <= N, "basic_fixed_string::append - count exceeds capacity" );
		std::fill_n( _arrData + _size, count, c );
		_size += count;
		_arrData[_size] = T{ 0 };
		return *this;
	}

	template <typename T, uint32 N>
	basic_fixed_string<T, N>& basic_fixed_string<T, N>::append( const std::basic_string_view<T>& str )
	{
		const uint32 length = static_cast<uint32>( str.length() );
		SW_LOG_ASSERT( _size + length <= N, "Resulting string too int32" );
		Memory::copy( _arrData + _size, str.data(), sizeof( T ) * length );
		_size += length;
		_arrData[_size] = T{ 0 };
		return *this;
	}

	template <typename T, uint32 N>
	uint32 basic_fixed_string<T, N>::find( const T* str, uint32 pos ) const
	{
		if ( str == nullptr || pos >= _size )
			return npos;
		const T* result = StringUtil::strstr( _arrData + pos, str );
		return result != nullptr ? static_cast<uint32>( result - _arrData ) : npos;
	}

	template <typename T, uint32 N>
	uint32 basic_fixed_string<T, N>::find( T c, uint32 pos ) const
	{
		if ( pos >= _size )
			return npos;
		const T* result = StringUtil::strchr( _arrData + pos, c );
		return result ? static_cast<uint32>( result - _arrData ) : npos;
	}

	template <typename T, uint32 N>
	basic_fixed_string<T, N> basic_fixed_string<T, N>::substr( uint32 pos, uint32 length ) const
	{
		SW_LOG_ASSERT( pos <= _size, "basic_fixed_string::substr out of range" );

		if ( pos >= _size )
			return basic_fixed_string{};

		const uint32	   actualLength = MathUtil::min( length, _size - pos );
		basic_fixed_string result{};

		if ( actualLength > 0 )
		{
			Memory::copy( result._arrData, _arrData + pos, sizeof( T ) * actualLength );
			result._size				  = actualLength;
			result._arrData[actualLength] = T{ 0 };
		}

		return result;
	}

	template <typename T, uint32 N>
	basic_fixed_string<T, N> operator+( const basic_fixed_string<T, N>& lhs, const basic_fixed_string<T, N>& rhs )
	{
		basic_fixed_string<T, N> result{ lhs };
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
	/** @brief basic_fixed_string을 std::unordered_map/set 키로 쓸 수 있도록 지원하는 FNV 해시 특수화 */
	struct hash<sw::basic_fixed_string<T, N>>
	{
		size_t operator()( const sw::basic_fixed_string<T, N>& key ) const noexcept
		{
			if constexpr ( sizeof( size_t ) == 8 )
				return static_cast<size_t>( sw::StringUtil::computeHash64( key.c_str(), key.size() ) );
			else
				return static_cast<size_t>( sw::StringUtil::computeHash32( key.c_str(), key.size() ) );
		}
	};
} // namespace std
