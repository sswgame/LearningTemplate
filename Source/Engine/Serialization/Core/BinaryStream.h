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
                Memory::copy( _buffer.data() + offset, &value, sizeof( T ) );
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

        void writeVarUint( uint64 value )
        {
            VarIntUtil::encodeVarUint64( value, _buffer );
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
                _buffer.insert( _buffer.end(), bytes.begin(), bytes.end() );
        }

        void writeRawBytes( const void* pData, size_t size )
        {
            if ( pData != nullptr && size > 0 )
            {
                const uint8* pSrc = reinterpret_cast<const uint8*>( pData );
                _buffer.insert( _buffer.end(), pSrc, pSrc + size );
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

        /**
         * @brief @p count 바이트를 건너뜁니다. 남은 것보다 많으면 위치를 그대로 두고 false.
         * @details 뺄셈으로 비교한다 — `_offset + count > _size` 는 스트림에서 읽은 큰 수에서
         *          **덧셈이 넘쳐 작은 값이 되어 검사를 통과한다.** 이 클래스의 다른 검사들은
         *          길이가 `uint32` 라 넘칠 수 없지만, 이 함수만 `size_t` 를 받고 호출부가
         *          파일에서 읽은 `uint64` 페이로드 크기를 그대로 넘긴다. `_offset <= _size` 는
         *          항상 참이므로(위치는 검사를 통과한 뒤에만 나아간다) 뺄셈은 안전하다.
         */
        bool skip( size_t count )
        {
            if ( count > _size - _offset )
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

        bool readStringView( string_view& outView )
        {
            uint32 size{ 0 };
            if ( read( size ) == false )
                return false;
            if ( size == 0 )
            {
                outView = {};
                return true;
            }
            if ( _offset + size > _size )
                return false;
            outView = string_view{ reinterpret_cast<const utf8*>( _pData + _offset ), size };
            _offset += size;
            return true;
        }

        bool readVarUint( uint64& outValue )
        {
            return VarIntUtil::decodeVarUint64( _pData, _size, _offset, outValue );
        }

        bool readVarUint( uint32& outValue )
        {
            return VarIntUtil::decodeVarUint32( _pData, _size, _offset, outValue );
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
            if ( size == 0 )
            {
                outBytes.clear();
                return true;
            }
            if ( _offset + size > _size )
            {
                outBytes.clear();
                return false;
            }
            outBytes.resize( size );
            Memory::copy( outBytes.data(), _pData + _offset, size );
            _offset += size;
            return true;
        }

    private:
        const uint8* _pData;
        size_t       _size;
        size_t       _offset;
    };

} // namespace sw
