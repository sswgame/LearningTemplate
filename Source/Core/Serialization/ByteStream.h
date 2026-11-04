/**
 * @file ByteStream.h
 * @brief 바이트 단위 직렬화 — 리틀 엔디언 정수 · 실수를 버퍼 끝에 덧붙이는 `ByteWriter` 와, 범위를 검사하며 읽는 `ByteReader` 입니다.
 * @details 파일 형식(프로브 · 메시 · 파괴 기록)과 패킷이 같은 바이트 순서 규칙을 쓰도록 한 자리에 둡니다. 바이트 순서는 `LittleEndianUtil` 이 정하고
 *          (호스트 순서와 무관), 이 두 타입은 위치와 범위를 맡습니다. 비트 단위로 줄여 쓰는 패킷은 `BitStream.h` 입니다.
 *          읽기가 남은 바이트를 넘으면 0 을 돌려주고 `hasOverflowed` 가 참이 됩니다(`BitReader` 와 같은 규칙) — 깨진 파일을 받아도 버퍼 밖을 읽지 않으며,
 *          부르는 쪽은 필드를 모두 읽은 뒤 한 번 확인합니다. `read*( T& )` 꼴(가변 정수 · 길이 붙은 글 · POD)은 바로 bool 로도 답한다(넘침 표시도 함께 남긴다).
 *          POD 통째 쓰기(`writePod` · `readPod`)는 호스트 바이트를 그대로 옮긴다 — 지원 플랫폼이 모두 리틀 엔디언이라 정수 쓰기와 같은 바이트다(아래 static_assert).
 *          예전 Engine `BinaryStreamWriter/Reader`(호스트 순서 memcpy · 가변 정수)를 이 둘로 합쳤다 — 한 층에 바이트 스트림 모델이 둘이었다.
 */
#pragma once
#include "Core/Container/LittleEndianUtil.h"
#include "Core/Container/VarIntUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include <type_traits>

#if defined( __BYTE_ORDER__ )
static_assert( __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "ByteStream writePod/readPod copy host bytes - they equal the little-endian file format only on little-endian hosts" );
#endif

namespace sw
{
    /**
     * @class ByteWriter
     * @brief 빌린 바이트 벡터 끝에 리틀 엔디언 값을 덧붙입니다. 벡터는 이 객체보다 오래 삽니다.
     */
    class ByteWriter
    {
    public:
        explicit ByteWriter( vector<uint8>& outBytes )
            : _bytes{ outBytes }
        {
        }

        void writeUint8( uint8 value ) { _bytes.push_back( value ); }
        void writeUint16( uint16 value ) { LittleEndianUtil::appendUint16( _bytes, value ); }
        void writeUint32( uint32 value ) { LittleEndianUtil::appendUint32( _bytes, value ); }
        void writeUint64( uint64 value ) { LittleEndianUtil::appendUint64( _bytes, value ); }
        void writeInt64( int64 value ) { LittleEndianUtil::appendInt64( _bytes, value ); }
        void writeFloat32( float32 value ) { LittleEndianUtil::appendFloat32( _bytes, value ); }
        void writeBytes( const uint8* pData, size_t byteCount )
        {
            if ( byteCount > 0 )
                _bytes.insert( _bytes.end(), pData, pData + byteCount );
        }
        void writeVarUint( uint64 value ) { VarIntUtil::encodeVarUint64( value, _bytes ); }
        void writeVarInt( int64 value ) { VarIntUtil::encodeVarInt64( value, _bytes ); }
        /** @brief 길이(uint32)와 바이트를 씁니다. */
        void writeString( string_view text )
        {
            writeUint32( static_cast<uint32>( text.size() ) );
            writeBytes( reinterpret_cast<const uint8*>( text.data() ), text.size() );
        }
        /** @brief 길이(uint32)와 바이트를 씁니다. */
        void writeByteVector( const vector<uint8>& bytes )
        {
            writeUint32( static_cast<uint32>( bytes.size() ) );
            writeBytes( bytes.data(), bytes.size() );
        }
        /** @brief 사소히 복사되는 값 하나를 호스트 바이트 그대로 씁니다(머리 구조체 · float3 등). */
        template <typename T>
        void writePod( const T& value )
        {
            static_assert( std::is_trivially_copyable_v<T>, "writePod copies bytes - T must be trivially copyable" );
            writeBytes( reinterpret_cast<const uint8*>( &value ), sizeof( T ) );
        }
        /** @brief 이미 쓴 자리 @p offset 에 값을 덮어씁니다(길이 · 오프셋 표를 나중에 채운다). 자리가 모자라면 아무것도 하지 않습니다. */
        template <typename T>
        void writePodAt( size_t offset, const T& value )
        {
            static_assert( std::is_trivially_copyable_v<T>, "writePodAt copies bytes - T must be trivially copyable" );
            if ( offset <= _bytes.size() && sizeof( T ) <= _bytes.size() - offset )
                Memory::copy( _bytes.data() + offset, &value, sizeof( T ) );
        }
        void reserve( size_t additionalByteCount ) { _bytes.reserve( _bytes.size() + additionalByteCount ); }

        /** @brief 지금까지 버퍼에 있는 바이트 수입니다(이 객체가 만들기 전부터 있던 바이트 포함). */
        size_t getByteCount() const { return _bytes.size(); }

    private:
        vector<uint8>& _bytes;
    };
} // namespace sw

namespace sw
{
    /**
     * @class ByteReader
     * @brief 빌린 바이트를 앞에서부터 리틀 엔디언 값으로 읽습니다. 남은 바이트가 모자라면 읽지 않고 넘침으로 표시합니다.
     */
    class ByteReader
    {
    public:
        ByteReader( const uint8* pData, size_t byteCount )
            : _pData{ pData }
            , _byteCount{ pData != nullptr ? byteCount : 0 }
            , _offset{ 0 }
            , _bOverflow{ SW_FALSE }
        {
        }

