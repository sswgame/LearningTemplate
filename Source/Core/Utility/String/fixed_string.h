#pragma once
/**
 * @file fixed_string.h
 * @brief Auto-generated documentation header
 */

#include "Core/Common/Types.h"
#include "Core/Common/CommonHeaders.h"
#include "Core/Utility/String/StringUtil.h"
#include "Core/Utility/Log/Logger.h"

namespace sw
{
	template <typename T, uint32 N>
	class basic_fixed_string
	{
		static_assert( N > 0, "basic_fixed_string must have a positive capacity" );
		static_assert( std::is_same_v<T, utf8> || std::is_same_v<T, utf16>, "basic_fixed_string only supports utf8 or utf16" );

	public:
		static constexpr uint32 npos = static_cast<uint32>( -1 );

		using value_type	  = T;
		using size_type		  = uint32;
		using reference		  = T&;
		using const_reference = const T&;
		using pointer		  = T*;
		using const_pointer	  = const T*;
		using iterator		  = T*;
		using const_iterator  = const T*;

	public:
		basic_fixed_string() { _data[0] = T{ 0 }; }
		~basic_fixed_string() = default;

		basic_fixed_string( const T* str );
		basic_fixed_string( const std::basic_string<T>& str );
		basic_fixed_string( const uint32 count, T ch );
		basic_fixed_string( const basic_fixed_string& rhs );
		basic_fixed_string( basic_fixed_string&& rhs ) noexcept = default;

		basic_fixed_string& operator=( const basic_fixed_string& rhs );
		basic_fixed_string& operator=( const T* str );
		basic_fixed_string& operator=( const std::basic_string<T>& str );
		basic_fixed_string& operator=( basic_fixed_string&& rhs ) noexcept = default;

		reference		operator[]( uint32 pos );
		const_reference operator[]( uint32 pos ) const;
		/**
		 * @brief at 처리를 수행합니다.
		 */
		reference		at( uint32 pos );
		/**
		 * @brief at 처리를 수행합니다.
		 */
		const_reference at( uint32 pos ) const;

		/**
		 * @brief front 처리를 수행합니다.
		 */
		reference		front();
		/**
		 * @brief front 처리를 수행합니다.
		 */
		const_reference front() const;
		/**
		 * @brief back 처리를 수행합니다.
		 */
		reference		back();
		/**
		 * @brief back 처리를 수행합니다.
		 */
		const_reference back() const;

		pointer		  data() { return _data; }
		const_pointer c_str() const { return _data; }

		iterator	   begin() { return _data; }
		const_iterator begin() const { return _data; }
		const_iterator cbegin() const { return _data; }
		iterator	   end() { return _data + _size; }
		const_iterator end() const { return _data + _size; }
		const_iterator cend() const { return _data + _size; }

		bool   empty() const { return _size == 0; }
		uint32 size() const { return _size; }
		uint32 length() const { return _size; }
		/**
		 * @brief clear 처리를 수행합니다.
		 */
		void   clear();

		static constexpr uint32 max_size() { return N; }
		static constexpr uint32 capacity() { return N; }

		/**
		 * @brief insert 처리를 수행합니다.
		 */
		basic_fixed_string& insert( uint32 pos, const T* str );
		basic_fixed_string& insert( const uint32 pos, const basic_fixed_string& str ) { return insert( pos, str.c_str() ); }
		/**
		 * @brief erase 처리를 수행합니다.
		 */
		basic_fixed_string& erase( uint32 pos = 0, uint32 length = npos );

		/**
		 * @brief push_back 처리를 수행합니다.
		 */
		void push_back( T ch );
		/**
		 * @brief pop_back 처리를 수행합니다.
		 */
		void pop_back();

		/**
		 * @brief append 처리를 수행합니다.
		 */
		basic_fixed_string& append( const T* str );
		basic_fixed_string& append( const basic_fixed_string& str ) { return append( str.c_str() ); }
		/**
		 * @brief append 처리를 수행합니다.
		 */
		basic_fixed_string& append( uint32 count, T c );
		/**
		 * @brief append 처리를 수행합니다.
		 */
		basic_fixed_string& append( const std::basic_string_view<T>& str );

		/**
		 * @brief find 처리를 수행합니다.
		 */
		uint32 find( const T* str, uint32 pos = 0 ) const;
		/**
		 * @brief find 처리를 수행합니다.
		 */
		uint32 find( T c, uint32 pos = 0 ) const;
		uint32 find( const basic_fixed_string& str, uint32 pos = 0 ) const { return find( str.c_str(), pos ); }

		/**
		 * @brief substr 처리를 수행합니다.
		 */
		basic_fixed_string substr( uint32 pos = 0, uint32 length = npos ) const;

		int32 compare( const basic_fixed_string& other ) const { return StringUtil::strcmp( _data, other._data ); }
		int32 compare( const T* str ) const { return ( str != nullptr ) ? StringUtil::strcmp( _data, str ) : 1; }

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

		operator std::basic_string<T>() const { return std::basic_string<T>{ _data, _size }; }

	private:

		T	   _data[N + 1];
		uint32 _size = 0;
	};

