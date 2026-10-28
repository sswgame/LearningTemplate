/**
 * @file ByteStream.h
 * @brief 바이트 단위 직렬화 — 리틀 엔디언 정수 · 실수를 버퍼 끝에 덧붙이는 `ByteWriter` 와, 범위를 검사하며 읽는 `ByteReader` 입니다.
 * @details 파일 형식(프로브 · 메시 · 파괴 기록)과 패킷이 같은 바이트 순서 규칙을 쓰도록 한 자리에 둡니다. 바이트 순서는 `LittleEndianUtil` 이 정하고
 *          (호스트 순서와 무관), 이 두 타입은 위치와 범위를 맡습니다. 비트 단위로 줄여 쓰는 패킷은 `BitStream.h` 입니다.
 *          읽기가 남은 바이트를 넘으면 0 을 돌려주고 `hasOverflowed` 가 참이 됩니다(`BitReader` 와 같은 규칙) — 깨진 파일을 받아도 버퍼 밖을 읽지 않으며,
 *          부르는 쪽은 필드를 모두 읽은 뒤 한 번 확인합니다.
 */
#pragma once
#include "Core/Container/LittleEndianUtil.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

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
        void writeBytes( const uint8* pData, size_t byteCount ) { _bytes.insert( _bytes.end(), pData, pData + byteCount ); }

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

        bool   hasOverflowed() const { return _bOverflow != SW_FALSE; }
        size_t getOffset() const { return _offset; }
        size_t getRemainingByteCount() const { return _byteCount - _offset; }

    private:
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

        const uint8* _pData;
        size_t       _byteCount;
        size_t       _offset;
        uint8        _bOverflow;
    };
} // namespace sw
