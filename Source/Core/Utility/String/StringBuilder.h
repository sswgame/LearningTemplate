#pragma once
/**
 * @file StringBuilder.h
 * @brief Auto-generated documentation header
 */

#include "Core/Common/Types.h"
#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Utility/String/formatString.h"
#include "Core/Utility/String/StringUtil.h"
#include <cstring>

namespace sw
{

	template <uint32 Capacity = 256>
	class StringBuilder
	{
	public:
		StringBuilder()
		{
			_bufferPtr = _staticBuffer;
			clear();
		}

		~StringBuilder()
		{
			if ( _dynamicBuffer != nullptr )
			{
				delete[] _dynamicBuffer;
				_dynamicBuffer = nullptr;
			}
		}

		StringBuilder( const StringBuilder& )			 = delete;
		StringBuilder& operator=( const StringBuilder& ) = delete;

		SW_INLINE void ensureCapacity( uint32 extraLength )
		{
			if ( _length + extraLength < _capacity )
				return;

			uint32 newCapacity = _capacity * 2;
			while ( _length + extraLength >= newCapacity )
				newCapacity *= 2;

			char* newBuffer = new char[newCapacity];
			std::memcpy( newBuffer, _bufferPtr, _length + 1 );

			if ( _dynamicBuffer != nullptr )
				delete[] _dynamicBuffer;

			_dynamicBuffer = newBuffer;
			_bufferPtr	   = _dynamicBuffer;
			_capacity	   = newCapacity;
		}

		SW_INLINE StringBuilder& append( const std::string_view sv )
		{
			if ( sv.empty() )
				return *this;

			ensureCapacity( static_cast<uint32>( sv.size() ) );
			std::memcpy( _bufferPtr + _length, sv.data(), sv.size() );
			_length += static_cast<uint32>( sv.size() );
			_bufferPtr[_length] = '\0';
			return *this;
		}

		SW_INLINE StringBuilder& append( const utf8* str )
		{
			if ( str == nullptr )
				return *this;

			uint32 strLen = StringUtil::strlen( str );
			ensureCapacity( strLen );
			std::memcpy( _bufferPtr + _length, str, strLen );
			_length += strLen;
			_bufferPtr[_length] = '\0';
			return *this;
		}

		SW_INLINE StringBuilder& append( utf8 c )
		{
			ensureCapacity( 1 );
			_bufferPtr[_length++] = c;
			_bufferPtr[_length]	  = '\0';
			return *this;
		}

		SW_INLINE StringBuilder& append( int32 val )
		{
			utf8 tmp[32];
			formatstring( tmp, static_cast<uint32>( sizeof( tmp ) ), "%#", val );
			return append( tmp );
		}

		SW_INLINE StringBuilder& append( uint32 val )
		{
			utf8 tmp[32];
			formatstring( tmp, static_cast<uint32>( sizeof( tmp ) ), "%#", val );
			return append( tmp );
		}

		SW_INLINE StringBuilder& append( float32 val )
		{
			utf8 tmp[32];
			formatstring( tmp, static_cast<uint32>( sizeof( tmp ) ), "%#", Fmt( val, Format( 4 ) ) );
			return append( tmp );
		}

		template <typename... Args>
		SW_INLINE StringBuilder& appendFormat( std::string_view format, const Args&... args )
		{
			if ( format.empty() )
				return *this;

			uint32 available = _capacity - _length;
			if ( available < 2 )
			{
				ensureCapacity( 256 );
				available = _capacity - _length;
			}

			for ( ;; )
			{
				formatstring( _bufferPtr + _length, available, format, args... );
				const uint32 written = StringUtil::strlen( _bufferPtr + _length );

				// formatstring truncates when the result fills available-1; grow and retry.
				if ( written < available - 1 )
				{
					_length += written;
					return *this;
				}

				_bufferPtr[_length] = '\0';
				ensureCapacity( available );
				available = _capacity - _length;
			}
		}

		SW_INLINE const char* c_str() const { return _bufferPtr; }

		SW_INLINE std::string_view view() const { return std::string_view( _bufferPtr, _length ); }

		SW_INLINE uint32 size() const { return _length; }

		SW_INLINE uint32 capacity() const { return _capacity; }

		SW_INLINE void clear()
		{
			_length		  = 0;
			_bufferPtr[0] = '\0';
		}

	private:
		char   _staticBuffer[Capacity]{};
		char*  _dynamicBuffer = nullptr;
		char*  _bufferPtr	  = nullptr;
		uint32 _capacity	  = Capacity;
		uint32 _length		  = 0;
	};
}
