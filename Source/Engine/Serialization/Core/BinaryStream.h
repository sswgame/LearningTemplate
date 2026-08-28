#pragma once
#include "Core/Common/Types.h"
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
		explicit BinaryStreamWriter( vector<uint8>& listBuffer )
			: _listBuffer{ listBuffer }
		{
		}

		size_t getOffset() const
		{
			return _listBuffer.size();
		}

		template <typename T>
		void writeAt( size_t offset, const T& value )
		{
			if ( offset + sizeof( T ) <= _listBuffer.size() )
			{
				Memory::copy( _listBuffer.data() + offset, &value, sizeof( T ) );
			}
		}

		void writeString( string_view str )
		{
			const uint32 size = static_cast<uint32>( str.size() );
			write( size );
			if ( size > 0 )
			{
				const uint8* pSrc = reinterpret_cast<const uint8*>( str.data() );
				_listBuffer.insert( _listBuffer.end(), pSrc, pSrc + size );
			}
		}

		template <typename T>
		void write( const T& value )
		{
			const uint8* pSrc = reinterpret_cast<const uint8*>( &value );
			_listBuffer.insert( _listBuffer.end(), pSrc, pSrc + sizeof( T ) );
		}

		void writeBytes( const vector<uint8>& listByte )
		{
			write( static_cast<uint32>( listByte.size() ) );
			if ( listByte.empty() == false )
			{
				_listBuffer.insert( _listBuffer.end(), listByte.begin(), listByte.end() );
			}
		}

		void reserve( size_t additionalCapacity )
		{
			_listBuffer.reserve( _listBuffer.size() + additionalCapacity );
		}

	private:
		vector<uint8>& _listBuffer;
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

		template <typename T>
		bool read( T& outValue )
		{
			if ( _offset + sizeof( T ) > _size )
				return false;
			Memory::copy( &outValue, _pData + _offset, sizeof( T ) );
			_offset += sizeof( T );
			return true;
		}

		bool readBytes( vector<uint8>& listOutByte )
		{
			uint32 size{ 0 };
			if ( read( size ) == false )
				return false;
			listOutByte.resize( size );
			if ( size > 0 )
			{
				if ( _offset + size > _size )
					return false;
				Memory::copy( listOutByte.data(), _pData + _offset, size );
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