        [[nodiscard]] uint8 readUint8()
        {
            const uint8* pBytes = consume( 1 );
            return pBytes != nullptr ? pBytes[0] : uint8{ 0 };
        }
        [[nodiscard]] uint16 readUint16()
        {
            const uint8* pBytes = consume( 2 );
            return pBytes != nullptr ? LittleEndianUtil::readUint16( pBytes ) : uint16{ 0 };
        }
        [[nodiscard]] uint32 readUint32()
        {
            const uint8* pBytes = consume( 4 );
            return pBytes != nullptr ? LittleEndianUtil::readUint32( pBytes ) : 0u;
        }
        [[nodiscard]] uint64 readUint64()
        {
            const uint8* pBytes = consume( 8 );
            return pBytes != nullptr ? LittleEndianUtil::readUint64( pBytes ) : 0ull;
        }
        [[nodiscard]] int64   readInt64() { return static_cast<int64>( readUint64() ); }
        [[nodiscard]] float32 readFloat32()
        {
            const uint8* pBytes = consume( 4 );
            return pBytes != nullptr ? LittleEndianUtil::readFloat32( pBytes ) : 0.0f;
        }
        /** @brief @p byteCount 바이트를 @p pOutData 에 복사합니다. 모자라면 false 이고 아무것도 쓰지 않습니다. */
        [[nodiscard]] bool readBytes( uint8* pOutData, size_t byteCount )
        {
            if ( byteCount == 0 )
                return true;
            const uint8* pBytes = consume( byteCount );
            if ( pBytes == nullptr )
                return false;
            Memory::copy( pOutData, pBytes, byteCount );
            return true;
        }
        /** @brief @p byteCount 바이트를 읽지 않고 넘깁니다. 모자라면 false 입니다. */
        [[nodiscard]] bool skipBytes( size_t byteCount ) { return byteCount == 0 || consume( byteCount ) != nullptr; }

        [[nodiscard]] bool readVarUint( uint64& outValue ) { return markOverflowUnless( VarIntUtil::decodeVarUint64( _pData, _byteCount, _offset, outValue ) ); }
        [[nodiscard]] bool readVarUint( uint32& outValue ) { return markOverflowUnless( VarIntUtil::decodeVarUint32( _pData, _byteCount, _offset, outValue ) ); }
        [[nodiscard]] bool readVarInt( int64& outValue ) { return markOverflowUnless( VarIntUtil::decodeVarInt64( _pData, _byteCount, _offset, outValue ) ); }
        [[nodiscard]] bool readVarInt( int32& outValue ) { return markOverflowUnless( VarIntUtil::decodeVarInt32( _pData, _byteCount, _offset, outValue ) ); }
        /** @brief `writeString` 이 쓴 글을 읽습니다. 모자라면 false 입니다. */
        [[nodiscard]] bool readString( string& outText )
        {
            string_view view;
            if ( readStringView( view ) == false )
                return false;
            outText.assign( view.data(), view.size() );
            return true;
        }
        /** @brief `writeString` 이 쓴 글을 복사 없이 봅니다(버퍼보다 오래 쓰지 않는다). 모자라면 false 입니다. */
        [[nodiscard]] bool readStringView( string_view& outView )
        {
            const uint32 size = readUint32();
            if ( hasOverflowed() )
                return false;
            const uint8* pBytes = size == 0 ? nullptr : consume( size );
            if ( size > 0 && pBytes == nullptr )
                return false;
            outView = size == 0 ? string_view{} : string_view{ reinterpret_cast<const utf8*>( pBytes ), size };
            return true;
        }
        /** @brief `writeByteVector` 가 쓴 바이트를 읽습니다. 모자라면 비우고 false 입니다. */
        [[nodiscard]] bool readByteVector( vector<uint8>& outBytes )
        {
            outBytes.clear();
            const uint32 size = readUint32();
            if ( hasOverflowed() )
                return false;
            outBytes.resize( size );
            if ( readBytes( outBytes.data(), size ) == false )
            {
                outBytes.clear();
                return false;
            }
            return true;
        }
        /** @brief `writePod` 가 쓴 값을 읽습니다. 모자라면 false 이고 @p outValue 는 그대로입니다. */
        template <typename T>
        [[nodiscard]] bool readPod( T& outValue )
        {
            static_assert( std::is_trivially_copyable_v<T>, "readPod copies bytes - T must be trivially copyable" );
            return readBytes( reinterpret_cast<uint8*>( &outValue ), sizeof( T ) );
        }

        bool   hasOverflowed() const { return _bOverflow != SW_FALSE; }
        size_t getOffset() const { return _offset; }
        size_t getRemainingByteCount() const { return _byteCount - _offset; }

    private:
        bool markOverflowUnless( bool bRead )
        {
            if ( bRead == false )
                _bOverflow = SW_TRUE;
            return bRead;
        }
        /** @brief @p byteCount 바이트를 차지하고 그 시작을 돌려줍니다. 모자라면 nullptr 이고 위치는 그대로, 넘침으로 표시합니다. */
        const uint8* consume( size_t byteCount )
        {
            if ( byteCount > _byteCount - _offset )
            {
                _bOverflow = SW_TRUE;
                return nullptr;
            }
            const uint8* pBytes = _pData + _offset;
            _offset += byteCount;
            return pBytes;
        }

    private:
        const uint8* _pData;
        size_t       _byteCount;
        size_t       _offset;
        uint8        _bOverflow;
    };
} // namespace sw