	template <uint32 N>
	using fixed_string = basic_fixed_string<utf8, N>;

	template <uint32 N>
	using fixed_wstring = basic_fixed_string<utf16, N>;

#pragma region IMPLEMENTATION

	template <typename T, uint32 N>
	basic_fixed_string<T, N>::basic_fixed_string( const T* str )
		: _size( 0 )
	{
		if ( str != nullptr )
		{
			const uint32 length = StringUtil::strlen( str );
			SW_LOG_ASSERT( length <= N, "String too long for basic_fixed_string capacity" );
			std::memcpy( _data, str, sizeof( T ) * length );
			_size = length;
		}
		_data[_size] = T{ 0 };
	}

	template <typename T, uint32 N>
	basic_fixed_string<T, N>::basic_fixed_string( const std::basic_string<T>& str )
		: _size( 0 )
	{
		SW_LOG_ASSERT( str.length() <= N, "String too long for basic_fixed_string capacity" );
		std::memcpy( _data, str.data(), sizeof( T ) * str.length() );
		_size		 = static_cast<uint32>( str.length() );
		_data[_size] = T{ 0 };
	}

	template <typename T, uint32 N>
	basic_fixed_string<T, N>::basic_fixed_string( const uint32 count, T ch )
		: _size( count )
	{
		SW_LOG_ASSERT( count <= N, "String too long for basic_fixed_string capacity" );
		std::fill_n( _data, count, ch );
		_data[_size] = T{ 0 };
	}

	template <typename T, uint32 N>
	basic_fixed_string<T, N>::basic_fixed_string( const basic_fixed_string& rhs )
		: _size( rhs._size )
	{

		std::memcpy( _data, rhs._data, sizeof( T ) * ( _size + 1 ) );
	}

	template <typename T, uint32 N>
	basic_fixed_string<T, N>& basic_fixed_string<T, N>::operator=( const basic_fixed_string& rhs )
	{
		if ( this != &rhs )
		{
			_size = rhs._size;
			std::memcpy( _data, rhs._data, sizeof( T ) * ( _size + 1 ) );
		}
		return *this;
	}

	template <typename T, uint32 N>
	basic_fixed_string<T, N>& basic_fixed_string<T, N>::operator=( const T* str )
	{
		if ( str != nullptr )
		{
			const uint32 length = StringUtil::strlen( str );
			SW_LOG_ASSERT( length <= N, "String too long for basic_fixed_string capacity" );
			std::memcpy( _data, str, sizeof( T ) * length );
			_size = length;
		}
		else
		{
			_size = 0;
		}
		_data[_size] = T{ 0 };
		return *this;
	}

	template <typename T, uint32 N>
	basic_fixed_string<T, N>& basic_fixed_string<T, N>::operator=( const std::basic_string<T>& str )
	{
		SW_LOG_ASSERT( str.length() <= N, "String too long for basic_fixed_string capacity" );
		std::memcpy( _data, str.data(), sizeof( T ) * str.length() );
		_size		 = static_cast<uint32>( str.length() );
		_data[_size] = T{ 0 };
		return *this;
	}

	template <typename T, uint32 N>
	typename basic_fixed_string<T, N>::reference basic_fixed_string<T, N>::operator[]( uint32 pos )
	{
		SW_LOG_ASSERT( pos < N, "basic_fixed_string::operator[] - position out of range" );
		return _data[pos];
	}

	template <typename T, uint32 N>
	typename basic_fixed_string<T, N>::const_reference basic_fixed_string<T, N>::operator[]( uint32 pos ) const
	{
		SW_LOG_ASSERT( pos < N, "basic_fixed_string::operator[] - position out of range" );
		return _data[pos];
	}

	template <typename T, uint32 N>
	typename basic_fixed_string<T, N>::reference basic_fixed_string<T, N>::at( uint32 pos )
	{
		SW_LOG_ASSERT( pos < _size, "basic_fixed_string::at - position out of range" );
		return _data[pos];
	}

	template <typename T, uint32 N>
	typename basic_fixed_string<T, N>::const_reference basic_fixed_string<T, N>::at( uint32 pos ) const
	{
		SW_LOG_ASSERT( pos < _size, "basic_fixed_string::at - position out of range" );
		return _data[pos];
	}

	template <typename T, uint32 N>
	typename basic_fixed_string<T, N>::reference basic_fixed_string<T, N>::front()
	{
		SW_LOG_ASSERT( _size > 0, "basic_fixed_string::front on empty string" );
		return _data[0];
	}

	template <typename T, uint32 N>
	typename basic_fixed_string<T, N>::const_reference basic_fixed_string<T, N>::front() const
	{
		SW_LOG_ASSERT( _size > 0, "basic_fixed_string::front on empty string" );
		return _data[0];
	}

	template <typename T, uint32 N>
	typename basic_fixed_string<T, N>::reference basic_fixed_string<T, N>::back()
	{
		SW_LOG_ASSERT( _size > 0, "basic_fixed_string::back on empty string" );
		return _data[_size - 1];
	}

