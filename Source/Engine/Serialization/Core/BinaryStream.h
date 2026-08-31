#pragma once
#include "Core/Common/Types.h"
#include "Core/Common/VarIntUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

namespace sw
{
	/**
	 * @class BinaryStreamWriter
	 * @brief vector<uint8> 버퍼에 데이터를 직렬화(Append)하는 바이너리 스트림 라이터
	 */
	class BinaryStreamWriter
	{
	public:
		explicit BinaryStreamWriter( vector<uint8>& buffer )
			: _buffer{ buffer }
		{
		}

		size_t getOffset() const
		{
			return _buffer.size();
		}

		template <typename T>
		void writeAt( size_t offset, const T& value )
		{
			if ( offset + sizeof( T ) <= _buffer.size() )
			{
				Memory::copy( _buffer.data() + offset, &value, sizeof( T ) );
			}
		}

		void writeString( string_view str )
		{
			const uint32 size = static_cast<uint32>( str.size() );
			write( size );
			if ( size > 0 )
			{
				const uint8* pSrc = reinterpret_cast<const uint8*>( str.data() );
				_buffer.insert( _buffer.end(), pSrc, pSrc + size );
			}
		}

		void writeVarUInt( uint64 value )
		{
			VarIntUtil::encodeVarUInt64( value, _buffer );
		}

		void writeVarInt( int64 value )
		{
			VarIntUtil::encodeVarInt64( value, _buffer );
		}

		template <typename T>
		void write( const T& value )
		{
			const uint8* pSrc = reinterpret_cast<const uint8*>( &value );
			_buffer.insert( _buffer.end(), pSrc, pSrc + sizeof( T ) );
		}

		void writeBytes( const vector<uint8>& bytes )
		{
			write( static_cast<uint32>( bytes.size() ) );
			if ( bytes.empty() == false )
			{
				_buffer.insert( _buffer.end(), bytes.begin(), bytes.end() );
			}
		}

		void reserve( size_t additionalCapacity )
		{
			_buffer.reserve( _buffer.size() + additionalCapacity );
		}

	private:
		vector<uint8>& _buffer;
	};

	/**
	 * @class BinaryStreamReader
	 * @brief 메모리 버퍼(const uint8*)로부터 데이터를 역직렬화하는 바이너리 스트림 리더
	 */
	class BinaryStreamReader
	{
	public:
		BinaryStreamReader( const uint8* pData, size_t size )
			: _pData{ pData }
			, _size{ size }
			, _offset{ 0 }
		{
		}

		size_t getOffset() const { return _offset; }

		bool skip( size_t count )
		{
			if ( _offset + count > _size )
				return false;
			_offset += count;
			return true;
		}

		bool readString( string& outStr )
		{
			uint32 size{ 0 };
			if ( read( size ) == false )
				return false;
			if ( size == 0 )
			{
				outStr.clear();
				return true;
			}
			if ( _offset + size > _size )
				return false;
			outStr.assign( reinterpret_cast<const utf8*>( _pData + _offset ), size );
			_offset += size;
			return true;
		}

		bool readVarUInt( uint64& outValue )
		{
			return VarIntUtil::decodeVarUInt64( _pData, _size, _offset, outValue );
		}

		bool readVarUInt( uint32& outValue )
		{
			return VarIntUtil::decodeVarUInt32( _pData, _size, _offset, outValue );
		}

		bool readVarInt( int64& outValue )
		{
			return VarIntUtil::decodeVarInt64( _pData, _size, _offset, outValue );
		}

		bool readVarInt( int32& outValue )
		{
			return VarIntUtil::decodeVarInt32( _pData, _size, _offset, outValue );
		}

		template <typename T>
		bool read( T& outValue )
		{
			if ( _offset + sizeof( T ) > _size )
				return false;
			Memory::copy( &outValue, _pData + _offset, sizeof( T ) );
			_offset += sizeof( T );
			return true;
		}

		bool readBytes( vector<uint8>& outBytes )
		{
			uint32 size{ 0 };
			if ( read( size ) == false )
				return false;
			outBytes.resize( size );
			if ( size > 0 )
			{
				if ( _offset + size > _size )
					return false;
				Memory::copy( outBytes.data(), _pData + _offset, size );
				_offset += size;
			}
			return true;
		}

	private:
		const uint8* _pData;
		size_t		 _size;
		size_t		 _offset;
	};

} // namespace sw