	template <typename T, uint32 N>
	typename basic_fixed_string<T, N>::const_reference basic_fixed_string<T, N>::back() const
	{
		SW_LOG_ASSERT( _size > 0, "basic_fixed_string::back on empty string" );
		return _data[_size - 1];
	}

	template <typename T, uint32 N>
	void basic_fixed_string<T, N>::clear()
	{
		_size	 = 0;
		_data[0] = T{ 0 };
	}

	template <typename T, uint32 N>
	basic_fixed_string<T, N>& basic_fixed_string<T, N>::insert( uint32 pos, const T* str )
	{
		if ( str == nullptr )
			return *this;

		const uint32 length = StringUtil::strlen( str );
		if ( length == 0 )
			return *this;

		SW_LOG_ASSERT( pos <= _size, "Insert position out of range" );
		SW_LOG_ASSERT( _size + length <= N, "Resulting string too long" );

		std::memmove( _data + pos + length, _data + pos, sizeof( T ) * ( _size - pos + 1 ) );
		std::memcpy( _data + pos, str, sizeof( T ) * length );
		_size += length;

		return *this;
	}

	template <typename T, uint32 N>
	basic_fixed_string<T, N>& basic_fixed_string<T, N>::erase( uint32 pos, uint32 length )
	{
		SW_LOG_ASSERT( pos <= _size, "basic_fixed_string::erase position out of range" );

		if ( length == npos || pos + length >= _size )
		{
			_size		 = pos;
			_data[_size] = T{ 0 };
		}
		else
		{

			std::memmove( _data + pos, _data + pos + length, sizeof( T ) * ( _size - pos - length + 1 ) );
			_size -= length;
		}

		return *this;
	}

	template <typename T, uint32 N>
	void basic_fixed_string<T, N>::push_back( T ch )
	{
		SW_LOG_ASSERT( _size < N, "basic_fixed_string capacity exceeded" );
		_data[_size] = ch;
		++_size;
		_data[_size] = T{ 0 };
	}

	template <typename T, uint32 N>
	void basic_fixed_string<T, N>::pop_back()
	{
		if ( _size == 0 )
			return;
		--_size;
		_data[_size] = T{ 0 };
	}

	template <typename T, uint32 N>
	basic_fixed_string<T, N>& basic_fixed_string<T, N>::append( const T* str )
	{
		if ( str != nullptr )
		{
			const uint32 length = StringUtil::strlen( str );
			SW_LOG_ASSERT( _size + length <= N, "Resulting string too long" );
			std::memcpy( _data + _size, str, sizeof( T ) * length );
			_size += length;
			_data[_size] = T{ 0 };
		}
		return *this;
	}

	template <typename T, uint32 N>
	basic_fixed_string<T, N>& basic_fixed_string<T, N>::append( uint32 count, T c )
	{
		if ( count == 0 )
			return *this;
		SW_LOG_ASSERT( _size + count <= N, "basic_fixed_string::append - count exceeds capacity" );
		std::fill_n( _data + _size, count, c );
		_size += count;
		_data[_size] = T{ 0 };
		return *this;
	}

	template <typename T, uint32 N>
	basic_fixed_string<T, N>& basic_fixed_string<T, N>::append( const std::basic_string_view<T>& str )
	{
		const uint32 length = static_cast<uint32>( str.length() );
		SW_LOG_ASSERT( _size + length <= N, "Resulting string too long" );
		std::memcpy( _data + _size, str.data(), sizeof( T ) * length );
		_size += length;
		_data[_size] = T{ 0 };
		return *this;
	}

	template <typename T, uint32 N>
	uint32 basic_fixed_string<T, N>::find( const T* str, uint32 pos ) const
	{
		if ( str == nullptr || pos >= _size )
			return npos;
		const T* result = StringUtil::strstr( _data + pos, str );
		return result != nullptr ? static_cast<uint32>( result - _data ) : npos;
	}

	template <typename T, uint32 N>
	uint32 basic_fixed_string<T, N>::find( T c, uint32 pos ) const
	{
		if ( pos >= _size )
			return npos;
		const T* result = StringUtil::strchr( _data + pos, c );
		return result ? static_cast<uint32>( result - _data ) : npos;
	}

	template <typename T, uint32 N>
	basic_fixed_string<T, N> basic_fixed_string<T, N>::substr( uint32 pos, uint32 length ) const
	{
		SW_LOG_ASSERT( pos <= _size, "basic_fixed_string::substr out of range" );

		const uint32	   actualLength = std::min( length, _size - pos );
		basic_fixed_string result{};

		if ( actualLength > 0 )
		{
			std::memcpy( result._data, _data + pos, sizeof( T ) * actualLength );
			result._size			   = actualLength;
			result._data[actualLength] = T{ 0 };
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
	std::ostream& operator<<( std::ostream& os, const basic_fixed_string<T, N>& str )
	{
		return os << str.c_str();
	}

	template <typename T, uint32 N>
	std::istream& operator>>( std::istream& is, basic_fixed_string<T, N>& str )
	{
		std::basic_string<T> temp;
		is >> temp;
		str = temp;
		return is;
	}

#pragma endregion
}
